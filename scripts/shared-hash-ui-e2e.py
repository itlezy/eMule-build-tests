"""Real Win32 UI regression for shared-hash shutdown and interruption scenarios."""

from __future__ import annotations

import argparse
import importlib.util
import json
import shutil
import subprocess
import sys
import time
from pathlib import Path

import win32api
import win32con
import win32event
import win32gui
import win32process

try:
    from pywinauto import Application

    _PYWINAUTO_IMPORT_ERROR = None
except ModuleNotFoundError as exc:  # pragma: no cover - environment dependent
    Application = object  # type: ignore[assignment]
    _PYWINAUTO_IMPORT_ERROR = exc


def load_local_module(module_name: str, filename: str):
    """Loads one sibling helper module from a hyphenated script filename."""

    module_path = Path(__file__).resolve().with_name(filename)
    spec = importlib.util.spec_from_file_location(module_name, module_path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Unable to load helper module from '{module_path}'.")
    module = importlib.util.module_from_spec(spec)
    sys.modules[module_name] = module
    spec.loader.exec_module(module)
    return module


live_common = load_local_module("emule_live_profile_common", "emule-live-profile-common.py")
harness_cli_common = load_local_module("harness_cli_common", "harness-cli-common.py")
shared_files_ui = load_local_module("shared_files_ui_e2e", "shared-files-ui-e2e.py")

SCENARIO_NAMES = [
    "clean-close-during-hash-startup",
    "clean-close-during-hash-files-page",
    "hard-kill-during-hash-startup",
    "hard-kill-during-hash-files-page",
]
MAX_CLEAN_SHUTDOWN_DURATION_MS = 20000.0
HASH_STRESS_FILE_SPECS = [
    ("alpha_hash_payload.bin", 96 * 1024 * 1024, 0x61),
    ("beta_hash_payload.bin", 96 * 1024 * 1024, 0x62),
    ("gamma_hash_payload.bin", 96 * 1024 * 1024, 0x63),
]
HASH_STRESS_NESTED_SEGMENTS = [
    "segment-00-shared-hash-startup",
    "segment-01-shared-hash-recursive",
    "segment-02-shared-hash-drain",
    "segment-03-shared-hash-timeout",
    "segment-04-shared-hash-recovery",
]


def write_json(path: Path, payload) -> None:
    """Writes one UTF-8 JSON artifact with stable formatting."""

    path.write_text(json.dumps(payload, indent=2, sort_keys=True), encoding="utf-8")


def write_pattern_file(path: Path, size_bytes: int, fill_byte: int) -> None:
    """Writes one deterministic large file without building the full payload in memory."""

    path.parent.mkdir(parents=True, exist_ok=True)
    chunk = bytes([fill_byte]) * (1024 * 1024)
    remaining = size_bytes
    with path.open("wb") as handle:
        while remaining > 0:
            current = chunk if remaining >= len(chunk) else chunk[:remaining]
            handle.write(current)
            remaining -= len(current)


def prepare_hash_interruption_fixture(seed_config_dir: Path, scenario_dir: Path) -> dict[str, object]:
    """Creates one recursive shared-tree fixture that keeps shared hashing active after startup."""

    shared_root = scenario_dir / "shared-hash-root"
    expected_visible_names: list[str] = []
    for index, (file_name, size_bytes, fill_byte) in enumerate(HASH_STRESS_FILE_SPECS):
        branch_root = shared_root / f"branch-{index:02d}-shared-hash-interruption"
        current = branch_root
        for segment in HASH_STRESS_NESTED_SEGMENTS:
            current = current / f"{segment}-{index:02d}"
        file_path = current / file_name
        write_pattern_file(file_path, size_bytes, fill_byte)
        expected_visible_names.append(file_name)

    fixture = live_common.prepare_profile_base(
        seed_config_dir=seed_config_dir,
        artifacts_dir=scenario_dir,
        shared_dirs=live_common.enumerate_recursive_directories(shared_root),
    )
    fixture.update(
        {
            "shared_root": live_common.win_path(shared_root.resolve(), trailing_slash=True),
            "expected_row_count": len(expected_visible_names),
            "expected_visible_names": sorted(expected_visible_names, key=str.lower),
        }
    )
    return fixture


def archive_trace_if_present(source_path: Path, destination_path: Path) -> None:
    """Copies one startup trace when it exists."""

    if source_path.exists():
        shutil.copy2(source_path, destination_path)


def wait_for_hashing_active(startup_profile_path: Path, *, timeout: float = 90.0) -> dict[str, object]:
    """Waits until startup reached Shared Files readiness with queued hashing still in flight."""

    def resolve() -> dict[str, object] | None:
        if not startup_profile_path.exists():
            return None
        try:
            text = startup_profile_path.read_text(encoding="utf-8", errors="ignore")
            phases = live_common.parse_startup_profile(text)
            counters = live_common.parse_startup_profile_counters(text)
        except Exception:
            return None

        ready_phase = live_common.get_phase_by_id(
            phases,
            live_common.STARTUP_PROFILE_SHARED_FILES_READY_PHASE_ID,
        )
        if ready_phase is None:
            return None

        hashing_done_phase = live_common.get_phase_by_id(
            phases,
            live_common.STARTUP_PROFILE_SHARED_FILES_HASHING_DONE_PHASE_ID,
        )
        if hashing_done_phase is not None:
            raise RuntimeError("Hashing completed before the interruption target was reached.")

        pending_hashes = live_common.get_counter_by_id(counters, "shared.model.pending_hashes")
        pending_hash_count = int(pending_hashes["value"]) if pending_hashes is not None else 0
        if pending_hash_count <= 0:
            return None

        return {
            "shared_files_ready_absolute_ms": float(ready_phase["absolute_ms"]),
            "pending_hashes_at_readiness": pending_hash_count,
        }

    return live_common.wait_for(resolve, timeout=timeout, interval=0.25, description="shared hashing active after readiness")


def wait_for_expected_shared_files(main_hwnd: int, process_id: int, expected_names: list[str]) -> dict[str, object]:
    """Opens the Shared Files page and waits until the expected visible rows converge."""

    list_hwnd, _static_hwnd = shared_files_ui.open_shared_files_page(main_hwnd)
    row_count = shared_files_ui.wait_for_exact_list_count(list_hwnd, len(expected_names))
    process_handle = shared_files_ui.open_process(process_id)
    try:
        visible_names = shared_files_ui.get_list_names(process_handle, list_hwnd, row_count)
    finally:
        shared_files_ui.close_process(process_handle)

    if sorted(visible_names, key=str.lower) != expected_names:
        raise RuntimeError(f"Unexpected Shared Files rows: {visible_names!r}")
    return {
        "row_count": row_count,
        "visible_names": visible_names,
    }


def open_shared_files_page_snapshot(main_hwnd: int) -> dict[str, object]:
    """Opens the Shared Files page and records the immediate visible row count."""

    list_hwnd, _static_hwnd = shared_files_ui.open_shared_files_page(main_hwnd)
    time.sleep(0.2)
    return {
        "row_count": int(win32gui.SendMessage(list_hwnd, shared_files_ui.LVM_GETITEMCOUNT, 0, 0)),
    }


def wait_for_process_exit(process_id: int, *, timeout: float = 15.0) -> None:
    """Waits until the target process exits."""

    try:
        process_handle = win32api.OpenProcess(win32con.SYNCHRONIZE, False, int(process_id))
    except Exception:
        return
    try:
        wait_result = win32event.WaitForSingleObject(process_handle, int(timeout * 1000))
        if wait_result != win32event.WAIT_OBJECT_0:
            raise RuntimeError(f"Timed out waiting for process {process_id} to exit.")
    finally:
        win32api.CloseHandle(process_handle)


def hard_kill_app(app: Application) -> int:
    """Forcefully terminates the running app process."""

    process_id = int(getattr(app, "process", 0) or 0)
    if process_id <= 0:
        raise RuntimeError("Unable to resolve the running process id for hard-kill.")
    completed = subprocess.run(
        ["taskkill", "/F", "/PID", str(process_id), "/T"],
        capture_output=True,
        text=True,
        check=False,
    )
    if completed.returncode != 0:
        raise RuntimeError(
            "taskkill failed for process "
            f"{process_id}: stdout={completed.stdout!r} stderr={completed.stderr!r}"
        )
    wait_for_process_exit(process_id, timeout=15.0)
    return process_id


def validate_clean_shutdown_duration(duration_ms: float) -> None:
    """Fails when the bounded shutdown regression reappears."""

    if duration_ms > MAX_CLEAN_SHUTDOWN_DURATION_MS:
        raise RuntimeError(
            f"Clean shutdown took {duration_ms:.3f} ms, expected <= {MAX_CLEAN_SHUTDOWN_DURATION_MS:.3f} ms."
        )


def run_interruption_scenario(
    app_exe: Path,
    seed_config_dir: Path,
    scenario_dir: Path,
    *,
    name: str,
    open_shared_files_before_interrupt: bool,
    interrupt_mode: str,
    require_startup_profile: bool,
) -> dict[str, object]:
    """Runs one shutdown or hard-kill interruption scenario plus a recovery relaunch."""

    fixture = prepare_hash_interruption_fixture(seed_config_dir, scenario_dir)
    summary = {
        "name": name,
        "status": "failed",
        "app_exe": str(app_exe),
        "profile_base": str(fixture["profile_base"]),
        "shared_root": fixture["shared_root"],
        "expected_row_count": fixture["expected_row_count"],
        "expected_visible_names": fixture["expected_visible_names"],
        "interrupt_mode": interrupt_mode,
        "open_shared_files_before_interrupt": open_shared_files_before_interrupt,
        "command_line": subprocess.list2cmdline(
            [str(app_exe), "-ignoreinstances", "-c", str(fixture["profile_base"])]
        ),
    }

    app = None
    try:
        app = live_common.launch_app(app_exe, Path(str(fixture["profile_base"])))
        main_window = live_common.wait_for_main_window(app)
        main_hwnd = main_window.handle
        process_id = int(win32process.GetWindowThreadProcessId(main_hwnd)[1])
        summary["first_launch_process_id"] = process_id

        if open_shared_files_before_interrupt:
            summary["first_launch_visible_before_interrupt"] = open_shared_files_page_snapshot(main_hwnd)

        hashing_active = wait_for_hashing_active(Path(str(fixture["startup_profile_path"])))
        summary["first_launch_hashing_active"] = hashing_active

        if interrupt_mode == "clean-close":
            close_started = time.perf_counter()
            live_common.close_app_cleanly(app, window_timeout=30.0, process_timeout=30.0)
            close_duration_ms = round((time.perf_counter() - close_started) * 1000.0, 3)
            validate_clean_shutdown_duration(close_duration_ms)
            summary["first_launch_close_duration_ms"] = close_duration_ms
            app = None
            archive_trace_if_present(
                Path(str(fixture["startup_profile_path"])),
                scenario_dir / "startup-profile-first-launch.trace.json",
            )
            startup_summary, startup_phases, _startup_counters = shared_files_ui.collect_startup_profile_bundle(
                Path(str(fixture["startup_profile_path"])),
                require_startup_profile=require_startup_profile,
            )
            if startup_phases:
                live_common.enforce_deferred_shared_hashing_boundary(startup_phases, name + ".first_launch")
            summary["first_launch_startup_profile"] = startup_summary
        else:
            summary["first_launch_hard_kill_process_id"] = hard_kill_app(app)
            app = None
            archive_trace_if_present(
                Path(str(fixture["startup_profile_path"])),
                scenario_dir / "startup-profile-first-launch.partial.trace.json",
            )

        app = live_common.launch_app(app_exe, Path(str(fixture["profile_base"])))
        main_window = live_common.wait_for_main_window(app)
        main_hwnd = main_window.handle
        process_id = int(win32process.GetWindowThreadProcessId(main_hwnd)[1])
        summary["relaunch_process_id"] = process_id
        summary["relaunch_visible_shared_files"] = wait_for_expected_shared_files(
            main_hwnd,
            process_id,
            list(fixture["expected_visible_names"]),
        )

        close_started = time.perf_counter()
        live_common.close_app_cleanly(app, window_timeout=45.0, process_timeout=45.0)
        relaunch_close_duration_ms = round((time.perf_counter() - close_started) * 1000.0, 3)
        validate_clean_shutdown_duration(relaunch_close_duration_ms)
        summary["relaunch_close_duration_ms"] = relaunch_close_duration_ms
        app = None

        archive_trace_if_present(
            Path(str(fixture["startup_profile_path"])),
            scenario_dir / "startup-profile-relaunch.trace.json",
        )
        relaunch_summary, relaunch_phases, _relaunch_counters = shared_files_ui.collect_startup_profile_bundle(
            Path(str(fixture["startup_profile_path"])),
            require_startup_profile=require_startup_profile,
        )
        if relaunch_phases:
            live_common.enforce_deferred_shared_hashing_boundary(relaunch_phases, name + ".relaunch")
        summary["relaunch_startup_profile"] = relaunch_summary
        summary["status"] = "passed"
        summary["error"] = None
        return summary
    except Exception as exc:
        summary["error"] = str(exc)
        try:
            if app is not None:
                main_window = app.top_window()
                live_common.dump_window_tree(main_window.handle, scenario_dir / "window-tree-failure.json")
                try:
                    image = main_window.capture_as_image()
                    image.save(scenario_dir / "failure.png")
                except Exception:
                    pass
        except Exception:
            pass
        return summary
    finally:
        if app is not None:
            try:
                live_common.close_app_cleanly(app)
            except Exception:
                try:
                    hard_kill_app(app)
                except Exception:
                    pass
        write_json(scenario_dir / "result.json", summary)


def run_shared_hash_ui_suite(
    app_exe: Path,
    seed_config_dir: Path,
    artifacts_dir: Path,
    *,
    scenario_names: list[str],
    require_startup_profile: bool,
) -> None:
    """Runs the requested shared-hash interruption scenarios."""

    combined = {
        "generated_at": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "status": "passed",
        "app_exe": str(app_exe),
        "seed_config_dir": str(seed_config_dir),
        "artifact_dir": str(artifacts_dir),
        "scenario_names": scenario_names,
        "scenarios": [],
    }
    failures: list[str] = []

    for name in scenario_names:
        scenario_dir = artifacts_dir / name
        scenario_dir.mkdir(parents=True, exist_ok=True)
        open_shared_files_before_interrupt = name.endswith("files-page")
        interrupt_mode = "hard-kill" if name.startswith("hard-kill") else "clean-close"
        result = run_interruption_scenario(
            app_exe,
            seed_config_dir,
            scenario_dir,
            name=name,
            open_shared_files_before_interrupt=open_shared_files_before_interrupt,
            interrupt_mode=interrupt_mode,
            require_startup_profile=require_startup_profile,
        )
        combined["scenarios"].append(result)
        if result.get("status") != "passed":
            failures.append(name)

    if failures:
        combined["status"] = "failed"
    write_json(artifacts_dir / "result.json", combined)
    if failures:
        raise RuntimeError("Shared hash UI scenarios failed: " + ", ".join(failures))


def main(argv: list[str]) -> int:
    """Parses arguments, runs the interruption suite, and publishes the report artifacts."""

    parser = argparse.ArgumentParser()
    parser.add_argument("--workspace-root")
    parser.add_argument("--app-root")
    parser.add_argument("--app-exe")
    parser.add_argument("--seed-config-dir")
    parser.add_argument("--artifacts-dir")
    parser.add_argument("--keep-artifacts", action="store_true")
    parser.add_argument("--configuration", choices=["Debug", "Release"], default="Release")
    parser.add_argument("--startup-profile-mode", choices=["required", "optional"], default="required")
    parser.add_argument("--scenario", dest="scenarios", action="append", choices=SCENARIO_NAMES)
    args = parser.parse_args(argv)

    if _PYWINAUTO_IMPORT_ERROR is not None:
        live_common.require_pywinauto()

    paths = harness_cli_common.prepare_run_paths(
        script_file=__file__,
        suite_name="shared-hash-ui-e2e",
        configuration=args.configuration,
        workspace_root=args.workspace_root,
        app_root=args.app_root,
        app_exe=args.app_exe,
        artifacts_dir=args.artifacts_dir,
        keep_artifacts=args.keep_artifacts,
    )
    artifacts_dir = paths.source_artifacts_dir
    seed_config_dir = Path(args.seed_config_dir).resolve() if args.seed_config_dir else paths.seed_config_dir
    scenario_names = args.scenarios or list(SCENARIO_NAMES)

    try:
        run_shared_hash_ui_suite(
            app_exe=paths.app_exe,
            seed_config_dir=seed_config_dir,
            artifacts_dir=artifacts_dir,
            scenario_names=scenario_names,
            require_startup_profile=(args.startup_profile_mode == "required"),
        )
        harness_cli_common.publish_run_artifacts(paths)
        summary_payload = harness_cli_common.build_live_ui_summary(status="passed", paths=paths)
        summary_path = paths.run_report_dir / "ui-summary.json"
        harness_cli_common.write_json_file(summary_path, summary_payload)
        harness_cli_common.publish_latest_report(paths)
        harness_cli_common.update_harness_summary(paths.repo_root, live_ui_summary_path=summary_path)
        harness_cli_common.cleanup_source_artifacts(paths)
        return 0
    except Exception as exc:
        harness_cli_common.publish_run_artifacts(paths)
        summary_payload = harness_cli_common.build_live_ui_summary(status="failed", paths=paths, error_message=str(exc))
        summary_path = paths.run_report_dir / "ui-summary.json"
        harness_cli_common.write_json_file(summary_path, summary_payload)
        harness_cli_common.publish_latest_report(paths)
        harness_cli_common.update_harness_summary(paths.repo_root, live_ui_summary_path=summary_path)
        if not paths.keep_source_artifacts:
            print(f"Shared-hash UI suite failed; preserving artifacts at {paths.source_artifacts_dir}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
