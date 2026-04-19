"""Runs the canonical isolated live E2E suite against the in-process eMule REST API."""

from __future__ import annotations

import argparse
import json
import socket
import subprocess
import time
import urllib.error
import urllib.request
from pathlib import Path
from typing import Any

from emule_live_profile_common import (
    close_app_cleanly,
    launch_app,
    patch_ini_value,
    prepare_profile_base,
    wait_for,
    wait_for_main_window,
    write_json,
)

DEFAULT_SERVER_SEARCH_QUERIES = ("ubuntu", "linux", "debian")
DEFAULT_KAD_SEARCH_QUERIES = ("ubuntu", "linux", "debian")


def choose_listen_port() -> int:
    """Returns one ephemeral localhost TCP port for the smoke listener."""

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
        probe.bind(("127.0.0.1", 0))
        return int(probe.getsockname()[1])


def upsert_ini_section_value(text: str, section: str, key: str, value: str) -> str:
    """Upserts one key/value pair inside a simple INI section."""

    section_header = f"[{section}]"
    lines = text.splitlines()
    output: list[str] = []
    inside_target = False
    inserted = False
    saw_section = False

    for raw_line in lines:
        stripped = raw_line.strip()
        if stripped.startswith("[") and stripped.endswith("]"):
            if inside_target and not inserted:
                output.append(f"{key}={value}")
                inserted = True
            inside_target = stripped == section_header
            saw_section = saw_section or inside_target
            output.append(raw_line)
            continue

        if inside_target and raw_line.partition("=")[0].strip().lower() == key.lower():
            output.append(f"{key}={value}")
            inserted = True
            continue

        output.append(raw_line)

    if saw_section:
        if inside_target and not inserted:
            output.append(f"{key}={value}")
    else:
        if output and output[-1] != "":
            output.append("")
        output.append(section_header)
        output.append(f"{key}={value}")

    return "\r\n".join(output) + "\r\n"


def configure_webserver_profile(
    config_dir: Path,
    app_exe: Path,
    api_key: str,
    port: int,
    bind_addr: str,
    enable_upnp: bool,
) -> None:
    """Enables the WebServer listener and REST API key inside the temp profile."""

    preferences_path = config_dir / "preferences.ini"
    text = preferences_path.read_text(encoding="utf-8", errors="ignore")
    text = patch_ini_value(text, "ConfirmExit", "0")
    for key, value in (
        ("Autoconnect", "1"),
        ("Reconnect", "1"),
        ("NetworkED2K", "1"),
        ("NetworkKademlia", "1"),
    ):
        text = patch_ini_value(text, key, value)
    template_path = app_exe.parent.parent.parent / "webinterface" / "eMule.tmpl"
    text = patch_ini_value(text, "WebTemplateFile", str(template_path))
    for key, value in (
        ("Password", ""),
        ("PasswordLow", ""),
        ("ApiKey", api_key),
        ("BindAddr", bind_addr),
        ("Port", str(port)),
        ("WebUseUPnP", "0"),
        ("Enabled", "1"),
        ("UseGzip", "1"),
        ("PageRefreshTime", "120"),
        ("UseLowRightsUser", "0"),
        ("AllowAdminHiLevelFunc", "1"),
        ("WebTimeoutMins", "5"),
        ("UseHTTPS", "0"),
        ("HTTPSCertificate", ""),
        ("HTTPSKey", ""),
    ):
        text = upsert_ini_section_value(text, "WebServer", key, value)
    text = upsert_ini_section_value(text, "UPnP", "EnableUPnP", "1" if enable_upnp else "0")
    preferences_path.write_text(text, encoding="utf-8", newline="\r\n")


