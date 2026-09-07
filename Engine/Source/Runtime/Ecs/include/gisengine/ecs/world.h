#pragma once

#include <cstddef>
#include <functional>
#include <limits>
#include <memory>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <vector>

#include "gisengine/core/diagnostics.h"
#include "gisengine/core/entity.h"

namespace gisengine::ecs {

namespace detail {

class IComponentStore {
   public:
    virtual ~IComponentStore() = default;
    virtual void remove_if_present(core::EntityHandle entity) noexcept = 0;
};

template <typename Component>
class ComponentStore final : public IComponentStore {
   public:
    template <typename... Arguments>
    Component& emplace(core::EntityHandle entity, Arguments&&... arguments) {
        const std::size_t existing_index = dense_index(entity);
        if (existing_index != k_missing_index) {
            components_[existing_index] = Component{std::forward<Arguments>(arguments)...};
            return components_[existing_index];
        }

        ensure_sparse_capacity(entity.index);
        const std::size_t new_index = components_.size();
        entities_.push_back(entity);
        components_.emplace_back(std::forward<Arguments>(arguments)...);
        sparse_[entity.index] = new_index;
        return components_.back();
    }

    [[nodiscard]] bool remove(core::EntityHandle entity) noexcept {
        const std::size_t removed_index = dense_index(entity);
        if (removed_index == k_missing_index) {
            return false;
        }

        const std::size_t last_index = components_.size() - 1U;
        if (removed_index != last_index) {
            entities_[removed_index] = entities_[last_index];
            components_[removed_index] = std::move(components_[last_index]);
            sparse_[entities_[removed_index].index] = removed_index;
        }
        entities_.pop_back();
        components_.pop_back();
        sparse_[entity.index] = k_missing_index;
        return true;
    }

    [[nodiscard]] bool contains(core::EntityHandle entity) const noexcept {
        return dense_index(entity) != k_missing_index;
    }

    [[nodiscard]] Component* try_get(core::EntityHandle entity) noexcept {
        const std::size_t index = dense_index(entity);
        return index == k_missing_index ? nullptr : &components_[index];
    }

    [[nodiscard]] const Component* try_get(core::EntityHandle entity) const noexcept {
        const std::size_t index = dense_index(entity);
        return index == k_missing_index ? nullptr : &components_[index];
    }

    template <typename Visitor>
    void for_each(Visitor&& visitor) {
        for (std::size_t index = 0; index < components_.size(); ++index) {
            std::invoke(visitor, entities_[index], components_[index]);
        }
    }

    [[nodiscard]] std::size_t size() const noexcept { return components_.size(); }

    void remove_if_present(core::EntityHandle entity) noexcept override { static_cast<void>(remove(entity)); }

   private:
    static constexpr std::size_t k_missing_index = std::numeric_limits<std::size_t>::max();

    void ensure_sparse_capacity(std::uint32_t entity_index) {
        const std::size_t required_size = static_cast<std::size_t>(entity_index) + 1U;
        if (sparse_.size() < required_size) {
            sparse_.resize(required_size, k_missing_index);
        }
    }

    [[nodiscard]] std::size_t dense_index(core::EntityHandle entity) const noexcept {
        if (!entity.valid() || entity.index >= sparse_.size()) {
            return k_missing_index;
        }
        const std::size_t index = sparse_[entity.index];
        if (index == k_missing_index || index >= entities_.size() || entities_[index] != entity) {
            return k_missing_index;
        }
        return index;
    }

    std::vector<std::size_t> sparse_;
    std::vector<core::EntityHandle> entities_;
    std::vector<Component> components_;
};

}  // namespace detail

class World final {
   public:
    [[nodiscard]] core::EntityHandle create_entity() { return entities_.create(); }

    [[nodiscard]] bool destroy_entity(core::EntityHandle entity) {
        if (!entities_.is_alive(entity)) {
            return false;
        }
        for (const auto& [type, store] : stores_) {
            static_cast<void>(type);
            store->remove_if_present(entity);
        }
        return entities_.destroy(entity);
    }

    [[nodiscard]] bool is_alive(core::EntityHandle entity) const noexcept { return entities_.is_alive(entity); }

    [[nodiscard]] std::size_t entity_count() const noexcept { return entities_.alive_count(); }

    template <typename Component, typename... Arguments>
    Component& emplace(core::EntityHandle entity, Arguments&&... arguments) {
        GISENGINE_ENSURE(entities_.is_alive(entity), "不能向无效实体添加组件");
        return store<Component>().emplace(entity, std::forward<Arguments>(arguments)...);
    }

    template <typename Component>
    [[nodiscard]] bool remove(core::EntityHandle entity) noexcept {
        if (auto* component_store = find_store<Component>(); component_store != nullptr) {
            return component_store->remove(entity);
        }
        return false;
    }

    template <typename Component>
    [[nodiscard]] bool has(core::EntityHandle entity) const noexcept {
        const auto* component_store = find_store<Component>();
        return component_store != nullptr && component_store->contains(entity);
    }

    template <typename Component>
    [[nodiscard]] Component* try_get(core::EntityHandle entity) noexcept {
        if (auto* component_store = find_store<Component>(); component_store != nullptr) {
            return component_store->try_get(entity);
        }
        return nullptr;
    }

    template <typename Component>
    [[nodiscard]] const Component* try_get(core::EntityHandle entity) const noexcept {
        if (const auto* component_store = find_store<Component>(); component_store != nullptr) {
            return component_store->try_get(entity);
        }
        return nullptr;
    }

    template <typename Component, typename Visitor>
    void each(Visitor&& visitor) {
        if (auto* component_store = find_store<Component>(); component_store != nullptr) {
            component_store->for_each(std::forward<Visitor>(visitor));
        }
    }

    template <typename Component>
    [[nodiscard]] std::size_t component_count() const noexcept {
        const auto* component_store = find_store<Component>();
        return component_store != nullptr ? component_store->size() : 0U;
    }

   private:
    template <typename Component>
    detail::ComponentStore<Component>& store() {
        const std::type_index type{typeid(Component)};
        const auto [iterator, inserted] =
            stores_.try_emplace(type, std::make_unique<detail::ComponentStore<Component>>());
        static_cast<void>(inserted);
        return static_cast<detail::ComponentStore<Component>&>(*iterator->second);
    }

    template <typename Component>
    [[nodiscard]] detail::ComponentStore<Component>* find_store() noexcept {
        const auto iterator = stores_.find(std::type_index{typeid(Component)});
        return iterator == stores_.end() ? nullptr
                                         : static_cast<detail::ComponentStore<Component>*>(iterator->second.get());
    }

    template <typename Component>
    [[nodiscard]] const detail::ComponentStore<Component>* find_store() const noexcept {
        const auto iterator = stores_.find(std::type_index{typeid(Component)});
        return iterator == stores_.end()
                   ? nullptr
                   : static_cast<const detail::ComponentStore<Component>*>(iterator->second.get());
    }

    core::EntityPool entities_;
    std::unordered_map<std::type_index, std::unique_ptr<detail::IComponentStore>> stores_;
};

}  // namespace gisengine::ecs
