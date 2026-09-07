#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace gisengine::core {

struct EntityHandle {
    std::uint32_t index{0};
    std::uint32_t generation{0};

    [[nodiscard]] constexpr bool operator==(const EntityHandle&) const noexcept = default;
    [[nodiscard]] constexpr bool valid() const noexcept { return generation != 0U; }
};

class EntityPool final {
   public:
    [[nodiscard]] EntityHandle create();
    [[nodiscard]] bool destroy(EntityHandle entity);
    [[nodiscard]] bool is_alive(EntityHandle entity) const noexcept;
    [[nodiscard]] std::size_t alive_count() const noexcept;

   private:
    std::vector<std::uint32_t> generations_;
    std::vector<std::uint8_t> alive_;
    std::vector<std::uint32_t> free_indices_;
    std::size_t alive_count_{0};
};

}  // namespace gisengine::core
