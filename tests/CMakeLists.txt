# QtMCP Tests using Qt Test framework

enable_testing()

# Deploy the minimal platform plugin for headless testing (QT_QPA_PLATFORM=minimal).
# windeployqt only deploys 'qwindows' — we need 'qminimal' explicitly.
if(WIN32)
    get_target_property(_qt_plugins_dir Qt${QT_VERSION_MAJOR}::Core LOCATION)
    get_filename_component(_qt_bin_dir "${_qt_plugins_dir}" DIRECTORY)

    # Deploy both Debug and Release variants so tests work in either config.
    set(_qminimal_debug_src "${_qt_bin_dir}/../plugins/platforms/qminimald.dll")
    set(_qminimal_release_src "${_qt_bin_dir}/../plugins/platforms/qminimal.dll")
    set(_deploy_outputs "")

    if(EXISTS "${_qminimal_debug_src}")
        set(_debug_dest "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Debug/platforms/qminimald.dll")
        add_custom_command(OUTPUT "${_debug_dest}"
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Debug/platforms"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_qminimal_debug_src}" "${_debug_dest}"
            COMMENT "Deploying qminimald.dll for headless tests (Debug)..."
        )
        list(APPEND _deploy_outputs "${_debug_dest}")
    endif()

    if(EXISTS "${_qminimal_release_src}")
        set(_release_dest "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Release/platforms/qminimal.dll")
        add_custom_command(OUTPUT "${_release_dest}"
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Release/platforms"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_qminimal_release_src}" "${_release_dest}"
            COMMENT "Deploying qminimal.dll for headless tests (Release)..."
        )
        list(APPEND _deploy_outputs "${_release_dest}")
    endif()

    if(_deploy_outputs)
        add_custom_target(deploy_qminimal ALL DEPENDS ${_deploy_outputs})
    endif()
endif()

# Helper function to reduce boilerplate for test definitions
function(qtmcp_add_test)
    cmake_parse_arguments(ARG "" "NAME" "SOURCES;LIBS;ENV" ${ARGN})

    add_executable(${ARG_NAME} ${ARG_SOURCES})

    # Core libraries: probe + Qt Core + Qt Test
    if(QT_VERSION_MAJOR EQUAL 6)
        target_link_libraries(${ARG_NAME} PRIVATE
            qtmcp_probe Qt6::Core Qt6::Test ${ARG_LIBS})
    else()
        target_link_libraries(${ARG_NAME} PRIVATE
            qtmcp_probe Qt5::Core Qt5::Test ${ARG_LIBS})
    endif()

    target_include_directories(${ARG_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/src/probe)

    add_test(NAME ${ARG_NAME} COMMAND ${ARG_NAME}
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:${ARG_NAME}>")

    if(ARG_ENV)
        set_tests_properties(${ARG_NAME} PROPERTIES ENVIRONMENT "${ARG_ENV}")
    endif()

    # Copy Qt Test DLL on Windows
    if(WIN32)
        add_custom_command(TARGET ${ARG_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:Qt${QT_VERSION_MAJOR}::Test>"
                "$<TARGET_FILE_DIR:${ARG_NAME}>"
            COMMENT "Copying Qt Test DLL for ${ARG_NAME}..."
        )
    endif()
endfunction()

# --- Test definitions ---

qtmcp_add_test(NAME test_jsonrpc SOURCES test_jsonrpc.cpp)

qtmcp_add_test(NAME test_object_registry SOURCES test_object_registry.cpp
    ENV "QTMCP_ENABLED=0")

qtmcp_add_test(NAME test_object_id SOURCES test_object_id.cpp
    LIBS Qt${QT_VERSION_MAJOR}::Gui Qt${QT_VERSION_MAJOR}::Widgets
    ENV "QTMCP_ENABLED=0;QT_QPA_PLATFORM=minimal")

qtmcp_add_test(NAME test_meta_inspector SOURCES test_meta_inspector.cpp
    LIBS Qt${QT_VERSION_MAJOR}::Gui Qt${QT_VERSION_MAJOR}::Widgets
    ENV "QTMCP_ENABLED=0;QT_QPA_PLATFORM=minimal")

qtmcp_add_test(NAME test_ui_interaction SOURCES test_ui_interaction.cpp
    LIBS Qt${QT_VERSION_MAJOR}::Gui Qt${QT_VERSION_MAJOR}::Widgets
    ENV "QTMCP_ENABLED=0;QT_QPA_PLATFORM=minimal")

qtmcp_add_test(NAME test_signal_monitor SOURCES test_signal_monitor.cpp
    LIBS Qt${QT_VERSION_MAJOR}::Gui Qt${QT_VERSION_MAJOR}::Widgets
    ENV "QTMCP_ENABLED=0;QT_QPA_PLATFORM=minimal")

qtmcp_add_test(NAME test_jsonrpc_introspection SOURCES test_jsonrpc_introspection.cpp
    LIBS Qt${QT_VERSION_MAJOR}::Gui Qt${QT_VERSION_MAJOR}::Widgets
    ENV "QTMCP_ENABLED=0;QT_QPA_PLATFORM=minimal")

qtmcp_add_test(NAME test_native_mode_api SOURCES test_native_mode_api.cpp
    LIBS Qt${QT_VERSION_MAJOR}::Gui Qt${QT_VERSION_MAJOR}::Widgets
    ENV "QTMCP_ENABLED=0;QT_QPA_PLATFORM=minimal")

qtmcp_add_test(NAME test_key_name_mapper SOURCES test_key_name_mapper.cpp
    LIBS Qt${QT_VERSION_MAJOR}::Gui Qt${QT_VERSION_MAJOR}::Widgets
    ENV "QTMCP_ENABLED=0;QT_QPA_PLATFORM=minimal")

qtmcp_add_test(NAME test_computer_use_api SOURCES test_computer_use_api.cpp
    LIBS Qt${QT_VERSION_MAJOR}::Gui Qt${QT_VERSION_MAJOR}::Widgets
    ENV "QTMCP_ENABLED=0;QT_QPA_PLATFORM=minimal")

qtmcp_add_test(NAME test_chrome_mode_api SOURCES test_chrome_mode_api.cpp
    LIBS Qt${QT_VERSION_MAJOR}::Gui Qt${QT_VERSION_MAJOR}::Widgets
    ENV "QTMCP_ENABLED=0;QT_QPA_PLATFORM=minimal")

qtmcp_add_test(NAME test_model_navigator SOURCES test_model_navigator.cpp
    LIBS Qt${QT_VERSION_MAJOR}::Gui Qt${QT_VERSION_MAJOR}::Widgets
    ENV "QTMCP_ENABLED=0;QT_QPA_PLATFORM=minimal")

qtmcp_add_test(NAME test_event_capture SOURCES test_event_capture.cpp
    LIBS Qt${QT_VERSION_MAJOR}::Gui Qt${QT_VERSION_MAJOR}::Widgets
    ENV "QTMCP_ENABLED=0;QT_QPA_PLATFORM=minimal")

# E2E admin injection test — requires elevated terminal to run.
# Skips automatically (QSKIP) when not running as admin.
# Target with: ctest -L admin    Exclude with: ctest -LE admin
qtmcp_add_test(NAME test_admin_injection
    SOURCES test_admin_injection.cpp
    LIBS Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::WebSockets
    ENV "QTMCP_ENABLED=0;QT_QPA_PLATFORM=minimal")
set_tests_properties(test_admin_injection PROPERTIES LABELS "admin")
