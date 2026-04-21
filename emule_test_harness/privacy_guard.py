"""Tracked-file privacy guard for committed harness assets."""

from __future__ import annotations

import json
import os
import re
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Any


@dataclass(frozen=True)
class GuardRule:
    """One path or content rule loaded from the privacy policy."""

    id: str
    reason: str
    regex: str


class PrivacyGuardFailure(RuntimeError):
    """Failure raised when the tracked-file privacy scan finds violations."""

    def __init__(self, summary: dict[str, Any]) -> None:
        super().__init__("Tracked-file privacy guard failed.")
        self.summary = summary


def run_privacy_guard(
    *,
    repo_root: Path,
    policy_path: Path,
    summary_path: Path | None = None,
) -> dict[str, Any]:
    """Scans tracked files for local-path or personal-identifier leaks."""

    repo_root_path = repo_root.resolve()
    if not policy_path.is_file():
        raise RuntimeError(f"Privacy-guard policy not found at '{policy_path}'.")

    policy = json.loads(policy_path.read_text(encoding="utf-8"))
    excluded_path_regexes = [str(value) for value in policy.get("excludedPathRegexes", [])]
    personal_identifiers = get_personal_identifiers(repo_root_path)
    dynamic_path_rules = get_dynamic_path_rules(personal_identifiers)
    path_rules = [GuardRule(**entry) for entry in policy.get("pathRules", [])] + dynamic_path_rules
    content_rules = [GuardRule(**entry) for entry in policy.get("contentRules", [])]
    tracked_files = get_tracked_files(repo_root_path)

    scanned_tracked_files: list[str] = []
    excluded_tracked_files: list[str] = []
    path_matches: list[dict[str, str]] = []
    for relative_path in tracked_files:
        if test_path_excluded(relative_path, excluded_path_regexes):
            excluded_tracked_files.append(relative_path)
            continue
        scanned_tracked_files.append(relative_path)
        path_result = test_relative_path_against_rules(relative_path, path_rules)
        if path_result is not None:
            path_matches.append({"path": relative_path, **path_result})

    content_matches = find_content_matches(repo_root_path, scanned_tracked_files, content_rules)
    summary = {
        "schemaVersion": "tracked-file-privacy-guard-summary/v1",
        "repoRoot": str(repo_root_path),
        "policyVersion": policy.get("policyVersion"),
        "personalIdentifierRuleCount": len(dynamic_path_rules),
        "scannedTrackedFiles": len(scanned_tracked_files),
        "excludedTrackedFiles": excluded_tracked_files,
        "pathMatches": path_matches,
        "contentMatches": content_matches,
        "passed": not path_matches and not content_matches,
    }

    if summary_path is not None:
        summary_path.parent.mkdir(parents=True, exist_ok=True)
        summary_path.write_text(json.dumps(summary, indent=2), encoding="utf-8")
    if not summary["passed"]:
        raise PrivacyGuardFailure(summary)
    return summary


def get_tracked_files(repo_root: Path) -> list[str]:
    """Returns the Git-tracked paths under one repository."""

    completed = subprocess.run(
        ["git", "-C", str(repo_root), "ls-files", "-z"],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if completed.returncode != 0:
        detail = completed.stderr.decode("utf-8", errors="replace").strip()
        raise RuntimeError(f"git ls-files failed for '{repo_root}'." + (f" {detail}" if detail else ""))
    output = completed.stdout.decode("utf-8", errors="surrogateescape")
    return [entry for entry in output.split("\0") if entry.strip()]


def get_personal_identifiers(repo_root: Path) -> tuple[str, ...]:
    """Builds the dynamic personal-identifier list from environment and local overrides."""

    candidates: list[str] = []
    for variable_name in ("USERPROFILE", "HOME"):
        profile_path = os.environ.get(variable_name)
        if profile_path:
            candidates.append(Path(profile_path).name)
    for variable_name in ("USERNAME", "USER"):
        value = os.environ.get(variable_name)
        if value:
            candidates.append(value)
    for value in re.split(r"[,;]", os.environ.get("TRACKED_FILE_PRIVACY_IDENTIFIERS", "")):
        if value.strip():
            candidates.append(value.strip())

    local_identifier_path = repo_root / ".tracked-file-privacy-identifiers.local.json"
    if local_identifier_path.is_file():
        local_policy = json.loads(local_identifier_path.read_text(encoding="utf-8"))
        for value in local_policy.get("personalIdentifiers", []):
            if str(value).strip():
                candidates.append(str(value).strip())

    seen: set[str] = set()
    normalized: list[str] = []
    for value in candidates:
        identifier = value.strip().lower()
        if not re.match(r"^[a-z0-9][a-z0-9._-]{2,}$", identifier):
            continue
        if identifier in seen:
            continue
        seen.add(identifier)
        normalized.append(identifier)
    return tuple(normalized)


def get_dynamic_path_rules(personal_identifiers: tuple[str, ...]) -> list[GuardRule]:
    """Creates filename rules for environment-derived personal identifiers."""

    return [
        GuardRule(
            id="personal-identifier-filename",
            reason="Tracked filenames must not embed configured or environment-derived personal identifiers.",
            regex=rf"(^|[\\/])[^\\/]*{re.escape(identifier)}[^\\/]*$",
        )
        for identifier in personal_identifiers
        if identifier
    ]


def test_path_excluded(relative_path: str, excluded_path_regexes: list[str]) -> bool:
    """Returns whether a tracked path is excluded from scanning."""

    return any(re.search(regex, relative_path, flags=re.IGNORECASE) for regex in excluded_path_regexes)


def test_relative_path_against_rules(relative_path: str, rules: list[GuardRule]) -> dict[str, str] | None:
    """Returns the first path-rule match for a tracked path, if any."""

    for rule in rules:
        if re.search(rule.regex, relative_path, flags=re.IGNORECASE):
            return {"rule": rule.id, "reason": rule.reason, "regex": rule.regex}
    return None


def find_content_matches(repo_root: Path, relative_paths: list[str], rules: list[GuardRule]) -> list[dict[str, str]]:
    """Scans tracked text files for configured content-rule matches."""

    matches: list[dict[str, str]] = []
    for relative_path in relative_paths:
        file_path = repo_root / relative_path
        if not file_path.is_file():
            continue
        raw = file_path.read_bytes()
        if b"\0" in raw:
            continue
        content = raw.decode("utf-8", errors="replace")
        lines = content.splitlines()
        for rule in rules:
            compiled = re.compile(rule.regex)
            for line_number, line in enumerate(lines, start=1):
                if compiled.search(line):
                    matches.append(
                        {
                            "rule": rule.id,
                            "reason": rule.reason,
                            "path": relative_path,
                            "line": str(line_number),
                            "preview": line,
                        }
                    )
    return matches
