"""Real Win32 UI regression for the Shared Files owner-data list."""

from __future__ import annotations

import argparse
import ctypes
import json
import os
import re
import shutil
import sys
import time
from pathlib import Path

import win32con
import win32gui
import win32process
from pywinauto import Application, findwindows, mouse

WM_COMMAND = 0x0111
BM_CLICK = 0x00F5
MP_HM_FILES = 10213
IDC_RELOADSHAREDFILES = 2049
IDC_SFLIST = 2167
IDC_SF_FNAME = 3038

LVM_FIRST = 0x1000
LVM_GETITEMCOUNT = LVM_FIRST + 4
LVM_GETITEMRECT = LVM_FIRST + 14
LVM_ENSUREVISIBLE = LVM_FIRST + 19
LVM_GETHEADER = LVM_FIRST + 31
LVM_SETITEMSTATE = LVM_FIRST + 43
LVM_SETSELECTIONMARK = LVM_FIRST + 67
LVM_GETITEMTEXTW = LVM_FIRST + 115

HDM_FIRST = 0x1200
HDM_GETITEMRECT = HDM_FIRST + 7

LVIF_TEXT = 0x0001
LVIR_BOUNDS = 0
LVIS_FOCUSED = 0x0001
LVIS_SELECTED = 0x0002

PROCESS_VM_OPERATION = 0x0008
PROCESS_VM_READ = 0x0010
PROCESS_VM_WRITE = 0x0020
PROCESS_QUERY_INFORMATION = 0x0400
MEM_COMMIT = 0x1000
MEM_RESERVE = 0x2000
MEM_RELEASE = 0x8000
PAGE_READWRITE = 0x04

kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
kernel32.OpenProcess.argtypes = [ctypes.c_uint32, ctypes.c_int, ctypes.c_uint32]
kernel32.OpenProcess.restype = ctypes.c_void_p
kernel32.VirtualAllocEx.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint32, ctypes.c_uint32]
kernel32.VirtualAllocEx.restype = ctypes.c_void_p
kernel32.VirtualFreeEx.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint32]
kernel32.VirtualFreeEx.restype = ctypes.c_int
kernel32.WriteProcessMemory.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
kernel32.WriteProcessMemory.restype = ctypes.c_int
kernel32.ReadProcessMemory.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
kernel32.ReadProcessMemory.restype = ctypes.c_int
kernel32.CloseHandle.argtypes = [ctypes.c_void_p]
kernel32.CloseHandle.restype = ctypes.c_int


class LVITEMW(ctypes.Structure):
    """Mirror of the Win32 LVITEMW structure for remote list-view text retrieval."""

    _fields_ = [
        ("mask", ctypes.c_uint),
        ("iItem", ctypes.c_int),
        ("iSubItem", ctypes.c_int),
        ("state", ctypes.c_uint),
        ("stateMask", ctypes.c_uint),
        ("pszText", ctypes.c_uint64 if ctypes.sizeof(ctypes.c_void_p) == 8 else ctypes.c_uint32),
        ("cchTextMax", ctypes.c_int),
        ("iImage", ctypes.c_int),
        ("lParam", ctypes.c_longlong if ctypes.sizeof(ctypes.c_void_p) == 8 else ctypes.c_long),
        ("iIndent", ctypes.c_int),
        ("iGroupId", ctypes.c_int),
        ("cColumns", ctypes.c_uint),
        ("puColumns", ctypes.c_uint64 if ctypes.sizeof(ctypes.c_void_p) == 8 else ctypes.c_uint32),
        ("piColFmt", ctypes.c_uint64 if ctypes.sizeof(ctypes.c_void_p) == 8 else ctypes.c_uint32),
        ("iGroup", ctypes.c_int),
    ]


class RECT(ctypes.Structure):
    """Mirror of the Win32 RECT structure used for cross-process item geometry queries."""

    _fields_ = [
        ("left", ctypes.c_long),
        ("top", ctypes.c_long),
        ("right", ctypes.c_long),
        ("bottom", ctypes.c_long),
    ]


