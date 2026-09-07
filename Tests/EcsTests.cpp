#include <gtest/gtest.h>

#include "gisengine/ecs/world.h"

namespace gisengine::tests {

namespace {

struct Position {
    int value{0};
};

struct Velocity {
    int value{0};
};

}  // namespace

TEST(WorldTest, StoresQueriesAndRemovesSparseComponents) {
    ecs::World world;
    const core::EntityHandle first = world.create_entity();
    const core::EntityHandle second = world.create_entity();

    world.emplace<Position>(first, 3);
    world.emplace<Position>(second, 5);
    world.emplace<Velocity>(second, 2);

    int position_total = 0;
    world.each<Position>(
        [&position_total](core::EntityHandle, Position& position) { position_total += position.value; });

    EXPECT_EQ(position_total, 8);
    EXPECT_TRUE(world.has<Velocity>(second));
    EXPECT_TRUE(world.remove<Position>(first));
    EXPECT_FALSE(world.has<Position>(first));
    EXPECT_EQ(world.component_count<Position>(), 1U);
}

TEST(WorldTest, RemovesAllComponentsAndRejectsDestroyedEntity) {
    ecs::World world;
    const core::EntityHandle entity = world.create_entity();
    world.emplace<Position>(entity, 1);
    world.emplace<Velocity>(entity, 1);

    ASSERT_TRUE(world.destroy_entity(entity));

    EXPECT_FALSE(world.is_alive(entity));
    EXPECT_EQ(world.component_count<Position>(), 0U);
    EXPECT_EQ(world.component_count<Velocity>(), 0U);
    EXPECT_THROW(world.emplace<Position>(entity, 2), core::AssertionError);
}

}  // namespace gisengine::tests
