#include <iostream>

#include "gisengine/runtime/runtime.h"

// 进程入口由操作系统接管异常边界；此处不应把流输出的理论异常扩散规则应用到业务代码。
// NOLINTNEXTLINE(bugprone-exception-escape)
int main() {
    gisengine::runtime::RuntimeHost host{
        gisengine::core::EngineConfig{.project_name = "TerrainDemo", .fixed_update_hz = 60U}};
    host.initialize();
    std::cout << "TerrainDemo 基线已启动；DEM 与 Vulkan 将在后续里程碑接入。\n";
    return 0;
}
