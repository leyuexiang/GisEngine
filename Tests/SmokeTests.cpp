#include "gisengine/runtime/runtime.h"

#include <iostream>

int main() {
    gisengine::runtime::RuntimeHost host{
        gisengine::core::EngineConfig{.project_name = "SmokeTest", .fixed_update_hz = 60U}};
    if (host.engine().state() != gisengine::core::EngineState::created) {
        std::cerr << "新引擎状态错误\n";
        return 1;
    }

    host.initialize();
    if (host.engine().state() != gisengine::core::EngineState::running) {
        std::cerr << "初始化后引擎未运行\n";
        return 2;
    }
    if (host.engine().project_name() != "SmokeTest") {
        std::cerr << "项目名称未保存\n";
        return 3;
    }

    host.shutdown();
    if (host.engine().state() != gisengine::core::EngineState::stopped) {
        std::cerr << "关闭后引擎状态错误\n";
        return 4;
    }
    return 0;
}
