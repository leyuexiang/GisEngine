#include "gisengine/runtime/runtime.h"

#include <iostream>

int main() {
    gisengine::runtime::RuntimeHost host{
        gisengine::core::EngineConfig{.project_name = "GisEngineEditor", .fixed_update_hz = 60U}};
    host.initialize();
    std::cout << "GisEngineEditor 基线已启动；编辑器面板将在 M06 接入。\n";
    return 0;
}
