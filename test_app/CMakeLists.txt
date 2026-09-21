# qtPilot Test Application

set(TEST_APP_SOURCES
    main.cpp
    mainwindow.cpp
    childwindow.cpp
    custom_gauge.cpp
    complex_table_model.cpp
    qml_bridge.cpp
)

set(TEST_APP_HEADERS
    mainwindow.h
    childwindow.h
    custom_gauge.h
    complex_table_model.h
    qml_bridge.h
)

set(TEST_APP_UI
    mainwindow.ui
)

set(TEST_APP_RESOURCES
    test_app_qml.qrc
)

add_executable(qtPilot_test_app
    ${TEST_APP_SOURCES}
    ${TEST_APP_HEADERS}
    ${TEST_APP_UI}
    ${TEST_APP_RESOURCES}
)

# Link Qt libraries
if(QT_VERSION_MAJOR EQUAL 6)
    find_package(Qt6 QUIET COMPONENTS Quick QuickWidgets Qml)
    target_link_libraries(qtPilot_test_app
        PRIVATE
            Qt6::Core
            Qt6::Widgets
    )
    if(Qt6QuickWidgets_FOUND)
        target_link_libraries(qtPilot_test_app
            PRIVATE
                Qt6::Quick
                Qt6::QuickWidgets
                Qt6::Qml
        )
        target_compile_definitions(qtPilot_test_app PRIVATE QTPILOT_HAS_QUICKWIDGETS=1)
    endif()
else()
    find_package(Qt5 QUIET COMPONENTS Quick QuickWidgets Qml)
    target_link_libraries(qtPilot_test_app
        PRIVATE
            Qt5::Core
            Qt5::Widgets
    )
    if(Qt5QuickWidgets_FOUND)
        target_link_libraries(qtPilot_test_app
            PRIVATE
                Qt5::Quick
                Qt5::QuickWidgets
                Qt5::Qml
        )
        target_compile_definitions(qtPilot_test_app PRIVATE QTPILOT_HAS_QUICKWIDGETS=1)
    endif()
endif()

# Set output name
set_target_properties(qtPilot_test_app PROPERTIES
    OUTPUT_NAME "qtPilot-test-app"
)

# On Windows, set WIN32 to hide console
if(WIN32)
    set_target_properties(qtPilot_test_app PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
endif()
