"""
Verification tests for CI rebuild spec.

Validates that all changes from the ci-rebuild-spec.md have been
correctly applied: old files deleted, build system simplified,
new CI workflows written, and configuration is consistent.

Run with: python -m pytest tests/test_ci_rebuild.py -v
"""

import json
import os
import re

import pytest

# Project root is one level up from tests/
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def read_file(relpath):
    """Read a file relative to project root. Returns None if not found."""
    path = os.path.join(ROOT, relpath)
    if not os.path.exists(path):
        return None
    with open(path, "r", encoding="utf-8") as f:
        return f.read()


def file_exists(relpath):
    return os.path.exists(os.path.join(ROOT, relpath))


def dir_exists(relpath):
    return os.path.isdir(os.path.join(ROOT, relpath))


# ============================================================
# Phase 1: Old infrastructure deleted
# ============================================================

class TestPhase1Deletions:
    """Verify all old CI infrastructure has been removed."""

    def test_old_ci_yml_deleted(self):
        assert not file_exists(".github/workflows/ci.yml") or \
            "workflow_call" in read_file(".github/workflows/ci.yml"), \
            "Old ci.yml should be deleted or replaced with new version"

    def test_ci_patched_qt_deleted(self):
        assert not file_exists(".github/workflows/ci-patched-qt.yml"), \
            "ci-patched-qt.yml should be deleted"

    def test_old_release_yml_deleted(self):
        # release.yml should exist but be the NEW version (with v* tag trigger)
        content = read_file(".github/workflows/release.yml")
        if content:
            assert "v*" in content, "release.yml should be the new version with v* tag trigger"

    def test_github_actions_dir_deleted(self):
        assert not dir_exists(".github/actions"), \
            ".github/actions/ directory should be deleted"

    def test_ci_dir_deleted(self):
        assert not dir_exists(".ci"), \
            ".ci/ directory should be deleted"

    def test_vcpkg_json_deleted(self):
        assert not file_exists("vcpkg.json"), \
            "vcpkg.json should be deleted"

    def test_ports_dir_deleted(self):
        assert not dir_exists("ports"), \
            "ports/ directory should be deleted"

    def test_package_json_deleted(self):
        assert not file_exists("package.json"), \
            "package.json should be deleted"

    def test_node_modules_deleted(self):
        assert not dir_exists("node_modules"), \
            "node_modules/ directory should be deleted"

    def test_publish_pypi_kept(self):
        assert file_exists(".github/workflows/publish-pypi.yml"), \
            "publish-pypi.yml should be kept"


# ============================================================
# Phase 2: Build system simplification
# ============================================================

class TestPhase2_1_CMakePresets:
    """Verify CMakePresets.json has been rewritten correctly."""

    @pytest.fixture
    def presets(self):
        content = read_file("CMakePresets.json")
        assert content is not None, "CMakePresets.json should exist"
        return json.loads(content)

    def test_no_vcpkg_references(self, presets):
        raw = json.dumps(presets)
        assert "vcpkg" not in raw.lower(), \
            "CMakePresets.json should have no vcpkg references"

    def test_has_base_hidden_preset(self, presets):
        configure = presets["configurePresets"]
        base = [p for p in configure if p["name"] == "base"]
        assert len(base) == 1, "Should have exactly one 'base' preset"
        assert base[0].get("hidden") is True, "base preset should be hidden"

    def test_has_four_configure_presets(self, presets):
        visible = [p for p in presets["configurePresets"] if not p.get("hidden")]
        names = {p["name"] for p in visible}
        expected = {"debug", "release", "windows-debug", "windows-release"}
        assert names == expected, \
            f"Expected presets {expected}, got {names}"

    def test_no_ci_only_presets(self, presets):
        names = {p["name"] for p in presets["configurePresets"]}
        assert "ci-linux" not in names, "ci-linux preset should not exist"
        assert "ci-windows" not in names, "ci-windows preset should not exist"

    def test_four_build_presets(self, presets):
        names = {p["name"] for p in presets["buildPresets"]}
        expected = {"debug", "release", "windows-debug", "windows-release"}
        assert names == expected, \
            f"Expected build presets {expected}, got {names}"

    def test_four_test_presets(self, presets):
        names = {p["name"] for p in presets["testPresets"]}
        expected = {"debug", "release", "windows-debug", "windows-release"}
        assert names == expected, \
            f"Expected test presets {expected}, got {names}"

    def test_windows_presets_have_vs_generator(self, presets):
        for p in presets["configurePresets"]:
            if "windows" in p["name"] and not p.get("hidden"):
                assert "Visual Studio 17 2022" in p.get("generator", ""), \
                    f"Preset {p['name']} should use VS 2022 generator"

    def test_base_preset_has_binary_and_install_dir(self, presets):
        base = [p for p in presets["configurePresets"] if p["name"] == "base"][0]
        assert "binaryDir" in base
        assert "installDir" in base

    def test_base_preset_has_compile_commands(self, presets):
        base = [p for p in presets["configurePresets"] if p["name"] == "base"][0]
        cache = base.get("cacheVariables", {})
        assert cache.get("CMAKE_EXPORT_COMPILE_COMMANDS") == "ON"


