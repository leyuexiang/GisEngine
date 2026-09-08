#include <gtest/gtest.h>

#include <utility>

#include "gisengine/render/render.h"

namespace gisengine::tests {

TEST(RenderSnapshotTest, CopiesAndSortsGameThreadExtractionData) {
    render::RenderSnapshotBuilder builder{17U, 2U};
    math::Mat4f first_world = math::Mat4f::translation({.x = 4.0F, .y = 0.0F, .z = 0.0F});
    builder.add_item({
        .entity = {.index = 8U, .generation = 1U},
        .world_matrix = first_world,
        .mesh_id = 3U,
        .material_id = 2U,
        .sort_key = 9U,
    });
    builder.add_item({
        .entity = {.index = 4U, .generation = 2U},
        .world_matrix = math::Mat4f::translation({.x = 1.0F, .y = 2.0F, .z = 3.0F}),
        .mesh_id = 1U,
        .material_id = 1U,
        .sort_key = 2U,
    });
    first_world = math::Mat4f::identity();

    const render::RenderSnapshot snapshot = std::move(builder).build();
    ASSERT_EQ(snapshot.frame_number(), 17U);
    ASSERT_EQ(snapshot.items().size(), 2U);
    EXPECT_EQ(snapshot.items()[0].entity.index, 4U);
    EXPECT_EQ(snapshot.items()[1].entity.index, 8U);
    EXPECT_EQ(snapshot.items()[1].world_matrix.transform_point({}), (math::Vec3f{.x = 4.0F, .y = 0.0F, .z = 0.0F}));
}

TEST(RenderSnapshotTest, RejectsIncompleteRenderItems) {
    EXPECT_THROW(static_cast<void>(render::RenderSnapshotBuilder{0U}), core::AssertionError);

    render::RenderSnapshotBuilder builder{1U};
    EXPECT_THROW(builder.add_item({
                     .entity = {},
                     .world_matrix = math::Mat4f::identity(),
                     .mesh_id = 1U,
                     .material_id = 1U,
                     .sort_key = 0U,
                 }),
                 core::AssertionError);
    EXPECT_THROW(builder.add_item({
                     .entity = {.index = 1U, .generation = 1U},
                     .world_matrix = math::Mat4f::identity(),
                     .mesh_id = 0U,
                     .material_id = 1U,
                     .sort_key = 0U,
                 }),
                 core::AssertionError);
}

TEST(RenderGraphTest, CompilesDependenciesAndResourceTransitions) {
    render::RenderGraph graph;
    const render::RenderResourceHandle vertex_buffer = graph.add_resource({
        .debug_name = "顶点上传缓冲",
        .type = render::RenderResourceType::buffer,
    });
    const render::RenderResourceHandle color_target = graph.add_resource({
        .debug_name = "场景颜色",
        .type = render::RenderResourceType::texture,
    });
    const render::RenderPassHandle upload = graph.add_pass({
        .debug_name = "上传",
        .reads = {},
        .writes = {{.resource = vertex_buffer, .state = rhi::ResourceState::copy_destination}},
        .prerequisites = {},
    });
    const render::RenderPassHandle opaque = graph.add_pass({
        .debug_name = "不透明",
        .reads = {{.resource = vertex_buffer, .state = rhi::ResourceState::vertex_buffer}},
        .writes = {{.resource = color_target, .state = rhi::ResourceState::color_attachment}},
        .prerequisites = {upload},
    });
    const render::RenderPassHandle post_process = graph.add_pass({
        .debug_name = "后处理",
        .reads = {{.resource = color_target, .state = rhi::ResourceState::shader_read}},
        .writes = {},
        .prerequisites = {opaque},
    });

    const render::CompiledRenderGraph compiled = graph.compile();
    EXPECT_EQ(compiled.execution_order(), (std::vector<render::RenderPassHandle>{upload, opaque, post_process}));
    ASSERT_EQ(compiled.barriers().size(), 4U);
    EXPECT_EQ(compiled.barriers()[0].before, rhi::ResourceState::undefined);
    EXPECT_EQ(compiled.barriers()[0].after, rhi::ResourceState::copy_destination);
    EXPECT_EQ(compiled.barriers()[1].before, rhi::ResourceState::copy_destination);
    EXPECT_EQ(compiled.barriers()[1].after, rhi::ResourceState::vertex_buffer);
    EXPECT_EQ(compiled.barriers()[3].before, rhi::ResourceState::color_attachment);
    EXPECT_EQ(compiled.barriers()[3].after, rhi::ResourceState::shader_read);
}

TEST(RenderGraphTest, RejectsUnknownOrAmbiguousResourceAccess) {
    render::RenderGraph graph;
    const render::RenderResourceHandle resource = graph.add_resource({
        .debug_name = "颜色",
        .type = render::RenderResourceType::texture,
        .initial_state = rhi::ResourceState::undefined,
    });

    EXPECT_THROW(static_cast<void>(graph.add_pass({
                     .debug_name = "无效状态",
                     .reads = {{.resource = resource, .state = rhi::ResourceState::undefined}},
                     .writes = {},
                     .prerequisites = {},
                 })),
                 core::AssertionError);
    EXPECT_THROW(static_cast<void>(graph.add_pass({
                     .debug_name = "重复访问",
                     .reads = {{.resource = resource, .state = rhi::ResourceState::shader_read}},
                     .writes = {{.resource = resource, .state = rhi::ResourceState::color_attachment}},
                     .prerequisites = {},
                 })),
                 core::AssertionError);
    EXPECT_THROW(static_cast<void>(graph.add_pass({
                     .debug_name = "未知资源",
                     .reads = {{.resource = {.index = 3U}, .state = rhi::ResourceState::shader_read}},
                     .writes = {},
                     .prerequisites = {},
                 })),
                 core::AssertionError);
}

}  // namespace gisengine::tests
