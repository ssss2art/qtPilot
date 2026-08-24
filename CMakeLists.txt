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

# macOS deployment target (10.15 = Catalina, required for CGPreflightScreenCaptureAccess)
if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS deployment target")
endif()

# Options
# Android and iOS are both UNIX (and iOS is also APPLE) as far as CMake is
# concerned, so every desktop-only branch below must exclude them explicitly or
# it silently applies to mobile too.
if(ANDROID OR IOS)
    set(QTPILOT_MOBILE ON)
else()
    set(QTPILOT_MOBILE OFF)
endif()

# On mobile the probe cannot be INJECTED -- neither platform lets you insert a
# library into a third-party app. It is instead LINKED into a development build,
# where Q_COREAPP_STARTUP_FUNCTION starts it. A static probe avoids having to
# embed and code-sign an extra dynamic library inside the app bundle.
option(QTPILOT_PROBE_STATIC "Build the probe as a static library (linked into the app)" ${QTPILOT_MOBILE})

# Refuse the one configuration that would build and then be unusable. On Windows
# the probe links qtPilot_shared and Detours PRIVATELY and neither is installed, so
# a static probe cannot be consumed through the installed package -- the consumer's
# link dies on unresolved Detours symbols. Failing here beats shipping a package
# that only fails at the point somebody tries to link against it. Windows has
# working injection, so a linked probe has no use case there anyway; static
# delivery exists for Android and iOS (see docs/MOBILE.md).
if(QTPILOT_PROBE_STATIC AND WIN32)
    message(FATAL_ERROR
        "QTPILOT_PROBE_STATIC is not supported on Windows: the probe's Detours and "
        "qtPilot_shared dependencies are not installed, so the resulting archive "
        "cannot be linked by a consumer. Use the shared probe with injection.")
endif()

option(QTPILOT_BUILD_TESTS "Build unit tests" ON)
# Complexity benchmarks. OFF by default: the target fetches google/benchmark over
# the network at configure time, and it is a diagnostic tool rather than part of the
# build/test cycle. See benchmarks/bench_object_id.cpp.
option(QTPILOT_BUILD_BENCHMARKS "Build micro-benchmarks (fetches google/benchmark)" OFF)

# Sanitizers. A comma-separated -fsanitize= list, empty for off, e.g.
# "address,undefined" or "thread". Not a boolean because the useful combinations are
# mutually exclusive: ASan and TSan cannot be linked together.
set(QTPILOT_SANITIZE "" CACHE STRING "Sanitizers to enable (e.g. address,undefined or thread)")
option(QTPILOT_BUILD_TEST_APP "Build the test Qt application" ON)
if(QTPILOT_MOBILE)
    # Neither the harness app nor the host-side tests deploy to a device.
    set(QTPILOT_BUILD_TESTS OFF)
    set(QTPILOT_BUILD_TEST_APP OFF)
