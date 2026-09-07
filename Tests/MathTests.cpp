#include <gtest/gtest.h>

#include "gisengine/math/conventions.h"

namespace gisengine::tests {

TEST(MathTest, ComputesCrossProductAndNormalization) {
    const math::Vec3f x_axis{.x = 1.0F, .y = 0.0F, .z = 0.0F};
    const math::Vec3f y_axis{.x = 0.0F, .y = 1.0F, .z = 0.0F};

    const math::Vec3f normal = x_axis.cross(y_axis);

    EXPECT_EQ(normal, (math::Vec3f{.x = 0.0F, .y = 0.0F, .z = 1.0F}));
    EXPECT_FLOAT_EQ(normal.normalized().length(), 1.0F);
    EXPECT_EQ(math::Vec3f{}.normalized(), math::Vec3f{});
}

TEST(MathTest, AppliesScaleBeforeTranslationForColumnVectors) {
    const math::Mat4f transform = math::Mat4f::translation({.x = 2.0F, .y = 3.0F, .z = 4.0F}) *
                                  math::Mat4f::scale({.x = 2.0F, .y = 2.0F, .z = 2.0F});

    EXPECT_EQ(transform.transform_point({.x = 1.0F, .y = 1.0F, .z = 1.0F}),
              (math::Vec3f{.x = 4.0F, .y = 5.0F, .z = 6.0F}));
}

TEST(MathTest, IntersectsPlaneAndBounds) {
    const math::Planef ground =
        math::Planef::from_point_normal({.x = 0.0F, .y = 0.0F, .z = 0.0F}, {.x = 0.0F, .y = 1.0F, .z = 0.0F});
    const math::Rayf ray =
        math::Rayf::from_origin_direction({.x = 0.0F, .y = 2.0F, .z = 0.0F}, {.x = 0.0F, .y = -1.0F, .z = 0.0F});
    const math::Aabbf bounds =
        math::Aabbf::from_center_extents({.x = 0.0F, .y = 0.0F, .z = 0.0F}, {.x = 1.0F, .y = 1.0F, .z = 1.0F});

    ASSERT_TRUE(ray.intersect_plane(ground).has_value());
    EXPECT_FLOAT_EQ(*ray.intersect_plane(ground), 2.0F);
    EXPECT_TRUE(bounds.contains({.x = 0.5F, .y = 0.0F, .z = -0.5F}));
    EXPECT_FALSE(bounds.contains({.x = 2.0F, .y = 0.0F, .z = 0.0F}));
}

TEST(MathTest, FreezesRightHandedVulkanCompatibleConvention) {
    EXPECT_EQ(math::k_world_handedness, math::Handedness::right_handed);
    EXPECT_EQ(math::k_clip_space_depth_range, math::ClipSpaceDepthRange::zero_to_one);
    EXPECT_EQ(math::k_world_right.cross(math::k_world_up), math::k_world_forward * -1.0F);
    EXPECT_FLOAT_EQ(math::degrees_to_radians(180.0F), math::k_pi);
}

}  // namespace gisengine::tests
