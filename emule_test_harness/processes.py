"""Subprocess helpers for Python harness runners."""

from __future__ import annotations

import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Sequence


@dataclass(frozen=True)
class CompletedHarnessCommand:
    """Captured result for one harness subprocess invocation."""

    command: tuple[str, ...]
    returncode: int
    output: str


def run_captured(
    command: Sequence[str],
    *,
    cwd: Path | None = None,
    log_path: Path | None = None,
    check: bool = True,
) -> CompletedHarnessCommand:
    """Runs a command, captures combined output, and optionally writes a log."""

    completed = subprocess.run(
        [str(part) for part in command],
        cwd=str(cwd) if cwd is not None else None,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        check=False,
    )
    if log_path is not None:
        log_path.parent.mkdir(parents=True, exist_ok=True)
        log_path.write_text(completed.stdout, encoding="utf-8")
    if check and completed.returncode != 0:
        raise RuntimeError(
            f"Command failed with exit code {completed.returncode}: "
            + subprocess.list2cmdline([str(part) for part in command])
        )
    return CompletedHarnessCommand(
        command=tuple(str(part) for part in command),
        returncode=completed.returncode,
        output=completed.stdout,
    )
