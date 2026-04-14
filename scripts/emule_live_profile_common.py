"""Shared helpers for deterministic live-profile eMule harness runs."""

from __future__ import annotations

import json
import os
import re
import shutil
import struct
import subprocess
import time
from pathlib import Path

import win32con
import win32gui
from pywinauto import Application

PREFERENCES_DAT_VERSION = 0x14
WINDOW_PLACEMENT_LENGTH = 44
WINDOW_SHOW_MAXIMIZED = 3
DEFAULT_WINDOW_RECT = (10, 10, 700, 500)

REQUIRED_SEED_KEYS = (
    "AppVersion",
    "Nick",
    "Port",
    "UDPPort",
    "ServerUDPPort",
    "Language",
    "StartupMinimized",
    "BringToFront",
    "ConfirmExit",
    "RestoreLastMainWndDlg",
    "Splashscreen",
    "Autoconnect",
    "Reconnect",
    "NetworkED2K",
    "NetworkKademlia",
    "ShowSharedFilesDetails",
    "IgnoreInstances",
)

STARTUP_PROFILE_LINE_PATTERN = re.compile(r"^(?P<absolute_ms>\d+) ms \| (?P<duration_ms>\d+) ms \| (?P<name>.+)$")


def write_json(path: Path, payload) -> None:
    """Writes a UTF-8 JSON artifact with stable formatting."""

    path.write_text(json.dumps(payload, indent=2, sort_keys=True), encoding="utf-8")


def patch_ini_value(text: str, key: str, value: str) -> str:
    """Upserts one simple INI key in the eMule seed profile."""

    pattern = re.compile(rf"(?im)^(?P<key>{re.escape(key)})=.*$")
    replacement = f"{key}={value}"
    if pattern.search(text):
        return pattern.sub(replacement, text)
    suffix = "" if text.endswith("\n") else "\r\n"
    return f"{text}{suffix}{replacement}\r\n"


def parse_ini_values(text: str) -> dict[str, str]:
    """Parses one simple INI text blob into key/value pairs for seed validation."""

    values: dict[str, str] = {}
    for raw_line in text.splitlines():
        line = raw_line.strip()
        if not line or line.startswith("[") or line.startswith(";"):
            continue
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        values[key.strip()] = value.strip()
    return values


def ensure_seed_profile_initialized(text: str) -> None:
    """Fails fast when the checked-in test seed stops being a fully initialized profile."""

    values = parse_ini_values(text)
    missing_keys = [key for key in REQUIRED_SEED_KEYS if not values.get(key, "").strip()]
    if missing_keys:
        raise RuntimeError(
            "Seed preferences.ini is missing required initialized keys: "
            + ", ".join(missing_keys)
        )


def win_path(path: Path, trailing_slash: bool = False) -> str:
    """Formats a path as an absolute Windows string, optionally with a trailing separator."""

    resolved = str(path.resolve())
    return resolved + ("\\" if trailing_slash and not resolved.endswith("\\") else "")


def write_preferences_dat(
    path: Path,
    show_cmd: int = WINDOW_SHOW_MAXIMIZED,
    normal_rect: tuple[int, int, int, int] = DEFAULT_WINDOW_RECT,
) -> None:
    """Writes a deterministic preferences.dat carrying the requested main-window placement."""

    data = struct.pack(
        "<B16sIIIiiiiiiii",
        PREFERENCES_DAT_VERSION,
        b"\0" * 16,
        WINDOW_PLACEMENT_LENGTH,
        0,
        show_cmd,
        0,
        0,
        0,
        0,
        normal_rect[0],
        normal_rect[1],
        normal_rect[2],
        normal_rect[3],
    )
    path.write_bytes(data)


def write_shared_directories_file(path: Path, shared_dirs: list[str]) -> None:
    """Writes the eMule shared-directory list as UTF-16 text."""

    contents = "".join(f"{entry}\r\n" for entry in shared_dirs)
    path.write_text(contents, encoding="utf-16")


