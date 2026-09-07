if(NOT DEFINED PROJECT_SOURCE_DIR OR NOT DEFINED BUILD_DIRECTORY)
    message(FATAL_ERROR "静态检查脚本缺少必要参数。")
endif()

if(NOT CLANG_TIDY_EXECUTABLE)
    find_program(CLANG_TIDY_EXECUTABLE NAMES clang-tidy REQUIRED)
endif()
if(NOT EXISTS "${BUILD_DIRECTORY}/compile_commands.json")
    message(FATAL_ERROR "未找到 compile_commands.json，请先重新配置构建目录。")
endif()

file(GLOB_RECURSE TIDY_SOURCES
    "${PROJECT_SOURCE_DIR}/Engine/*.cpp"
    "${PROJECT_SOURCE_DIR}/Samples/*.cpp"
    "${PROJECT_SOURCE_DIR}/Tests/*.cpp"
    "${PROJECT_SOURCE_DIR}/Tools/*.cpp"
)

execute_process(
    # CMake 会按参数边界传递命令；不要在 --p 的值中嵌入引号，否则 clang-tidy 会把引号视为路径内容。
    COMMAND "${CLANG_TIDY_EXECUTABLE}" "--p=${BUILD_DIRECTORY}" ${TIDY_SOURCES}
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    COMMAND_ERROR_IS_FATAL ANY
)
