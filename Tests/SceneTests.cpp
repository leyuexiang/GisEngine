#include <gtest/gtest.h>

#include "gisengine/scene/scene.h"

namespace gisengine::tests {

TEST(SceneTest, UpdatesNestedWorldTransformsIteratively) {
    scene::Scene scene;
    const core::EntityHandle root = scene.create_entity();
    const core::EntityHandle child = scene.create_entity();
    const core::EntityHandle grandchild = scene.create_entity();

    scene.try_get_transform(root)->local_position = {.x = 1.0F, .y = 0.0F, .z = 0.0F};
    scene.try_get_transform(child)->local_position = {.x = 0.0F, .y = 2.0F, .z = 0.0F};
    scene.try_get_transform(grandchild)->local_position = {.x = 0.0F, .y = 0.0F, .z = 3.0F};
    ASSERT_TRUE(scene.set_parent(child, root));
    ASSERT_TRUE(scene.set_parent(grandchild, child));

    scene.update_transforms();

    const math::Vec3f point = scene.try_get_transform(grandchild)->world_matrix.transform_point({});
    EXPECT_EQ(point, (math::Vec3f{.x = 1.0F, .y = 2.0F, .z = 3.0F}));
}

TEST(SceneTest, PreventsCyclesAndDetachesChildrenOnDestruction) {
    scene::Scene scene;
    const core::EntityHandle root = scene.create_entity();
    const core::EntityHandle child = scene.create_entity();
    ASSERT_TRUE(scene.set_parent(child, root));

    EXPECT_FALSE(scene.set_parent(root, child));
    ASSERT_TRUE(scene.destroy_entity(root));
    EXPECT_FALSE(scene.world().is_alive(root));
    ASSERT_NE(scene.try_get_transform(child), nullptr);
    EXPECT_FALSE(scene.try_get_transform(child)->parent.valid());
}

}  // namespace gisengine::tests
