"""Builds startup-profile artifacts for deterministic live-profile scenarios."""

from __future__ import annotations

import argparse
import subprocess
import sys
import time
from pathlib import Path

import win32con
import win32gui
import win32process

import emule_live_profile_common as live_common

HIGHLIGHTED_PHASES = [
    "Construct CSharedFileList (share cache/scan)",
    "CSharedFilesWnd::OnInitDialog total",
    "BuildSharedDirectoryTree done",
    "StartupTimer complete",
]


def create_fixture_shared_dirs(artifacts_dir: Path) -> tuple[list[str], dict[str, object]]:
    """Creates the small deterministic shared-files fixture used by the UI regression."""

    shared_a_dir = artifacts_dir / "shared-a"
    shared_b_dir = artifacts_dir / "shared-b"
    shared_a_dir.mkdir(parents=True, exist_ok=True)
    shared_b_dir.mkdir(parents=True, exist_ok=True)

    files = [
        (shared_a_dir / "middle_small.txt", b"small\n"),
        (shared_a_dir / "zeta_large.bin", b"z" * 4096),
        (shared_b_dir / "alpha_medium.txt", b"a" * 600),
    ]
    for file_path, content in files:
        file_path.write_bytes(content)

    shared_dirs = [
        live_common.win_path(shared_a_dir, trailing_slash=True),
        live_common.win_path(shared_b_dir, trailing_slash=True),
    ]
    return shared_dirs, {
        "directory_count_including_root": 2,
        "file_count": len(files),
        "shared_directory_count": len(shared_dirs),
    }


def build_scenario_definition(name: str, artifacts_dir: Path, shared_root: Path) -> dict[str, object]:
    """Resolves one named startup-profile scenario into concrete shared roots and metadata."""

    resolved_root = shared_root.resolve()
    if name == "baseline-no-shares":
        return {
            "name": name,
            "description": "Seeded profile with no shared directories.",
            "shared_dirs": [],
            "tree_summary": {
                "directory_count_including_root": 0,
                "file_count": 0,
                "shared_directory_count": 0,
            },
        }
    if name == "fixture-three-files":
        shared_dirs, tree_summary = create_fixture_shared_dirs(artifacts_dir)
        return {
            "name": name,
            "description": "Two deterministic shared roots with three visible fixture files.",
            "shared_dirs": shared_dirs,
            "tree_summary": tree_summary,
        }
    if name == "long-paths-root-only":
        tree_summary = live_common.summarize_existing_tree(resolved_root)
        tree_summary["shared_directory_count"] = 1
        return {
            "name": name,
            "description": "Shares only the long-path root without expanding child directories into shareddir.dat.",
            "shared_dirs": [live_common.win_path(resolved_root, trailing_slash=True)],
            "tree_summary": tree_summary,
        }
    if name == "long-paths-recursive":
        shared_dirs = live_common.enumerate_recursive_directories(resolved_root)
        tree_summary = live_common.summarize_existing_tree(resolved_root)
        tree_summary["shared_directory_count"] = len(shared_dirs)
        return {
            "name": name,
            "description": "Expands the long-path root recursively into shareddir.dat before launch.",
            "shared_dirs": shared_dirs,
            "tree_summary": tree_summary,
        }
    raise RuntimeError(f"Unknown startup-profile scenario: {name}")


