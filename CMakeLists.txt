cmake_minimum_required(VERSION 3.27)

project(hyprfield
    DESCRIPTION "Hyprland plugins"
    LANGUAGES CXX
)

include(cmake/HyprfieldPlugin.cmake)

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

find_package(PkgConfig REQUIRED)
pkg_check_modules(HYPRLAND REQUIRED IMPORTED_TARGET hyprland)
pkg_check_modules(LUA REQUIRED IMPORTED_TARGET lua)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/hyprpm.toml" HYPRFIELD_PIN_LINE REGEX "^[ \\t]*commit_pins[ \\t]*=")
list(LENGTH HYPRFIELD_PIN_LINE HYPRFIELD_PIN_LINE_COUNT)
if(NOT HYPRFIELD_PIN_LINE_COUNT EQUAL 1)
    message(FATAL_ERROR "hyprpm.toml must declare exactly one commit_pins entry")
endif()
string(REGEX MATCH "\"([0-9]+\\.[0-9]+\\.[0-9]+)\"" HYPRFIELD_PIN_MATCH "${HYPRFIELD_PIN_LINE}")
set(HYPRFIELD_HYPRLAND_VERSION_PIN "${CMAKE_MATCH_1}")
if(NOT HYPRFIELD_HYPRLAND_VERSION_PIN)
    message(FATAL_ERROR "hyprpm.toml must declare one exact Hyprland version pin")
endif()
if(NOT HYPRLAND_VERSION VERSION_EQUAL HYPRFIELD_HYPRLAND_VERSION_PIN)
    message(FATAL_ERROR
        "Hyprland version ${HYPRLAND_VERSION} does not match exact pin ${HYPRFIELD_HYPRLAND_VERSION_PIN}. "
        "Run 'just compatibility-check' before changing the pin.")
endif()

add_subdirectory(hello)
add_subdirectory(whiteboard)

include(CTest)
if(BUILD_TESTING)
    add_executable(host-harness-test tests/host_harness_test.cpp tests/host_harness.cpp)
    target_compile_features(host-harness-test PRIVATE cxx_std_23)
    target_link_libraries(host-harness-test PRIVATE PkgConfig::HYPRLAND PkgConfig::LUA ${CMAKE_DL_LIBS})
    target_link_options(host-harness-test PRIVATE "-rdynamic")
    add_test(NAME host-harness COMMAND host-harness-test "$<TARGET_FILE:hello>")

    add_executable(whiteboard-test tests/whiteboard_test.cpp tests/host_harness.cpp)
    target_compile_features(whiteboard-test PRIVATE cxx_std_23)
    target_link_libraries(whiteboard-test PRIVATE PkgConfig::HYPRLAND PkgConfig::LUA ${CMAKE_DL_LIBS})
    target_link_options(whiteboard-test PRIVATE "-rdynamic")
    add_test(NAME whiteboard COMMAND whiteboard-test "$<TARGET_FILE:whiteboard>")
    set_tests_properties(whiteboard PROPERTIES ENVIRONMENT "XDG_STATE_HOME=/tmp/hyprfield-whiteboard-test")
endif()
