#include <gtest/gtest.h>

#include <memory_resource>
#include <stdexcept>
#include <string>
#include <vector>

#include "gisengine/core/diagnostics.h"
#include "gisengine/core/engine.h"
#include "gisengine/core/event.h"
#include "gisengine/runtime/runtime.h"

namespace gisengine::tests {

TEST(EngineTest, StartsAndStopsWithConfiguredProjectName) {
    runtime::RuntimeHost host{core::EngineConfig{.project_name = "单元测试", .fixed_update_hz = 60U}};

    host.initialize();

    EXPECT_EQ(host.engine().state(), core::EngineState::running);
    EXPECT_EQ(host.engine().project_name(), "单元测试");

    host.shutdown();

    EXPECT_EQ(host.engine().state(), core::EngineState::stopped);
}

TEST(EngineTest, RejectsInvalidConfiguration) {
    // 在对象创建时拒绝无效配置，避免错误状态进入帧循环后才暴露。
    EXPECT_THROW((core::Engine{core::EngineConfig{.project_name = "", .fixed_update_hz = 60U}}), std::invalid_argument);
    EXPECT_THROW((core::Engine{core::EngineConfig{.project_name = "无频率", .fixed_update_hz = 0U}}),
                 std::invalid_argument);
}

TEST(EngineTest, RejectsRestartAfterShutdown) {
    core::Engine engine{core::EngineConfig{.project_name = "状态机", .fixed_update_hz = 60U}};
    engine.start();
    engine.stop();

    // 停止后的实例不可复用，防止被释放的平台服务重新进入运行状态。
    EXPECT_THROW(engine.start(), std::logic_error);
}

TEST(CoreServicesTest, DeliversLogRecordsAndFailsContracts) {
    std::vector<core::LogRecord> records;
    core::Logger logger{[&records](const core::LogRecord& record) { records.push_back(record); }};

    logger.log(core::LogLevel::warning, "配置已降级");

    ASSERT_EQ(records.size(), 1U);
    EXPECT_EQ(records.front().level, core::LogLevel::warning);
    EXPECT_EQ(records.front().message, "配置已降级");
    EXPECT_THROW(GISENGINE_ENSURE(false, "测试契约"), core::AssertionError);
}

TEST(CoreServicesTest, PublishesSnapshotWhenSubscribersChange) {
    core::Event<int> event;
    int first_total = 0;
    int second_total = 0;
    const auto first = event.subscribe([&first_total](int value) { first_total += value; });
    const auto second = event.subscribe([&event, &first, &second_total](int value) {
        second_total += value;
        static_cast<void>(event.unsubscribe(first));
    });

    event.publish(2);

    EXPECT_EQ(first_total, 2);
    EXPECT_EQ(second_total, 2);
    EXPECT_EQ(event.subscriber_count(), 1U);
    EXPECT_TRUE(first.valid());
    EXPECT_TRUE(second.valid());
}

TEST(CoreServicesTest, TracksOptInMemoryResourceUsage) {
    core::TrackingMemoryResource resource;
    {
        std::pmr::vector<int> values{&resource};
        values.resize(32U);
        EXPECT_GT(resource.stats().active_bytes, 0U);
    }

    const core::MemoryStats stats = resource.stats();
    EXPECT_GT(stats.allocation_count, 0U);
    EXPECT_EQ(stats.allocation_count, stats.deallocation_count);
    EXPECT_EQ(stats.active_bytes, 0U);
    EXPECT_GT(stats.peak_active_bytes, 0U);
}

}  // namespace gisengine::tests
