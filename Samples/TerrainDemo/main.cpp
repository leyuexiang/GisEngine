#include "gisengine/runtime/runtime.h"

#include <iostream>

int main() {
    gisengine::runtime::RuntimeHost host{
        gisengine::core::EngineConfig{.project_name = "TerrainDemo", .fixed_update_hz = 60U}};
    host.initialize();
    std::cout << "TerrainDemo 基线已启动；DEM 与 Vulkan 将在后续里程碑接入。\n";
    return 0;
}
