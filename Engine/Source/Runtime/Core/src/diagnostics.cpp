#include "gisengine/core/diagnostics.h"

#include <algorithm>

namespace gisengine::core {

Logger::Logger(Sink sink) : sink_(std::move(sink)) {}

void Logger::log(LogLevel level, std::string_view message) const {
    Sink sink;
    {
        std::scoped_lock lock{mutex_};
        sink = sink_;
    }

    if (sink) {
        sink({.level = level, .message = std::string{message}});
    }
}

AssertionError::AssertionError(const std::string& message) : std::logic_error(message) {}

void ensure(bool condition, std::string_view expression, std::string_view message) {
    if (condition) {
        return;
    }

    std::string error{"契约断言失败: "};
    error.append(expression);
    if (!message.empty()) {
        error.append("；");
        error.append(message);
    }
    throw AssertionError{error};
}

TrackingMemoryResource::TrackingMemoryResource(std::pmr::memory_resource* upstream) noexcept
    : upstream_(upstream != nullptr ? upstream : std::pmr::get_default_resource()) {}

MemoryStats TrackingMemoryResource::stats() const noexcept {
    return {
        .allocation_count = allocation_count_.load(std::memory_order_relaxed),
        .deallocation_count = deallocation_count_.load(std::memory_order_relaxed),
        .active_bytes = active_bytes_.load(std::memory_order_relaxed),
        .peak_active_bytes = peak_active_bytes_.load(std::memory_order_relaxed),
    };
}

void* TrackingMemoryResource::do_allocate(std::size_t bytes, std::size_t alignment) {
    void* memory = upstream_->allocate(bytes, alignment);
    allocation_count_.fetch_add(1U, std::memory_order_relaxed);
    const std::size_t active_bytes = active_bytes_.fetch_add(bytes, std::memory_order_relaxed) + bytes;

    std::size_t observed_peak = peak_active_bytes_.load(std::memory_order_relaxed);
    while (observed_peak < active_bytes &&
           !peak_active_bytes_.compare_exchange_weak(observed_peak, active_bytes, std::memory_order_relaxed,
                                                     std::memory_order_relaxed)) {
    }
    return memory;
}

void TrackingMemoryResource::do_deallocate(void* memory, std::size_t bytes, std::size_t alignment) {
    upstream_->deallocate(memory, bytes, alignment);
    deallocation_count_.fetch_add(1U, std::memory_order_relaxed);
    active_bytes_.fetch_sub(bytes, std::memory_order_relaxed);
}

bool TrackingMemoryResource::do_is_equal(const std::pmr::memory_resource& other) const noexcept {
    return this == &other;
}

}  // namespace gisengine::core
