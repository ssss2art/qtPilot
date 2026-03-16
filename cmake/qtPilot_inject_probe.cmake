# qtPilot_inject_probe.cmake
# ---------------------------------------------------------------------------
# Helper function for downstream consumers to inject the qtPilot probe
# into their Qt application targets.
#
# Usage:
#   find_package(Qt6 COMPONENTS Core Widgets REQUIRED)
#   find_package(qtPilot REQUIRED)
#
#   add_executable(myapp main.cpp)
#   target_link_libraries(myapp PRIVATE Qt6::Core Qt6::Widgets)
#   qtPilot_inject_probe(myapp)
#
# What it does:
#   - Links qtPilot::Probe to the target (PRIVATE)
#   - On Windows: copies the probe DLL next to the target executable
#   - On Linux: generates a helper script for LD_PRELOAD injection
# ---------------------------------------------------------------------------

function(qtPilot_inject_probe TARGET_NAME)
    if(NOT TARGET qtPilot::Probe)
        message(FATAL_ERROR
            "qtPilot_inject_probe: qtPilot::Probe target not found. "
            "Call find_package(qtPilot) first.")
    endif()

    if(NOT TARGET ${TARGET_NAME})
        message(FATAL_ERROR
            "qtPilot_inject_probe: Target '${TARGET_NAME}' does not exist.")
    endif()

    # Link the probe library
    target_link_libraries(${TARGET_NAME} PRIVATE qtPilot::Probe)

    # On Windows, copy the probe DLL next to the target executable
    if(WIN32)
        add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:qtPilot::Probe>"
                "$<TARGET_FILE_DIR:${TARGET_NAME}>"
            COMMENT "Copying qtPilot probe DLL for ${TARGET_NAME}"
            VERBATIM
        )
    endif()

    # On Linux, generate a helper script for LD_PRELOAD-based injection
    if(UNIX AND NOT APPLE)
        get_target_property(_probe_location qtPilot::Probe IMPORTED_LOCATION)
        if(_probe_location)
            set(_preload_script "${CMAKE_CURRENT_BINARY_DIR}/qtpilot-preload-${TARGET_NAME}.sh")
            file(GENERATE OUTPUT "${_preload_script}"
                CONTENT "#!/bin/sh\nLD_PRELOAD=$<TARGET_FILE:qtPilot::Probe> exec $<TARGET_FILE:${TARGET_NAME}> \"$@\"\n"
            )
        endif()
    endif()
endfunction()
