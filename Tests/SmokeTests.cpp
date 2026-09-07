#include <cmath>
#include <iostream>
#include <memory_resource>
#include <string>
#include <vector>

#include "gisengine/core/diagnostics.h"
#include "gisengine/core/entity.h"
#include "gisengine/core/event.h"
#include "gisengine/core/guid.h"
#include "gisengine/ecs/world.h"
#include "gisengine/math/conventions.h"
#include "gisengine/rhi/rhi.h"
#include "gisengine/runtime/runtime.h"
#include "gisengine/scene/scene.h"

namespace {

bool nearly_equal(float left, float right, float tolerance = 1.0e-5F) {
    return std::abs(left - right) <= tolerance;
}

struct TestPosition {
    int value{0};
};

struct TestVelocity {
    int value{0};
};

}  // namespace

int main() {
    gisengine::runtime::RuntimeHost host{
        gisengine::core::EngineConfig{.project_name = "SmokeTest", .fixed_update_hz = 60U}};
    if (host.engine().state() != gisengine::core::EngineState::created) {
        std::cerr << "新引擎状态错误\n";
        return 1;
    }

    host.initialize();
    if (host.engine().state() != gisengine::core::EngineState::running) {
        std::cerr << "初始化后引擎未运行\n";
        return 2;
    }
    if (host.engine().project_name() != "SmokeTest") {
        std::cerr << "项目名称未保存\n";
        return 3;
    }

    host.shutdown();
    if (host.engine().state() != gisengine::core::EngineState::stopped) {
        std::cerr << "关闭后引擎状态错误\n";
        return 4;
    }

    const gisengine::math::Vec3f x_axis{.x = 1.0F, .y = 0.0F, .z = 0.0F};
    const gisengine::math::Vec3f y_axis{.x = 0.0F, .y = 1.0F, .z = 0.0F};
    const auto z_axis = x_axis.cross(y_axis);
    if (!nearly_equal(z_axis.z, 1.0F) || !nearly_equal(z_axis.length(), 1.0F)) {
        std::cerr << "向量叉积或长度计算错误\n";
        return 5;
    }

    if (gisengine::math::Vec3f{}.normalized().length_squared() != 0.0F) {
        std::cerr << "零向量归一化错误\n";
        return 6;
    }

    const auto translated = gisengine::math::Mat4f::translation({.x = 2.0F, .y = 3.0F, .z = 4.0F})
                                .transform_point({.x = 1.0F, .y = 1.0F, .z = 1.0F});
    if (!nearly_equal(translated.x, 3.0F) || !nearly_equal(translated.y, 4.0F) || !nearly_equal(translated.z, 5.0F)) {
        std::cerr << "矩阵平移计算错误\n";
        return 7;
    }

    gisengine::core::EntityPool entities;
    const auto first_entity = entities.create();
    if (!entities.is_alive(first_entity) || entities.alive_count() != 1U) {
        std::cerr << "实体创建状态错误\n";
        return 8;
    }
    if (!entities.destroy(first_entity) || entities.is_alive(first_entity) || entities.alive_count() != 0U) {
        std::cerr << "实体销毁或旧句柄校验错误\n";
        return 9;
    }
    const auto replacement_entity = entities.create();
    if (replacement_entity.index != first_entity.index || replacement_entity.generation == first_entity.generation ||
        !entities.is_alive(replacement_entity)) {
        std::cerr << "实体代数复用错误\n";
        return 10;
    }

    const auto first_guid = gisengine::core::Guid::generate();
    const auto second_guid = gisengine::core::Guid::generate();
    if (!first_guid.valid() || first_guid == second_guid || first_guid.to_string().size() != 36U) {
        std::cerr << "GUID 生成或文本格式错误\n";
        return 11;
    }

    const auto ground = gisengine::math::Planef::from_point_normal({.x = 0.0F, .y = 0.0F, .z = 0.0F},
                                                                   {.x = 0.0F, .y = 1.0F, .z = 0.0F});
    const auto ray = gisengine::math::Rayf::from_origin_direction({.x = 0.0F, .y = 2.0F, .z = 0.0F},
                                                                  {.x = 0.0F, .y = -1.0F, .z = 0.0F});
    const auto hit_distance = ray.intersect_plane(ground);
    if (!hit_distance.has_value() || !nearly_equal(*hit_distance, 2.0F)) {
        std::cerr << "射线与平面求交错误\n";
        return 12;
    }

    if (gisengine::math::k_world_handedness != gisengine::math::Handedness::right_handed ||
        gisengine::math::k_clip_space_depth_range != gisengine::math::ClipSpaceDepthRange::zero_to_one) {
        std::cerr << "坐标或深度约定错误\n";
        return 13;
    }

    std::vector<gisengine::core::LogRecord> records;
    gisengine::core::Logger logger{[&records](const gisengine::core::LogRecord& record) { records.push_back(record); }};
    logger.log(gisengine::core::LogLevel::info, "冒烟测试日志");
    if (records.size() != 1U || records.front().message != "冒烟测试日志") {
        std::cerr << "日志派发错误\n";
        return 14;
    }

    gisengine::core::Event<int> event;
    int event_total = 0;
    const auto connection = event.subscribe([&event_total](int value) { event_total += value; });
    event.publish(3);
    if (event_total != 3 || !event.unsubscribe(connection) || event.subscriber_count() != 0U) {
        std::cerr << "事件订阅或取消订阅错误\n";
        return 15;
    }

    gisengine::core::TrackingMemoryResource memory_resource;
    {
        std::pmr::vector<int> values{&memory_resource};
        values.resize(16U);
    }
    if (memory_resource.stats().allocation_count == 0U || memory_resource.stats().active_bytes != 0U) {
        std::cerr << "内存统计资源错误\n";
        return 16;
    }

    try {
        GISENGINE_ENSURE(false, "冒烟测试契约");
        std::cerr << "断言未抛出错误\n";
        return 17;
    } catch (const gisengine::core::AssertionError&) {
    }

    gisengine::ecs::World world;
    const auto first_world_entity = world.create_entity();
    const auto second_world_entity = world.create_entity();
    world.emplace<TestPosition>(first_world_entity, 2);
    world.emplace<TestPosition>(second_world_entity, 3);
    world.emplace<TestVelocity>(second_world_entity, 4);
    int position_total = 0;
    world.each<TestPosition>(
        [&position_total](gisengine::core::EntityHandle, TestPosition& position) { position_total += position.value; });
    if (position_total != 5 || !world.has<TestVelocity>(second_world_entity) ||
        !world.destroy_entity(second_world_entity) || world.component_count<TestPosition>() != 1U ||
        world.component_count<TestVelocity>() != 0U) {
        std::cerr << "ECS 稀疏集合存储错误\n";
        return 18;
    }
    try {
        world.emplace<TestPosition>(second_world_entity, 6);
        std::cerr << "已销毁实体仍可添加组件\n";
        return 19;
    } catch (const gisengine::core::AssertionError&) {
    }

    gisengine::scene::Scene scene;
    const auto root_scene_entity = scene.create_entity();
    const auto child_scene_entity = scene.create_entity();
    scene.try_get_transform(root_scene_entity)->local_position = {.x = 1.0F, .y = 0.0F, .z = 0.0F};
    scene.try_get_transform(child_scene_entity)->local_position = {.x = 0.0F, .y = 2.0F, .z = 0.0F};
    if (!scene.set_parent(child_scene_entity, root_scene_entity) ||
        scene.set_parent(root_scene_entity, child_scene_entity)) {
        std::cerr << "Transform 层级循环校验错误\n";
        return 20;
    }
    scene.update_transforms();
    const auto child_origin = scene.try_get_transform(child_scene_entity)->world_matrix.transform_point({});
    if (!nearly_equal(child_origin.x, 1.0F) || !nearly_equal(child_origin.y, 2.0F)) {
        std::cerr << "Transform 世界矩阵更新错误\n";
        return 21;
    }

    gisengine::rhi::NullRenderDevice null_device;
    const auto original_buffer = null_device.create_buffer(
        {.size_bytes = 128U,
         .usage = gisengine::rhi::BufferUsage::vertex | gisengine::rhi::BufferUsage::copy_destination,
         .debug_name = "冒烟顶点缓冲"});
    if (!null_device.is_valid(original_buffer) || !null_device.destroy(original_buffer) ||
        null_device.is_valid(original_buffer)) {
        std::cerr << "Null RHI 资源生命周期错误\n";
        return 22;
    }
    const auto copy_source = null_device.create_buffer(
        {.size_bytes = 64U, .usage = gisengine::rhi::BufferUsage::copy_source, .debug_name = "源"});
    const auto copy_destination = null_device.create_buffer(
        {.size_bytes = 64U, .usage = gisengine::rhi::BufferUsage::copy_destination, .debug_name = "目标"});
    auto current_frame = null_device.begin_frame();
    null_device.transition(current_frame, copy_source, gisengine::rhi::ResourceState::undefined,
                           gisengine::rhi::ResourceState::copy_source);
    null_device.transition(current_frame, copy_destination, gisengine::rhi::ResourceState::undefined,
                           gisengine::rhi::ResourceState::copy_destination);
    null_device.copy_buffer(current_frame, copy_source, copy_destination, 32U);
    const auto fence = null_device.submit(current_frame);
    if (!null_device.is_fence_complete(fence) || null_device.last_submitted_command_count() != 3U) {
        std::cerr << "Null RHI 同步或命令验证错误\n";
        return 23;
    }
    null_device.simulate_device_loss();
    if (null_device.state() != gisengine::rhi::DeviceState::lost) {
        std::cerr << "Null RHI 设备丢失状态错误\n";
        return 24;
    }
    null_device.recover();
    if (null_device.capabilities().backend != gisengine::rhi::BackendType::null_backend) {
        std::cerr << "Null RHI 能力模型错误\n";
        return 25;
    }

    return 0;
}
