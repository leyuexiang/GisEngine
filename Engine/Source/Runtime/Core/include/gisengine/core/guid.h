#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace gisengine::core {

class Guid final {
   public:
    [[nodiscard]] static Guid generate();

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] std::string to_string() const;
    [[nodiscard]] constexpr bool operator==(const Guid&) const noexcept = default;

   private:
    explicit constexpr Guid(std::array<std::uint8_t, 16> bytes) noexcept : bytes_(bytes) {}

    std::array<std::uint8_t, 16> bytes_{};
};

}  // namespace gisengine::core
