cmake_minimum_required(VERSION 3.27)

project(hyprfield
    DESCRIPTION "Hyprland plugins"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(HYPRFIELD_USE_CCACHE "Use ccache when it is available" ON)
if(HYPRFIELD_USE_CCACHE)
    find_program(CCACHE_PROGRAM ccache)
    if(CCACHE_PROGRAM)
        set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
        message(STATUS "Using ccache: ${CCACHE_PROGRAM}")
    else()
        message(STATUS "ccache not found; compiling without it")
    endif()
endif()

option(HYPRFIELD_NATIVE_OPTIMIZATIONS "Optimize Release builds for the build machine" ON)
if(HYPRFIELD_NATIVE_OPTIMIZATIONS)
    add_compile_options("$<$<CONFIG:Release>:-O3;-march=native;-mtune=native>")
    message(STATUS "Using native Release optimizations")
endif()

add_subdirectory(hello)
