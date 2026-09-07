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

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)

if(GISENGINE_ENABLE_WARNINGS)
    if(MSVC)
        add_compile_options(/W4 /permissive-)
    else()
        add_compile_options(-Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    endif()
endif()

add_library(GisEngineCore STATIC
    Engine/Source/Runtime/Core/src/engine.cpp
)
add_library(GisEngine::Core ALIAS GisEngineCore)
target_include_directories(GisEngineCore
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/Engine/Source/Runtime/Core/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

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
endfunction()

gisengine_configure_target(GisEngineCore)
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
    target_link_libraries(GisEngineSmokeTests PRIVATE GisEngine::Runtime)
    gisengine_configure_target(GisEngineSmokeTests)
    add_test(NAME GisEngine.Smoke COMMAND GisEngineSmokeTests)
endif()

install(TARGETS GisEngineCore GisEnginePlatform GisEngineRuntime
    EXPORT GisEngineTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY Engine/Source/Runtime/Core/include/
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