class RemoteBuffer:
    """Owns one temporary allocation inside the target process for control-message marshalling."""

    def __init__(self, process_handle: int, size: int) -> None:
        self.process_handle = process_handle
        self.size = size
        self.address = kernel32.VirtualAllocEx(
            process_handle,
            None,
            size,
            MEM_COMMIT | MEM_RESERVE,
            PAGE_READWRITE,
        )
        if not self.address:
            raise ctypes.WinError(ctypes.get_last_error())

    def close(self) -> None:
        if self.address:
            kernel32.VirtualFreeEx(self.process_handle, self.address, 0, MEM_RELEASE)
            self.address = 0

    def __enter__(self) -> "RemoteBuffer":
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.close()


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


def win_path(path: Path, trailing_slash: bool = False) -> str:
    """Formats a path as an absolute Windows string, optionally with a trailing separator."""

    resolved = str(path.resolve())
    return resolved + ("\\" if trailing_slash and not resolved.endswith("\\") else "")


def prepare_fixture(seed_config_dir: Path, artifacts_dir: Path) -> dict:
    """Creates an isolated profile base plus deterministic shared roots for the UI harness."""

    profile_base = artifacts_dir / "profile-base"
    config_dir = profile_base / "config"
    log_dir = profile_base / "logs"
    incoming_dir = artifacts_dir / "incoming"
    temp_dir = artifacts_dir / "temp"
    shared_a_dir = artifacts_dir / "shared-a"
    shared_b_dir = artifacts_dir / "shared-b"

    shutil.copytree(seed_config_dir, config_dir)
    log_dir.mkdir(parents=True, exist_ok=True)
    incoming_dir.mkdir(parents=True, exist_ok=True)
    temp_dir.mkdir(parents=True, exist_ok=True)
    shared_a_dir.mkdir(parents=True, exist_ok=True)
    shared_b_dir.mkdir(parents=True, exist_ok=True)

    files = [
        (shared_a_dir / "zeta_small.txt", b"small\n"),
        (shared_a_dir / "middle_medium.txt", b"m" * 600),
        (shared_b_dir / "alpha_large.bin", b"a" * 4096),
    ]
    for file_path, content in files:
        file_path.write_bytes(content)

    preferences_path = config_dir / "preferences.ini"
    preferences_text = preferences_path.read_text(encoding="utf-8", errors="ignore")
    for key, value in (
        ("AppVersion", "0.72a x64"),
        ("Language", "1033"),
        ("StartupMinimized", "0"),
        ("BringToFront", "1"),
        ("ConfirmExit", "0"),
        ("RestoreLastMainWndDlg", "0"),
        ("Splashscreen", "0"),
        ("Autoconnect", "0"),
        ("Reconnect", "0"),
        ("NetworkED2K", "0"),
        ("NetworkKademlia", "0"),
        ("ShowSharedFilesDetails", "0"),
        ("IncomingDir", win_path(incoming_dir, trailing_slash=True)),
        ("TempDir", win_path(temp_dir, trailing_slash=True)),
        ("TempDirs", win_path(temp_dir, trailing_slash=True)),
    ):
        preferences_text = patch_ini_value(preferences_text, key, value)
    preferences_path.write_text(preferences_text, encoding="utf-8", newline="\r\n")

    shareddir_path = config_dir / "shareddir.dat"
    shareddir_contents = (
        win_path(shared_a_dir, trailing_slash=True)
        + "\r\n"
        + win_path(shared_b_dir, trailing_slash=True)
        + "\r\n"
    )
    shareddir_path.write_text(shareddir_contents, encoding="utf-16")

    return {
        "profile_base": profile_base,
        "config_dir": config_dir,
        "log_dir": log_dir,
        "incoming_dir": incoming_dir,
        "temp_dir": temp_dir,
        "shared_a_dir": shared_a_dir,
        "shared_b_dir": shared_b_dir,
        "expected_name_order_by_name": ["alpha_large.bin", "middle_medium.txt", "zeta_small.txt"],
        "expected_name_order_by_size": ["zeta_small.txt", "middle_medium.txt", "alpha_large.bin"],
    }


