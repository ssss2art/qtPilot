# qtPilot Tests using Qt Test framework

enable_testing()

# Deploy the headless platform plugins the tests run on: minimal for most, and
# offscreen for the tests that sample rendered pixels (QT_QPA_PLATFORM=offscreen).
# windeployqt only deploys 'qwindows', so these are copied explicitly.
if(WIN32)
    get_target_property(_qt_plugins_dir Qt${QT_VERSION_MAJOR}::Core LOCATION)
    get_filename_component(_qt_bin_dir "${_qt_plugins_dir}" DIRECTORY)

    # Deploy both Debug and Release variants so tests work in either config.
    set(_deploy_outputs "")
    foreach(_plugin qminimal qoffscreen)
        foreach(_config Debug Release)
            if(_config STREQUAL "Debug")
                set(_dll "${_plugin}d.dll")
            else()
                set(_dll "${_plugin}.dll")
            endif()
            set(_src "${_qt_bin_dir}/../plugins/platforms/${_dll}")
            if(EXISTS "${_src}")
                set(_dest "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${_config}/platforms/${_dll}")
                add_custom_command(OUTPUT "${_dest}"
                    COMMAND ${CMAKE_COMMAND} -E make_directory
                        "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${_config}/platforms"
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_src}" "${_dest}"
                    COMMENT "Deploying ${_dll} for headless tests (${_config})..."
                )
                list(APPEND _deploy_outputs "${_dest}")
            endif()
        endforeach()
    endforeach()

    if(_deploy_outputs)
        add_custom_target(deploy_qminimal ALL DEPENDS ${_deploy_outputs})
    endif()
endif()

# GoogleTest / GoogleMock for expressive test assertions and matchers
include(FetchContent)

set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK ON CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.18.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(googletest)

if(TARGET gmock)
    target_compile_options(gmock PRIVATE -w)
endif()
if(TARGET gtest)
    target_compile_options(gtest PRIVATE -w)
endif()

