#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>

namespace gisengine::math {

template <typename Scalar>
struct Vec2 {
    Scalar x{};
    Scalar y{};

    [[nodiscard]] constexpr bool operator==(const Vec2&) const noexcept = default;

    [[nodiscard]] constexpr Vec2 operator+(const Vec2& other) const noexcept {
        return {.x = x + other.x, .y = y + other.y};
    }

    [[nodiscard]] constexpr Vec2 operator-(const Vec2& other) const noexcept {
        return {.x = x - other.x, .y = y - other.y};
    }

    [[nodiscard]] constexpr Vec2 operator*(Scalar scalar) const noexcept { return {.x = x * scalar, .y = y * scalar}; }

    [[nodiscard]] constexpr Vec2 operator/(Scalar scalar) const noexcept { return {.x = x / scalar, .y = y / scalar}; }

    [[nodiscard]] constexpr Scalar dot(const Vec2& other) const noexcept { return x * other.x + y * other.y; }

    [[nodiscard]] constexpr Scalar length_squared() const noexcept { return dot(*this); }

    [[nodiscard]] Scalar length() const noexcept { return static_cast<Scalar>(std::sqrt(length_squared())); }

    [[nodiscard]] Vec2 normalized() const noexcept {
        const Scalar vector_length = length();
        if (vector_length <= std::numeric_limits<Scalar>::epsilon()) {
            return {};
        }
        return *this / vector_length;
    }
};

template <typename Scalar>
struct Vec3 {
    Scalar x{};
    Scalar y{};
    Scalar z{};

    [[nodiscard]] constexpr bool operator==(const Vec3&) const noexcept = default;

    [[nodiscard]] constexpr Vec3 operator+(const Vec3& other) const noexcept {
        return {.x = x + other.x, .y = y + other.y, .z = z + other.z};
    }

    [[nodiscard]] constexpr Vec3 operator-(const Vec3& other) const noexcept {
        return {.x = x - other.x, .y = y - other.y, .z = z - other.z};
    }

    [[nodiscard]] constexpr Vec3 operator*(Scalar scalar) const noexcept {
        return {.x = x * scalar, .y = y * scalar, .z = z * scalar};
    }

    [[nodiscard]] constexpr Vec3 operator/(Scalar scalar) const noexcept {
        return {.x = x / scalar, .y = y / scalar, .z = z / scalar};
    }

    [[nodiscard]] constexpr Scalar dot(const Vec3& other) const noexcept {
        return x * other.x + y * other.y + z * other.z;
    }

    [[nodiscard]] constexpr Vec3 cross(const Vec3& other) const noexcept {
        return {
            .x = y * other.z - z * other.y,
            .y = z * other.x - x * other.z,
            .z = x * other.y - y * other.x,
        };
    }

    [[nodiscard]] constexpr Scalar length_squared() const noexcept { return dot(*this); }

    [[nodiscard]] Scalar length() const noexcept { return static_cast<Scalar>(std::sqrt(length_squared())); }

    [[nodiscard]] Vec3 normalized() const noexcept {
        const Scalar vector_length = length();
        if (vector_length <= std::numeric_limits<Scalar>::epsilon()) {
            return {};
        }
        return *this / vector_length;
    }
};

template <typename Scalar>
struct Vec4 {
    Scalar x{};
    Scalar y{};
    Scalar z{};
    Scalar w{};

    [[nodiscard]] constexpr bool operator==(const Vec4&) const noexcept = default;

    [[nodiscard]] constexpr Vec4 operator+(const Vec4& other) const noexcept {
        return {.x = x + other.x, .y = y + other.y, .z = z + other.z, .w = w + other.w};
    }

    [[nodiscard]] constexpr Vec4 operator-(const Vec4& other) const noexcept {
        return {.x = x - other.x, .y = y - other.y, .z = z - other.z, .w = w - other.w};
    }

    [[nodiscard]] constexpr Vec4 operator*(Scalar scalar) const noexcept {
        return {.x = x * scalar, .y = y * scalar, .z = z * scalar, .w = w * scalar};
    }
};

template <typename Scalar>
struct Mat4 {
    // 采用行主序存储、列向量变换；平移分量位于最后一列。
    std::array<Scalar, 16> values{};

    [[nodiscard]] constexpr bool operator==(const Mat4&) const noexcept = default;

    [[nodiscard]] static constexpr Mat4 identity() noexcept {
        Mat4 result{};
        result.at(0, 0) = static_cast<Scalar>(1);
        result.at(1, 1) = static_cast<Scalar>(1);
        result.at(2, 2) = static_cast<Scalar>(1);
        result.at(3, 3) = static_cast<Scalar>(1);
        return result;
    }

