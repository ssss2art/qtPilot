# qtPilot QML Model Test Application
#
# A pure Qt Quick app whose views are backed by real QAbstractItemModel
# instances, so the qt.models.* probe surface can be exercised against QML.
#
# Qt 6 only: qt_add_qml_module has no Qt 5 equivalent, and the QML model API
# this exercises is what Qt 6 apps use. Guarded by QTPILOT_HAS_QML at the
# call site in the root CMakeLists.

# Opt in to the ':/qt/qml/' resource prefix so the module layout matches modern
# Qt 6 apps (and configure stays warning-free).
qt_policy(SET QTP0001 NEW)

qt_add_executable(qtPilot_test_app_qml
    main.cpp
    models.cpp
    models.h
)

qt_add_qml_module(qtPilot_test_app_qml
    URI qtPilotQmlTestApp
    VERSION 1.0
    QML_FILES
        Main.qml
)

target_link_libraries(qtPilot_test_app_qml
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
)

set_target_properties(qtPilot_test_app_qml PROPERTIES
    OUTPUT_NAME "qtPilot-test-app-qml"
    # A bundle on macOS so the launcher can inject into it the same way it does
    # for any other GUI app.
    MACOSX_BUNDLE ON
    WIN32_EXECUTABLE ON
)
