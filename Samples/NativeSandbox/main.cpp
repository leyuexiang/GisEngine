#include <exception>
#include <iostream>

#include "gisengine/runtime/runtime.h"

// 进程入口已经将引擎初始化失败转换为退出码；流输出的理论异常不属于可恢复业务错误。
// NOLINTNEXTLINE(bugprone-exception-escape)
int main() {
    try {
        gisengine::runtime::RuntimeHost host{
            gisengine::core::EngineConfig{.project_name = "NativeSandbox", .fixed_update_hz = 60U}};
        host.initialize();

        std::cout << "GisEngine NativeSandbox\n"
                  << "平台: " << host.platform_info().operating_system << "\n"
                  << "编译器: " << host.platform_info().compiler << "\n"
                  << "状态: " << static_cast<int>(host.engine().state()) << "\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "启动失败: " << error.what() << '\n';
        return 1;
    }
}