class TestPhase2_2_RootCMakeLists:
    """Verify root CMakeLists.txt has been simplified."""

    @pytest.fixture
    def cmake(self):
        content = read_file("CMakeLists.txt")
        assert content is not None
        return content

    def test_no_clang_tidy_option(self, cmake):
        assert "QTPILOT_ENABLE_CLANG_TIDY" not in cmake, \
            "QTPILOT_ENABLE_CLANG_TIDY option should be removed"

    def test_no_deploy_qt_option(self, cmake):
        assert "QTPILOT_DEPLOY_QT" not in cmake, \
            "QTPILOT_DEPLOY_QT option should be removed"

    def test_no_windeployqt(self, cmake):
        assert "windeployqt" not in cmake.lower(), \
            "windeployqt references should be removed"

    def test_no_nlohmann_json(self, cmake):
        assert "nlohmann_json" not in cmake, \
            "nlohmann_json references should be removed"

    def test_no_spdlog(self, cmake):
        assert "spdlog" not in cmake, \
            "spdlog references should be removed"

    def test_no_write_basic_package_version_file(self, cmake):
        assert "write_basic_package_version_file" not in cmake, \
            "write_basic_package_version_file call should be removed"

    def test_flat_install_path(self, cmake):
        # Should use "lib" not "lib/qtpilot/${QTPILOT_QT_VERSION_TAG}"
        assert '"lib"' in cmake or "'lib'" in cmake, \
            "Install path should be flat 'lib', not versioned"
        assert "lib/qtpilot/" not in cmake, \
            "Versioned lib/qtpilot/ path should be removed"

    def test_has_qt_detection(self, cmake):
        assert "find_package(Qt6" in cmake
        assert "find_package(Qt5" in cmake

    def test_has_qt_version_tag(self, cmake):
        assert "QTPILOT_QT_VERSION_TAG" in cmake

    def test_has_compiler_warnings(self, cmake):
        assert "-Wall" in cmake or "/W4" in cmake

    def test_has_automoc(self, cmake):
        assert "CMAKE_AUTOMOC" in cmake

    def test_has_subdirectories(self, cmake):
        assert "add_subdirectory(src/probe)" in cmake
        assert "add_subdirectory(src/launcher)" in cmake
        assert "add_subdirectory(tests)" in cmake

    def test_no_qtpilot_deploy_qt_function(self, cmake):
        assert "function(qtpilot_deploy_qt" not in cmake, \
            "qtpilot_deploy_qt function definition should be removed"

    def test_line_count_reduced(self, cmake):
        lines = len(cmake.strip().split("\n"))
        assert lines < 300, \
            f"Root CMakeLists.txt should be ~200 lines, got {lines}"


