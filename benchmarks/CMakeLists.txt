# Micro-benchmarks for the probe's hot paths.
#
# Opt-in (QTPILOT_BUILD_BENCHMARKS=OFF by default) for two reasons: the target pulls
# google/benchmark over the network at configure time, and nothing in the normal
# build or test cycle needs it. It is a tool for answering "did this change the
# COMPLEXITY of a hot path", not a gate.

# google/benchmark does not build clean under this project's -Werror set: its
# headers trip -Wswitch-default and colorprint.cc trips -Wformat-nonliteral. Third
# party code is not ours to fix, and relaxing the flags globally would weaken the
# probe. Clear the inherited options for THIS directory only -- src/ and tests/ keep
# the strict set. The benchmark sources here are diagnostics, not shipped code.
set_directory_properties(PROPERTIES COMPILE_OPTIONS "")

include(FetchContent)

set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(BENCHMARK_INSTALL_DOCS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    googlebenchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG        v1.9.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(googlebenchmark)

add_executable(qtPilot_bench_object_id bench_object_id.cpp)

target_link_libraries(qtPilot_bench_object_id PRIVATE
    qtPilot_probe
    benchmark::benchmark
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
)
if(QTPILOT_HAS_QML)
    target_link_libraries(qtPilot_bench_object_id PRIVATE
        Qt${QT_VERSION_MAJOR}::Qml Qt${QT_VERSION_MAJOR}::Quick)
endif()

target_include_directories(qtPilot_bench_object_id PRIVATE ${CMAKE_SOURCE_DIR}/src/probe)
