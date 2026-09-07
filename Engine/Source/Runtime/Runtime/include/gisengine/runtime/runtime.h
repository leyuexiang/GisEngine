#pragma once

#include "gisengine/core/engine.h"
#include "gisengine/platform/platform.h"

namespace gisengine::runtime {

class RuntimeHost final {
public:
    explicit RuntimeHost(core::EngineConfig config);
    ~RuntimeHost();

    RuntimeHost(const RuntimeHost&) = delete;
    RuntimeHost& operator=(const RuntimeHost&) = delete;

    void initialize();
    void shutdown() noexcept;

    [[nodiscard]] core::Engine& engine() noexcept;
    [[nodiscard]] const platform::PlatformInfo& platform_info() const noexcept;

private:
    core::Engine engine_;
    platform::PlatformInfo platform_info_;
};

} // namespace gisengine::runtime
