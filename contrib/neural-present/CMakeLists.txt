cmake_minimum_required(VERSION 3.20)
project(xemu_neural_present_reference_adapters LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

get_filename_component(XEMU_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

function(xemu_neural_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
    endif()
endfunction()

add_library(xemu-neural-present-pass-through SHARED
    xemu-neural-present-pass-through.c)
target_include_directories(xemu-neural-present-pass-through PRIVATE
    "${XEMU_ROOT}")
set_target_properties(xemu-neural-present-pass-through PROPERTIES
    PREFIX ""
    OUTPUT_NAME "xemu-neural-present-pass-through")
xemu_neural_warnings(xemu-neural-present-pass-through)

find_package(Vulkan QUIET)
if(Vulkan_FOUND)
    add_library(xemu-neural-present-vulkan-copy SHARED
        xemu-neural-present-vulkan-copy.c)
    target_include_directories(xemu-neural-present-vulkan-copy PRIVATE
        "${XEMU_ROOT}")
    target_link_libraries(xemu-neural-present-vulkan-copy PRIVATE
        Vulkan::Vulkan)
    set_target_properties(xemu-neural-present-vulkan-copy PROPERTIES
        PREFIX ""
        OUTPUT_NAME "xemu-neural-present-vulkan-copy")
    xemu_neural_warnings(xemu-neural-present-vulkan-copy)
else()
    message(STATUS
        "Vulkan SDK not found; skipping xemu-neural-present-vulkan-copy")
endif()

set(XEMU_STREAMLINE_SDK_ROOT "" CACHE PATH
    "Optional NVIDIA Streamline SDK root for the fail-closed adapter outline")

if(WIN32 AND XEMU_STREAMLINE_SDK_ROOT)
    enable_language(CXX)
    if(NOT EXISTS "${XEMU_STREAMLINE_SDK_ROOT}/include/sl.h")
        message(FATAL_ERROR
            "XEMU_STREAMLINE_SDK_ROOT does not contain include/sl.h")
    endif()

    add_library(xemu-neural-present-streamline-outline SHARED
        streamline-adapter-outline.cpp)
    target_include_directories(xemu-neural-present-streamline-outline PRIVATE
        "${XEMU_ROOT}"
        "${XEMU_STREAMLINE_SDK_ROOT}/include")
    target_compile_features(xemu-neural-present-streamline-outline PRIVATE
        cxx_std_17)
    target_compile_definitions(xemu-neural-present-streamline-outline PRIVATE
        UNICODE _UNICODE)
    target_link_libraries(xemu-neural-present-streamline-outline PRIVATE
        wintrust crypt32)
    set_target_properties(xemu-neural-present-streamline-outline PROPERTIES
        PREFIX ""
        OUTPUT_NAME "xemu-neural-present-streamline-outline")
    xemu_neural_warnings(xemu-neural-present-streamline-outline)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(xemu-neural-present-loader-smoke
        "${XEMU_ROOT}/tests/unit/test-xbox-neural-present-plugin-loader.c"
        "${XEMU_ROOT}/hw/xbox/nv2a/pgraph/neural-present-loader.c")
    target_include_directories(xemu-neural-present-loader-smoke PRIVATE
        "${XEMU_ROOT}")
    if(NOT WIN32)
        target_compile_definitions(xemu-neural-present-loader-smoke PRIVATE
            XEMU_NEURAL_PLUGIN_ENABLE_POSIX_LOADER=1)
        target_link_libraries(xemu-neural-present-loader-smoke PRIVATE
            ${CMAKE_DL_LIBS})
    endif()
    xemu_neural_warnings(xemu-neural-present-loader-smoke)

    add_test(NAME neural-present-pass-through-load
        COMMAND xemu-neural-present-loader-smoke
            $<TARGET_FILE:xemu-neural-present-pass-through>)
endif()
