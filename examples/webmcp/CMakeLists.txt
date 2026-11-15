cmake_minimum_required(VERSION 3.21)

project(qtPilotWebMcpExample LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2)
qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(qtpilot_webmcp_example main.cpp)
qt_add_qml_module(qtpilot_webmcp_example
    URI qtPilotWebMcpExample
    VERSION 1.0
    QML_FILES Main.qml
)
target_link_libraries(qtpilot_webmcp_example PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2
)

option(QTPILOT_EXAMPLE_ENABLE_PROBE "Link the development-only qtPilot probe" OFF)
if(QTPILOT_EXAMPLE_ENABLE_PROBE)
    find_package(qtPilot REQUIRED)
    qtPilot_inject_probe(qtpilot_webmcp_example)
endif()
