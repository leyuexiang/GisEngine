#include "gisengine/runtime/runtime.h"

#include <utility>

namespace gisengine::runtime {

RuntimeHost::RuntimeHost(core::EngineConfig config)
    : engine_(std::move(config)) {
}

RuntimeHost::~RuntimeHost() {
    shutdown();
}

void RuntimeHost::initialize() {
    platform_info_ = platform::query_platform_info();
    engine_.start();
}

void RuntimeHost::shutdown() noexcept {
    if (engine_.state() == core::EngineState::running) {
        engine_.stop();
    }
}

core::Engine& RuntimeHost::engine() noexcept {
    return engine_;
}

const platform::PlatformInfo& RuntimeHost::platform_info() const noexcept {
    return platform_info_;
}

} // namespace gisengine::runtime