def open_process(process_id: int) -> int:
    """Opens the target process for the remote memory operations needed by Win32 list controls."""

    access = PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_QUERY_INFORMATION
    handle = kernel32.OpenProcess(access, False, process_id)
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())
    return handle


def close_process(process_handle: int) -> None:
    """Closes a process handle opened for cross-process control access."""

    if process_handle:
        kernel32.CloseHandle(process_handle)


def write_remote(process_handle: int, remote_address: int, data) -> None:
    """Writes one structure or byte buffer into a remote process allocation."""

    if isinstance(data, (bytes, bytearray)):
        buffer = (ctypes.c_ubyte * len(data)).from_buffer_copy(data)
        size = len(data)
        source = buffer
    else:
        size = ctypes.sizeof(data)
        source = data
    written = ctypes.c_size_t()
    if not kernel32.WriteProcessMemory(process_handle, remote_address, ctypes.byref(source), size, ctypes.byref(written)):
        raise ctypes.WinError(ctypes.get_last_error())


def read_remote(process_handle: int, remote_address: int, size: int) -> bytes:
    """Reads one byte range from a remote process allocation."""

    buffer = (ctypes.c_ubyte * size)()
    read = ctypes.c_size_t()
    if not kernel32.ReadProcessMemory(process_handle, remote_address, ctypes.byref(buffer), size, ctypes.byref(read)):
        raise ctypes.WinError(ctypes.get_last_error())
    return bytes(buffer[: read.value])


def get_list_item_text(process_handle: int, list_hwnd: int, row_index: int, sub_item: int) -> str:
    """Reads one owner-data list-view cell text through `LVM_GETITEMTEXTW`."""

    max_chars = 1024
    text_bytes = max_chars * ctypes.sizeof(ctypes.c_wchar)
    total_size = ctypes.sizeof(LVITEMW) + text_bytes
    with RemoteBuffer(process_handle, total_size) as remote:
        remote_text_address = remote.address + ctypes.sizeof(LVITEMW)
        item = LVITEMW()
        item.mask = LVIF_TEXT
        item.iItem = row_index
        item.iSubItem = sub_item
        item.pszText = remote_text_address
        item.cchTextMax = max_chars
        write_remote(process_handle, remote.address, item)
        win32gui.SendMessage(list_hwnd, LVM_GETITEMTEXTW, row_index, remote.address)
        raw_text = read_remote(process_handle, remote_text_address, text_bytes)
        return raw_text.decode("utf-16-le", errors="ignore").split("\x00", 1)[0]


def get_remote_rect(process_handle: int, hwnd: int, message: int, index: int, left_seed: int = 0) -> RECT:
    """Queries one control rectangle by marshalling a RECT through remote process memory."""

    with RemoteBuffer(process_handle, ctypes.sizeof(RECT)) as remote:
        rect = RECT(left_seed, 0, 0, 0)
        write_remote(process_handle, remote.address, rect)
        if not win32gui.SendMessage(hwnd, message, index, remote.address):
            raise RuntimeError(f"Control message 0x{message:04X} failed for index {index}.")
        return RECT.from_buffer_copy(read_remote(process_handle, remote.address, ctypes.sizeof(RECT)))


def get_control_handle(main_hwnd: int, control_id: int, class_name: str) -> int:
    """Finds one descendant control by numeric ID and window class."""

    matches = []

    def walk(hwnd: int) -> None:
        child = win32gui.GetWindow(hwnd, win32con.GW_CHILD)
        while child:
            try:
                child_class = win32gui.GetClassName(child)
                child_id = win32gui.GetDlgCtrlID(child)
                if child_class == class_name and child_id == control_id:
                    matches.append(child)
            except win32gui.error:
                pass
            walk(child)
            child = win32gui.GetWindow(child, win32con.GW_HWNDNEXT)

    walk(main_hwnd)
    if not matches:
        raise RuntimeError(f"Unable to find {class_name} with control id {control_id}.")
    return matches[0]


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


