from __future__ import annotations

import json
from pathlib import Path

from emule_test_harness.native_coverage import (
    build_coverage_command,
    get_open_cpp_coverage_executable_path,
    parse_doctest_suite_execution_stats,
    read_cobertura_line_metrics,
)


def test_get_open_cpp_coverage_executable_path_resolves_existing_tool(tmp_path: Path) -> None:
    executable = tmp_path / "OpenCppCoverage.exe"
    executable.write_text("", encoding="utf-8")

    assert get_open_cpp_coverage_executable_path(tmp_path) == executable


def test_parse_doctest_suite_execution_stats_uses_last_summary_line() -> None:
    stats = parse_doctest_suite_execution_stats(
        "\n".join(
            [
                "[doctest] test cases: 1 | 1 passed | 0 failed | 0 skipped",
                "[doctest] test cases: 5 | 4 passed | 1 failed | 2 skipped",
            ]
        )
    )

    assert stats == {"total": 5, "passed": 4, "failed": 1, "skipped": 2, "executed": 5}


def test_build_coverage_command_matches_opencppcoverage_contract(tmp_path: Path) -> None:
    command = build_coverage_command(
        coverage_executable_path=tmp_path / "OpenCppCoverage.exe",
        binary_path=tmp_path / "build" / "emule-tests.exe",
        app_root=tmp_path / "app",
        test_repo_root=tmp_path / "tests",
        suite_name="parity",
        suite_binary_coverage_path=tmp_path / "suite.cov",
        cobertura_path=tmp_path / "coverage.xml",
        merged_binary_coverage_path=tmp_path / "previous.cov",
    )

    assert command[0].endswith("OpenCppCoverage.exe")
    assert "--modules" in command
    assert "--input_coverage" in command
    assert any(str(part).startswith("binary:") for part in command)
    assert any(str(part).startswith("cobertura:") for part in command)
    assert "--test-suite=parity" in command


def test_read_cobertura_line_metrics_prefers_line_counts(tmp_path: Path) -> None:
    cobertura_path = tmp_path / "coverage.xml"
    cobertura_path.write_text(
        '<coverage lines-covered="25" lines-valid="100" line-rate="0.9" />',
        encoding="utf-8",
    )

    assert read_cobertura_line_metrics(cobertura_path) == (25, 100, 25.0)


def test_coverage_summary_shape_stays_json_serializable(tmp_path: Path) -> None:
    payload = {
        "report_dir": str(tmp_path),
        "suite_runs": [{"suite_name": "parity", "execution": {"executed": 1}}],
        "line_rate_percent": 50.0,
    }

    assert json.loads(json.dumps(payload))["line_rate_percent"] == 50.0