# Helper function to reduce boilerplate for test definitions
function(qtPilot_add_test)
    cmake_parse_arguments(ARG "" "NAME" "SOURCES;LIBS;ENV" ${ARGN})

    add_executable(${ARG_NAME} ${ARG_SOURCES})

    # Core libraries: probe + Qt Core + Qt Test + GMock
    if(QT_VERSION_MAJOR EQUAL 6)
        target_link_libraries(${ARG_NAME} PRIVATE
            qtPilot_probe gmock Qt6::Core Qt6::Test ${ARG_LIBS})
    else()
        target_link_libraries(${ARG_NAME} PRIVATE
            qtPilot_probe gmock Qt5::Core Qt5::Test ${ARG_LIBS})
    endif()

    target_include_directories(${ARG_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/src/probe
        ${CMAKE_SOURCE_DIR}/tests)

    add_test(NAME ${ARG_NAME} COMMAND ${ARG_NAME}
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:${ARG_NAME}>")

    # Pin the widget style on macOS.
    #
    # These tests create real widgets, and macOS's native style (QMacStyle) renders
    # several controls by drawing through NSView -- i.e. through AppKit. Under
    # QT_QPA_PLATFORM=minimal there is no AppKit environment for it to draw into,
    # and on Qt 5.15 that path dereferences garbage: six tests died with SIGSEGV
    # inside QMacStylePrivate::drawNSViewInRect the first time a QPushButton
    # painted, taking 173 assertions with them that had therefore never run on this
    # configuration. Fusion is pure QPainter and safe headless.
    #
    # Kept even though Qt 5.15 on macOS is not a supported configuration (see
    # docs/MACOS.md): the pin is the right thing for macOS regardless of Qt version,
    # since it removes an AppKit dependency from headless rendering and makes both
    # Qt versions exercise the same style. Not applied off macOS: the default styles
    # there are already QPainter-based, and changing them would alter the widget
    # metrics that the geometry and hit-test assertions are written against.
    set(_test_env "")
    if(APPLE)
        list(APPEND _test_env "QT_STYLE_OVERRIDE=Fusion")
    endif()
    if(ARG_ENV)
        list(APPEND _test_env ${ARG_ENV})
    endif()
    if(_test_env)
        set_tests_properties(${ARG_NAME} PROPERTIES ENVIRONMENT "${_test_env}")
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

qtPilot_add_test(NAME test_qt_matchers SOURCES test_qt_matchers.cpp
    LIBS Qt${QT_VERSION_MAJOR}::Gui
    ENV "QTPILOT_ENABLED=0;QT_QPA_PLATFORM=minimal")

qtPilot_add_test(NAME test_jsonrpc SOURCES test_jsonrpc.cpp)

# Lets the getVersion tests assert the reported version against the project's,
# rather than merely asserting the key exists.
target_compile_definitions(test_jsonrpc PRIVATE
    QTPILOT_EXPECTED_VERSION="${PROJECT_VERSION}")

qtPilot_add_test(NAME test_object_registry SOURCES test_object_registry.cpp
    ENV "QTPILOT_ENABLED=0")

qtPilot_add_test(NAME test_object_id SOURCES test_object_id.cpp
    LIBS Qt${QT_VERSION_MAJOR}::Gui Qt${QT_VERSION_MAJOR}::Widgets
    ENV "QTPILOT_ENABLED=0;QT_QPA_PLATFORM=minimal")

qtPilot_add_test(NAME test_object_id_refresh SOURCES test_object_id_refresh.cpp
    LIBS Qt${QT_VERSION_MAJOR}::Gui Qt${QT_VERSION_MAJOR}::Widgets
    ENV "QTPILOT_ENABLED=0;QT_QPA_PLATFORM=minimal")

qtPilot_add_test(NAME test_meta_inspector SOURCES test_meta_inspector.cpp
    LIBS Qt${QT_VERSION_MAJOR}::Gui Qt${QT_VERSION_MAJOR}::Widgets
    ENV "QTPILOT_ENABLED=0;QT_QPA_PLATFORM=minimal")

qtPilot_add_test(NAME test_ui_interaction SOURCES test_ui_interaction.cpp
    LIBS Qt${QT_VERSION_MAJOR}::Gui Qt${QT_VERSION_MAJOR}::Widgets
    ENV "QTPILOT_ENABLED=0;QT_QPA_PLATFORM=minimal")

qtPilot_add_test(NAME test_qwindow_input SOURCES test_qwindow_input.cpp
    LIBS Qt${QT_VERSION_MAJOR}::Gui
    ENV "QTPILOT_ENABLED=0;QT_QPA_PLATFORM=minimal")

if(QTPILOT_HAS_QML)
    qtPilot_add_test(NAME test_qml_screenshot SOURCES test_qml_screenshot.cpp
        LIBS Qt${QT_VERSION_MAJOR}::Gui Qt${QT_VERSION_MAJOR}::Qml Qt${QT_VERSION_MAJOR}::Quick
        ENV "QTPILOT_ENABLED=0;QT_QPA_PLATFORM=minimal")

    qtPilot_add_test(NAME test_qml_interaction SOURCES test_qml_interaction.cpp
        LIBS Qt${QT_VERSION_MAJOR}::Gui Qt${QT_VERSION_MAJOR}::Qml Qt${QT_VERSION_MAJOR}::Quick
        ENV "QTPILOT_ENABLED=0;QT_QPA_PLATFORM=minimal")

    qtPilot_add_test(NAME test_qml_delegate_ids SOURCES test_qml_delegate_ids.cpp
        LIBS Qt${QT_VERSION_MAJOR}::Gui Qt${QT_VERSION_MAJOR}::Qml Qt${QT_VERSION_MAJOR}::Quick
        ENV "QTPILOT_ENABLED=0;QT_QPA_PLATFORM=minimal")

    # qt.ui.screenshot on Qt Quick targets asserts on pixels, so it needs a
    # platform that really renders: the offscreen plugin, not minimal.
    find_package(Qt${QT_VERSION_MAJOR} QUIET COMPONENTS QuickWidgets)
    set(_screenshot_quick_libs
        Qt${QT_VERSION_MAJOR}::Gui Qt${QT_VERSION_MAJOR}::Widgets
        Qt${QT_VERSION_MAJOR}::Qml Qt${QT_VERSION_MAJOR}::Quick)
    if(TARGET Qt${QT_VERSION_MAJOR}::QuickWidgets)
        list(APPEND _screenshot_quick_libs Qt${QT_VERSION_MAJOR}::QuickWidgets)
    endif()
    set(_screenshot_quick_env "QTPILOT_ENABLED=0;QT_QPA_PLATFORM=offscreen")
    # A headless Linux or Windows runner may have no GPU API the scene graph can
    # render through, so render in software there. macOS keeps its default RHI
    # backend (Metal), so the hardware path is covered too.
    if(NOT APPLE)
        list(APPEND _screenshot_quick_env "QT_QUICK_BACKEND=software")
    endif()

    qtPilot_add_test(NAME test_ui_screenshot_quick SOURCES test_ui_screenshot_quick.cpp
        LIBS ${_screenshot_quick_libs}
        ENV "${_screenshot_quick_env}")
    if(TARGET Qt${QT_VERSION_MAJOR}::QuickWidgets)
        target_compile_definitions(test_ui_screenshot_quick PRIVATE QTPILOT_TEST_HAS_QUICKWIDGETS)
    endif()

    # The same cases at a device pixel ratio of 2, so the crop's DPR scaling is
    # checked against real numbers rather than a ratio of 1 that hides it.
    add_test(NAME test_ui_screenshot_quick_dpr2 COMMAND test_ui_screenshot_quick
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:test_ui_screenshot_quick>")
    set(_screenshot_quick_dpr2_env ${_screenshot_quick_env} "QT_SCALE_FACTOR=2")
    if(APPLE)
        list(APPEND _screenshot_quick_dpr2_env "QT_STYLE_OVERRIDE=Fusion")
    endif()
    set_tests_properties(test_ui_screenshot_quick_dpr2 PROPERTIES
        ENVIRONMENT "${_screenshot_quick_dpr2_env}")

    # On Qt 6 with Quick Widgets every case must run: a skip there would mean the
    # pixel checks passed by not looking, and CTest counts a skip as a pass.
    if(QT_VERSION_MAJOR EQUAL 6 AND TARGET Qt${QT_VERSION_MAJOR}::QuickWidgets)
        set_tests_properties(test_ui_screenshot_quick test_ui_screenshot_quick_dpr2 PROPERTIES
            FAIL_REGULAR_EXPRESSION "SKIP   :")
    endif()
endif()

if(QTPILOT_HAS_QML AND QT_VERSION_MAJOR EQUAL 6)
    # qt.ui.touch is Qt 6 only: it delivers through QPointingDevice, which Qt 5 lacks.
    # Offscreen, like the Quick screenshot test, so windows are really exposed.
    set(_touch_env "QTPILOT_ENABLED=0;QT_QPA_PLATFORM=offscreen")
    # As for the Quick screenshot test: a headless Linux or Windows runner may have
    # no GPU API for the scene graph, so render in software there.
    if(NOT APPLE)
        list(APPEND _touch_env "QT_QUICK_BACKEND=software")
    endif()
    qtPilot_add_test(NAME test_ui_touch SOURCES test_ui_touch.cpp
        LIBS Qt6::Gui Qt6::Widgets Qt6::Qml Qt6::Quick
        ENV "${_touch_env}")
endif()

qtPilot_add_test(NAME test_signal_monitor SOURCES test_signal_monitor.cpp
    LIBS Qt${QT_VERSION_MAJOR}::Gui Qt${QT_VERSION_MAJOR}::Widgets
    ENV "QTPILOT_ENABLED=0;QT_QPA_PLATFORM=minimal")

qtPilot_add_test(NAME test_jsonrpc_introspection SOURCES test_jsonrpc_introspection.cpp
    LIBS Qt${QT_VERSION_MAJOR}::Gui Qt${QT_VERSION_MAJOR}::Widgets
    ENV "QTPILOT_ENABLED=0;QT_QPA_PLATFORM=minimal")

qtPilot_add_test(NAME test_native_mode_api SOURCES test_native_mode_api.cpp
    LIBS Qt${QT_VERSION_MAJOR}::Gui Qt${QT_VERSION_MAJOR}::Widgets
    ENV "QTPILOT_ENABLED=0;QT_QPA_PLATFORM=minimal")

qtPilot_add_test(NAME test_graphics_view SOURCES test_graphics_view.cpp
    LIBS Qt${QT_VERSION_MAJOR}::Gui Qt${QT_VERSION_MAJOR}::Widgets
    ENV "QTPILOT_ENABLED=0;QT_QPA_PLATFORM=minimal")

qtPilot_add_test(NAME test_transport_chaos SOURCES test_transport_chaos.cpp
    LIBS Qt${QT_VERSION_MAJOR}::WebSockets
    ENV "QTPILOT_ENABLED=0;QT_QPA_PLATFORM=offscreen")

qtPilot_add_test(NAME test_item_targeting SOURCES test_item_targeting.cpp
    LIBS Qt${QT_VERSION_MAJOR}::Gui Qt${QT_VERSION_MAJOR}::Widgets
    ENV "QTPILOT_ENABLED=0;QT_QPA_PLATFORM=offscreen")

qtPilot_add_test(NAME test_input_modifiers SOURCES test_input_modifiers.cpp
    LIBS Qt${QT_VERSION_MAJOR}::Gui Qt${QT_VERSION_MAJOR}::Widgets
    ENV "QTPILOT_ENABLED=0;QT_QPA_PLATFORM=offscreen")

qtPilot_add_test(NAME test_held_mouse_gestures SOURCES test_held_mouse_gestures.cpp
    LIBS Qt${QT_VERSION_MAJOR}::Gui Qt${QT_VERSION_MAJOR}::Widgets
    ENV "QTPILOT_ENABLED=0;QT_QPA_PLATFORM=offscreen")

qtPilot_add_test(NAME test_key_name_mapper SOURCES test_key_name_mapper.cpp
    LIBS Qt${QT_VERSION_MAJOR}::Gui Qt${QT_VERSION_MAJOR}::Widgets
    ENV "QTPILOT_ENABLED=0;QT_QPA_PLATFORM=minimal")

qtPilot_add_test(NAME test_bind_policy SOURCES test_bind_policy.cpp
    LIBS Qt${QT_VERSION_MAJOR}::Network
    ENV "QTPILOT_ENABLED=0;QT_QPA_PLATFORM=minimal")

qtPilot_add_test(NAME test_websocket_server_bind SOURCES test_websocket_server_bind.cpp
    LIBS Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::WebSockets
    ENV "QTPILOT_ENABLED=0;QT_QPA_PLATFORM=minimal")

qtPilot_add_test(NAME test_websocket_teardown SOURCES test_websocket_teardown.cpp
    LIBS Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::WebSockets
    ENV "QTPILOT_ENABLED=0;QT_QPA_PLATFORM=minimal")

qtPilot_add_test(NAME test_websocket_dispatch SOURCES test_websocket_dispatch.cpp
    LIBS Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::WebSockets
    ENV "QTPILOT_ENABLED=0;QT_QPA_PLATFORM=minimal")

qtPilot_add_test(NAME test_probe_version SOURCES test_probe_version.cpp
    ENV "QTPILOT_ENABLED=0;QT_QPA_PLATFORM=minimal")

# PROJECT_VERSION reaches the test so it can assert the generated header still
# tracks the project rather than just asserting the header equals itself.
target_compile_definitions(test_probe_version PRIVATE
    QTPILOT_EXPECTED_VERSION="${PROJECT_VERSION}")

qtPilot_add_test(NAME test_computer_use_api SOURCES test_computer_use_api.cpp
    LIBS Qt${QT_VERSION_MAJOR}::Gui Qt${QT_VERSION_MAJOR}::Widgets
    ENV "QTPILOT_ENABLED=0;QT_QPA_PLATFORM=minimal")

qtPilot_add_test(NAME test_chrome_mode_api SOURCES test_chrome_mode_api.cpp
    LIBS Qt${QT_VERSION_MAJOR}::Gui Qt${QT_VERSION_MAJOR}::Widgets
    ENV "QTPILOT_ENABLED=0;QT_QPA_PLATFORM=minimal")

qtPilot_add_test(NAME test_model_navigator SOURCES test_model_navigator.cpp
    LIBS Qt${QT_VERSION_MAJOR}::Gui Qt${QT_VERSION_MAJOR}::Widgets
    ENV "QTPILOT_ENABLED=0;QT_QPA_PLATFORM=minimal")

qtPilot_add_test(NAME test_event_capture SOURCES test_event_capture.cpp
    LIBS Qt${QT_VERSION_MAJOR}::Gui Qt${QT_VERSION_MAJOR}::Widgets
    ENV "QTPILOT_ENABLED=0;QT_QPA_PLATFORM=minimal")

qtPilot_add_test(NAME test_notification_queue SOURCES test_notification_queue.cpp
    LIBS Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::WebSockets
    ENV "QTPILOT_ENABLED=0;QT_QPA_PLATFORM=minimal")

# E2E admin injection test — requires elevated terminal to run.
# Skips automatically (QSKIP) when not running as admin.
# Target with: ctest -L admin    Exclude with: ctest -LE admin
qtPilot_add_test(NAME test_admin_injection
    SOURCES test_admin_injection.cpp
    LIBS Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::WebSockets
    ENV "QTPILOT_ENABLED=0;QT_QPA_PLATFORM=minimal")
set_tests_properties(test_admin_injection PROPERTIES LABELS "admin")

# E2E launcher test: runs the real launcher against a plain (non-Qt) target that records the
# arguments it receives, so nothing between the command line and the target can hide one.
if(TARGET qtPilot_launcher)
    add_executable(launcher_args_echo launcher_args_echo.cpp)
    if(MINGW)
        target_link_options(launcher_args_echo PRIVATE -municode)  # its Windows entry is wmain
    endif()

    qtPilot_add_test(NAME test_launcher_args SOURCES test_launcher_args.cpp
        ENV "QTPILOT_ENABLED=0")
    add_dependencies(test_launcher_args qtPilot_launcher launcher_args_echo)
    set_tests_properties(test_launcher_args PROPERTIES LABELS "launcher")
    target_compile_definitions(test_launcher_args PRIVATE
        QTPILOT_TEST_LAUNCHER="$<TARGET_FILE:qtPilot_launcher>"
        QTPILOT_TEST_PROBE="$<TARGET_FILE:qtPilot_probe>"
        QTPILOT_TEST_ARGS_ECHO="$<TARGET_FILE:launcher_args_echo>")
    # Windows finds the probe's Qt DLLs through the launcher's --qt-dir (Qt's bin/..).
    if(WIN32)
        target_compile_definitions(test_launcher_args PRIVATE
            QTPILOT_TEST_QT_DIR="$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>/..")
    endif()
endif()

qtPilot_add_test(NAME test_network_policy SOURCES test_network_policy.cpp
    ENV "QTPILOT_ENABLED=0")

qtPilot_add_test(NAME test_authenticated_transport SOURCES test_authenticated_transport.cpp
    LIBS Qt${QT_VERSION_MAJOR}::WebSockets
    ENV "QTPILOT_ENABLED=0")

# E2E launcher test: the launcher exits cleanly after its target. Windows only: the launcher's
# pre-flight load of the probe into itself, which this guards, only happens there.
if(WIN32 AND TARGET qtPilot_launcher)
    # Its own trivial target: never inject into an operating-system binary (see AGENTS.md).
    add_executable(launcher_exit_target launcher_exit_target.cpp)

    qtPilot_add_test(NAME test_launcher_exit SOURCES test_launcher_exit.cpp
        ENV "QTPILOT_ENABLED=0")
    add_dependencies(test_launcher_exit qtPilot_launcher launcher_exit_target)
    set_tests_properties(test_launcher_exit PROPERTIES LABELS "launcher")
    target_compile_definitions(test_launcher_exit PRIVATE
        QTPILOT_TEST_LAUNCHER="$<TARGET_FILE:qtPilot_launcher>"
        QTPILOT_TEST_EXIT_TARGET="$<TARGET_FILE:launcher_exit_target>"
        QTPILOT_TEST_PROBE="$<TARGET_FILE:qtPilot_probe>"
        QTPILOT_TEST_QT_DIR="$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>/..")
endif()