def prepare_profile_base(
    seed_config_dir: Path,
    artifacts_dir: Path,
    shared_dirs: list[str],
    incoming_dir: Path | None = None,
    temp_dir: Path | None = None,
) -> dict[str, object]:
    """Copies the seed profile and patches per-run mutable paths into an isolated base."""

    profile_base = artifacts_dir / "profile-base"
    config_dir = profile_base / "config"
    log_dir = profile_base / "logs"
    incoming_dir = incoming_dir or (artifacts_dir / "incoming")
    temp_dir = temp_dir or (artifacts_dir / "temp")

    shutil.copytree(seed_config_dir, config_dir)
    log_dir.mkdir(parents=True, exist_ok=True)
    incoming_dir.mkdir(parents=True, exist_ok=True)
    temp_dir.mkdir(parents=True, exist_ok=True)

    preferences_path = config_dir / "preferences.ini"
    preferences_text = preferences_path.read_text(encoding="utf-8", errors="ignore")
    ensure_seed_profile_initialized(preferences_text)
    for key, value in (
        ("IncomingDir", win_path(incoming_dir, trailing_slash=True)),
        ("TempDir", win_path(temp_dir, trailing_slash=True)),
        ("TempDirs", win_path(temp_dir, trailing_slash=True)),
    ):
        preferences_text = patch_ini_value(preferences_text, key, value)
    preferences_path.write_text(preferences_text, encoding="utf-8", newline="\r\n")

    write_preferences_dat(config_dir / "preferences.dat")
    write_shared_directories_file(config_dir / "shareddir.dat", shared_dirs)

    return {
        "profile_base": profile_base,
        "config_dir": config_dir,
        "log_dir": log_dir,
        "incoming_dir": incoming_dir,
        "temp_dir": temp_dir,
        "startup_profile_path": config_dir / "startup-profile.txt",
    }


def enumerate_recursive_directories(root: Path) -> list[str]:
    """Returns one deterministic shared-directory list for a recursively shared root."""

    resolved_root = root.resolve()
    directories: list[str] = []
    for current_root, dir_names, _ in os.walk(resolved_root):
        dir_names.sort(key=str.lower)
        directories.append(win_path(Path(current_root), trailing_slash=True))
    return directories


def summarize_existing_tree(root: Path) -> dict[str, object]:
    """Summarizes an existing filesystem tree for startup-profile reporting."""

    resolved_root = root.resolve()
    directory_count = 0
    file_count = 0
    for _, dir_names, file_names in os.walk(resolved_root):
        directory_count += len(dir_names)
        file_count += len(file_names)
    return {
        "root": win_path(resolved_root, trailing_slash=True),
        "directory_count_including_root": directory_count + 1,
        "file_count": file_count,
    }


def wait_for(predicate, timeout: float, interval: float, description: str):
    """Polls until the predicate returns a truthy value or raises on timeout."""

    deadline = time.time() + timeout
    last_value = None
    while time.time() < deadline:
        try:
            last_value = predicate()
        except Exception as exc:
            last_value = f"{type(exc).__name__}: {exc}"
            time.sleep(interval)
            continue
        if last_value:
            return last_value
        time.sleep(interval)
    raise RuntimeError(f"Timed out waiting for {description}. Last value: {last_value!r}")


def launch_app(app_exe: Path, profile_base: Path) -> Application:
    """Starts the real app with the isolated `-c` override and startup profiling enabled."""

    os.environ["EMULE_STARTUP_PROFILE"] = "1"
    command_line = subprocess.list2cmdline(
        [str(app_exe), "-ignoreinstances", "-c", str(profile_base)]
    )
    return Application(backend="win32").start(command_line, wait_for_idle=False)


def is_main_emule_window(hwnd: int) -> bool:
    """Reports whether one visible top-level window is the real main eMule dialog."""

    return win32gui.GetClassName(hwnd) == "#32770" and win32gui.GetWindowText(hwnd).startswith("eMule v")


def describe_startup_dialog(hwnd: int) -> str:
    """Collects one top-level modal dialog description for failure reporting."""

    dialog_texts = []
    child = win32gui.GetWindow(hwnd, win32con.GW_CHILD)
    while child:
        if win32gui.GetClassName(child) == "Static":
            dialog_texts.append(win32gui.GetWindowText(child))
        child = win32gui.GetWindow(child, win32con.GW_HWNDNEXT)
    return "\n".join(filter(None, dialog_texts)).strip()


