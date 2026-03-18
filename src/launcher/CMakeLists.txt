# qtPilot Launcher
# Launches Qt applications with the probe library injected.
# Works on both Windows (DLL injection) and Linux (LD_PRELOAD).

set(LAUNCHER_SOURCES
    main.cpp
    qt_env_setup.cpp
)

set(LAUNCHER_HEADERS
    injector.h
    elevation_windows.h
    qt_env_setup.h
)

# Platform-specific injection implementation
if(WIN32)
    list(APPEND LAUNCHER_SOURCES injector_windows.cpp elevation_windows.cpp)
elseif(APPLE)
    list(APPEND LAUNCHER_SOURCES injector_macos.cpp)
elseif(UNIX)
    list(APPEND LAUNCHER_SOURCES injector_linux.cpp)
endif()

add_executable(qtPilot_launcher
    ${LAUNCHER_SOURCES}
    ${LAUNCHER_HEADERS}
)

# Link Qt libraries
if(QT_VERSION_MAJOR EQUAL 6)
    target_link_libraries(qtPilot_launcher
        PRIVATE
            Qt6::Core
    )
else()
    target_link_libraries(qtPilot_launcher
        PRIVATE
            Qt5::Core
    )
endif()

# Windows: link shared injection utilities (provides Psapi transitively)
if(WIN32)
    target_link_libraries(qtPilot_launcher PRIVATE qtPilot_shared shell32)
endif()

set_target_properties(qtPilot_launcher PROPERTIES
    OUTPUT_NAME "qtPilot-launcher"
    DEBUG_POSTFIX "d"
)

# Include the launcher source directory for injector.h
target_include_directories(qtPilot_launcher PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Embed build-time Qt prefix so ensureQtEnvironment can use it as a fallback
# when QLibraryInfo reports a relocated (incorrect) path for dev builds.
get_target_property(_qt_core_loc Qt${QT_VERSION_MAJOR}::Core LOCATION)
if(_qt_core_loc)
    if(APPLE AND _qt_core_loc MATCHES "\\.framework/")
        # macOS framework: e.g. .../lib/QtCore.framework/Versions/A/QtCore
        # Navigate up to the lib/ directory, then one more to get the prefix
        string(REGEX REPLACE "/lib/Qt[^/]*\\.framework/.*$" "" _qt_prefix "${_qt_core_loc}")
    else()
        # Windows: bin/Qt6Core.dll -> bin/ -> prefix
        # Linux: lib/libQt6Core.so -> lib/ -> prefix
        get_filename_component(_qt_bin_dir "${_qt_core_loc}" DIRECTORY)
        get_filename_component(_qt_prefix "${_qt_bin_dir}" DIRECTORY)
    endif()
    target_compile_definitions(qtPilot_launcher PRIVATE
        QTPILOT_BUILD_QT_PREFIX="${_qt_prefix}"
    )
endif()
