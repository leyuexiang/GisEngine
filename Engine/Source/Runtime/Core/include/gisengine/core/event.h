#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <utility>
#include <vector>

namespace gisengine::core {

// 发布前复制订阅者快照，使回调中订阅或取消订阅不会破坏本次遍历。
template <typename... Arguments>
class Event final {
   public:
    using Subscriber = std::function<void(Arguments...)>;

    struct Connection {
        std::uint64_t value{0};

        [[nodiscard]] constexpr bool valid() const noexcept { return value != 0U; }
    };

    [[nodiscard]] Connection subscribe(Subscriber subscriber) {
        std::scoped_lock lock{mutex_};
        const Connection connection{.value = next_connection_++};
        subscribers_.push_back({.connection = connection, .subscriber = std::move(subscriber)});
        return connection;
    }

    [[nodiscard]] bool unsubscribe(Connection connection) {
        if (!connection.valid()) {
            return false;
        }

        std::scoped_lock lock{mutex_};
        const auto previous_size = subscribers_.size();
        std::erase_if(subscribers_,
                      [connection](const Entry& entry) { return entry.connection.value == connection.value; });
        return subscribers_.size() != previous_size;
    }

    void publish(Arguments... arguments) const {
        std::vector<Subscriber> snapshot;
        {
            std::scoped_lock lock{mutex_};
            snapshot.reserve(subscribers_.size());
            for (const Entry& entry : subscribers_) {
                snapshot.push_back(entry.subscriber);
            }
        }

        for (const Subscriber& subscriber : snapshot) {
            subscriber(arguments...);
        }
    }

    [[nodiscard]] std::size_t subscriber_count() const noexcept {
        std::scoped_lock lock{mutex_};
        return subscribers_.size();
    }

   private:
    struct Entry {
        Connection connection;
        Subscriber subscriber;
    };

    mutable std::mutex mutex_;
    mutable std::vector<Entry> subscribers_;
    std::uint64_t next_connection_{1U};
};

}  // namespace gisengine::core