class TestPhase2_3_TestsCMakeLists:
    """Verify tests/CMakeLists.txt has been rewritten with helper function."""

    @pytest.fixture
    def cmake(self):
        content = read_file("tests/CMakeLists.txt")
        assert content is not None
        return content

    def test_has_helper_function(self, cmake):
        assert "function(qtPilot_add_test)" in cmake, \
            "Should define qtPilot_add_test helper function"

    def test_uses_helper_for_tests(self, cmake):
        calls = re.findall(r"qtPilot_add_test\(", cmake)
        assert len(calls) >= 13, \
            f"Should have at least 13 qtPilot_add_test calls, found {len(calls)}"

    def test_all_test_names_present(self, cmake):
        expected_tests = [
            "test_jsonrpc",
            "test_object_registry",
            "test_object_id",
            "test_meta_inspector",
            "test_ui_interaction",
            "test_signal_monitor",
            "test_jsonrpc_introspection",
            "test_native_mode_api",
            "test_key_name_mapper",
            "test_computer_use_api",
            "test_chrome_mode_api",
            "test_model_navigator",
            "test_event_capture",
        ]
        for test in expected_tests:
            assert test in cmake, f"Test '{test}' should be present"

    def test_qminimal_deployment_kept(self, cmake):
        assert "qminimald.dll" in cmake, \
            "qminimal.dll deployment block should be kept"

    def test_line_count_reduced(self, cmake):
        lines = len(cmake.strip().split("\n"))
        assert lines < 150, \
            f"tests/CMakeLists.txt should be ~60-110 lines, got {lines}"

    def test_no_repeated_qt_linking_blocks(self, cmake):
        # The helper function should handle Qt linking; we shouldn't have
        # multiple manual if(QT_VERSION_MAJOR EQUAL 6) blocks for test targets
        # outside the function definition
        outside_function = cmake.split("endfunction()")[1] if "endfunction()" in cmake else ""
        qt_blocks = re.findall(r"if\(QT_VERSION_MAJOR EQUAL 6\)", outside_function)
        assert len(qt_blocks) == 0, \
            "Should not have Qt version checks outside the helper function for test targets"


class TestPhase2_4_ProbeCMakeLists:
    """Verify src/probe/CMakeLists.txt has been simplified."""

    @pytest.fixture
    def cmake(self):
        content = read_file("src/probe/CMakeLists.txt")
        assert content is not None
        return content

    def test_no_nlohmann_json(self, cmake):
        assert "nlohmann_json" not in cmake, \
            "nlohmann_json conditional linking should be removed"

    def test_no_spdlog(self, cmake):
        assert "spdlog" not in cmake, \
            "spdlog conditional linking should be removed"

    def test_no_deploy_qt(self, cmake):
        assert "qtpilot_deploy_qt" not in cmake, \
            "qtpilot_deploy_qt call should be removed"

    def test_keeps_output_name_with_version_tag(self, cmake):
        assert "qtPilot-probe-${QTPILOT_QT_VERSION_TAG}" in cmake, \
            "OUTPUT_NAME should keep version tag"


class TestPhase2_5_LauncherCMakeLists:
    """Verify src/launcher/CMakeLists.txt has been simplified."""

    @pytest.fixture
    def cmake(self):
        content = read_file("src/launcher/CMakeLists.txt")
        assert content is not None
        return content

    def test_no_deploy_qt(self, cmake):
        assert "qtpilot_deploy_qt" not in cmake, \
            "qtpilot_deploy_qt call should be removed"

    def test_output_name_is_just_launcher(self, cmake):
        assert '"qtPilot-launcher"' in cmake, \
            "Output name should be just 'qtPilot-launcher' (not versioned)"
        assert "qtpilot-launch-${QTPILOT_QT_VERSION_TAG}" not in cmake, \
            "Old versioned output name should be removed"


class TestPhase2_6_TestAppCMakeLists:
    """Verify test_app/CMakeLists.txt has been simplified."""

    @pytest.fixture
    def cmake(self):
        content = read_file("test_app/CMakeLists.txt")
        assert content is not None
        return content

    def test_no_deploy_qt(self, cmake):
        assert "qtpilot_deploy_qt" not in cmake, \
            "qtpilot_deploy_qt call should be removed"


