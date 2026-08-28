cmake_minimum_required(VERSION 3.27)

project(hello
    DESCRIPTION "A minimal Hyprland plugin example"
    VERSION 0.1.0
    LANGUAGES CXX
)

find_package(PkgConfig REQUIRED)
pkg_check_modules(HYPRLAND REQUIRED IMPORTED_TARGET hyprland)

add_library(hello SHARED main.cpp)
target_compile_features(hello PRIVATE cxx_std_23)
target_link_libraries(hello PRIVATE PkgConfig::HYPRLAND)

set_target_properties(hello PROPERTIES
    PREFIX ""
    OUTPUT_NAME "hello"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/hello"
)

install(TARGETS hello LIBRARY DESTINATION lib)
