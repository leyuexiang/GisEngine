#pragma once

#include <string>

namespace gisengine::platform {

struct PlatformInfo {
    std::string operating_system;
    std::string compiler;
    bool web_build{false};
};

[[nodiscard]] PlatformInfo query_platform_info();

} // namespace gisengine::platform