class TestPhase2_7_qtPilotConfigCmake:
    """Verify cmake/qtPilotConfig.cmake.in has been simplified."""

    @pytest.fixture
    def cmake(self):
        content = read_file("cmake/qtPilotConfig.cmake.in")
        assert content is not None
        return content

    def test_no_fallback_qt_detection(self, cmake):
        # Should not try to find_package Qt itself
        assert "find_package(Qt6 QUIET COMPONENTS Core)" not in cmake, \
            "Fallback Qt auto-detection block should be removed"

    def test_no_debug_variant_detection(self, cmake):
        assert "_probe_dll_d" not in cmake, \
            "Debug variant detection for Windows should be removed"
        assert "_probe_so_d" not in cmake, \
            "Debug variant detection for Linux should be removed"

    def test_flat_lib_path(self, cmake):
        assert "lib/qtpilot/" not in cmake, \
            "Versioned lib/qtpilot/ path should be removed"
        # Should reference just lib/
        assert '${QTPILOT_PREFIX}/lib"' in cmake or "${QTPILOT_PREFIX}/lib" in cmake

    def test_has_imported_target(self, cmake):
        assert "qtPilot::Probe" in cmake

    def test_has_include_dirs(self, cmake):
        assert "INTERFACE_INCLUDE_DIRECTORIES" in cmake

    def test_line_count_reduced(self, cmake):
        lines = len(cmake.strip().split("\n"))
        assert lines < 140, \
            f"qtPilotConfig.cmake.in should be ~80-125 lines, got {lines}"


class TestPhase2_8_PyprojectToml:
    """Verify python/pyproject.toml has dev dependencies."""

    @pytest.fixture
    def toml(self):
        content = read_file("python/pyproject.toml")
        assert content is not None
        return content

    def test_has_optional_dependencies_section(self, toml):
        assert "[project.optional-dependencies]" in toml

    def test_has_pytest_dev_dependency(self, toml):
        assert "pytest" in toml
        assert "dev" in toml


# ============================================================
# Phase 3: New CI workflows
# ============================================================

class TestPhase3_1_NewCIWorkflow:
    """Verify the new .github/workflows/ci.yml is correct."""

    @pytest.fixture
    def ci(self):
        content = read_file(".github/workflows/ci.yml")
        assert content is not None, "ci.yml should exist"
        return content

    def test_has_push_trigger(self, ci):
        assert "push:" in ci

    def test_has_pr_trigger(self, ci):
        assert "pull_request:" in ci

    def test_has_workflow_dispatch(self, ci):
        assert "workflow_dispatch" in ci

    def test_has_workflow_call(self, ci):
        assert "workflow_call" in ci

    def test_has_lint_job(self, ci):
        assert "lint:" in ci
        assert "clang-format" in ci

    def test_has_build_job(self, ci):
        assert "build:" in ci

    def test_has_python_job(self, ci):
        assert "python:" in ci

    def test_python_job_correct_path(self, ci):
        assert "python" in ci
        # Should NOT reference src/mcp_server
        assert "src/mcp_server" not in ci, \
            "Python job should not reference old src/mcp_server path"

    def test_python_job_uses_dev_deps(self, ci):
        assert ".[dev]" in ci, \
            "Python job should install with dev dependencies"

    def test_build_matrix_has_8_cells(self, ci):
        # 4 Qt versions x 2 platforms
        qt_versions = re.findall(r'qt:\s*"?([0-9]+\.[0-9]+\.[0-9]+)"?', ci)
        assert len(qt_versions) == 8, \
            f"Build matrix should have 8 entries (4 Qt x 2 platforms), found {len(qt_versions)}"

    def test_build_matrix_qt_versions(self, ci):
        for version in ["5.15.2", "6.5.3", "6.8.0", "6.9.0"]:
            assert version in ci, f"Qt {version} should be in the build matrix"

    def test_uses_install_qt_action(self, ci):
        assert "jurplel/install-qt-action" in ci, \
            "Should use install-qt-action instead of vcpkg"

    def test_no_vcpkg_references(self, ci):
        assert "vcpkg" not in ci.lower(), \
            "CI should have no vcpkg references"

    def test_no_continue_on_error(self, ci):
        assert "continue-on-error" not in ci, \
            "Python tests should not have continue-on-error"

    def test_uses_release_and_windows_release_presets(self, ci):
        assert "release" in ci
        assert "windows-release" in ci

    def test_no_ci_only_presets(self, ci):
        assert "ci-linux" not in ci
        assert "ci-windows" not in ci

    def test_has_path_filters(self, ci):
        assert "src/**" in ci
        assert "tests/**" in ci
        assert "CMakeLists.txt" in ci


