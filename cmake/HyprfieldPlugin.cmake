include_guard(GLOBAL)

function(hyprfield_add_plugin plugin_name)
    cmake_parse_arguments(PLUGIN "" "VERSION;DESCRIPTION;SOURCE" "" ${ARGN})

    foreach(required VERSION DESCRIPTION SOURCE)
        if(NOT PLUGIN_${required})
            message(FATAL_ERROR "hyprfield_add_plugin(${plugin_name}) requires ${required}")
        endif()
    endforeach()

    add_library(${plugin_name} SHARED ${PLUGIN_SOURCE})
    target_compile_features(${plugin_name} PRIVATE cxx_std_23)
    target_link_libraries(${plugin_name} PRIVATE PkgConfig::HYPRLAND)
    if(HYPRFIELD_HYPRLAND_VERSION_PIN)
        target_compile_definitions(${plugin_name} PRIVATE
            HYPRFIELD_HYPRLAND_VERSION_PIN="${HYPRFIELD_HYPRLAND_VERSION_PIN}")
    endif()

    set_target_properties(${plugin_name} PROPERTIES
        VERSION "${PLUGIN_VERSION}"
        PREFIX ""
        OUTPUT_NAME "${plugin_name}"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${plugin_name}"
    )

    install(TARGETS ${plugin_name} LIBRARY DESTINATION lib)
endfunction()
