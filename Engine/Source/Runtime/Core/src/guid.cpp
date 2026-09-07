#include "gisengine/core/guid.h"

#include <cstddef>
#include <random>

namespace gisengine::core {

Guid Guid::generate() {
    // 每个线程独立持有生成器，避免生成资产或实体标识时产生全局锁竞争。
    thread_local std::mt19937_64 generator{[] {
        std::random_device device;
        std::seed_seq seed{device(), device(), device(), device(), device(), device(), device(), device()};
        return std::mt19937_64{seed};
    }()};

    std::array<std::uint8_t, 16> bytes{};
    for (std::size_t offset = 0; offset < bytes.size(); offset += sizeof(std::uint64_t)) {
        const std::uint64_t random_value = generator();
        for (std::size_t byte_index = 0; byte_index < sizeof(random_value); ++byte_index) {
            const auto shift = static_cast<unsigned int>(byte_index * 8U);
            bytes[offset + byte_index] = static_cast<std::uint8_t>(random_value >> shift);
        }
    }

    // 使用 RFC 4122 版本 4 与变体位，便于跨工具识别标准 UUID 文本。
    bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0FU) | 0x40U);
    bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3FU) | 0x80U);
    return Guid{bytes};
}

bool Guid::valid() const noexcept {
    for (const std::uint8_t byte : bytes_) {
        if (byte != 0U) {
            return true;
        }
    }
    return false;
}

std::string Guid::to_string() const {
    static constexpr char hexadecimal[] = "0123456789abcdef";
    std::string text;
    text.reserve(36U);

    for (std::size_t index = 0; index < bytes_.size(); ++index) {
        if (index == 4U || index == 6U || index == 8U || index == 10U) {
            text.push_back('-');
        }
        const std::uint8_t byte = bytes_[index];
        text.push_back(hexadecimal[byte >> 4U]);
        text.push_back(hexadecimal[byte & 0x0FU]);
    }
    return text;
}

}  // namespace gisengine::core