    [[nodiscard]] static constexpr Mat4 translation(const Vec3<Scalar>& offset) noexcept {
        Mat4 result = identity();
        result.at(0, 3) = offset.x;
        result.at(1, 3) = offset.y;
        result.at(2, 3) = offset.z;
        return result;
    }

    [[nodiscard]] static constexpr Mat4 scale(const Vec3<Scalar>& factor) noexcept {
        Mat4 result{};
        result.at(0, 0) = factor.x;
        result.at(1, 1) = factor.y;
        result.at(2, 2) = factor.z;
        result.at(3, 3) = static_cast<Scalar>(1);
        return result;
    }

    [[nodiscard]] constexpr Scalar& at(std::size_t row, std::size_t column) noexcept {
        return values[row * 4U + column];
    }

    [[nodiscard]] constexpr const Scalar& at(std::size_t row, std::size_t column) const noexcept {
        return values[row * 4U + column];
    }

    [[nodiscard]] constexpr Mat4 operator*(const Mat4& other) const noexcept {
        Mat4 result{};
        for (std::size_t row = 0; row < 4U; ++row) {
            for (std::size_t column = 0; column < 4U; ++column) {
                for (std::size_t inner = 0; inner < 4U; ++inner) {
                    result.at(row, column) += at(row, inner) * other.at(inner, column);
                }
            }
        }
        return result;
    }

    [[nodiscard]] constexpr Vec3<Scalar> transform_point(const Vec3<Scalar>& point) const noexcept {
        const Scalar x_result = at(0, 0) * point.x + at(0, 1) * point.y + at(0, 2) * point.z + at(0, 3);
        const Scalar y_result = at(1, 0) * point.x + at(1, 1) * point.y + at(1, 2) * point.z + at(1, 3);
        const Scalar z_result = at(2, 0) * point.x + at(2, 1) * point.y + at(2, 2) * point.z + at(2, 3);
        const Scalar w_result = at(3, 0) * point.x + at(3, 1) * point.y + at(3, 2) * point.z + at(3, 3);

        if (std::abs(w_result) <= std::numeric_limits<Scalar>::epsilon() || w_result == static_cast<Scalar>(1)) {
            return {.x = x_result, .y = y_result, .z = z_result};
        }
        return {.x = x_result / w_result, .y = y_result / w_result, .z = z_result / w_result};
    }
};

template <typename Scalar>
struct Quat {
    Scalar x{};
    Scalar y{};
    Scalar z{};
    Scalar w{static_cast<Scalar>(1)};

    [[nodiscard]] static Quat from_axis_angle(Vec3<Scalar> axis, Scalar angle_radians) noexcept {
        const Vec3<Scalar> normalized_axis = axis.normalized();
        if (normalized_axis.length_squared() <= std::numeric_limits<Scalar>::epsilon()) {
            return {};
        }
        const Scalar half_angle = angle_radians / static_cast<Scalar>(2);
        const Scalar sine = static_cast<Scalar>(std::sin(half_angle));
        return {
            .x = normalized_axis.x * sine,
            .y = normalized_axis.y * sine,
            .z = normalized_axis.z * sine,
            .w = static_cast<Scalar>(std::cos(half_angle)),
        };
    }

    [[nodiscard]] Quat normalized() const noexcept {
        const Scalar quaternion_length = static_cast<Scalar>(std::sqrt(x * x + y * y + z * z + w * w));
        if (quaternion_length <= std::numeric_limits<Scalar>::epsilon()) {
            return {};
        }
        return {.x = x / quaternion_length,
                .y = y / quaternion_length,
                .z = z / quaternion_length,
                .w = w / quaternion_length};
    }

    [[nodiscard]] constexpr Quat conjugate() const noexcept { return {.x = -x, .y = -y, .z = -z, .w = w}; }

    [[nodiscard]] Mat4<Scalar> to_matrix() const noexcept {
        const Quat quaternion = normalized();
        const Scalar xx = quaternion.x * quaternion.x;
        const Scalar yy = quaternion.y * quaternion.y;
        const Scalar zz = quaternion.z * quaternion.z;
        const Scalar xy = quaternion.x * quaternion.y;
        const Scalar xz = quaternion.x * quaternion.z;
        const Scalar yz = quaternion.y * quaternion.z;
        const Scalar xw = quaternion.x * quaternion.w;
        const Scalar yw = quaternion.y * quaternion.w;
        const Scalar zw = quaternion.z * quaternion.w;
        const Scalar two = static_cast<Scalar>(2);

        Mat4<Scalar> result = Mat4<Scalar>::identity();
        result.at(0, 0) = static_cast<Scalar>(1) - two * (yy + zz);
        result.at(0, 1) = two * (xy - zw);
        result.at(0, 2) = two * (xz + yw);
        result.at(1, 0) = two * (xy + zw);
        result.at(1, 1) = static_cast<Scalar>(1) - two * (xx + zz);
        result.at(1, 2) = two * (yz - xw);
        result.at(2, 0) = two * (xz - yw);
        result.at(2, 1) = two * (yz + xw);
        result.at(2, 2) = static_cast<Scalar>(1) - two * (xx + yy);
        return result;
    }

