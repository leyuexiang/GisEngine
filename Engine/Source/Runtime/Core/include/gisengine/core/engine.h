#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace gisengine::core {

struct EngineConfig {
    std::string project_name{"GisEngine"};
    std::uint32_t fixed_update_hz{60};
};

enum class EngineState : std::uint8_t {
    created,
    running,
    stopped,
};

class Engine final {
   public:
    explicit Engine(EngineConfig config);
    ~Engine() = default;

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    Engine(Engine&&) = delete;
    Engine& operator=(Engine&&) = delete;

    [[nodiscard]] const EngineConfig& config() const noexcept;
    [[nodiscard]] EngineState state() const noexcept;
    [[nodiscard]] std::string_view project_name() const noexcept;

    void start();
    void stop() noexcept;

   private:
    EngineConfig config_;
    EngineState state_{EngineState::created};
};

}  // namespace gisengine::core
