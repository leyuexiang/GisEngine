include_guard(GLOBAL)

set(GISENGINE_LLVM_HINTS
    "$ENV{LLVM_ROOT}/bin"
    "$ENV{ProgramFiles}/LLVM/bin"
)

if(WIN32)
    find_program(GISENGINE_VSWHERE_EXECUTABLE NAMES vswhere HINTS
        "C:/Program Files (x86)/Microsoft Visual Studio/Installer"
        "D:/Program Files (x86)/Microsoft Visual Studio/Installer"
    )
    if(GISENGINE_VSWHERE_EXECUTABLE)
        execute_process(
            COMMAND "${GISENGINE_VSWHERE_EXECUTABLE}" -latest -products * -property installationPath
            OUTPUT_VARIABLE GISENGINE_VISUAL_STUDIO_ROOT
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(GISENGINE_VISUAL_STUDIO_ROOT)
            list(APPEND GISENGINE_LLVM_HINTS "${GISENGINE_VISUAL_STUDIO_ROOT}/VC/Tools/Llvm/x64/bin")
        endif()
    endif()
endif()

function(gisengine_find_llvm_tool output_variable tool_name)
    string(TOUPPER "${tool_name}" tool_name_upper)
    string(REPLACE "-" "_" tool_name_upper "${tool_name_upper}")
    set(cache_variable "GISENGINE_${tool_name_upper}_EXECUTABLE")
    find_program(${cache_variable} NAMES "${tool_name}" HINTS ${GISENGINE_LLVM_HINTS})
    set(${output_variable} "${${cache_variable}}" PARENT_SCOPE)
endfunction()

function(gisengine_enable_warnings target_name)
    if(NOT GISENGINE_ENABLE_WARNINGS)
        return()
    endif()

    if(MSVC)
        set(warning_options /W4 /permissive-)
        if(GISENGINE_WARNINGS_AS_ERRORS)
            list(APPEND warning_options /WX)
        endif()
    else()
        set(warning_options -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
        if(GISENGINE_WARNINGS_AS_ERRORS)
            list(APPEND warning_options -Werror)
        endif()
    endif()
    target_compile_options(${target_name} PRIVATE ${warning_options})
endfunction()

function(gisengine_enable_sanitizers target_name)
    if(NOT GISENGINE_ENABLE_SANITIZERS)
        return()
    endif()

    if(MSVC)
        # MSVC 提供 AddressSanitizer；UndefinedBehaviorSanitizer 由 Clang/GCC 专用预设覆盖。
        target_compile_options(${target_name} PRIVATE /fsanitize=address /Zi)
        target_link_options(${target_name} PRIVATE /INFERASANLIBS)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        # 同时保留栈帧信息，使 Sanitizer 报告能准确定位引擎调用链。
        target_compile_options(${target_name} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${target_name} PRIVATE -fsanitize=address,undefined)
    else()
        message(FATAL_ERROR "GISENGINE_ENABLE_SANITIZERS 当前仅支持 MSVC、GNU 或 Clang 编译器。")
    endif()
endfunction()

function(gisengine_enable_clang_tidy target_name)
    if(NOT GISENGINE_ENABLE_CLANG_TIDY)
        return()
    endif()

    gisengine_find_llvm_tool(GISENGINE_CLANG_TIDY_EXECUTABLE clang-tidy)
    if(NOT GISENGINE_CLANG_TIDY_EXECUTABLE)
        message(FATAL_ERROR "未找到 clang-tidy，请安装 LLVM 或 Visual Studio C++ 代码分析组件。")
    endif()
    set_property(TARGET ${target_name} PROPERTY
        CXX_CLANG_TIDY
        "${GISENGINE_CLANG_TIDY_EXECUTABLE};--config-file=${PROJECT_SOURCE_DIR}/.clang-tidy")
endfunction()

function(gisengine_add_quality_targets)
    gisengine_find_llvm_tool(GISENGINE_CLANG_FORMAT_EXECUTABLE clang-format)
    gisengine_find_llvm_tool(GISENGINE_CLANG_TIDY_EXECUTABLE clang-tidy)

    add_custom_target(format
        COMMAND ${CMAKE_COMMAND}
            -DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}
            -DFORMAT_MODE=apply
            -DCLANG_FORMAT_EXECUTABLE=${GISENGINE_CLANG_FORMAT_EXECUTABLE}
            -P ${PROJECT_SOURCE_DIR}/cmake/FormatSources.cmake
        COMMENT "使用 clang-format 格式化项目 C++ 源码"
        VERBATIM
    )

    add_custom_target(format-check
        COMMAND ${CMAKE_COMMAND}
            -DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}
            -DFORMAT_MODE=check
            -DCLANG_FORMAT_EXECUTABLE=${GISENGINE_CLANG_FORMAT_EXECUTABLE}
            -P ${PROJECT_SOURCE_DIR}/cmake/FormatSources.cmake
        COMMENT "检查项目 C++ 源码格式"
        VERBATIM
    )

    add_custom_target(lint
        COMMAND ${CMAKE_COMMAND}
            -DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}
            -DBUILD_DIRECTORY=${CMAKE_BINARY_DIR}
            -DCLANG_TIDY_EXECUTABLE=${GISENGINE_CLANG_TIDY_EXECUTABLE}
            -P ${PROJECT_SOURCE_DIR}/cmake/RunClangTidy.cmake
        COMMENT "使用 clang-tidy 执行静态检查"
        VERBATIM
    )

    add_custom_target(quality-check DEPENDS format-check lint)
endfunction()
