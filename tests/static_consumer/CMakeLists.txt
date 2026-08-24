# Standalone consumer of an INSTALLED qtPilot package.
#
# Deliberately a separate project, configured against install/ rather than the
# build tree: it exercises find_package(qtPilot) and qtPilot_inject_probe() the way
# a real integrator does. Building it in-tree would prove nothing about the
# installed package config.
cmake_minimum_required(VERSION 3.16)
project(qtpilot_static_consumer CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 COMPONENTS Core REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Core Gui Widgets REQUIRED)
find_package(qtPilot REQUIRED)

add_executable(static_consumer main.cpp)
target_link_libraries(static_consumer PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Widgets
)
qtPilot_inject_probe(static_consumer)
