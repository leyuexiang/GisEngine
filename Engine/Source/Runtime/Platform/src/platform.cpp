#include "gisengine/platform/platform.h"

namespace gisengine::platform {

PlatformInfo query_platform_info() {
    PlatformInfo info{
        .operating_system = "未知",
        .compiler = "未知",
        .web_build = false,
    };

#if defined(__EMSCRIPTEN__)
    info.operating_system = "WebAssembly";
    info.compiler = "Emscripten";
    info.web_build = true;
#elif defined(_WIN32)
    info.operating_system = "Windows";
#elif defined(__APPLE__)
    info.operating_system = "macOS";
#elif defined(__linux__)
    info.operating_system = "Linux";
#endif

#if defined(_MSC_VER)
    info.compiler = "MSVC";
#elif defined(__clang__)
    info.compiler = "Clang";
#elif defined(__GNUC__)
    info.compiler = "GCC";
#endif

    return info;
}

} // namespace gisengine::platform