def apply_p2p_bindaddr_override(
    config_dir: Path,
    interface_name: str,
    bind_updater_script: Path,
) -> None:
    """Runs the canonical bind-address updater against the isolated profile."""

    command = [
        "powershell.exe",
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-File",
        str(bind_updater_script),
        "-InterfaceName",
        interface_name,
        "-ConfigDirectory",
        str(config_dir),
    ]
    completed = subprocess.run(
        command,
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if completed.returncode != 0:
        raise RuntimeError(
            "P2P bind updater failed with exit code "
            f"{completed.returncode}: {completed.stdout}{completed.stderr}"
        )


def http_request(
    base_url: str,
    path: str,
    *,
    method: str = "GET",
    api_key: str | None = None,
    json_body=None,
    request_timeout_seconds: float = 5.0,
) -> dict[str, object]:
    """Performs one HTTP request and returns a compact structured result."""

    data = None
    headers: dict[str, str] = {}
    if api_key is not None:
        headers["X-API-Key"] = api_key
    if json_body is not None:
        data = json.dumps(json_body).encode("utf-8")
        headers["Content-Type"] = "application/json; charset=utf-8"

    request = urllib.request.Request(base_url + path, data=data, method=method, headers=headers)
    try:
        with urllib.request.urlopen(request, timeout=request_timeout_seconds) as response:
            body_bytes = response.read()
            body_text = body_bytes.decode("utf-8", errors="replace")
            content_type = response.headers.get("Content-Type", "")
            payload = None
            if "application/json" in content_type:
                payload = json.loads(body_text)
            return {
                "status": int(response.status),
                "content_type": content_type,
                "body_text": body_text,
                "json": payload,
            }
    except urllib.error.HTTPError as exc:
        body_text = exc.read().decode("utf-8", errors="replace")
        content_type = exc.headers.get("Content-Type", "")
        payload = None
        if "application/json" in content_type and body_text:
            payload = json.loads(body_text)
        return {
            "status": int(exc.code),
            "content_type": content_type,
            "body_text": body_text,
            "json": payload,
        }


def require_json_object(result: dict[str, object], expected_status: int) -> dict[str, Any]:
    """Asserts one REST response is the expected JSON object payload."""

    assert int(result["status"]) == expected_status
    assert isinstance(result["json"], dict)
    return result["json"]


def get_app_process_id(app: object) -> int | None:
    """Returns the launched process id when pywinauto exposes it."""

    process_id = getattr(app, "process", None)
    if callable(process_id):
        try:
            process_id = process_id()
        except TypeError:
            process_id = None
    if isinstance(process_id, int):
        return process_id
    return None


def compact_server_status(payload: dict[str, Any]) -> dict[str, Any]:
    """Keeps the server-status timeline compact and stable in artifacts."""

    current_server = payload.get("currentServer")
    compact_current = None
    if isinstance(current_server, dict):
        compact_current = {
            "name": current_server.get("name"),
            "address": current_server.get("address"),
            "port": current_server.get("port"),
            "current": current_server.get("current"),
            "connected": current_server.get("connected"),
            "connecting": current_server.get("connecting"),
        }

    return {
        "connected": payload.get("connected"),
        "connecting": payload.get("connecting"),
        "lowId": payload.get("lowId"),
        "serverCount": payload.get("serverCount"),
        "currentServer": compact_current,
    }


def compact_kad_status(payload: dict[str, Any]) -> dict[str, Any]:
    """Keeps the Kad-status timeline compact and stable in artifacts."""

    return {
        "running": payload.get("running"),
        "connected": payload.get("connected"),
        "bootstrapping": payload.get("bootstrapping"),
        "firewalled": payload.get("firewalled"),
        "users": payload.get("users"),
        "files": payload.get("files"),
    }


def compact_http_result(result: dict[str, object]) -> dict[str, object]:
    """Strips one HTTP result down to stable artifact fields."""

    compact: dict[str, object] = {
        "status": int(result["status"]),
        "content_type": result["content_type"],
    }
    if isinstance(result.get("json"), dict | list):
        compact["json"] = result["json"]
    elif isinstance(result.get("body_text"), str):
        compact["body_text"] = result["body_text"]
    return compact


def wait_for_server_activity(base_url: str, api_key: str, timeout_seconds: float) -> dict[str, object]:
    """Waits until the live server flow shows observable progress."""

    observations: list[dict[str, Any]] = []

    def resolve():
        result = http_request(base_url, "/api/v1/servers/status", api_key=api_key)
        if int(result["status"]) != 200 or not isinstance(result["json"], dict):
            return None
        payload = require_json_object(result, 200)
        snapshot = compact_server_status(payload)
        snapshot["observed_at"] = round(time.time(), 3)
        observations.append(snapshot)
        if payload.get("connected") or payload.get("connecting") or payload.get("currentServer") is not None:
            return {
                "status": result,
                "observations": observations,
            }
        return None

    return wait_for(resolve, timeout=timeout_seconds, interval=1.0, description="server activity")


def wait_for_server_connected(
    base_url: str,
    api_key: str,
    timeout_seconds: float,
    *,
    expected_server: dict[str, object] | None = None,
) -> dict[str, object]:
    """Waits until eD2K reaches a connected state, optionally for one target server."""

    observations: list[dict[str, Any]] = []
    expected_address = None if expected_server is None else str(expected_server.get("address") or "")
    expected_port = None if expected_server is None else int(expected_server.get("port") or 0)

    def resolve():
        result = http_request(base_url, "/api/v1/servers/status", api_key=api_key)
        if int(result["status"]) != 200 or not isinstance(result["json"], dict):
            return None
        payload = require_json_object(result, 200)
        snapshot = compact_server_status(payload)
        snapshot["observed_at"] = round(time.time(), 3)
        observations.append(snapshot)

        current_server = payload.get("currentServer")
        matches_expected = expected_server is None
        if isinstance(current_server, dict) and expected_server is not None:
            matches_expected = (
                str(current_server.get("address") or "") == expected_address
                and int(current_server.get("port") or 0) == expected_port
            )

        if payload.get("connected") and matches_expected:
            return {
                "status": result,
                "observations": observations,
            }
        return None

    return wait_for(resolve, timeout=timeout_seconds, interval=1.0, description="server connected state")


def observe_server_connect_attempt(
    base_url: str,
    api_key: str,
    timeout_seconds: float,
) -> dict[str, object]:
    """Watches one accepted server-connect attempt until it connects or clearly aborts."""

    observations: list[dict[str, Any]] = []
    last_result: dict[str, object] | None = None
    saw_progress = False
    deadline = time.time() + timeout_seconds

    while time.time() < deadline:
        try:
            result = http_request(base_url, "/api/v1/servers/status", api_key=api_key)
        except Exception as exc:
            observations.append(
                {
                    "observed_at": round(time.time(), 3),
                    "transport_error": {
                        "type": type(exc).__name__,
                        "message": str(exc),
                    },
                }
            )
            time.sleep(2.0)
            continue

        if int(result["status"]) != 200 or not isinstance(result["json"], dict):
            observations.append(
                {
                    "observed_at": round(time.time(), 3),
                    "unexpected_status": compact_http_result(result),
                }
            )
            time.sleep(2.0)
            continue

        payload = require_json_object(result, 200)
        last_result = result
        snapshot = compact_server_status(payload)
        snapshot["observed_at"] = round(time.time(), 3)
        observations.append(snapshot)

        if bool(payload.get("connected")):
            return {
                "connected": True,
                "status": result,
                "observations": observations,
            }

        if bool(payload.get("connecting")) or payload.get("currentServer") is not None:
            saw_progress = True
        elif saw_progress:
            return {
                "connected": False,
                "aborted": True,
                "status": result,
                "observations": observations,
            }

        time.sleep(2.0)

    return {
        "connected": False,
        "aborted": False,
        "status": last_result,
        "observations": observations,
    }


def wait_for_kad_running(base_url: str, api_key: str, timeout_seconds: float) -> dict[str, object]:
    """Waits until Kad reports a running state after the connect request."""

    observations: list[dict[str, Any]] = []

    def resolve():
        result = http_request(base_url, "/api/v1/kad/status", api_key=api_key)
        if int(result["status"]) != 200 or not isinstance(result["json"], dict):
            return None
        payload = require_json_object(result, 200)
        snapshot = compact_kad_status(payload)
        snapshot["observed_at"] = round(time.time(), 3)
        observations.append(snapshot)
        if payload.get("running"):
            return {
                "status": result,
                "observations": observations,
            }
        return None

    return wait_for(resolve, timeout=timeout_seconds, interval=1.0, description="Kad running state")


def wait_for_network_ready(base_url: str, api_key: str, timeout_seconds: float) -> dict[str, object]:
    """Waits until the requested live network modes become usable for searches."""

    return wait_for_requested_networks(
        base_url,
        api_key,
        timeout_seconds,
        require_server_connected=False,
        require_kad_connected=False,
    )


def wait_for_requested_networks(
    base_url: str,
    api_key: str,
    timeout_seconds: float,
    *,
    require_server_connected: bool,
    require_kad_connected: bool,
) -> dict[str, object]:
    """Waits until the requested live server/Kad connectivity requirements are met."""

    observations: list[dict[str, Any]] = []
    last_server_result = None
    last_kad_result = None
    deadline = time.time() + timeout_seconds

    while time.time() < deadline:
        server_result = http_request(base_url, "/api/v1/servers/status", api_key=api_key)
        kad_result = http_request(base_url, "/api/v1/kad/status", api_key=api_key)
        if int(server_result["status"]) != 200 or int(kad_result["status"]) != 200:
            time.sleep(2.0)
            continue
        if not isinstance(server_result["json"], dict) or not isinstance(kad_result["json"], dict):
            time.sleep(2.0)
            continue

        server_payload = require_json_object(server_result, 200)
        kad_payload = require_json_object(kad_result, 200)
        last_server_result = server_result
        last_kad_result = kad_result
        snapshot = {
            "observed_at": round(time.time(), 3),
            "server": compact_server_status(server_payload),
            "kad": compact_kad_status(kad_payload),
        }
        observations.append(snapshot)

        server_connected = bool(server_payload.get("connected"))
        kad_connected = bool(kad_payload.get("connected"))

        if require_server_connected or require_kad_connected:
            if (not require_server_connected or server_connected) and (
                not require_kad_connected or kad_connected
            ):
                mode = "both" if server_connected and kad_connected else (
                    "server" if server_connected else "kad"
                )
                return {
                    "ready": True,
                    "mode": mode,
                    "server_ready": server_connected,
                    "kad_ready": kad_connected,
                    "server_status": server_result,
                    "kad_status": kad_result,
                    "observations": observations,
                }
        elif server_connected:
            return {
                "ready": True,
                "mode": "server",
                "server_ready": True,
                "kad_ready": kad_connected,
                "server_status": server_result,
                "kad_status": kad_result,
                "observations": observations,
            }
        elif kad_connected:
            return {
                "ready": True,
                "mode": "kad",
                "server_ready": server_connected,
                "kad_ready": True,
                "server_status": server_result,
                "kad_status": kad_result,
                "observations": observations,
            }
        time.sleep(2.0)

    raise RuntimeError(
        "Timed out waiting for live network readiness. "
        f"Last server status: {compact_http_result(last_server_result) if isinstance(last_server_result, dict) else None!r}; "
        f"last Kad status: {compact_http_result(last_kad_result) if isinstance(last_kad_result, dict) else None!r}"
    )


def build_search_method_candidates(mode: str) -> list[str]:
    """Returns one resilient search-method preference order for live runs."""

    if mode == "server":
        return ["server", "global", "automatic"]
    if mode == "kad":
        return ["kad", "automatic"]
    return ["automatic", "server", "kad"]


def build_search_plan(server_search_count: int, kad_search_count: int) -> list[dict[str, object]]:
    """Builds one deterministic multi-search plan for the requested network counts."""

    plan: list[dict[str, object]] = []
    for index in range(server_search_count):
        plan.append(
            {
                "network": "server",
                "query": DEFAULT_SERVER_SEARCH_QUERIES[index % len(DEFAULT_SERVER_SEARCH_QUERIES)],
                "ordinal": index + 1,
            }
        )
    for index in range(kad_search_count):
        plan.append(
            {
                "network": "kad",
                "query": DEFAULT_KAD_SEARCH_QUERIES[index % len(DEFAULT_KAD_SEARCH_QUERIES)],
                "ordinal": index + 1,
            }
        )
    return plan


def start_live_search(
    base_url: str,
    api_key: str,
    mode: str,
    query: str,
    forced_method: str | None = None,
) -> dict[str, object]:
    """Starts one real live search, retrying through sensible transport methods."""

    attempts: list[dict[str, Any]] = []
    method_candidates = [forced_method] if forced_method else build_search_method_candidates(mode)
    for method_name in method_candidates:
        response = http_request(
            base_url,
            "/api/v1/search/start",
            method="POST",
            api_key=api_key,
            json_body={
                "query": query,
                "method": method_name,
                "type": "program",
            },
        )
        attempt = {
            "method": method_name,
            "response": response,
        }
        attempts.append(attempt)
        if int(response["status"]) == 200 and isinstance(response["json"], dict) and response["json"].get("search_id"):
            return {
                "ok": True,
                "attempts": attempts,
                "selected_method": method_name,
                "method_candidates": method_candidates,
                "response": response,
            }
    return {
        "ok": False,
        "attempts": attempts,
        "selected_method": None,
        "method_candidates": method_candidates,
        "response": attempts[-1]["response"] if attempts else None,
    }


def connect_to_live_server(
    base_url: str,
    api_key: str,
    server_rows: list[dict[str, object]],
    timeout_seconds: float,
) -> dict[str, object]:
    """Attempts real server connections until one seeded candidate reaches connected state."""

    candidates = [
        {
            "name": row.get("name"),
            "address": row.get("address"),
            "port": row.get("port"),
            "description": row.get("description"),
        }
        for row in server_rows
        if isinstance(row, dict) and row.get("address") and row.get("port")
    ]
    if not candidates:
        raise AssertionError("No server candidates were available for live connect attempts.")

    deadline = time.time() + timeout_seconds
    attempts: list[dict[str, object]] = []

    for index, candidate in enumerate(candidates, start=1):
        remaining_seconds = deadline - time.time()
        if remaining_seconds <= 0:
            break

        attempt: dict[str, object] = {
            "ordinal": index,
            "candidate": candidate,
        }
        try:
            connect_response = http_request(
                base_url,
                "/api/v1/servers/connect",
                method="POST",
                api_key=api_key,
                json_body={"addr": candidate["address"], "port": candidate["port"]},
                request_timeout_seconds=15.0,
            )
            attempt["connect_response"] = compact_http_result(connect_response)
        except Exception as exc:
            attempt["error"] = {
                "type": type(exc).__name__,
                "message": str(exc),
            }
            attempts.append(attempt)
            continue

        if int(connect_response["status"]) != 200 or not isinstance(connect_response["json"], dict):
            attempts.append(attempt)
            continue

        settle = observe_server_connect_attempt(
            base_url,
            api_key,
            min(remaining_seconds, 120.0),
        )
        attempt["settle"] = settle
        attempts.append(attempt)
        if bool(settle.get("connected")):
            return {
                "selected_server": candidate,
                "attempts": attempts,
                "final_status": settle["status"],
            }
        if not bool(settle.get("aborted")):
            break

    raise AssertionError(f"Failed to connect to any seeded server candidate. Attempts: {attempts!r}")


def wait_for_search_observation(
    base_url: str,
    api_key: str,
    search_id: str,
    timeout_seconds: float,
) -> dict[str, object]:
    """Polls one live search until results are observable or the search completes."""

    observations: list[dict[str, Any]] = []

    def resolve():
        result = http_request(base_url, f"/api/v1/search/results?search_id={search_id}", api_key=api_key)
        if int(result["status"]) != 200 or not isinstance(result["json"], dict):
            return None
        payload = require_json_object(result, 200)
        results = payload.get("results")
        assert isinstance(results, list)
        snapshot = {
            "observed_at": round(time.time(), 3),
            "status": payload.get("status"),
            "result_count": len(results),
        }
        observations.append(snapshot)
        if len(results) > 0:
            return {
                "result": result,
                "observations": observations,
                "terminal_state": "results",
            }
        if payload.get("status") == "complete":
            return {
                "result": result,
                "observations": observations,
                "terminal_state": "complete",
            }
        if payload.get("status") == "running" and len(observations) >= 2:
            return {
                "result": result,
                "observations": observations,
                "terminal_state": "running",
            }
        return None

    return wait_for(resolve, timeout=timeout_seconds, interval=2.0, description="live search activity")


def stop_live_search(base_url: str, api_key: str, search_id: str) -> dict[str, object]:
    """Stops one live search and returns the raw REST response."""

    return http_request(
        base_url,
        "/api/v1/search/stop",
        method="POST",
        api_key=api_key,
        json_body={"search_id": search_id},
    )


def execute_search_plan(
    base_url: str,
    api_key: str,
    search_plan: list[dict[str, object]],
    observation_timeout_seconds: float,
    *,
    search_method_override: str | None,
) -> tuple[list[dict[str, object]], str | None]:
    """Runs one deterministic search plan and returns completed cycle artifacts."""

    completed_cycles: list[dict[str, object]] = []
    active_search_id: str | None = None

    for cycle_index, cycle_plan in enumerate(search_plan, start=1):
        network = str(cycle_plan["network"])
        query = str(cycle_plan["query"])
        cycle_report: dict[str, object] = {
            "cycle_index": cycle_index,
            "network": network,
            "query": query,
            "ordinal": int(cycle_plan["ordinal"]),
        }
        try:
            live_search = start_live_search(
                base_url,
                api_key,
                network,
                query,
                forced_method=search_method_override or network,
            )
            cycle_report["start"] = live_search
            if not bool(live_search["ok"]):
                raise AssertionError(
                    "Failed to start a live search via methods "
                    f"{live_search['method_candidates']!r} for network {network!r}."
                )

            assert isinstance(live_search["response"], dict)
            search_payload = require_json_object(live_search["response"], 200)
            active_search_id = str(search_payload["search_id"])
            cycle_report["search_id"] = active_search_id
            cycle_report["selected_method"] = live_search["selected_method"]

            try:
                cycle_report["activity"] = wait_for_search_observation(
                    base_url,
                    api_key,
                    active_search_id,
                    observation_timeout_seconds,
                )
            finally:
                if active_search_id is not None:
                    stop_result = stop_live_search(base_url, api_key, active_search_id)
                    cycle_report["stop"] = compact_http_result(stop_result)
                    assert int(stop_result["status"]) == 200
                    assert isinstance(stop_result["json"], dict)
                    active_search_id = None
        except Exception:
            completed_cycles.append(cycle_report)
            raise

        completed_cycles.append(cycle_report)

    return completed_cycles, active_search_id


def wait_for_rest_ready(base_url: str, api_key: str, timeout_seconds: float) -> dict[str, object]:
    """Polls until the live REST listener answers the version route."""

    def resolve():
        try:
            result = http_request(base_url, "/api/v1/app/version", api_key=api_key)
        except OSError:
            return None
        if int(result["status"]) != 200:
            return None
        return result

    return wait_for(resolve, timeout=timeout_seconds, interval=0.5, description="REST API readiness")


def set_phase(report: dict[str, object], phase: str) -> str:
    """Records the current execution phase in the report and returns it."""

    report["current_phase"] = phase
    phase_history = report.setdefault("phase_history", [])
    assert isinstance(phase_history, list)
    phase_history.append(
        {
            "phase": phase,
            "entered_at": round(time.time(), 3),
        }
    )
    return phase


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--app-exe", required=True)
    parser.add_argument("--seed-config-dir", required=True)
    parser.add_argument("--artifacts-dir", required=True)
    parser.add_argument("--api-key", default="rest-smoke-test-key")
    parser.add_argument("--bind-addr", default="127.0.0.1")
    parser.add_argument("--enable-upnp", action="store_true")
    parser.add_argument("--p2p-bind-interface-name")
    parser.add_argument("--bind-updater-script")
    parser.add_argument("--rest-ready-timeout-seconds", type=float, default=45.0)
    parser.add_argument("--server-activity-timeout-seconds", type=float, default=45.0)
    parser.add_argument("--kad-running-timeout-seconds", type=float, default=30.0)
    parser.add_argument("--network-ready-timeout-seconds", type=float, default=120.0)
    parser.add_argument("--search-observation-timeout-seconds", type=float, default=30.0)
    parser.add_argument("--server-search-count", type=int, default=0)
    parser.add_argument("--kad-search-count", type=int, default=0)
    parser.add_argument("--search-method-override", choices=["automatic", "server", "global", "kad"])
    parser.add_argument("--keep-running", action="store_true")
    args = parser.parse_args()
    if args.server_search_count < 0 or args.kad_search_count < 0:
        raise ValueError("Search counts must be zero or greater.")

    app_exe = Path(args.app_exe).resolve()
    seed_config_dir = Path(args.seed_config_dir).resolve()
    artifacts_dir = Path(args.artifacts_dir).resolve()
    artifacts_dir.mkdir(parents=True, exist_ok=True)

    port = choose_listen_port()
    base_url = f"http://127.0.0.1:{port}"
    profile = prepare_profile_base(seed_config_dir, artifacts_dir, shared_dirs=[])
    configure_webserver_profile(
        Path(profile["config_dir"]),
        app_exe,
        args.api_key,
        port,
        args.bind_addr,
        args.enable_upnp,
    )
    if args.p2p_bind_interface_name:
        if not args.bind_updater_script:
            raise ValueError("bind updater script path is required when a P2P bind interface name is supplied.")
        apply_p2p_bindaddr_override(
            Path(profile["config_dir"]),
            args.p2p_bind_interface_name,
            Path(args.bind_updater_script).resolve(),
        )

    app = None
    search_id = None
    report: dict[str, object] = {
        "base_url": base_url,
        "port": port,
        "suite": "rest-api-live-e2e",
        "launch_inputs": {
            "app_exe": str(app_exe),
            "seed_config_dir": str(seed_config_dir),
            "artifacts_dir": str(artifacts_dir),
            "profile_base": str(profile["profile_base"]),
            "config_dir": str(profile["config_dir"]),
            "api_key_length": len(args.api_key),
            "bind_addr": args.bind_addr,
            "enable_upnp": bool(args.enable_upnp),
            "p2p_bind_interface_name": args.p2p_bind_interface_name,
            "bind_updater_script": args.bind_updater_script,
            "keep_running": bool(args.keep_running),
            "server_search_count": args.server_search_count,
            "kad_search_count": args.kad_search_count,
            "search_method_override": args.search_method_override,
            "timeouts": {
                "rest_ready_seconds": args.rest_ready_timeout_seconds,
                "server_activity_seconds": args.server_activity_timeout_seconds,
                "kad_running_seconds": args.kad_running_timeout_seconds,
                "network_ready_seconds": args.network_ready_timeout_seconds,
                "search_observation_seconds": args.search_observation_timeout_seconds,
            },
        },
        "checks": {},
        "cleanup": {},
        "status": "failed",
    }
    current_phase = set_phase(report, "launch")
    pending_error: Exception | None = None

    try:
        app = launch_app(app_exe, Path(profile["profile_base"]))
        report["launched_process_id"] = get_app_process_id(app)
        main_window = wait_for_main_window(app)
        report["main_window_title"] = main_window.window_text()

        current_phase = set_phase(report, "rest_ready")
        ready = wait_for_rest_ready(base_url, args.api_key, args.rest_ready_timeout_seconds)
        report["checks"]["ready"] = compact_http_result(ready)

        current_phase = set_phase(report, "auth_checks")
        no_key = http_request(base_url, "/api/v1/app/version")
        assert no_key["status"] == 401
        assert isinstance(no_key["json"], dict)
        assert no_key["json"]["error"] == "UNAUTHORIZED"
        report["checks"]["missing_key"] = compact_http_result(no_key)

        wrong_key = http_request(base_url, "/api/v1/app/version", api_key="wrong-key")
        assert wrong_key["status"] == 401
        assert isinstance(wrong_key["json"], dict)
        assert wrong_key["json"]["error"] == "UNAUTHORIZED"
        report["checks"]["wrong_key"] = compact_http_result(wrong_key)

        current_phase = set_phase(report, "app_version")
        version = http_request(base_url, "/api/v1/app/version", api_key=args.api_key)
        assert version["status"] == 200
        assert isinstance(version["json"], dict)
        assert version["json"]["appName"] == "eMule"
        assert "version" in version["json"]
        report["checks"]["app_version"] = compact_http_result(version)

        current_phase = set_phase(report, "stats_global")
        stats = http_request(base_url, "/api/v1/stats/global", api_key=args.api_key)
        assert stats["status"] == 200
        assert isinstance(stats["json"], dict)
        assert "connected" in stats["json"]
        report["checks"]["stats_global"] = compact_http_result(stats)

        current_phase = set_phase(report, "servers_list")
        servers = http_request(base_url, "/api/v1/servers/list", api_key=args.api_key)
        assert servers["status"] == 200
        assert isinstance(servers["json"], list)
        assert len(servers["json"]) > 0
        first_server = servers["json"][0]
        assert isinstance(first_server, dict)
        assert "address" in first_server and "port" in first_server
        report["checks"]["servers_list"] = {
            "count": len(servers["json"]),
            "first_server": {
                "name": first_server.get("name"),
                "address": first_server.get("address"),
                "port": first_server.get("port"),
                "description": first_server.get("description"),
            },
        }

        current_phase = set_phase(report, "servers_status_initial")
        initial_server_status = http_request(base_url, "/api/v1/servers/status", api_key=args.api_key)
        assert initial_server_status["status"] == 200
        assert isinstance(initial_server_status["json"], dict)
        report["checks"]["servers_status_initial"] = compact_http_result(initial_server_status)

        current_phase = set_phase(report, "servers_connect")
        server_connect = connect_to_live_server(
            base_url,
            api_key=args.api_key,
            server_rows=list(servers["json"]),
            timeout_seconds=args.network_ready_timeout_seconds,
        )
        report["checks"]["servers_connect"] = server_connect
        report["selected_server_target"] = dict(server_connect["selected_server"])

        current_phase = set_phase(report, "kad_status_initial")
        initial_kad_status = http_request(base_url, "/api/v1/kad/status", api_key=args.api_key)
        assert initial_kad_status["status"] == 200
        assert isinstance(initial_kad_status["json"], dict)
        report["checks"]["kad_status_initial"] = compact_http_result(initial_kad_status)

        current_phase = set_phase(report, "kad_connect")
        kad_connect = http_request(base_url, "/api/v1/kad/connect", method="POST", api_key=args.api_key, json_body={})
        assert kad_connect["status"] == 200
        assert isinstance(kad_connect["json"], dict)
        report["checks"]["kad_connect"] = compact_http_result(kad_connect)

        current_phase = set_phase(report, "kad_running")
        kad_running = wait_for_kad_running(base_url, args.api_key, args.kad_running_timeout_seconds)
        report["checks"]["kad_running"] = kad_running

        current_phase = set_phase(report, "network_ready")
        live_network = wait_for_requested_networks(
            base_url,
            args.api_key,
            args.network_ready_timeout_seconds,
            require_server_connected=args.server_search_count > 0,
            require_kad_connected=args.kad_search_count > 0,
        )
        report["checks"]["network_ready"] = live_network
        assert bool(live_network.get("ready"))

        search_plan = build_search_plan(args.server_search_count, args.kad_search_count)
        if not search_plan:
            search_plan = [
                {
                    "network": str(live_network["mode"]),
                    "query": DEFAULT_SERVER_SEARCH_QUERIES[0],
                    "ordinal": 1,
                }
            ]
        report["checks"]["search_plan"] = search_plan

        current_phase = set_phase(report, "search_cycles")
        completed_cycles, search_id = execute_search_plan(
            base_url,
            args.api_key,
            search_plan,
            args.search_observation_timeout_seconds,
            search_method_override=args.search_method_override,
        )
        report["checks"]["search_cycles"] = completed_cycles
        search_id = None

        current_phase = set_phase(report, "log_limit")
        log_entries = http_request(base_url, "/api/v1/log?limit=1", api_key=args.api_key)
        assert log_entries["status"] == 200
        assert isinstance(log_entries["json"], list)
        assert len(log_entries["json"]) <= 1
        report["checks"]["log_limit"] = compact_http_result(log_entries)

        current_phase = set_phase(report, "kad_disconnect")
        kad_disconnect = http_request(base_url, "/api/v1/kad/disconnect", method="POST", api_key=args.api_key, json_body={})
        assert kad_disconnect["status"] == 200
        assert isinstance(kad_disconnect["json"], dict)
        assert "running" in kad_disconnect["json"]
        report["checks"]["kad_disconnect"] = compact_http_result(kad_disconnect)

        current_phase = set_phase(report, "html_root")
        html_root = http_request(base_url, "/")
        assert html_root["status"] == 200
        assert "text/html" in str(html_root["content_type"]).lower()
        assert "<html" in str(html_root["body_text"]).lower()
        report["checks"]["html_root"] = {
            "status": html_root["status"],
            "content_type": html_root["content_type"],
            "body_preview": str(html_root["body_text"])[:200],
        }

        current_phase = set_phase(report, "completed")
        report["status"] = "passed"
    except Exception as exc:
        pending_error = exc
        report["status"] = "failed"
        report["failed_phase"] = current_phase
        report["error"] = {
            "type": type(exc).__name__,
            "message": str(exc),
        }
    finally:
        cleanup = report["cleanup"]
        assert isinstance(cleanup, dict)
        if app is not None and search_id is not None:
            cleanup["search_stop_attempted"] = True
            try:
                stop_response = stop_live_search(base_url, args.api_key, search_id)
                cleanup["search_stop"] = compact_http_result(stop_response)
            except Exception as exc:  # pragma: no cover - best-effort live cleanup
                cleanup["search_stop_error"] = repr(exc)
        if app is not None:
            cleanup["process_id"] = get_app_process_id(app)
            cleanup["profile_base"] = str(profile["profile_base"])
            if args.keep_running and str(report.get("status")) == "passed":
                cleanup["app_closed"] = False
                cleanup["app_left_running"] = True
            else:
                try:
                    close_app_cleanly(app)
                    cleanup["app_closed"] = True
                except Exception as exc:
                    cleanup["app_closed"] = False
                    cleanup["app_close_error"] = repr(exc)
                    if pending_error is None:
                        pending_error = exc
                        report["status"] = "failed"
                        report["failed_phase"] = "cleanup"
                        report["error"] = {
                            "type": type(exc).__name__,
                            "message": str(exc),
                        }
        write_json(artifacts_dir / "result.json", report)
        if pending_error is not None:
            raise pending_error


if __name__ == "__main__":
    main()