endif()
# QML introspection is auto-detected from the Qt installation. This switch
# forces it off even when Qt Quick is present, so the QML-disabled build stays
# compilable -- CI otherwise never exercises it, because every Qt install in
# the matrix ships qtdeclarative.
option(QTPILOT_ENABLE_QML "Enable QML/Qt Quick introspection when Qt Quick is available" ON)

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
        # Fallback: Apple framework layout keeps private headers inside the
        # framework bundle rather than under include/. Two shapes, both real:
        # macOS is versioned (lib/QtCore.framework/Versions/A/Headers/<ver>/QtCore)
        # while an iOS framework is flat (lib/QtCore.framework/Headers/<ver>/QtCore).
        # The Qt5 branch below has had the macOS half of this for a while; Qt6 never
        # got it, so the iOS kit could only ever configure when the CorePrivate
        # component happened to resolve -- which is exactly how CI's Qt 6.8.0 iOS
        # leg failed while a local 6.11.1 build succeeded.
        if(NOT _qt6_private_include AND APPLE)
            file(GLOB _qt6_fw_version_dirs
                "${Qt6Core_DIR}/../../../lib/QtCore.framework/Headers/6.*"
                "${Qt6Core_DIR}/../../../lib/QtCore.framework/Versions/*/Headers/6.*")
            foreach(_vdir ${_qt6_fw_version_dirs})
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
    if(QTPILOT_ENABLE_QML)
        find_package(Qt6 COMPONENTS Qml Quick QUIET)
    endif()
    if(NOT QTPILOT_ENABLE_QML)
        set(QTPILOT_HAS_QML OFF)
        message(STATUS "QML introspection disabled by QTPILOT_ENABLE_QML=OFF")
    elseif(Qt6Qml_FOUND AND Qt6Quick_FOUND)
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
        # Fallback: macOS framework layout keeps private headers inside the
        # framework bundle (lib/QtCore.framework/Versions/5/Headers/<ver>/QtCore)
        # rather than under include/.
        if(NOT _qt5_private_include AND APPLE)
            file(GLOB _qt5_fw_version_dirs
                "${Qt5Core_DIR}/../../../lib/QtCore.framework/Versions/5/Headers/5.*")
            foreach(_vdir ${_qt5_fw_version_dirs})
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
    if(QTPILOT_ENABLE_QML)
        find_package(Qt5 COMPONENTS Qml Quick QUIET)
    endif()
    if(NOT QTPILOT_ENABLE_QML)
        set(QTPILOT_HAS_QML OFF)
        message(STATUS "QML introspection disabled by QTPILOT_ENABLE_QML=OFF")
    elseif(Qt5Qml_FOUND AND Qt5Quick_FOUND)
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
        -Wswitch-default
        -Wunused
    )
    # -Wmissing-include-dirs is useful but incompatible with the Xcode generator,
    # which puts DerivedSources paths on the command line before they exist. With
    # -Werror that turns every iOS/Xcode compile into a failure that has nothing
    # to do with the code. Keep it where the generator populates its own include
    # dirs up front.
    if(NOT CMAKE_GENERATOR STREQUAL "Xcode")
        add_compile_options(-Wmissing-include-dirs)
    endif()
    if(QTPILOT_SANITIZE)
        message(STATUS "Sanitizers: ${QTPILOT_SANITIZE}")
        # -fno-omit-frame-pointer so reports carry a usable stack.
        # -fno-sanitize-recover=undefined makes UBSan abort rather than log and
        # continue: a finding that only prints is a finding a green test run hides.
        add_compile_options(
            -fsanitize=${QTPILOT_SANITIZE}
            -fno-omit-frame-pointer
            -fno-sanitize-recover=undefined
        )
        add_link_options(-fsanitize=${QTPILOT_SANITIZE})
    endif()

    # Qt 6.10+ Q_GLOBAL_STATIC uses variadic macros with omitted/zero args.
    # These are Clang extensions not recognised by all compiler versions,
    # so probe before enabling to avoid -Werror,-Wunknown-warning-option.
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag(-Wno-variadic-macro-arguments-omitted
                            HAS_WNO_VARIADIC_MACRO_ARGUMENTS_OMITTED)
    if(HAS_WNO_VARIADIC_MACRO_ARGUMENTS_OMITTED)
        add_compile_options(-Wno-variadic-macro-arguments-omitted)
    endif()
    check_cxx_compiler_flag(-Wno-gnu-zero-variadic-macro-arguments
                            HAS_WNO_GNU_ZERO_VARIADIC_MACRO_ARGUMENTS)
    if(HAS_WNO_GNU_ZERO_VARIADIC_MACRO_ARGUMENTS)
        add_compile_options(-Wno-gnu-zero-variadic-macro-arguments)
    endif()
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

# The launcher injects the probe into an already-built process. Mobile has no
# such mechanism, so it is not merely unused there -- it does not apply.
if((WIN32 OR UNIX) AND NOT QTPILOT_MOBILE)
    add_subdirectory(src/launcher)
endif()

if(QTPILOT_BUILD_TEST_APP)
    add_subdirectory(test_app)
    install(TARGETS qtPilot_test_app RUNTIME DESTINATION bin)

    # Pure Qt Quick companion app backing its views with real
    # QAbstractItemModels, so qt.models.* can be exercised against QML. Qt 6
    # only -- qt_add_qml_module has no Qt 5 equivalent.
    if(QTPILOT_HAS_QML AND QT_VERSION_MAJOR EQUAL 6)
        find_package(Qt6 QUIET COMPONENTS QuickControls2)
        if(Qt6QuickControls2_FOUND)
            add_subdirectory(test_app_qml)
            install(TARGETS qtPilot_test_app_qml
                RUNTIME DESTINATION bin
                BUNDLE DESTINATION bin)
        else()
            message(STATUS "QtQuickControls2 not found - skipping QML model test app")
        endif()
    endif()
endif()

if(QTPILOT_BUILD_TESTS)
    enable_testing()
    message(STATUS "Building unit tests with Qt Test framework")
    add_subdirectory(tests)
endif()

if(QTPILOT_BUILD_BENCHMARKS)
    message(STATUS "Building complexity benchmarks (google/benchmark)")
    add_subdirectory(benchmarks)
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
message(STATUS "  Benchmarks:     ${QTPILOT_BUILD_BENCHMARKS}")
message(STATUS "  Sanitizers:     ${QTPILOT_SANITIZE}")
message(STATUS "  Build test app: ${QTPILOT_BUILD_TEST_APP}")
message(STATUS "  QML support:    ${QTPILOT_HAS_QML}")
message(STATUS "")