    [[nodiscard]] Vec3<Scalar> rotate(const Vec3<Scalar>& vector) const noexcept {
        const Quat normalized_quaternion = normalized();
        const Quat vector_quaternion{.x = vector.x, .y = vector.y, .z = vector.z, .w = 0};
        const Quat rotated = normalized_quaternion * vector_quaternion * normalized_quaternion.conjugate();
        return {.x = rotated.x, .y = rotated.y, .z = rotated.z};
    }

    [[nodiscard]] constexpr Quat operator*(const Quat& other) const noexcept {
        return {
            .x = w * other.x + x * other.w + y * other.z - z * other.y,
            .y = w * other.y - x * other.z + y * other.w + z * other.x,
            .z = w * other.z + x * other.y - y * other.x + z * other.w,
            .w = w * other.w - x * other.x - y * other.y - z * other.z,
        };
    }
};

template <typename Scalar>
struct Aabb {
    Vec3<Scalar> minimum{};
    Vec3<Scalar> maximum{};

    [[nodiscard]] static constexpr Aabb from_center_extents(const Vec3<Scalar>& center,
                                                            const Vec3<Scalar>& extents) noexcept {
        return {.minimum = center - extents, .maximum = center + extents};
    }

    [[nodiscard]] constexpr bool valid() const noexcept {
        return minimum.x <= maximum.x && minimum.y <= maximum.y && minimum.z <= maximum.z;
    }

    [[nodiscard]] constexpr Vec3<Scalar> center() const noexcept {
        return (minimum + maximum) * static_cast<Scalar>(0.5);
    }

    [[nodiscard]] constexpr Vec3<Scalar> extents() const noexcept {
        return (maximum - minimum) * static_cast<Scalar>(0.5);
    }

    [[nodiscard]] constexpr bool contains(const Vec3<Scalar>& point) const noexcept {
        return valid() && point.x >= minimum.x && point.x <= maximum.x && point.y >= minimum.y &&
               point.y <= maximum.y && point.z >= minimum.z && point.z <= maximum.z;
    }

    [[nodiscard]] constexpr bool intersects(const Aabb& other) const noexcept {
        return valid() && other.valid() && minimum.x <= other.maximum.x && maximum.x >= other.minimum.x &&
               minimum.y <= other.maximum.y && maximum.y >= other.minimum.y && minimum.z <= other.maximum.z &&
               maximum.z >= other.minimum.z;
    }
};

template <typename Scalar>
struct Sphere {
    Vec3<Scalar> center{};
    Scalar radius{};

    [[nodiscard]] constexpr bool valid() const noexcept { return radius >= static_cast<Scalar>(0); }

    [[nodiscard]] constexpr bool contains(const Vec3<Scalar>& point) const noexcept {
        return valid() && (point - center).length_squared() <= radius * radius;
    }

    [[nodiscard]] constexpr bool intersects(const Sphere& other) const noexcept {
        const Scalar combined_radius = radius + other.radius;
        return valid() && other.valid() &&
               (center - other.center).length_squared() <= combined_radius * combined_radius;
    }
};

template <typename Scalar>
struct Plane {
    Vec3<Scalar> normal{};
    Scalar distance{};

    [[nodiscard]] static Plane from_point_normal(const Vec3<Scalar>& point, Vec3<Scalar> normal) noexcept {
        const Vec3<Scalar> normalized_normal = normal.normalized();
        if (normalized_normal.length_squared() <= std::numeric_limits<Scalar>::epsilon()) {
            return {};
        }
        return {.normal = normalized_normal, .distance = -normalized_normal.dot(point)};
    }

    [[nodiscard]] static Plane from_coefficients(Scalar a, Scalar b, Scalar c, Scalar d) noexcept {
        const Vec3<Scalar> raw_normal{.x = a, .y = b, .z = c};
        const Scalar normal_length = raw_normal.length();
        if (normal_length <= std::numeric_limits<Scalar>::epsilon()) {
            return {};
        }
        return {.normal = raw_normal / normal_length, .distance = d / normal_length};
    }

    [[nodiscard]] constexpr Scalar signed_distance(const Vec3<Scalar>& point) const noexcept {
        return normal.dot(point) + distance;
    }
};

template <typename Scalar>
struct Ray {
    Vec3<Scalar> origin{};
    Vec3<Scalar> direction{};