class TestPhase3_2_NewReleaseWorkflow:
    """Verify the new .github/workflows/release.yml is correct."""

    @pytest.fixture
    def release(self):
        content = read_file(".github/workflows/release.yml")
        assert content is not None, "release.yml should exist"
        return content

    def test_tag_trigger(self, release):
        assert "v*" in release, \
            "Release should trigger on v* tags"

    def test_reuses_ci_workflow(self, release):
        assert "ci.yml" in release, \
            "Release should reuse ci.yml as workflow_call"

    def test_has_release_job(self, release):
        assert "release:" in release

    def test_downloads_artifacts(self, release):
        assert "download-artifact" in release

    def test_creates_github_release(self, release):
        assert "softprops/action-gh-release" in release

    def test_generates_checksums(self, release):
        assert "SHA256SUMS" in release or "sha256sum" in release

    def test_extracts_launcher(self, release):
        assert "qtPilot-launcher" in release


class TestPhase3_3_PublishPyPIKept:
    """Verify publish-pypi.yml is unchanged."""

    def test_publish_pypi_exists(self):
        assert file_exists(".github/workflows/publish-pypi.yml")


# ============================================================
# Cross-cutting consistency checks
# ============================================================

class TestConsistency:
    """Cross-cutting checks for consistency across all files."""

    def test_no_vcpkg_anywhere(self):
        """No vcpkg references in any build/CI file."""
        files_to_check = [
            "CMakeLists.txt",
            "CMakePresets.json",
            "tests/CMakeLists.txt",
            "src/probe/CMakeLists.txt",
            "src/launcher/CMakeLists.txt",
            "test_app/CMakeLists.txt",
            "cmake/qtPilotConfig.cmake.in",
            ".github/workflows/ci.yml",
            ".github/workflows/release.yml",
        ]
        for f in files_to_check:
            content = read_file(f)
            if content:
                assert "vcpkg" not in content.lower(), \
                    f"{f} should have no vcpkg references"

    def test_no_deploy_qt_anywhere(self):
        """No qtpilot_deploy_qt calls in any CMakeLists."""
        files_to_check = [
            "src/probe/CMakeLists.txt",
            "src/launcher/CMakeLists.txt",
            "test_app/CMakeLists.txt",
        ]
        for f in files_to_check:
            content = read_file(f)
            if content:
                assert "qtpilot_deploy_qt" not in content, \
                    f"{f} should have no qtpilot_deploy_qt calls"

    def test_no_nlohmann_or_spdlog_anywhere(self):
        """No nlohmann_json or spdlog in any build file."""
        files_to_check = [
            "CMakeLists.txt",
            "src/probe/CMakeLists.txt",
        ]
        for f in files_to_check:
            content = read_file(f)
            if content:
                assert "nlohmann_json" not in content, \
                    f"{f} should have no nlohmann_json references"
                assert "spdlog" not in content, \
                    f"{f} should have no spdlog references"

    def test_preset_names_match_ci(self):
        """CI workflow preset names match CMakePresets.json."""
        presets_json = read_file("CMakePresets.json")
        ci = read_file(".github/workflows/ci.yml")
        assert presets_json and ci

        presets = json.loads(presets_json)
        preset_names = {p["name"] for p in presets["configurePresets"] if not p.get("hidden")}

        # CI should reference release and windows-release
        assert "release" in preset_names
        assert "windows-release" in preset_names


# ============================================================
# File statistics
# ============================================================

class TestFileStatistics:
    """Verify file sizes are in the expected range after simplification."""

    def test_cmake_presets_reasonable_size(self):
        content = read_file("CMakePresets.json")
        if content:
            data = json.loads(content)
            n_configure = len([p for p in data["configurePresets"] if not p.get("hidden")])
            assert n_configure == 4, f"Expected 4 visible configure presets, got {n_configure}"

    def test_root_cmake_under_300_lines(self):
        content = read_file("CMakeLists.txt")
        if content:
            lines = len(content.strip().split("\n"))
            assert lines < 300, f"Root CMakeLists.txt has {lines} lines, target is ~200"

    def test_tests_cmake_under_150_lines(self):
        content = read_file("tests/CMakeLists.txt")
        if content:
            lines = len(content.strip().split("\n"))
            assert lines < 150, f"tests/CMakeLists.txt has {lines} lines, target is ~60"

    def test_config_cmake_under_140_lines(self):
        content = read_file("cmake/qtPilotConfig.cmake.in")
        if content:
            lines = len(content.strip().split("\n"))
            assert lines < 140, f"qtPilotConfig.cmake.in has {lines} lines, target is ~80"
