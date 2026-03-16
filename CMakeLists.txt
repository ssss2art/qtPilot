# qtPilot - Qt Application Introspection and Automation Library
# Copyright (c) 2024 qtPilot Contributors
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.16)

project(qtPilot
    VERSION 0.3.1
    DESCRIPTION "Qt application introspection and automation library with MCP integration"
    LANGUAGES CXX
)

# Require C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for IDEs
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Options
option(QTPILOT_BUILD_TESTS "Build unit tests" ON)
option(QTPILOT_BUILD_TEST_APP "Build the test Qt application" ON)

# Qt path hint - takes precedence over CMAKE_PREFIX_PATH
set(QTPILOT_QT_DIR "" CACHE PATH "Explicit path to Qt installation (takes precedence over CMAKE_PREFIX_PATH)")

# Set default build type if not specified
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

# Output directories
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Find Qt - prefer Qt6, fall back to Qt5 5.15+
set(QT_MIN_VERSION_5 "5.15.1")
set(QT_MIN_VERSION_6 "6.5")

# Apply Qt path hint if provided
if(QTPILOT_QT_DIR)
    list(PREPEND CMAKE_PREFIX_PATH "${QTPILOT_QT_DIR}")
    message(STATUS "Using Qt hint: ${QTPILOT_QT_DIR}")
endif()

find_package(Qt6 ${QT_MIN_VERSION_6} QUIET COMPONENTS
    Core
    Network
    WebSockets
    Widgets
    Test
)

if(Qt6_FOUND)
    set(QT_VERSION_MAJOR 6)
    set(QT_VERSION ${Qt6_VERSION})
    message(STATUS "Found Qt6: ${Qt6_VERSION}")

    find_package(Qt6 COMPONENTS CorePrivate QUIET)
    if(NOT Qt6CorePrivate_FOUND)
        message(STATUS "Qt6::CorePrivate not found via component - trying header detection")
        find_path(_qt6_private_include "private/qhooks_p.h"
            PATHS "${QT_ROOT_DIR}" "${Qt6Core_DIR}/../../.."
            PATH_SUFFIXES "include/QtCore/${Qt6_VERSION}/QtCore"
        )
        # Fallback: scan for any version subdirectory (handles cmake version ≠ header version)
        if(NOT _qt6_private_include)
            file(GLOB _qt6_private_version_dirs "${Qt6Core_DIR}/../../../include/QtCore/6.*")
            foreach(_vdir ${_qt6_private_version_dirs})
                if(EXISTS "${_vdir}/QtCore/private/qhooks_p.h")
                    set(_qt6_private_include "${_vdir}/QtCore")
                    break()
                endif()
            endforeach()
        endif()
        if(_qt6_private_include)
            message(STATUS "Found Qt6 private headers at: ${_qt6_private_include}")
            # Create synthetic Qt6::CorePrivate target so downstream targets can link it.
            get_filename_component(_qt6_private_parent "${_qt6_private_include}" DIRECTORY)
            if(NOT TARGET Qt6::CorePrivate)
                add_library(Qt6::CorePrivate INTERFACE IMPORTED)
                set_target_properties(Qt6::CorePrivate PROPERTIES
                    INTERFACE_INCLUDE_DIRECTORIES "${_qt6_private_include};${_qt6_private_parent}"
                )
            endif()
        else()
            message(FATAL_ERROR "Qt6::CorePrivate not found. Private headers are required for the probe.")
        endif()
    endif()

    # Optional: Qt Quick/QML support
    find_package(Qt6 COMPONENTS Qml Quick QUIET)
    if(Qt6Qml_FOUND AND Qt6Quick_FOUND)
        set(QTPILOT_HAS_QML ON)
        message(STATUS "Found Qt6 Qml/Quick - QML introspection enabled")
    else()
        set(QTPILOT_HAS_QML OFF)
        message(STATUS "Qt6 Qml/Quick not found - QML introspection disabled")
    endif()
