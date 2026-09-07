cmake_minimum_required(VERSION 3.24)

project(GisEngine
    VERSION 0.1.0
    DESCRIPTION "GisEngine native-first 3D game engine"
    LANGUAGES CXX
)

option(GISENGINE_BUILD_SAMPLES "构建原生示例" ON)
option(GISENGINE_BUILD_TOOLS "构建资产工具" ON)
option(GISENGINE_BUILD_TESTS "构建 CTest 测试" ON)
option(GISENGINE_ENABLE_WARNINGS "启用严格编译警告" ON)
option(GISENGINE_WARNINGS_AS_ERRORS "将项目编译警告视为错误" ON)
option(GISENGINE_ENABLE_CLANG_TIDY "在编译时执行 clang-tidy 静态检查" OFF)
option(GISENGINE_ENABLE_SANITIZERS "启用地址与未定义行为 Sanitizer" OFF)
option(GISENGINE_USE_SYSTEM_GTEST "使用已安装的 GoogleTest 构建单元测试" OFF)
option(GISENGINE_FETCH_GOOGLETEST "下载固定版本 GoogleTest 源码构建单元测试" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(GNUInstallDirs)
include(cmake/GisEngineQuality.cmake)

add_library(GisEngineMath INTERFACE)
add_library(GisEngine::Math ALIAS GisEngineMath)
target_include_directories(GisEngineMath
    INTERFACE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/Engine/Source/Runtime/Math/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_compile_features(GisEngineMath INTERFACE cxx_std_20)

add_library(GisEngineEcs INTERFACE)
add_library(GisEngine::Ecs ALIAS GisEngineEcs)
target_include_directories(GisEngineEcs
    INTERFACE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/Engine/Source/Runtime/Ecs/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(GisEngineEcs INTERFACE GisEngine::Core)
target_compile_features(GisEngineEcs INTERFACE cxx_std_20)

add_library(GisEngineScene INTERFACE)
add_library(GisEngine::Scene ALIAS GisEngineScene)
target_include_directories(GisEngineScene
    INTERFACE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/Engine/Source/Runtime/Scene/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(GisEngineScene INTERFACE GisEngine::Ecs GisEngine::Math)
target_compile_features(GisEngineScene INTERFACE cxx_std_20)

add_library(GisEngineCore STATIC
    Engine/Source/Runtime/Core/src/diagnostics.cpp
    Engine/Source/Runtime/Core/src/engine.cpp
    Engine/Source/Runtime/Core/src/entity.cpp
    Engine/Source/Runtime/Core/src/guid.cpp
)
add_library(GisEngine::Core ALIAS GisEngineCore)
target_include_directories(GisEngineCore
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/Engine/Source/Runtime/Core/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(GisEngineCore PUBLIC GisEngine::Math)

add_library(GisEngineRhi STATIC
    Engine/Source/Runtime/Rhi/src/rhi.cpp
    Engine/Source/Runtime/Rhi/src/vulkan.cpp
)
add_library(GisEngine::Rhi ALIAS GisEngineRhi)
target_include_directories(GisEngineRhi
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/Engine/Source/Runtime/Rhi/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(GisEngineRhi PUBLIC GisEngine::Core)
if(CMAKE_DL_LIBS)
    target_link_libraries(GisEngineRhi PRIVATE ${CMAKE_DL_LIBS})
endif()

add_library(GisEnginePlatform STATIC
    Engine/Source/Runtime/Platform/src/platform.cpp
)
add_library(GisEngine::Platform ALIAS GisEnginePlatform)
target_include_directories(GisEnginePlatform
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/Engine/Source/Runtime/Platform/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(GisEnginePlatform PUBLIC GisEngine::Core)

add_library(GisEngineRuntime STATIC
    Engine/Source/Runtime/Runtime/src/runtime.cpp
)
add_library(GisEngine::Runtime ALIAS GisEngineRuntime)
target_include_directories(GisEngineRuntime
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/Engine/Source/Runtime/Runtime/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(GisEngineRuntime PUBLIC GisEngine::Core GisEngine::Platform)

function(gisengine_configure_target target_name)
    target_compile_features(${target_name} PRIVATE cxx_std_20)
    set_target_properties(${target_name} PROPERTIES
        CXX_EXTENSIONS OFF
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
    )
    gisengine_enable_warnings(${target_name})
    gisengine_enable_sanitizers(${target_name})
    gisengine_enable_clang_tidy(${target_name})
endfunction()

gisengine_configure_target(GisEngineCore)
gisengine_configure_target(GisEngineRhi)
gisengine_configure_target(GisEnginePlatform)
gisengine_configure_target(GisEngineRuntime)

if(GISENGINE_BUILD_SAMPLES)
    add_executable(NativeSandbox Samples/NativeSandbox/main.cpp)
    target_link_libraries(NativeSandbox PRIVATE GisEngine::Runtime)
    gisengine_configure_target(NativeSandbox)

    add_executable(TerrainDemo Samples/TerrainDemo/main.cpp)
    target_link_libraries(TerrainDemo PRIVATE GisEngine::Runtime)
    gisengine_configure_target(TerrainDemo)

    add_executable(GisEngineEditor Samples/Editor/main.cpp)
    target_link_libraries(GisEngineEditor PRIVATE GisEngine::Runtime)
    gisengine_configure_target(GisEngineEditor)
endif()

if(GISENGINE_BUILD_TOOLS)
    add_executable(AssetCooker Tools/AssetCooker/main.cpp)
    target_link_libraries(AssetCooker PRIVATE GisEngine::Core)
    gisengine_configure_target(AssetCooker)
endif()

if(GISENGINE_BUILD_TESTS)
    include(CTest)
    enable_testing()

    add_executable(GisEngineSmokeTests Tests/SmokeTests.cpp)
    target_link_libraries(GisEngineSmokeTests PRIVATE GisEngine::Runtime GisEngine::Scene GisEngine::Rhi)
    gisengine_configure_target(GisEngineSmokeTests)
    add_test(NAME GisEngine.Smoke COMMAND GisEngineSmokeTests)
    set_tests_properties(GisEngine.Smoke PROPERTIES LABELS "smoke")

    if(GISENGINE_USE_SYSTEM_GTEST)
        find_package(GTest CONFIG REQUIRED)
    elseif(GISENGINE_FETCH_GOOGLETEST)
        include(FetchContent)
        # 固定提交对应 GoogleTest 1.17.0，保证 MinGW、MSVC 和 CI 各自从源码构建匹配 ABI 的测试库。
        set(INSTALL_GTEST OFF CACHE BOOL "不安装测试框架" FORCE)
        set(gtest_force_shared_crt ON CACHE BOOL "GoogleTest 与 MSVC 共享运行库" FORCE)
        FetchContent_Declare(
            googletest
            GIT_REPOSITORY https://gitee.com/mirrors/googletest.git
            GIT_TAG 52eb8108c5bdec04579160ae17225d66034bd723
            GIT_SHALLOW TRUE
        )
        if(EXISTS "${PROJECT_SOURCE_DIR}/ThirdParty/googletest/CMakeLists.txt")
            # 本地已缓存固定提交时完全离线构建，CI 或新环境再回退到镜像仓库。
            set(FETCHCONTENT_SOURCE_DIR_GOOGLETEST "${PROJECT_SOURCE_DIR}/ThirdParty/googletest" CACHE PATH
                "GoogleTest 本地源码目录" FORCE)
        endif()
        FetchContent_MakeAvailable(googletest)

        if(GISENGINE_ENABLE_SANITIZERS)
            # MSVC 会校验标准容器的 AddressSanitizer 注解；测试可执行文件与 GoogleTest 静态库必须使用一致选项。
            foreach(gisengine_gtest_target IN ITEMS gtest gtest_main gmock gmock_main)
                if(TARGET ${gisengine_gtest_target})
                    gisengine_enable_sanitizers(${gisengine_gtest_target})
                endif()
            endforeach()
        endif()
    else()
        message(STATUS "未启用 GoogleTest；仅构建无第三方依赖的冒烟测试。")
    endif()

    if(GISENGINE_USE_SYSTEM_GTEST OR GISENGINE_FETCH_GOOGLETEST)
        include(GoogleTest)
        add_executable(GisEngineUnitTests
            Tests/EngineTests.cpp
            Tests/EntityTests.cpp
            Tests/EcsTests.cpp
            Tests/MathTests.cpp
            Tests/SceneTests.cpp
            Tests/RhiTests.cpp
            Tests/VulkanTests.cpp
        )
        target_link_libraries(GisEngineUnitTests PRIVATE GisEngine::Runtime GisEngine::Scene GisEngine::Rhi GTest::gtest_main)
        gisengine_configure_target(GisEngineUnitTests)
        gtest_discover_tests(GisEngineUnitTests PROPERTIES LABELS "unit")
    endif()
endif()

gisengine_add_quality_targets()

install(TARGETS GisEngineMath GisEngineCore GisEngineEcs GisEngineScene GisEngineRhi GisEnginePlatform GisEngineRuntime
    EXPORT GisEngineTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY Engine/Source/Runtime/Core/include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY Engine/Source/Runtime/Math/include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY Engine/Source/Runtime/Ecs/include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY Engine/Source/Runtime/Scene/include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY Engine/Source/Runtime/Rhi/include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY Engine/Source/Runtime/Platform/include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY Engine/Source/Runtime/Runtime/include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(EXPORT GisEngineTargets
    NAMESPACE GisEngine::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/GisEngine
)