def run_scenario(app_exe: Path, seed_config_dir: Path, scenario_dir: Path, shared_root: Path, name: str) -> dict[str, object]:
    """Executes one startup-profile scenario and returns its machine-readable result."""

    scenario = build_scenario_definition(name=name, artifacts_dir=scenario_dir, shared_root=shared_root)
    fixture = live_common.prepare_profile_base(
        seed_config_dir=seed_config_dir,
        artifacts_dir=scenario_dir,
        shared_dirs=list(scenario["shared_dirs"]),
    )

    summary = {
        "name": scenario["name"],
        "description": scenario["description"],
        "status": "failed",
        "artifact_dir": str(scenario_dir),
        "profile_base": str(fixture["profile_base"]),
        "startup_profile_path": str(fixture["startup_profile_path"]),
        "command_line": subprocess.list2cmdline(
            [str(app_exe), "-ignoreinstances", "-c", str(fixture["profile_base"])]
        ),
        "shared_directory_count": len(list(scenario["shared_dirs"])),
        "shared_directories_preview": list(scenario["shared_dirs"])[:3],
        "shared_directories_tail": list(scenario["shared_dirs"])[-3:],
        "tree_summary": scenario["tree_summary"],
    }

    app = None
    try:
        app = live_common.launch_app(app_exe, fixture["profile_base"])
        main_window = live_common.wait_for_main_window(app)
        main_hwnd = main_window.handle
        main_window.set_focus()

        summary["process_id"] = win32process.GetWindowThreadProcessId(main_hwnd)[1]
        summary["main_window_rect"] = list(win32gui.GetWindowRect(main_hwnd))
        summary["main_window_show_cmd"] = live_common.get_window_show_cmd(main_hwnd)
        summary["main_window_is_maximized"] = summary["main_window_show_cmd"] == win32con.SW_SHOWMAXIMIZED
        if not summary["main_window_is_maximized"]:
            raise RuntimeError(f"Expected scenario '{name}' to start maximized, got showCmd={summary['main_window_show_cmd']}.")

        startup_profile_text = live_common.wait_for_startup_profile_complete(fixture["startup_profile_path"])
        startup_profile_phases = live_common.parse_startup_profile(startup_profile_text)
        summary["startup_profile_phase_count"] = len(startup_profile_phases)
        summary["startup_profile_highlights"] = live_common.summarize_startup_profile(
            startup_profile_phases,
            HIGHLIGHTED_PHASES,
        )
        summary["status"] = "passed"
        summary["error"] = None
        live_common.write_json(scenario_dir / "result.json", summary)
        return summary
    except Exception as exc:
        summary["error"] = str(exc)
        if app is not None:
            try:
                main_window = app.top_window()
                live_common.dump_window_tree(main_window.handle, scenario_dir / "window-tree-failure.json")
                try:
                    image = main_window.capture_as_image()
                    image.save(scenario_dir / "failure.png")
                except Exception:
                    pass
            except Exception:
                pass
        live_common.write_json(scenario_dir / "result.json", summary)
        return summary
    finally:
        if app is not None:
            try:
                live_common.close_app_cleanly(app)
            except Exception:
                try:
                    app.kill()
                except Exception:
                    pass


def main(argv: list[str]) -> int:
    """Parses arguments, runs the requested scenarios, and writes a combined summary."""

    parser = argparse.ArgumentParser()
    parser.add_argument("--app-exe", required=True)
    parser.add_argument("--seed-config-dir", required=True)
    parser.add_argument("--artifacts-dir", required=True)
    parser.add_argument("--shared-root", default=r"C:\tmp\00_long_paths")
    parser.add_argument(
        "--scenario",
        dest="scenarios",
        action="append",
        choices=[
            "baseline-no-shares",
            "fixture-three-files",
            "long-paths-root-only",
            "long-paths-recursive",
        ],
    )
    args = parser.parse_args(argv)

    artifacts_dir = Path(args.artifacts_dir).resolve()
    artifacts_dir.mkdir(parents=True, exist_ok=True)

    scenario_names = args.scenarios or [
        "baseline-no-shares",
        "fixture-three-files",
        "long-paths-root-only",
        "long-paths-recursive",
    ]
    shared_root = Path(args.shared_root).resolve()
    if any(name.startswith("long-paths-") for name in scenario_names) and not shared_root.exists():
        raise RuntimeError(f"Shared root was not found at '{shared_root}'.")

    combined = {
        "generated_at": None,
        "status": "passed",
        "app_exe": str(Path(args.app_exe).resolve()),
        "seed_config_dir": str(Path(args.seed_config_dir).resolve()),
        "artifact_dir": str(artifacts_dir),
        "shared_root": live_common.win_path(shared_root, trailing_slash=True),
        "scenarios": [],
    }

    failures = []
    for name in scenario_names:
        scenario_dir = artifacts_dir / name
        scenario_dir.mkdir(parents=True, exist_ok=True)
        result = run_scenario(
            app_exe=Path(args.app_exe).resolve(),
            seed_config_dir=Path(args.seed_config_dir).resolve(),
            scenario_dir=scenario_dir,
            shared_root=shared_root,
            name=name,
        )
        combined["scenarios"].append(result)
        if result["status"] != "passed":
            failures.append(name)

    combined["generated_at"] = time.strftime("%Y-%m-%dT%H:%M:%S")
    if failures:
        combined["status"] = "failed"

    live_common.write_json(artifacts_dir / "startup-profiles-summary.json", combined)
    if failures:
        raise RuntimeError("Startup-profile scenarios failed: " + ", ".join(failures))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
