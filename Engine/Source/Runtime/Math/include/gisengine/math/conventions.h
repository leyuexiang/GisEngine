#pragma once

#include "gisengine/math/math.h"

namespace gisengine::math {

enum class Handedness {
    right_handed,
};

enum class MatrixStorage {
    row_major,
};

enum class VectorConvention {
    column_vector,
};

enum class ClipSpaceDepthRange {
    zero_to_one,
};

// 集中声明跨模块约定，避免相机、RHI 与 GIS 适配器各自解释坐标轴和深度方向。
inline constexpr Handedness k_world_handedness = Handedness::right_handed;
inline constexpr MatrixStorage k_matrix_storage = MatrixStorage::row_major;
inline constexpr VectorConvention k_vector_convention = VectorConvention::column_vector;
inline constexpr ClipSpaceDepthRange k_clip_space_depth_range = ClipSpaceDepthRange::zero_to_one;

inline constexpr Vec3f k_world_right{.x = 1.0F, .y = 0.0F, .z = 0.0F};
inline constexpr Vec3f k_world_up{.x = 0.0F, .y = 1.0F, .z = 0.0F};
inline constexpr Vec3f k_world_forward{.x = 0.0F, .y = 0.0F, .z = -1.0F};

inline constexpr float k_pi = 3.14159265358979323846F;

[[nodiscard]] constexpr float degrees_to_radians(float degrees) noexcept {
    return degrees * (k_pi / 180.0F);
}

[[nodiscard]] constexpr float radians_to_degrees(float radians) noexcept {
    return radians * (180.0F / k_pi);
}

}  // namespace gisengine::math