def wait_for_main_window(app: Application):
    """Waits until the started eMule process exposes a visible top-level window."""

    def resolve():
        try:
            window = app.top_window()
        except Exception:
            return None
        if not window.handle or not win32gui.IsWindowVisible(window.handle):
            return None
        if is_main_emule_window(window.handle):
            return window
        if win32gui.GetClassName(window.handle) == "#32770":
            raise RuntimeError(
                "Unexpected startup dialog "
                f"{win32gui.GetWindowText(window.handle)!r}: "
                f"{describe_startup_dialog(window.handle)!r}"
            )
        return window

    return wait_for(resolve, timeout=90.0, interval=0.5, description="eMule main window")


def get_window_show_cmd(hwnd: int) -> int:
    """Returns the current Win32 show command for one top-level window."""

    return int(win32gui.GetWindowPlacement(hwnd)[1])


def dump_window_tree(main_hwnd: int, output_path: Path) -> None:
    """Writes a recursive Win32 control dump for failure diagnosis."""

    nodes = []

    def walk(hwnd: int, depth: int) -> None:
        class_name = win32gui.GetClassName(hwnd)
        text = win32gui.GetWindowText(hwnd)
        rect = win32gui.GetWindowRect(hwnd)
        try:
            control_id = win32gui.GetDlgCtrlID(hwnd)
        except win32gui.error:
            control_id = None
        nodes.append(
            {
                "depth": depth,
                "hwnd": hwnd,
                "class_name": class_name,
                "text": text,
                "control_id": control_id,
                "rect": rect,
            }
        )
        child = win32gui.GetWindow(hwnd, win32con.GW_CHILD)
        while child:
            walk(child, depth + 1)
            child = win32gui.GetWindow(child, win32con.GW_HWNDNEXT)

    walk(main_hwnd, 0)
    write_json(output_path, nodes)


def close_app_cleanly(app: Application) -> None:
    """Closes the app and fails if an exit-confirmation modal blocks shutdown."""

    main_window = app.top_window()
    main_window.close()

    def resolve() -> bool:
        try:
            window = app.top_window()
        except Exception:
            return True
        if not window.handle:
            return True
        if is_main_emule_window(window.handle):
            return False
        if win32gui.GetClassName(window.handle) == "#32770":
            raise RuntimeError(f"Unexpected shutdown dialog: {describe_startup_dialog(window.handle)!r}")
        return False

    wait_for(resolve, timeout=10.0, interval=0.2, description="clean app shutdown")


def wait_for_startup_profile_complete(startup_profile_path: Path) -> str:
    """Waits until the startup-profile text has reached its completion marker."""

    def resolve():
        if not startup_profile_path.exists():
            return None
        text = startup_profile_path.read_text(encoding="utf-8", errors="ignore")
        return text if "StartupTimer complete" in text else None

    return wait_for(resolve, timeout=120.0, interval=0.5, description="startup profile completion")


def parse_startup_profile(text: str) -> list[dict[str, object]]:
    """Parses one startup-profile.txt payload into structured phase rows."""

    phases: list[dict[str, object]] = []
    for raw_line in text.splitlines():
        match = STARTUP_PROFILE_LINE_PATTERN.match(raw_line.strip())
        if not match:
            continue
        phases.append(
            {
                "name": match.group("name"),
                "absolute_ms": int(match.group("absolute_ms")),
                "duration_ms": int(match.group("duration_ms")),
            }
        )
    return phases


def summarize_startup_profile(phases: list[dict[str, object]], interesting_names: list[str]) -> dict[str, object]:
    """Extracts highlighted timings for selected phase names from parsed startup-profile rows."""

    by_name = {str(phase["name"]): phase for phase in phases}
    highlights = {}
    for name in interesting_names:
        phase = by_name.get(name)
        if phase is None:
            continue
        highlights[name] = {
            "absolute_ms": int(phase["absolute_ms"]),
            "duration_ms": int(phase["duration_ms"]),
        }
    return highlights
