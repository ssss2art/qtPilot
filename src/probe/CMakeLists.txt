# qtPilot Probe Library

set(PROBE_SOURCES
    core/probe.cpp
    core/object_registry.cpp
    introspection/object_id.cpp
    introspection/variant_json.cpp
    introspection/meta_inspector.cpp
    introspection/signal_monitor.cpp
    introspection/event_capture.cpp
    introspection/qml_inspector.cpp
    introspection/model_navigator.cpp
    interaction/input_simulator.cpp
    interaction/screenshot.cpp
    interaction/hit_test.cpp
    transport/websocket_server.cpp
    transport/jsonrpc_handler.cpp
    transport/discovery_broadcaster.cpp
    api/response_envelope.cpp
    api/symbolic_name_map.cpp
    api/native_mode_api.cpp
    api/computer_use_mode_api.cpp
    api/chrome_mode_api.cpp
    interaction/key_name_mapper.cpp
    core/object_resolver.cpp
    accessibility/role_mapper.cpp
    accessibility/console_message_capture.cpp
    accessibility/accessibility_tree_walker.cpp
)

set(PROBE_HEADERS
    core/probe.h
    core/injector.h
    core/object_registry.h
    core/object_resolver.h
    introspection/object_id.h
    introspection/variant_json.h
    introspection/meta_inspector.h
    introspection/signal_monitor.h
    introspection/event_capture.h
    introspection/qml_inspector.h
    introspection/model_navigator.h
    interaction/input_simulator.h
    interaction/screenshot.h
    interaction/hit_test.h
    transport/websocket_server.h
    transport/jsonrpc_handler.h
    transport/discovery_broadcaster.h
    api/error_codes.h
    api/response_envelope.h
    api/symbolic_name_map.h
    api/native_mode_api.h
    api/computer_use_mode_api.h
    api/chrome_mode_api.h
    interaction/key_name_mapper.h
    accessibility/role_mapper.h
    accessibility/console_message_capture.h
    accessibility/accessibility_tree_walker.h
)

# Platform-specific initialization entry points
if(WIN32)
    list(APPEND PROBE_SOURCES
        core/probe_init_windows.cpp
        core/child_injector_windows.cpp
    )
    list(APPEND PROBE_HEADERS
        core/child_injector_windows.h
    )
elseif(UNIX AND NOT APPLE)
    list(APPEND PROBE_SOURCES core/probe_init_linux.cpp)
endif()

# Create shared library
add_library(qtPilot_probe SHARED
    ${PROBE_SOURCES}
    ${PROBE_HEADERS}
)

# Set library properties
set_target_properties(qtPilot_probe PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    OUTPUT_NAME "qtPilot-probe-${QTPILOT_QT_VERSION_TAG}"
    EXPORT_NAME Probe
    DEBUG_POSTFIX "d"
)

# Define export macro for Windows DLL
target_compile_definitions(qtPilot_probe PRIVATE
    QTPILOT_PROBE_LIBRARY
)

# Include directories
target_include_directories(qtPilot_probe
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include/qtpilot>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../
)

# Link Qt libraries
if(QT_VERSION_MAJOR EQUAL 6)
    target_link_libraries(qtPilot_probe
        PUBLIC
            Qt6::Core
            Qt6::Network
            Qt6::WebSockets
            Qt6::Widgets
            Qt6::Test
        PRIVATE
            Qt6::CorePrivate
    )
    if(QTPILOT_HAS_QML)
        target_link_libraries(qtPilot_probe PUBLIC Qt6::Qml Qt6::Quick)
        target_compile_definitions(qtPilot_probe PUBLIC QTPILOT_HAS_QML)
    endif()
else()
    target_link_libraries(qtPilot_probe
        PUBLIC
            Qt5::Core
            Qt5::Network
            Qt5::WebSockets
            Qt5::Widgets
            Qt5::Test
        PRIVATE
            Qt5::CorePrivate
    )
    if(QTPILOT_HAS_QML)
        target_link_libraries(qtPilot_probe PUBLIC Qt5::Qml Qt5::Quick)
        target_compile_definitions(qtPilot_probe PUBLIC QTPILOT_HAS_QML)
    endif()
endif()

# Windows: link shared injection utilities and Detours for child-process hooking
if(WIN32)
    target_link_libraries(qtPilot_probe PRIVATE qtPilot_shared detours)
endif()

# Platform-specific configurations
if(UNIX AND NOT APPLE)
    find_package(X11)
    if(X11_FOUND AND X11_XTest_FOUND)
        target_link_libraries(qtPilot_probe PRIVATE ${X11_LIBRARIES} ${X11_XTest_LIB})
        target_compile_definitions(qtPilot_probe PRIVATE QTPILOT_HAS_XTEST)
    endif()
endif()

# Alias for use in parent project
add_library(qtPilot::Probe ALIAS qtPilot_probe)
