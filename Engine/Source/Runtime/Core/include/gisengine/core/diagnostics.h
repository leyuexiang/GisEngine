#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory_resource>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>

namespace gisengine::core {

enum class LogLevel : std::uint8_t {
    trace,
    debug,
    info,
    warning,
    error,
};

struct LogRecord {
    LogLevel level{LogLevel::info};
    std::string message;
};

class Logger final {
   public:
    using Sink = std::function<void(const LogRecord&)>;

    explicit Logger(Sink sink = {});

    void log(LogLevel level, std::string_view message) const;

   private:
    mutable std::mutex mutex_;
    Sink sink_;
};

class AssertionError final : public std::logic_error {
   public:
    explicit AssertionError(const std::string& message);
};

void ensure(bool condition, std::string_view expression, std::string_view message);

#define GISENGINE_ENSURE(condition, message) ::gisengine::core::ensure((condition), #condition, (message))

struct MemoryStats {
    std::size_t allocation_count{0};
    std::size_t deallocation_count{0};
    std::size_t active_bytes{0};
    std::size_t peak_active_bytes{0};
};

// 只在需要观测的子系统显式注入，避免全局 new 重载影响热路径与第三方库。
class TrackingMemoryResource final : public std::pmr::memory_resource {
   public:
    explicit TrackingMemoryResource(std::pmr::memory_resource* upstream = std::pmr::get_default_resource()) noexcept;

    [[nodiscard]] MemoryStats stats() const noexcept;

   private:
    [[nodiscard]] void* do_allocate(std::size_t bytes, std::size_t alignment) override;
    void do_deallocate(void* memory, std::size_t bytes, std::size_t alignment) override;
    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override;

    std::pmr::memory_resource* upstream_;
    std::atomic<std::size_t> allocation_count_{0};
    std::atomic<std::size_t> deallocation_count_{0};
    std::atomic<std::size_t> active_bytes_{0};
    std::atomic<std::size_t> peak_active_bytes_{0};
};

}  // namespace gisengine::core
