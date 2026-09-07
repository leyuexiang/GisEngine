#include "gisengine/core/engine.h"

#include <stdexcept>
#include <utility>

namespace gisengine::core {

Engine::Engine(EngineConfig config)
    : config_(std::move(config)) {
    if (config_.project_name.empty()) {
        throw std::invalid_argument("项目名称不能为空");
    }
    if (config_.fixed_update_hz == 0U) {
        throw std::invalid_argument("固定更新频率必须大于零");
    }
}

const EngineConfig& Engine::config() const noexcept {
    return config_;
}

EngineState Engine::state() const noexcept {
    return state_;
}

std::string_view Engine::project_name() const noexcept {
    return config_.project_name;
}

void Engine::start() {
    if (state_ == EngineState::stopped) {
        throw std::logic_error("已停止的引擎实例不能重新启动");
    }
    state_ = EngineState::running;
}

void Engine::stop() noexcept {
    state_ = EngineState::stopped;
}

} // namespace gisengine::core