else()
    find_package(Qt5 ${QT_MIN_VERSION_5} REQUIRED COMPONENTS
        Core
        Network
        WebSockets
        Widgets
        Test
    )
    set(QT_VERSION_MAJOR 5)
    set(QT_VERSION ${Qt5_VERSION})
    message(STATUS "Found Qt5: ${Qt5_VERSION}")

    find_package(Qt5 COMPONENTS CorePrivate QUIET)
    if(NOT Qt5CorePrivate_FOUND)
        message(STATUS "Qt5::CorePrivate not found via component - trying header detection")
        find_path(_qt5_private_include "private/qhooks_p.h"
            PATHS "${Qt5Core_DIR}/../../.."
            PATH_SUFFIXES "include/QtCore/${Qt5_VERSION}/QtCore"
        )
        # Fallback: scan for any version subdirectory (handles cmake version ≠ header version)
        if(NOT _qt5_private_include)
            file(GLOB _qt5_private_version_dirs "${Qt5Core_DIR}/../../../include/QtCore/5.*")
            foreach(_vdir ${_qt5_private_version_dirs})
                if(EXISTS "${_vdir}/QtCore/private/qhooks_p.h")
                    set(_qt5_private_include "${_vdir}/QtCore")
                    break()
                endif()
            endforeach()
        endif()
        if(_qt5_private_include)
            message(STATUS "Found Qt5 private headers at: ${_qt5_private_include}")
            # Create synthetic Qt5::CorePrivate target so downstream targets can link it.
            # Include both the QtCore subdir (for "private/qhooks_p.h") and its parent
            # (for "QtCore/private/qglobal_p.h" referenced inside Qt private headers).
            get_filename_component(_qt5_private_parent "${_qt5_private_include}" DIRECTORY)
            if(NOT TARGET Qt5::CorePrivate)
                add_library(Qt5::CorePrivate INTERFACE IMPORTED)
                set_target_properties(Qt5::CorePrivate PROPERTIES
                    INTERFACE_INCLUDE_DIRECTORIES "${_qt5_private_include};${_qt5_private_parent}"
                )
            endif()
        else()
            message(FATAL_ERROR "Qt5::CorePrivate not found. Private headers are required for the probe.")
        endif()
    endif()

    # Optional: Qt Quick/QML support
    find_package(Qt5 COMPONENTS Qml Quick QUIET)
    if(Qt5Qml_FOUND AND Qt5Quick_FOUND)
        set(QTPILOT_HAS_QML ON)
        message(STATUS "Found Qt5 Qml/Quick - QML introspection enabled")
    else()
        set(QTPILOT_HAS_QML OFF)
        message(STATUS "Qt5 Qml/Quick not found - QML introspection disabled")
    endif()
endif()

# Verify Qt was found
if(NOT Qt6_FOUND AND NOT Qt5_FOUND)
    message(FATAL_ERROR "No Qt installation found. Set CMAKE_PREFIX_PATH or -DQTPILOT_QT_DIR=<path>")
endif()

# Enforce minimum patch versions
if(Qt5_FOUND AND QT_VERSION VERSION_LESS "5.15.1")
    message(FATAL_ERROR "Qt ${QT_VERSION} is below minimum supported version 5.15.1.")
endif()
if(Qt6_FOUND AND QT_VERSION VERSION_LESS "6.5")
    message(FATAL_ERROR "Qt ${QT_VERSION} is below minimum supported version 6.5.")
endif()

# Suppress APIs deprecated before Qt 5.15.0
add_definitions(-DQT_DISABLE_DEPRECATED_BEFORE=0x050F00)

# Extract Qt major.minor version tag for artifact naming
string(REGEX MATCH "^([0-9]+)\\.([0-9]+)" _qt_major_minor "${QT_VERSION}")
set(QTPILOT_QT_VERSION_TAG "qt${_qt_major_minor}" CACHE INTERNAL "Qt version tag for artifact naming")
message(STATUS "Qt version tag: ${QTPILOT_QT_VERSION_TAG}")

# Detect architecture tag for diagnostic output
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(QTPILOT_ARCH_TAG "x86" CACHE INTERNAL "Architecture tag for diagnostic output")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(QTPILOT_ARCH_TAG "x64" CACHE INTERNAL "Architecture tag for diagnostic output")
else()
    set(QTPILOT_ARCH_TAG "unknown" CACHE INTERNAL "Architecture tag for diagnostic output")
endif()
message(STATUS "Architecture:  ${QTPILOT_ARCH_TAG}")

# Flat install paths (no versioned subdirectories)
set(QTPILOT_INSTALL_LIBDIR "lib" CACHE INTERNAL "Library install directory")

# Create Qt version-independent target aliases
if(QT_VERSION_MAJOR EQUAL 6)
    set(QT_LIBRARIES Qt6::Core Qt6::Network Qt6::WebSockets Qt6::Widgets)
else()
    set(QT_LIBRARIES Qt5::Core Qt5::Network Qt5::WebSockets Qt5::Widgets)
endif()

