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
option(GISENGINE_FETCH_VULKAN_HEADERS "下载固定版本 Vulkan-Headers 以编译原生 Vulkan 后端" ON)
option(GISENGINE_FETCH_SHADERC "下载固定版本 shaderc/glslc 以编译 Vulkan 着色器" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(GNUInstallDirs)
include(cmake/GisEngineQuality.cmake)

if(NOT GISENGINE_FETCH_VULKAN_HEADERS)
    message(FATAL_ERROR "Vulkan 后端需要固定版本的 Vulkan-Headers。")
endif()

include(FetchContent)
# 固定 Vulkan-Headers 提交，避免本机 SDK 版本差异影响 Vulkan ABI 与跨平台构建。
set(VULKAN_HEADERS_ENABLE_TESTS OFF CACHE BOOL "不构建 Vulkan-Headers 自身测试" FORCE)
set(VULKAN_HEADERS_ENABLE_INSTALL OFF CACHE BOOL "不安装第三方 Vulkan-Headers" FORCE)
FetchContent_Declare(
    vulkan_headers
    # v1.4.362 的发布提交为 ee2ec5fd83dafce291024683b50dc89219333076；使用
    # 官方归档和校验值，避免 Git 传输在受限网络中长时间重试。
    URL https://codeload.github.com/KhronosGroup/Vulkan-Headers/zip/ee2ec5fd83dafce291024683b50dc89219333076
    URL_HASH SHA256=6dc2fb9e72afd2e90887a57e7ca799acb6100b98a6b222c8c52e91082d74cd44
    HTTP_HEADER "User-Agent: GisEngine-CMake/0.1"
    TIMEOUT 60
    INACTIVITY_TIMEOUT 20
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
)
if(EXISTS "${PROJECT_SOURCE_DIR}/ThirdParty/vulkan-headers/CMakeLists.txt")
    # 本地已缓存固定提交时完全离线构建，CI 或新环境再回退到官方仓库。
    set(FETCHCONTENT_SOURCE_DIR_VULKAN_HEADERS "${PROJECT_SOURCE_DIR}/ThirdParty/vulkan-headers" CACHE PATH
        "Vulkan-Headers 本地源码目录" FORCE)
endif()
FetchContent_MakeAvailable(vulkan_headers)

if(GISENGINE_FETCH_SHADERC)
    # 固定 shaderc 与其编译依赖版本，避免各开发机的 GLSL 到 SPIR-V 产物发生漂移。
    set(SHADERC_SKIP_TESTS ON CACHE BOOL "不构建 shaderc 自身测试" FORCE)
    set(SHADERC_SKIP_EXAMPLES ON CACHE BOOL "不构建 shaderc 示例" FORCE)
    set(SHADERC_SKIP_EXECUTABLES OFF CACHE BOOL "保留 glslc 构建目标" FORCE)
    set(SHADERC_SKIP_COPYRIGHT_CHECK ON CACHE BOOL "不在应用构建中运行 shaderc 版权检查" FORCE)
    set(SHADERC_SKIP_INSTALL ON CACHE BOOL "不安装 shaderc 第三方目标" FORCE)
    set(SHADERC_ENABLE_SHARED_CRT ON CACHE BOOL "shaderc 使用共享 MSVC 运行库" FORCE)

    # shaderc 的发布源码不携带编译依赖；其官方 DEPS 文件固定了以下三项提交。
    # 先仅下载并暴露源目录，再由 shaderc 按原生依赖顺序统一纳入构建，避免重复定义目标。
    FetchContent_Declare(
        shaderc_glslang
        URL https://codeload.github.com/KhronosGroup/glslang/zip/d213562e35573012b6348b2d584457c3704ac09b
        URL_HASH SHA256=28ff9e9feffc67cf1ab314c0c3713d9fb876f9934b7d90403977908d09e3993d
        HTTP_HEADER "User-Agent: GisEngine-CMake/0.1"
        TIMEOUT 60
        INACTIVITY_TIMEOUT 20
        # 仅准备源码目录，后续由 shaderc 以官方依赖顺序纳入同一构建图。
        SOURCE_SUBDIR __gisengine_source_only__
        DOWNLOAD_EXTRACT_TIMESTAMP FALSE
    )
    FetchContent_Declare(
        shaderc_spirv_tools
        URL https://codeload.github.com/KhronosGroup/SPIRV-Tools/zip/19042c8921f35f7bec56b9e5c96c5f5691588ca8
        URL_HASH SHA256=e1951be28c4c19f6c32311e903ea309f4010e78f959efe349cfee7d37700780f
        HTTP_HEADER "User-Agent: GisEngine-CMake/0.1"
        TIMEOUT 60
        INACTIVITY_TIMEOUT 20
        SOURCE_SUBDIR __gisengine_source_only__
        DOWNLOAD_EXTRACT_TIMESTAMP FALSE
    )
    FetchContent_Declare(
        shaderc_spirv_headers
        URL https://codeload.github.com/KhronosGroup/SPIRV-Headers/zip/01e0577914a75a2569c846778c2f93aa8e6feddd
        URL_HASH SHA256=b96204f9a68cf5aca4fcad17c60feb51176af52b9790609e7439fa589f87d5f2
        HTTP_HEADER "User-Agent: GisEngine-CMake/0.1"
        TIMEOUT 60
        INACTIVITY_TIMEOUT 20
        SOURCE_SUBDIR __gisengine_source_only__
        DOWNLOAD_EXTRACT_TIMESTAMP FALSE
    )
    FetchContent_MakeAvailable(shaderc_glslang shaderc_spirv_tools shaderc_spirv_headers)
    set(SHADERC_GLSLANG_DIR "${shaderc_glslang_SOURCE_DIR}" CACHE PATH
        "shaderc 固定 glslang 源码目录" FORCE)
    set(SHADERC_SPIRV_TOOLS_DIR "${shaderc_spirv_tools_SOURCE_DIR}" CACHE PATH
        "shaderc 固定 SPIRV-Tools 源码目录" FORCE)
    set(SHADERC_SPIRV_HEADERS_DIR "${shaderc_spirv_headers_SOURCE_DIR}" CACHE PATH
        "shaderc 固定 SPIRV-Headers 源码目录" FORCE)
    # SPIRV-Tools 的发布源码同样不带其 Headers 子模块，因此显式传递已校验的
    # 上游源码目录；该变量名遵循 SPIRV-Tools 官方 CMake 接口。
    set(SPIRV-Headers_SOURCE_DIR "${shaderc_spirv_headers_SOURCE_DIR}" CACHE PATH
        "SPIRV-Tools 固定 SPIRV-Headers 源码目录" FORCE)
    set(SPIRV_TOOLS_USE_MIMALLOC OFF CACHE BOOL "不引入 SPIRV-Tools 可选内存分配器" FORCE)
    set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "不构建 glslang 独立命令行程序" FORCE)
    set(ENABLE_SPVREMAPPER OFF CACHE BOOL "不构建当前未使用的 SPIR-V 重映射工具" FORCE)

    FetchContent_Declare(
        shaderc
        # v2025.4（提交 17c1832c）；使用官方源码归档规避部分网络环境下 Git
        # 传输被中断的问题，并以 SHA-256 保证下载内容可验证、可复现。
        URL https://codeload.github.com/google/shaderc/zip/17c1832c132ccfb11a3050945389f64dd485e327
        URL_HASH SHA256=b37a9c0cca3b64aeca0c48e708a413b1fc32f29876becfbc392a848435396365
        HTTP_HEADER "User-Agent: GisEngine-CMake/0.1"
        TIMEOUT 60
        INACTIVITY_TIMEOUT 20
        DOWNLOAD_EXTRACT_TIMESTAMP FALSE
    )
    if(EXISTS "${PROJECT_SOURCE_DIR}/ThirdParty/shaderc/CMakeLists.txt")
        # 本地已缓存固定提交时优先离线构建，CI 或新环境再回退到官方仓库。
        set(FETCHCONTENT_SOURCE_DIR_SHADERC "${PROJECT_SOURCE_DIR}/ThirdParty/shaderc" CACHE PATH
            "shaderc 本地源码目录" FORCE)
    endif()
    # glslang 的该固定版本会把生成器表达式误作安装开关真值，单独关闭 shaderc 的
    # 安装选项仍会泄漏第三方安装规则。将整个 shaderc 子工程排除出默认构建和安装；
    # GisEngineShaders 对 glslc 的显式依赖会按需构建该工具，不影响着色器产物。
    set(gisengine_saved_skip_install_rules "${CMAKE_SKIP_INSTALL_RULES}")
    set(CMAKE_SKIP_INSTALL_RULES ON)
    FetchContent_GetProperties(shaderc)
    if(NOT shaderc_POPULATED)
        # CMake 3.30 起将单参数 Populate 标记为弃用；保留其以兼容项目要求的
        # CMake 3.24，同时局部固定旧语义，避免在新版本产生开发者警告。
        if(POLICY CMP0169)
            cmake_policy(PUSH)
            cmake_policy(SET CMP0169 OLD)
        endif()
        FetchContent_Populate(shaderc)
        if(POLICY CMP0169)
            cmake_policy(POP)
        endif()
    endif()
    add_subdirectory("${shaderc_SOURCE_DIR}" "${shaderc_BINARY_DIR}" EXCLUDE_FROM_ALL)
    set(CMAKE_SKIP_INSTALL_RULES "${gisengine_saved_skip_install_rules}")
    unset(gisengine_saved_skip_install_rules)

    if(TARGET glslc_exe)
        set(GISENGINE_SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
        set(GISENGINE_VERTEX_SHADER_OUTPUT "${GISENGINE_SHADER_OUTPUT_DIR}/triangle.vert.spv")
        set(GISENGINE_FRAGMENT_SHADER_OUTPUT "${GISENGINE_SHADER_OUTPUT_DIR}/triangle.frag.spv")
        add_custom_command(
            OUTPUT "${GISENGINE_VERTEX_SHADER_OUTPUT}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${GISENGINE_SHADER_OUTPUT_DIR}"
            COMMAND $<TARGET_FILE:glslc_exe> -fshader-stage=vert "${PROJECT_SOURCE_DIR}/Shaders/triangle.vert"
                    -o "${GISENGINE_VERTEX_SHADER_OUTPUT}"
            DEPENDS glslc_exe Shaders/triangle.vert
            COMMENT "编译 Vulkan 三角形顶点着色器"
            VERBATIM
        )
        add_custom_command(
            OUTPUT "${GISENGINE_FRAGMENT_SHADER_OUTPUT}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${GISENGINE_SHADER_OUTPUT_DIR}"
            COMMAND $<TARGET_FILE:glslc_exe> -fshader-stage=frag "${PROJECT_SOURCE_DIR}/Shaders/triangle.frag"
                    -o "${GISENGINE_FRAGMENT_SHADER_OUTPUT}"
            DEPENDS glslc_exe Shaders/triangle.frag
            COMMENT "编译 Vulkan 三角形片元着色器"
            VERBATIM
        )
        add_custom_target(GisEngineShaders ALL
            DEPENDS "${GISENGINE_VERTEX_SHADER_OUTPUT}" "${GISENGINE_FRAGMENT_SHADER_OUTPUT}"
        )
    else()
        message(FATAL_ERROR "已启用 shaderc，但未生成 glslc 可执行目标。")
    endif()
endif()

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
target_link_libraries(GisEngineRhi
    PUBLIC GisEngine::Core
    PRIVATE "$<BUILD_INTERFACE:Vulkan::Headers>"
)
if(CMAKE_DL_LIBS)
    target_link_libraries(GisEngineRhi PRIVATE ${CMAKE_DL_LIBS})
endif()

add_library(GisEngineRender STATIC
    Engine/Source/Runtime/Render/src/render.cpp
)
add_library(GisEngine::Render ALIAS GisEngineRender)
target_include_directories(GisEngineRender
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/Engine/Source/Runtime/Render/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(GisEngineRender PUBLIC GisEngine::Math GisEngine::Rhi)

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
gisengine_configure_target(GisEngineRender)
gisengine_configure_target(GisEnginePlatform)
gisengine_configure_target(GisEngineRuntime)

if(GISENGINE_BUILD_SAMPLES)
    add_executable(NativeSandbox Samples/NativeSandbox/main.cpp)
    target_link_libraries(NativeSandbox PRIVATE GisEngine::Runtime GisEngine::Rhi)
    if(TARGET GisEngineShaders)
        add_dependencies(NativeSandbox GisEngineShaders)
        target_compile_definitions(NativeSandbox PRIVATE
            GISENGINE_TRIANGLE_VERTEX_SHADER_PATH="${GISENGINE_VERTEX_SHADER_OUTPUT}"
            GISENGINE_TRIANGLE_FRAGMENT_SHADER_PATH="${GISENGINE_FRAGMENT_SHADER_OUTPUT}"
        )
    endif()
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
            Tests/RenderTests.cpp
        )
        target_link_libraries(GisEngineUnitTests PRIVATE
            GisEngine::Runtime
            GisEngine::Scene
            GisEngine::Render
            GTest::gtest_main
        )
        if(TARGET GisEngineShaders)
            add_dependencies(GisEngineUnitTests GisEngineShaders)
            target_compile_definitions(GisEngineUnitTests PRIVATE
                GISENGINE_TRIANGLE_VERTEX_SHADER_PATH="${GISENGINE_VERTEX_SHADER_OUTPUT}"
                GISENGINE_TRIANGLE_FRAGMENT_SHADER_PATH="${GISENGINE_FRAGMENT_SHADER_OUTPUT}"
            )
        endif()
        gisengine_configure_target(GisEngineUnitTests)
        gtest_discover_tests(GisEngineUnitTests PROPERTIES LABELS "unit")
    endif()
endif()

gisengine_add_quality_targets()

install(TARGETS GisEngineMath GisEngineCore GisEngineEcs GisEngineScene GisEngineRhi GisEngineRender GisEnginePlatform GisEngineRuntime
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
install(DIRECTORY Engine/Source/Runtime/Render/include/
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