    [[nodiscard]] static Ray from_origin_direction(const Vec3<Scalar>& origin, Vec3<Scalar> direction) noexcept {
        return {.origin = origin, .direction = direction.normalized()};
    }

    [[nodiscard]] constexpr Vec3<Scalar> at(Scalar distance) const noexcept { return origin + direction * distance; }

    [[nodiscard]] std::optional<Scalar> intersect_plane(const Plane<Scalar>& plane) const noexcept {
        const Scalar denominator = plane.normal.dot(direction);
        if (std::abs(denominator) <= std::numeric_limits<Scalar>::epsilon()) {
            return std::nullopt;
        }

        const Scalar distance = -plane.signed_distance(origin) / denominator;
        if (distance < static_cast<Scalar>(0)) {
            return std::nullopt;
        }
        return distance;
    }
};

template <typename Scalar>
struct Frustum {
    Plane<Scalar> left{};
    Plane<Scalar> right{};
    Plane<Scalar> bottom{};
    Plane<Scalar> top{};
    Plane<Scalar> near{};
    Plane<Scalar> far{};

    // 从世界到裁剪空间的矩阵提取平面；深度范围固定为 Vulkan/WebGPU 的 [0, 1]。
    [[nodiscard]] static Frustum from_view_projection(const Mat4<Scalar>& matrix) noexcept {
        const auto plane = [&matrix](std::size_t first_row, Scalar first_scale, std::size_t second_row,
                                     Scalar second_scale) {
            return Plane<Scalar>::from_coefficients(
                first_scale * matrix.at(first_row, 0) + second_scale * matrix.at(second_row, 0),
                first_scale * matrix.at(first_row, 1) + second_scale * matrix.at(second_row, 1),
                first_scale * matrix.at(first_row, 2) + second_scale * matrix.at(second_row, 2),
                first_scale * matrix.at(first_row, 3) + second_scale * matrix.at(second_row, 3));
        };

        const Scalar positive = static_cast<Scalar>(1);
        const Scalar negative = static_cast<Scalar>(-1);
        return {
            .left = plane(3U, positive, 0U, positive),
            .right = plane(3U, positive, 0U, negative),
            .bottom = plane(3U, positive, 1U, positive),
            .top = plane(3U, positive, 1U, negative),
            .near = plane(2U, positive, 2U, static_cast<Scalar>(0)),
            .far = plane(3U, positive, 2U, negative),
        };
    }

    [[nodiscard]] constexpr bool contains(const Vec3<Scalar>& point) const noexcept {
        return left.signed_distance(point) >= static_cast<Scalar>(0) &&
               right.signed_distance(point) >= static_cast<Scalar>(0) &&
               bottom.signed_distance(point) >= static_cast<Scalar>(0) &&
               top.signed_distance(point) >= static_cast<Scalar>(0) &&
               near.signed_distance(point) >= static_cast<Scalar>(0) &&
               far.signed_distance(point) >= static_cast<Scalar>(0);
    }

    [[nodiscard]] constexpr bool intersects(const Aabb<Scalar>& bounds) const noexcept {
        return intersects_plane(left, bounds) && intersects_plane(right, bounds) && intersects_plane(bottom, bounds) &&
               intersects_plane(top, bounds) && intersects_plane(near, bounds) && intersects_plane(far, bounds);
    }

   private:
    [[nodiscard]] static constexpr bool intersects_plane(const Plane<Scalar>& plane,
                                                         const Aabb<Scalar>& bounds) noexcept {
        if (!bounds.valid()) {
            return false;
        }
        const Vec3<Scalar> positive_vertex{
            .x = plane.normal.x >= static_cast<Scalar>(0) ? bounds.maximum.x : bounds.minimum.x,
            .y = plane.normal.y >= static_cast<Scalar>(0) ? bounds.maximum.y : bounds.minimum.y,
            .z = plane.normal.z >= static_cast<Scalar>(0) ? bounds.maximum.z : bounds.minimum.z,
        };
        return plane.signed_distance(positive_vertex) >= static_cast<Scalar>(0);
    }
};

using Vec2f = Vec2<float>;
using Vec3f = Vec3<float>;
using Vec4f = Vec4<float>;
using Vec2d = Vec2<double>;
using Vec3d = Vec3<double>;
using Vec4d = Vec4<double>;
using Mat4f = Mat4<float>;
using Mat4d = Mat4<double>;
using Quatf = Quat<float>;
using Quatd = Quat<double>;
using Aabbf = Aabb<float>;
using Aabbd = Aabb<double>;
using Spheref = Sphere<float>;
using Sphered = Sphere<double>;
using Planef = Plane<float>;
using Planed = Plane<double>;
using Rayf = Ray<float>;
using Rayd = Ray<double>;
using Frustumf = Frustum<float>;
using Frustumd = Frustum<double>;

}  // namespace gisengine::math
