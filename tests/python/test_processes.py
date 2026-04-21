from __future__ import annotations

import sys
from pathlib import Path

import pytest

from emule_test_harness.processes import run_captured


def test_run_captured_records_output_and_exit_code(tmp_path: Path) -> None:
    log_path = tmp_path / "command.log"

    completed = run_captured(
        (sys.executable, "-c", "print('hello')"),
        log_path=log_path,
    )

    assert completed.returncode == 0
    assert completed.output.strip() == "hello"
    assert log_path.read_text(encoding="utf-8").strip() == "hello"


def test_run_captured_failure_includes_output_tail() -> None:
    with pytest.raises(RuntimeError, match="failure detail"):
        run_captured(
            (sys.executable, "-c", "print('failure detail'); raise SystemExit(9)"),
        )
