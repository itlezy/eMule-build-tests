"""Path helpers shared by Python harness runners."""

from __future__ import annotations

import re
from pathlib import Path


def make_file_token(value: str) -> str:
    """Converts a free-form string into the file token used by test reports."""

    token = re.sub(r'[\\/:*?"<>|\s]+', "-", value)
    token = re.sub(r"[^A-Za-z0-9._-]+", "-", token).strip("-")
    return token or "build"


def get_build_tag(workspace_root: Path, app_root: Path | None = None) -> str:
    """Returns the native-test build tag used by build and report wrappers."""

    resolved_workspace_root = workspace_root.resolve()
    workspace_leaf = resolved_workspace_root.name
    workspaces_root = resolved_workspace_root.parent
    workspace_owner = workspaces_root.parent.name if workspaces_root.parent else ""
    if not workspace_leaf or not workspace_owner:
        raise RuntimeError(f"Unable to derive build tag from workspace path: {workspace_root}")

    segments = [workspace_owner, workspace_leaf]
    if app_root is not None:
        segments.append(app_root.resolve().name)
    return re.sub(r"[^A-Za-z0-9._-]", "_", "-".join(segments))


def get_test_binary_path(
    test_repo_root: Path,
    *,
    build_tag: str,
    platform: str,
    configuration: str,
) -> Path:
    """Returns the expected emule-tests.exe path for one build tag."""

    return test_repo_root.resolve() / "build" / build_tag / platform / configuration / "emule-tests.exe"