# Enable Qt automoc/autouic/autorcc
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Platform-specific settings
if(WIN32)
    add_definitions(-DQTPILOT_PLATFORM_WINDOWS)
    add_definitions(-DNOMINMAX)
    add_definitions(-DWIN32_LEAN_AND_MEAN)
elseif(UNIX AND NOT APPLE)
    add_definitions(-DQTPILOT_PLATFORM_LINUX)
elseif(APPLE)
    add_definitions(-DQTPILOT_PLATFORM_MACOS)
    message(WARNING "macOS support is not yet implemented")
endif()

# Compiler warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        -Wno-unused-parameter
        -Wcast-align
        -Wformat=2
        -Wmissing-include-dirs
        -Wswitch-default
        -Wunused
    )
elseif(MSVC)
    add_compile_options(
        /W4
        /WX
        /permissive-
        /Zc:__cplusplus
        /external:anglebrackets
        /external:W0
        /wd4701
    )
endif()

# Microsoft Detours (Windows only) - for CreateProcessW hooking in probe
if(WIN32)
    include(FetchContent)
    FetchContent_Declare(
        detours
        GIT_REPOSITORY https://github.com/microsoft/Detours.git
        GIT_TAG        v4.0.1
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(detours)

    # Detours doesn't ship a CMakeLists.txt, so we build it ourselves
    add_library(detours STATIC
        ${detours_SOURCE_DIR}/src/detours.cpp
        ${detours_SOURCE_DIR}/src/modules.cpp
        ${detours_SOURCE_DIR}/src/disasm.cpp
        ${detours_SOURCE_DIR}/src/image.cpp
        ${detours_SOURCE_DIR}/src/creatwth.cpp
    )
    target_include_directories(detours PUBLIC ${detours_SOURCE_DIR}/src)
    # Suppress warnings in Detours sources that trigger /W4 /WX.
    # /permissive is needed because Detours uses goto that skips initialisers (C2362).
    target_compile_options(detours PRIVATE /wd4100 /wd4456 /wd4706 /W3 /permissive)
    # Detours needs _WIN32_WINNT for proper API level
    target_compile_definitions(detours PRIVATE _WIN32_WINNT=0x0601)
endif()

# Add subdirectories
add_subdirectory(src/probe)

if(WIN32)
    add_subdirectory(src/shared)
endif()

if(WIN32 OR (UNIX AND NOT APPLE))
    add_subdirectory(src/launcher)
endif()

if(QTPILOT_BUILD_TEST_APP)
    add_subdirectory(test_app)
endif()

if(QTPILOT_BUILD_TESTS)
    enable_testing()
    message(STATUS "Building unit tests with Qt Test framework")
    add_subdirectory(tests)
endif()

# Installation
include(GNUInstallDirs)

# Probe library: flat lib/ directory
install(TARGETS qtPilot_probe
    LIBRARY DESTINATION ${QTPILOT_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${QTPILOT_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${QTPILOT_INSTALL_LIBDIR}
)

# Launcher executable
if(TARGET qtPilot_launcher)
    install(TARGETS qtPilot_launcher
        RUNTIME DESTINATION bin
    )
endif()

# Headers
install(DIRECTORY src/probe/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/qtpilot
    FILES_MATCHING PATTERN "*.h"
)

# Package configuration
include(CMakePackageConfigHelpers)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/qtPilotConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/qtPilotConfig.cmake"
    INSTALL_DESTINATION share/cmake/qtPilot
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/qtPilotConfig.cmake"
    DESTINATION share/cmake/qtPilot
)

# Install helper cmake module alongside the config
install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/qtPilot_inject_probe.cmake"
    DESTINATION share/cmake/qtPilot
)

# Print configuration summary
message(STATUS "")
message(STATUS "qtPilot Configuration Summary:")
message(STATUS "  Version:        ${PROJECT_VERSION}")
message(STATUS "  Build type:     ${CMAKE_BUILD_TYPE}")
message(STATUS "  Qt version:     ${QT_VERSION} (Qt${QT_VERSION_MAJOR})")
message(STATUS "  Qt version tag: ${QTPILOT_QT_VERSION_TAG}")
message(STATUS "  Architecture:   ${QTPILOT_ARCH_TAG}")
message(STATUS "  Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "  Lib install:    ${QTPILOT_INSTALL_LIBDIR}")
message(STATUS "  Build tests:    ${QTPILOT_BUILD_TESTS}")
message(STATUS "  Build test app: ${QTPILOT_BUILD_TEST_APP}")
message(STATUS "  QML support:    ${QTPILOT_HAS_QML}")
message(STATUS "")
