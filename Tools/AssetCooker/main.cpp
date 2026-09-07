#include "gisengine/core/engine.h"

#include <iostream>

int main(int argc, char* argv[]) {
    std::cout << "AssetCooker 基线\n";
    if (argc < 2) {
        std::cout << "用法: AssetCooker <源资产路径>\n";
        return 0;
    }
    std::cout << "待处理资产: " << argv[1] << "\n";
    std::cout << "当前仅验证命令行入口；实际烘焙将在 T-COOK-001 实现。\n";
    return 0;
}
