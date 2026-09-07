if(NOT DEFINED PROJECT_SOURCE_DIR OR NOT DEFINED FORMAT_MODE)
    message(FATAL_ERROR "格式化脚本缺少必要参数。")
endif()

if(NOT CLANG_FORMAT_EXECUTABLE)
    find_program(CLANG_FORMAT_EXECUTABLE NAMES clang-format REQUIRED)
endif()
file(GLOB_RECURSE FORMAT_SOURCES
    "${PROJECT_SOURCE_DIR}/Engine/*.cpp"
    "${PROJECT_SOURCE_DIR}/Engine/*.h"
    "${PROJECT_SOURCE_DIR}/Samples/*.cpp"
    "${PROJECT_SOURCE_DIR}/Tests/*.cpp"
    "${PROJECT_SOURCE_DIR}/Tools/*.cpp"
)

if(FORMAT_SOURCES STREQUAL "")
    message(FATAL_ERROR "未找到需要格式化的 C++ 源码。")
endif()

if(FORMAT_MODE STREQUAL "apply")
    execute_process(
        COMMAND "${CLANG_FORMAT_EXECUTABLE}" -i --style=file ${FORMAT_SOURCES}
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        COMMAND_ERROR_IS_FATAL ANY
    )
elseif(FORMAT_MODE STREQUAL "check")
    execute_process(
        COMMAND "${CLANG_FORMAT_EXECUTABLE}" --dry-run --Werror --style=file ${FORMAT_SOURCES}
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        COMMAND_ERROR_IS_FATAL ANY
    )
else()
    message(FATAL_ERROR "未知的格式化模式：${FORMAT_MODE}")
endif()
