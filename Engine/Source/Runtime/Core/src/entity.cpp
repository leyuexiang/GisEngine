#include "gisengine/core/entity.h"

#include <limits>
#include <stdexcept>

namespace gisengine::core {

EntityHandle EntityPool::create() {
    std::uint32_t index = 0U;
    if (free_indices_.empty()) {
        if (generations_.size() > std::numeric_limits<std::uint32_t>::max()) {
            throw std::length_error("实体索引已耗尽");
        }
        index = static_cast<std::uint32_t>(generations_.size());
        generations_.push_back(1U);
        alive_.push_back(1U);
    } else {
        index = free_indices_.back();
        free_indices_.pop_back();
        alive_[index] = 1U;
    }

    ++alive_count_;
    return {.index = index, .generation = generations_[index]};
}

bool EntityPool::destroy(EntityHandle entity) {
    if (!is_alive(entity)) {
        return false;
    }

    alive_[entity.index] = 0U;
    if (generations_[entity.index] == std::numeric_limits<std::uint32_t>::max()) {
        // 跳过零代数，保证零始终表示无效句柄。
        generations_[entity.index] = 1U;
    } else {
        ++generations_[entity.index];
    }
    free_indices_.push_back(entity.index);
    --alive_count_;
    return true;
}

bool EntityPool::is_alive(EntityHandle entity) const noexcept {
    return entity.valid() && entity.index < generations_.size() && alive_[entity.index] != 0U &&
           generations_[entity.index] == entity.generation;
}

std::size_t EntityPool::alive_count() const noexcept {
    return alive_count_;
}

}  // namespace gisengine::core
