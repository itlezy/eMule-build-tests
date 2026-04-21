from __future__ import annotations

import json
import shutil
import subprocess
from pathlib import Path

import pytest

from emule_test_harness.privacy_guard import PrivacyGuardFailure, run_privacy_guard


def write_policy(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(
            {
                "policyVersion": "test-policy/v1",
                "excludedPathRegexes": [r"(^|[\\/])reports([\\/]|$)"],
                "pathRules": [],
                "contentRules": [
                    {
                        "id": "windows-user-profile-path",
                        "reason": "no profile paths",
                        "regex": r"C:\\Users\\[A-Za-z0-9_][A-Za-z0-9._-]*",
                    }
                ],
            }
        ),
        encoding="utf-8",
    )


def init_tracked_repo(repo_root: Path, files: dict[str, str]) -> None:
    if shutil.which("git") is None:
        pytest.skip("git is required for tracked-file privacy guard tests")
    subprocess.run(["git", "init", "--quiet"], cwd=repo_root, check=True)
    for relative_path, content in files.items():
        path = repo_root / relative_path
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8")
    subprocess.run(["git", "add", "."], cwd=repo_root, check=True)


def test_privacy_guard_passes_clean_tracked_files(tmp_path: Path, monkeypatch) -> None:
    monkeypatch.delenv("TRACKED_FILE_PRIVACY_IDENTIFIERS", raising=False)
    repo_root = tmp_path / "repo"
    repo_root.mkdir()
    init_tracked_repo(repo_root, {"README.md": "clean\n"})
    policy_path = repo_root / "policy.json"
    write_policy(policy_path)

    summary = run_privacy_guard(repo_root=repo_root, policy_path=policy_path)

    assert summary["passed"] is True
    assert summary["scannedTrackedFiles"] == 1


def test_privacy_guard_fails_on_content_rule_match(tmp_path: Path) -> None:
    repo_root = tmp_path / "repo"
    repo_root.mkdir()
    init_tracked_repo(repo_root, {"README.md": r"path C:\Users\Example"})
    policy_path = repo_root / "policy.json"
    write_policy(policy_path)

    with pytest.raises(PrivacyGuardFailure) as failure:
        run_privacy_guard(repo_root=repo_root, policy_path=policy_path)

    assert failure.value.summary["contentMatches"][0]["rule"] == "windows-user-profile-path"


def test_privacy_guard_fails_on_dynamic_identifier_filename(tmp_path: Path, monkeypatch) -> None:
    monkeypatch.setenv("TRACKED_FILE_PRIVACY_IDENTIFIERS", "privateid")
    repo_root = tmp_path / "repo"
    repo_root.mkdir()
    init_tracked_repo(repo_root, {"notes-privateid.txt": "clean\n"})
    policy_path = repo_root / "policy.json"
    write_policy(policy_path)

    with pytest.raises(PrivacyGuardFailure) as failure:
        run_privacy_guard(repo_root=repo_root, policy_path=policy_path)

    assert failure.value.summary["pathMatches"][0]["rule"] == "personal-identifier-filename"
