cmake_minimum_required(VERSION 3.27)

hyprfield_add_plugin(hyprdimension
    VERSION 0.1.0
    DESCRIPTION "Monitor-scoped dimensional workspaces"
    SOURCE main.cpp commands.cpp
)

add_library(hyprdimension-test-plugin SHARED main.cpp commands.cpp)
target_compile_features(hyprdimension-test-plugin PRIVATE cxx_std_23)
target_compile_definitions(hyprdimension-test-plugin PRIVATE HYPRDIMENSION_OFFLINE)
target_link_libraries(hyprdimension-test-plugin PRIVATE PkgConfig::HYPRLAND)
set_target_properties(hyprdimension-test-plugin PROPERTIES
    PREFIX ""
    OUTPUT_NAME "hyprdimension-test-plugin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/hyprdimension"
)