def click_client_rect(hwnd: int, rect: RECT) -> None:
    """Clicks the center of a client-rect region on screen."""

    origin = win32gui.ClientToScreen(hwnd, (0, 0))
    x = origin[0] + rect.left + max((rect.right - rect.left) // 2, 1)
    y = origin[1] + rect.top + max((rect.bottom - rect.top) // 2, 1)
    mouse.click(coords=(x, y))


def set_list_row_selected(process_handle: int, list_hwnd: int, row_index: int) -> None:
    """Selects one virtual list row through the real Win32 list-view state messages."""

    win32gui.SendMessage(list_hwnd, LVM_ENSUREVISIBLE, row_index, 0)
    with RemoteBuffer(process_handle, ctypes.sizeof(LVITEMW)) as remote:
        clear_state = LVITEMW()
        clear_state.stateMask = LVIS_SELECTED | LVIS_FOCUSED
        write_remote(process_handle, remote.address, clear_state)
        win32gui.SendMessage(list_hwnd, LVM_SETITEMSTATE, -1, remote.address)

        select_state = LVITEMW()
        select_state.state = LVIS_SELECTED | LVIS_FOCUSED
        select_state.stateMask = LVIS_SELECTED | LVIS_FOCUSED
        write_remote(process_handle, remote.address, select_state)
        win32gui.SendMessage(list_hwnd, LVM_SETITEMSTATE, row_index, remote.address)

    win32gui.SendMessage(list_hwnd, LVM_SETSELECTIONMARK, 0, row_index)


def launch_app(app_exe: Path, profile_base: Path) -> Application:
    """Starts the real app with the isolated `-c` override."""

    os.environ["EMULE_STARTUP_PROFILE"] = "1"
    command_line = f'"{app_exe}" -c "{profile_base}"'
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
            raise RuntimeError(f"Unexpected startup dialog: {describe_startup_dialog(window.handle)!r}")
        return window

    return wait_for(resolve, timeout=90.0, interval=0.5, description="eMule main window")


def wait_for_list_count(list_hwnd: int, minimum_count: int) -> int:
    """Waits until the Shared Files list exposes at least the requested item count."""

    def resolve():
        count = win32gui.SendMessage(list_hwnd, LVM_GETITEMCOUNT, 0, 0)
        return count if count >= minimum_count else 0

    return wait_for(resolve, timeout=90.0, interval=0.5, description="Shared Files list rows")


def wait_for_static_text(static_hwnd: int, expected_text: str) -> None:
    """Waits until a static control shows the expected file name."""

    def resolve():
        actual = win32gui.GetWindowText(static_hwnd)
        return actual if expected_text in actual else ""

    actual = wait_for(resolve, timeout=10.0, interval=0.2, description=f"details text '{expected_text}'")


def sort_by_size_column(process_handle: int, list_hwnd: int, before_first_row: str) -> str:
    """Clicks the Size header until row order differs from the default name sort."""

    header_hwnd = win32gui.SendMessage(list_hwnd, LVM_GETHEADER, 0, 0)
    if not header_hwnd:
        raise RuntimeError("Shared Files list header was not found.")

    for _ in range(2):
        rect = get_remote_rect(process_handle, header_hwnd, HDM_GETITEMRECT, 1)
        click_client_rect(header_hwnd, rect)
        time.sleep(0.5)
        first_row = get_list_item_text(process_handle, list_hwnd, 0, 0)
        if first_row != before_first_row:
            return first_row
    raise RuntimeError("Sorting by the Size column did not change the Shared Files row order.")


def click_reload_button(main_hwnd: int) -> None:
    """Invokes the real Reload button on the Shared Files page."""

    reload_hwnd = get_control_handle(main_hwnd, IDC_RELOADSHAREDFILES, "Button")
    win32gui.SendMessage(reload_hwnd, BM_CLICK, 0, 0)


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


def run_shared_files_e2e(app_exe: Path, seed_config_dir: Path, artifacts_dir: Path) -> None:
    """Executes the real Shared Files Win32 regression against an isolated fixture profile."""

    fixture = prepare_fixture(seed_config_dir, artifacts_dir)
    summary = {
        "app_exe": str(app_exe),
        "profile_base": str(fixture["profile_base"]),
        "expected_name_order_by_name": fixture["expected_name_order_by_name"],
        "expected_name_order_by_size": fixture["expected_name_order_by_size"],
    }

    app = None
    process_handle = 0
    try:
        app = launch_app(app_exe, fixture["profile_base"])
        main_window = wait_for_main_window(app)
        main_hwnd = main_window.handle
        main_window.set_focus()
        process_id = win32process.GetWindowThreadProcessId(main_hwnd)[1]
        process_handle = open_process(process_id)

        dump_window_tree(main_hwnd, artifacts_dir / "window-tree-initial.json")

        win32gui.SendMessage(main_hwnd, WM_COMMAND, MP_HM_FILES, 0)
        list_hwnd = wait_for(lambda: get_control_handle(main_hwnd, IDC_SFLIST, "SysListView32"), 30.0, 0.5, "Shared Files list control")
        static_hwnd = get_control_handle(main_hwnd, IDC_SF_FNAME, "Static")

        count = wait_for_list_count(list_hwnd, minimum_count=3)
        names_before = [get_list_item_text(process_handle, list_hwnd, i, 0) for i in range(min(count, 3))]
        summary["names_before_sort"] = names_before
        if names_before != fixture["expected_name_order_by_name"]:
            raise RuntimeError(f"Unexpected default Shared Files order: {names_before!r}")

        set_list_row_selected(process_handle, list_hwnd, 1)
        wait_for_static_text(static_hwnd, fixture["expected_name_order_by_name"][1])

        first_row_after_sort = sort_by_size_column(process_handle, list_hwnd, names_before[0])
        summary["first_row_after_sort"] = first_row_after_sort
        if first_row_after_sort != fixture["expected_name_order_by_size"][0]:
            raise RuntimeError(
                f"Unexpected first row after sorting by size: {first_row_after_sort!r} "
                f"(expected {fixture['expected_name_order_by_size'][0]!r})."
            )

        click_reload_button(main_hwnd)
        count_after_reload = wait_for_list_count(list_hwnd, minimum_count=3)
        names_after_reload = [get_list_item_text(process_handle, list_hwnd, i, 0) for i in range(min(count_after_reload, 3))]
        summary["names_after_reload"] = names_after_reload
        if set(fixture["expected_name_order_by_name"]) - set(names_after_reload):
            raise RuntimeError(f"Reload dropped expected Shared Files rows: {names_after_reload!r}")

        write_json(artifacts_dir / "result.json", summary)
    except Exception:
        if app is not None:
            try:
                main_window = app.top_window()
                dump_window_tree(main_window.handle, artifacts_dir / "window-tree-failure.json")
                try:
                    image = main_window.capture_as_image()
                    image.save(artifacts_dir / "failure.png")
                except Exception:
                    pass
            except Exception:
                pass
        raise
    finally:
        if process_handle:
            close_process(process_handle)
        if app is not None:
            try:
                close_app_cleanly(app)
            except Exception:
                try:
                    app.kill()
                except Exception:
                    pass


def main(argv: list[str]) -> int:
    """Parses arguments, executes the UI regression, and writes failure artifacts on disk."""

    parser = argparse.ArgumentParser()
    parser.add_argument("--app-exe", required=True)
    parser.add_argument("--seed-config-dir", required=True)
    parser.add_argument("--artifacts-dir", required=True)
    args = parser.parse_args(argv)

    artifacts_dir = Path(args.artifacts_dir).resolve()
    artifacts_dir.mkdir(parents=True, exist_ok=True)

    try:
        run_shared_files_e2e(
            app_exe=Path(args.app_exe).resolve(),
            seed_config_dir=Path(args.seed_config_dir).resolve(),
            artifacts_dir=artifacts_dir,
        )
        return 0
    except Exception as exc:
        (artifacts_dir / "error.txt").write_text(f"{exc}\n", encoding="utf-8")
        raise


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
