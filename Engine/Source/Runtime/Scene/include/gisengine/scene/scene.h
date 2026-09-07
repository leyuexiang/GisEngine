#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "gisengine/ecs/world.h"
#include "gisengine/math/math.h"

namespace gisengine::scene {

struct Transform {
    math::Vec3f local_position{};
    math::Quatf local_rotation{};
    math::Vec3f local_scale{.x = 1.0F, .y = 1.0F, .z = 1.0F};
    math::Mat4f world_matrix{math::Mat4f::identity()};
    core::EntityHandle parent{};
    std::vector<core::EntityHandle> children;

   private:
    std::uint64_t update_version_{0U};

    friend class Scene;
};

class Scene final {
   public:
    [[nodiscard]] core::EntityHandle create_entity() {
        const core::EntityHandle entity = world_.create_entity();
        world_.emplace<Transform>(entity);
        return entity;
    }

    [[nodiscard]] bool destroy_entity(core::EntityHandle entity) {
        Transform* transform = world_.try_get<Transform>(entity);
        if (transform == nullptr) {
            return world_.destroy_entity(entity);
        }

        // 保留子实体的局部变换，仅解除层级关系，避免销毁父节点时隐式删除业务对象。
        for (const core::EntityHandle child : transform->children) {
            if (Transform* child_transform = world_.try_get<Transform>(child); child_transform != nullptr) {
                child_transform->parent = {};
            }
        }
        detach_from_parent(entity, *transform);
        return world_.destroy_entity(entity);
    }

    [[nodiscard]] bool set_parent(core::EntityHandle child, core::EntityHandle parent) noexcept {
        Transform* child_transform = world_.try_get<Transform>(child);
        if (child_transform == nullptr || (parent.valid() && world_.try_get<Transform>(parent) == nullptr)) {
            return false;
        }
        if (parent == child || introduces_cycle(child, parent)) {
            return false;
        }
        if (child_transform->parent == parent) {
            return true;
        }

        detach_from_parent(child, *child_transform);
        if (!parent.valid()) {
            return true;
        }

        Transform* parent_transform = world_.try_get<Transform>(parent);
        parent_transform->children.push_back(child);
        child_transform->parent = parent;
        return true;
    }

    void update_transforms() {
        ++transform_version_;
        if (transform_version_ == 0U) {
            // 代数回绕时从一重新开始，零仍保留为“未更新”标记。
            transform_version_ = 1U;
            world_.each<Transform>([](core::EntityHandle, Transform& transform) { transform.update_version_ = 0U; });
        }

        world_.each<Transform>([this](core::EntityHandle entity, Transform& transform) {
            if (!transform.parent.valid() || !world_.is_alive(transform.parent)) {
                update_subtree(entity, math::Mat4f::identity());
            }
        });

        // 公共 API 阻止循环；此处仍处理外部反序列化损坏数据，保证每帧都有确定性降级结果。
        world_.each<Transform>([this](core::EntityHandle entity, Transform& transform) {
            if (transform.update_version_ != transform_version_) {
                update_subtree(entity, math::Mat4f::identity());
            }
        });
    }

    [[nodiscard]] Transform* try_get_transform(core::EntityHandle entity) noexcept {
        return world_.try_get<Transform>(entity);
    }

    [[nodiscard]] const Transform* try_get_transform(core::EntityHandle entity) const noexcept {
        return world_.try_get<Transform>(entity);
    }

    [[nodiscard]] ecs::World& world() noexcept { return world_; }

    [[nodiscard]] const ecs::World& world() const noexcept { return world_; }

   private:
    struct UpdateNode {
        core::EntityHandle entity;
        math::Mat4f parent_world;
    };

    [[nodiscard]] static math::Mat4f local_matrix(const Transform& transform) noexcept {
        return math::Mat4f::translation(transform.local_position) * transform.local_rotation.to_matrix() *
               math::Mat4f::scale(transform.local_scale);
    }

    [[nodiscard]] bool introduces_cycle(core::EntityHandle child, core::EntityHandle parent) const noexcept {
        for (core::EntityHandle current = parent; current.valid();) {
            if (current == child) {
                return true;
            }
            const Transform* transform = world_.try_get<Transform>(current);
            if (transform == nullptr) {
                return false;
            }
            current = transform->parent;
        }
        return false;
    }

    void detach_from_parent(core::EntityHandle child, Transform& child_transform) noexcept {
        if (Transform* parent_transform = world_.try_get<Transform>(child_transform.parent);
            parent_transform != nullptr) {
            std::erase(parent_transform->children, child);
        }
        child_transform.parent = {};
    }

    void update_subtree(core::EntityHandle root, const math::Mat4f& parent_world) {
        update_stack_.clear();
        update_stack_.push_back({.entity = root, .parent_world = parent_world});

        while (!update_stack_.empty()) {
            UpdateNode node = std::move(update_stack_.back());
            update_stack_.pop_back();
            Transform* transform = world_.try_get<Transform>(node.entity);
            if (transform == nullptr || transform->update_version_ == transform_version_) {
                continue;
            }

            transform->world_matrix = node.parent_world * local_matrix(*transform);
            transform->update_version_ = transform_version_;

            for (auto child = transform->children.rbegin(); child != transform->children.rend(); ++child) {
                Transform* child_transform = world_.try_get<Transform>(*child);
                if (child_transform != nullptr && child_transform->parent == node.entity) {
                    update_stack_.push_back({.entity = *child, .parent_world = transform->world_matrix});
                }
            }
        }
    }

    ecs::World world_;
    std::vector<UpdateNode> update_stack_;
    std::uint64_t transform_version_{0U};
};

}  // namespace gisengine::scene
