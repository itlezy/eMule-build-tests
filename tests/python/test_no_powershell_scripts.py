from __future__ import annotations

from pathlib import Path


def test_repo_does_not_carry_powershell_scripts() -> None:
    repo_root = Path(__file__).resolve().parents[2]

    assert list(repo_root.rglob("*.ps1")) == []
