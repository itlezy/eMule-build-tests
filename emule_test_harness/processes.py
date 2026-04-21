"""Subprocess helpers for Python harness runners."""

from __future__ import annotations

import subprocess
import sys
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
    echo: bool = False,
) -> CompletedHarnessCommand:
    """Runs a command, captures combined output, and optionally mirrors it live."""

    process = subprocess.Popen(
        [str(part) for part in command],
        cwd=str(cwd) if cwd is not None else None,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    output_parts: list[str] = []
    assert process.stdout is not None
    for line in process.stdout:
        output_parts.append(line)
        if echo:
            print(line, end="")
            sys.stdout.flush()

    returncode = process.wait()
    output = "".join(output_parts)
    if log_path is not None:
        log_path.parent.mkdir(parents=True, exist_ok=True)
        log_path.write_text(output, encoding="utf-8")
    if check and returncode != 0:
        tail = "\n".join(output.splitlines()[-20:])
        raise RuntimeError(
            f"Command failed with exit code {returncode}: "
            + subprocess.list2cmdline([str(part) for part in command])
            + (f"\nLast output:\n{tail}" if tail else "")
        )
    return CompletedHarnessCommand(
        command=tuple(str(part) for part in command),
        returncode=returncode,
        output=output,
    )
