"""Runs the isolated live auto-browse validation against real eD2K/Kad peers."""

from __future__ import annotations

import argparse
import importlib.util
import json
import sys
import time
import urllib.parse
from pathlib import Path
from typing import Any


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


harness_cli_common = load_local_module("harness_cli_common", "harness-cli-common.py")
rest_smoke = load_local_module("rest_api_smoke", "rest-api-smoke.py")
live_common = load_local_module("emule_live_profile_common", "emule-live-profile-common.py")

close_app_cleanly = live_common.close_app_cleanly
launch_app = live_common.launch_app
prepare_profile_base = live_common.prepare_profile_base
wait_for_main_window = live_common.wait_for_main_window
write_json = live_common.write_json

BOOTSTRAP_TRANSFER_HASH = "28EAB1A0AB1B9416AAF534E27A234941"
FALLBACK_SEARCH_QUERIES = ("ubuntu", "linux", "debian")
BOOTSTRAP_SEARCH_METHODS = ("server", "global", "automatic", "kad")
FALLBACK_SEARCH_METHODS = ("server", "global", "kad", "automatic")
LIVE_SOURCE_UNAVAILABLE_EXIT_CODE = 2
PREFERRED_SERVER_ADDRESSES = ("91.148.135.252", "45.82.80.155", "91.148.135.254")


class LiveSourceUnavailableError(RuntimeError):
    """Raised when live networks are reachable but no sourced transfer can be acquired."""


def compact_http_result(result: dict[str, object]) -> dict[str, object]:
    """Keeps stored HTTP results small and stable in artifacts."""

    return {
        "status": result.get("status"),
        "content_type": result.get("content_type"),
        "json": result.get("json"),
        "body_text": result.get("body_text"),
    }


def require_json_object(result: dict[str, object], expected_status: int) -> dict[str, Any]:
    """Asserts that one REST response returned the expected JSON object payload."""

    assert int(result["status"]) == expected_status, compact_http_result(result)
    payload = result.get("json")
    assert isinstance(payload, dict), compact_http_result(result)
    return payload


def get_tooling_bind_updater(paths: harness_cli_common.HarnessRunPaths) -> Path:
    """Returns the canonical bind-address updater helper from `eMule-tooling`."""

    tooling_root = harness_cli_common.get_emule_workspace_root(paths.repo_root) / "repos" / "eMule-tooling"
    script_path = (tooling_root / "scripts" / "config-bindaddr-updater.ps1").resolve()
    if not script_path.is_file():
        raise RuntimeError(f"Bind updater helper was not found at '{script_path}'.")
    return script_path


def configure_auto_browse_profile(
    config_dir: Path,
    app_exe: Path,
    api_key: str,
    port: int,
    web_bind_addr: str,
) -> None:
    """Enables REST, auto-browse, and visible browse logging inside the isolated profile."""

    rest_smoke.configure_webserver_profile(
        config_dir=config_dir,
        app_exe=app_exe,
        api_key=api_key,
        port=port,
        bind_addr=web_bind_addr,
        enable_upnp=True,
    )

    preferences_path = config_dir / "preferences.ini"
    text = preferences_path.read_text(encoding="utf-8", errors="ignore")
    for key, value in (
        ("Autoconnect", "1"),
        ("Reconnect", "1"),
        ("NetworkED2K", "1"),
        ("NetworkKademlia", "1"),
        ("AutoBrowseRemoteShares", "1"),
        ("SaveLogToDisk", "1"),
        ("Verbose", "1"),
        ("FullVerbose", "1"),
    ):
        text = rest_smoke.upsert_ini_section_value(text, "eMule", key, value)
    preferences_path.write_text(text, encoding="utf-8", newline="\r\n")


def reorder_server_rows(server_rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    """Moves previously successful live candidates to the front of the candidate list."""

    preferred: list[dict[str, Any]] = []
    remaining: list[dict[str, Any]] = []
    preferred_set = set(PREFERRED_SERVER_ADDRESSES)
    for row in server_rows:
        if str(row.get("address")) in preferred_set:
            preferred.append(row)
        else:
            remaining.append(row)
    preferred.sort(key=lambda row: PREFERRED_SERVER_ADDRESSES.index(str(row.get("address"))))
    return preferred + remaining


def build_ed2k_link(result_row: dict[str, Any]) -> str:
    """Builds one minimal ED2K file link from a REST search-result row."""

    file_name = urllib.parse.quote(str(result_row["name"]), safe="")
    return f"ed2k://|file|{file_name}|{int(result_row['size'])}|{result_row['hash']}|/"


def is_safe_download_result(result_row: dict[str, Any]) -> bool:
    """Rejects executable results so the live scenario never downloads `.exe` payloads from eMule."""

    file_name = str(result_row.get("name") or "").strip().lower()
    file_type = str(result_row.get("fileType") or "").strip().lower()
    return not file_name.endswith(".exe") and file_type != "program"


def fetch_search_results(base_url: str, api_key: str, search_id: str) -> dict[str, Any]:
    """Fetches one search-results payload and validates the response shape."""

    result = rest_smoke.http_request(base_url, f"/api/v1/search/results?search_id={search_id}", api_key=api_key)
    return require_json_object(result, 200)


def find_transfer_candidate(
    base_url: str,
    api_key: str,
    *,
    query: str,
    method_candidates: list[str],
    observation_timeout_seconds: float,
) -> dict[str, object]:
    """Starts live searches until one query/method pair yields a downloadable result."""

    attempts: list[dict[str, object]] = []
    for method in method_candidates:
        search = rest_smoke.start_live_search(base_url, api_key, method, query, forced_method=method)
        attempt: dict[str, object] = {
            "query": query,
            "method": method,
            "start": search,
        }
        attempts.append(attempt)
        if not bool(search["ok"]):
            continue

        search_id = str(require_json_object(search["response"], 200)["search_id"])
        attempt["search_id"] = search_id
        try:
            attempt["activity"] = rest_smoke.wait_for_search_observation(
                base_url,
                api_key,
                search_id,
                observation_timeout_seconds,
            )
            payload = fetch_search_results(base_url, api_key, search_id)
            rows = payload.get("results")
            if not isinstance(rows, list) or not rows:
                continue
            for row in rows:
                if not isinstance(row, dict):
                    continue
                if row.get("hash") and row.get("name") and row.get("size") and is_safe_download_result(row):
                    attempt["result"] = row
                    return {
                        "selected": attempt,
                        "attempts": attempts,
                    }
        finally:
            attempt["stop"] = compact_http_result(rest_smoke.stop_live_search(base_url, api_key, search_id))

    raise RuntimeError(f"No downloadable search result found for {query!r} via {method_candidates!r}.")


def build_transfer_acquisition_plan() -> list[tuple[str, list[str]]]:
    """Returns the live-search plan used to find one safe sourced transfer."""

    plan = [(BOOTSTRAP_TRANSFER_HASH, list(BOOTSTRAP_SEARCH_METHODS))]
    plan.extend((query, list(FALLBACK_SEARCH_METHODS)) for query in FALLBACK_SEARCH_QUERIES)
    return plan


def add_transfer_from_search_result(base_url: str, api_key: str, result_row: dict[str, Any]) -> dict[str, Any]:
    """Adds one transfer from a REST search-result row and returns the created transfer payload."""

    add_result = rest_smoke.http_request(
        base_url,
        "/api/v1/transfers/add",
        method="POST",
        api_key=api_key,
        json_body={"link": build_ed2k_link(result_row)},
    )
    return require_json_object(add_result, 200)


def wait_for_transfer_sources(
    base_url: str,
    api_key: str,
    transfer_hash: str,
    timeout_seconds: float,
) -> dict[str, object]:
    """Waits until one transfer reports live remote sources."""

    deadline = time.time() + timeout_seconds
    observations: list[dict[str, object]] = []
    while time.time() < deadline:
        result = rest_smoke.http_request(base_url, f"/api/v1/transfers/{transfer_hash}/sources", api_key=api_key)
        assert int(result["status"]) == 200, compact_http_result(result)
        payload = result.get("json")
        assert isinstance(payload, list), compact_http_result(result)
        snapshot = {
            "observed_at": round(time.time(), 3),
            "source_count": len(payload),
        }
        observations.append(snapshot)
        if payload:
            return {
                "observations": observations,
                "sources": payload,
            }
        time.sleep(5.0)

    raise RuntimeError(f"Timed out waiting for sources on transfer '{transfer_hash}'.")


def wait_for_auto_browse_success(
    base_url: str,
    api_key: str,
    cache_dir: Path,
    timeout_seconds: float,
) -> dict[str, object]:
    """Waits for one real auto-browse session to log success and persist cache files."""

    deadline = time.time() + timeout_seconds
    observations: list[dict[str, object]] = []
    while time.time() < deadline:
        result = rest_smoke.http_request(base_url, "/api/v1/log?limit=400", api_key=api_key)
        assert int(result["status"]) == 200, compact_http_result(result)
        payload = result.get("json")
        assert isinstance(payload, list), compact_http_result(result)

        messages = [
            entry.get("message")
            for entry in payload
            if isinstance(entry, dict) and isinstance(entry.get("message"), str)
        ]
        auto_lines = [message for message in messages if message.startswith("Auto-browsing shared files from ")]
        success_lines = [message for message in messages if message.startswith("Cached ") and " shared files from " in message]
        cache_files = sorted(path.name for path in cache_dir.glob("*.browsecache"))

        observations.append(
            {
                "observed_at": round(time.time(), 3),
                "auto_count": len(auto_lines),
                "success_count": len(success_lines),
                "cache_files": cache_files,
            }
        )

        if auto_lines and success_lines and cache_files:
            return {
                "observations": observations,
                "auto_lines": auto_lines[-10:],
                "success_lines": success_lines[-10:],
                "cache_files": cache_files,
            }

        time.sleep(10.0)

    raise RuntimeError(
        "Timed out waiting for a successful auto-browse session. "
        f"Last observations: {observations[-10:]!r}"
    )


def try_wait_for_auto_browse_success(
    base_url: str,
    api_key: str,
    cache_dir: Path,
    timeout_seconds: float,
) -> dict[str, object]:
    """Attempts to wait for one automatic browse success and reports timeout state without raising."""

    try:
        success = wait_for_auto_browse_success(base_url, api_key, cache_dir, timeout_seconds)
        return {
            "ok": True,
            "success": success,
        }
    except Exception as exc:
        return {
            "ok": False,
            "error": {
                "type": type(exc).__name__,
                "message": str(exc),
            },
        }


def wait_for_process_id(app: object) -> int | None:
    """Returns the launched process id when pywinauto exposes it for the live app."""

    process_id = getattr(app, "process", None)
    if callable(process_id):
        try:
            process_id = process_id()
        except TypeError:
            process_id = None
    return process_id if isinstance(process_id, int) else None


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--workspace-root")
    parser.add_argument("--app-root")
    parser.add_argument("--app-exe")
    parser.add_argument("--seed-config-dir")
    parser.add_argument("--artifacts-dir")
    parser.add_argument("--keep-artifacts", action="store_true")
    parser.add_argument("--keep-running", action="store_true")
    parser.add_argument("--configuration", choices=["Debug", "Release"], default="Release")
    parser.add_argument("--api-key", default="auto-browse-live-key")
    parser.add_argument("--web-bind-addr", default="127.0.0.1")
    parser.add_argument("--p2p-bind-interface-name", default="hide.me")
    parser.add_argument("--rest-ready-timeout-seconds", type=float, default=60.0)
    parser.add_argument("--server-connect-timeout-seconds", type=float, default=540.0)
    parser.add_argument("--kad-running-timeout-seconds", type=float, default=120.0)
    parser.add_argument("--source-discovery-timeout-seconds", type=float, default=600.0)
    parser.add_argument("--search-observation-timeout-seconds", type=float, default=120.0)
    parser.add_argument("--auto-browse-timeout-seconds", type=float, default=1800.0)
    parser.add_argument("--natural-auto-browse-timeout-seconds", type=float, default=900.0)
    parser.add_argument("--seed-download-timeout-seconds", type=float, default=30.0)
    parser.add_argument("--skip-live-seed-refresh", action="store_true")
    args = parser.parse_args()

    paths = harness_cli_common.prepare_run_paths(
        script_file=__file__,
        suite_name="auto-browse-live",
        configuration=args.configuration,
        workspace_root=args.workspace_root,
        app_root=args.app_root,
        app_exe=args.app_exe,
        artifacts_dir=args.artifacts_dir,
        keep_artifacts=(args.keep_artifacts or args.keep_running),
    )

    seed_config_dir = Path(args.seed_config_dir).resolve() if args.seed_config_dir else paths.seed_config_dir
    artifacts_dir = paths.source_artifacts_dir
    port = rest_smoke.choose_listen_port()
    base_url = f"http://127.0.0.1:{port}"
    bind_updater_script = get_tooling_bind_updater(paths)

    profile = prepare_profile_base(seed_config_dir, artifacts_dir, shared_dirs=[])
    seed_refresh = None
    if not args.skip_live_seed_refresh:
        seed_refresh = rest_smoke.refresh_seed_files(
            Path(profile["config_dir"]),
            timeout_seconds=args.seed_download_timeout_seconds,
        )
    configure_auto_browse_profile(
        config_dir=Path(profile["config_dir"]),
        app_exe=paths.app_exe,
        api_key=args.api_key,
        port=port,
        web_bind_addr=args.web_bind_addr,
    )
    rest_smoke.apply_p2p_bindaddr_override(
        config_dir=Path(profile["config_dir"]),
        interface_name=args.p2p_bind_interface_name,
        bind_updater_script=bind_updater_script,
    )

    app = None
    pending_error: Exception | None = None
    report: dict[str, object] = {
        "suite": "auto-browse-live",
        "status": "failed",
        "base_url": base_url,
        "port": port,
        "launch_inputs": {
            "app_exe": str(paths.app_exe),
            "seed_config_dir": str(seed_config_dir),
            "live_seed_source_url": rest_smoke.EMULE_SECURITY_HOME_URL,
            "live_seed_refresh": seed_refresh,
            "artifacts_dir": str(artifacts_dir),
            "profile_base": str(profile["profile_base"]),
            "config_dir": str(profile["config_dir"]),
            "api_key_length": len(args.api_key),
            "web_bind_addr": args.web_bind_addr,
            "p2p_bind_interface_name": args.p2p_bind_interface_name,
            "bind_updater_script": str(bind_updater_script),
            "enable_upnp": True,
            "autoconnect_via_preferences": True,
            "bootstrap_transfer_hash": BOOTSTRAP_TRANSFER_HASH,
            "reject_exe_results": True,
            "keep_running": bool(args.keep_running),
            "timeouts": {
                "rest_ready_seconds": args.rest_ready_timeout_seconds,
                "server_connect_seconds": args.server_connect_timeout_seconds,
                "kad_running_seconds": args.kad_running_timeout_seconds,
                "search_observation_seconds": args.search_observation_timeout_seconds,
                "source_discovery_seconds": args.source_discovery_timeout_seconds,
                "natural_auto_browse_seconds": args.natural_auto_browse_timeout_seconds,
                "auto_browse_seconds": args.auto_browse_timeout_seconds,
                "seed_download_seconds": args.seed_download_timeout_seconds,
            },
        },
        "checks": {},
        "cleanup": {},
    }

    try:
        app = launch_app(paths.app_exe, Path(profile["profile_base"]))
        report["launched_process_id"] = wait_for_process_id(app)
        report["main_window_title"] = wait_for_main_window(app).window_text()

        ready = rest_smoke.wait_for_rest_ready(base_url, args.api_key, args.rest_ready_timeout_seconds)
        report["checks"]["ready"] = compact_http_result(ready)

        no_key = rest_smoke.http_request(base_url, "/api/v1/app/version")
        wrong_key = rest_smoke.http_request(base_url, "/api/v1/app/version", api_key="wrong-key")
        assert int(no_key["status"]) == 401
        assert int(wrong_key["status"]) == 401
        report["checks"]["auth"] = {
            "missing": compact_http_result(no_key),
            "wrong": compact_http_result(wrong_key),
        }

        servers = rest_smoke.http_request(base_url, "/api/v1/servers/list", api_key=args.api_key)
        assert int(servers["status"]) == 200, compact_http_result(servers)
        server_rows = servers.get("json")
        assert isinstance(server_rows, list) and server_rows, compact_http_result(servers)
        ordered_server_rows = reorder_server_rows(list(server_rows))
        report["checks"]["servers_list"] = {
            "count": len(ordered_server_rows),
            "preferred_candidates": ordered_server_rows[:5],
        }

        network_ready = rest_smoke.wait_for_requested_networks(
            base_url,
            args.api_key,
            args.server_connect_timeout_seconds,
            require_server_connected=True,
            require_kad_connected=True,
        )
        report["checks"]["autoconnect_mode"] = {
            "source": "preferences",
            "server_candidates": ordered_server_rows[:5],
        }
        report["checks"]["network_ready"] = network_ready

        cache_dir = Path(profile["config_dir"]) / "RemoteBrowseCache"
        natural_auto_browse = try_wait_for_auto_browse_success(
            base_url,
            args.api_key,
            cache_dir,
            args.natural_auto_browse_timeout_seconds,
        )
        report["checks"]["natural_auto_browse"] = natural_auto_browse

        if bool(natural_auto_browse.get("ok")):
            report["checks"]["auto_browse"] = natural_auto_browse["success"]
            report["checks"]["bootstrap_strategy"] = {
                "mode": "natural-clients-only",
            }
        else:
            acquisition_attempts: list[dict[str, object]] = []
            for query, methods in build_transfer_acquisition_plan():
                try:
                    selection = find_transfer_candidate(
                        base_url,
                        args.api_key,
                        query=query,
                        method_candidates=methods,
                        observation_timeout_seconds=args.search_observation_timeout_seconds,
                    )
                    selected_attempt = selection["selected"]
                    assert isinstance(selected_attempt, dict)
                    result_row = selected_attempt["result"]
                    assert isinstance(result_row, dict)
                    add_payload = add_transfer_from_search_result(base_url, args.api_key, result_row)
                    selected_attempt["add_response"] = add_payload
                    selected_attempt["sources_ready"] = wait_for_transfer_sources(
                        base_url,
                        args.api_key,
                        str(add_payload["hash"]),
                        args.source_discovery_timeout_seconds,
                    )
                    acquisition_attempts.append(selection)
                    break
                except Exception as exc:
                    acquisition_attempts.append(
                        {
                            "query": query,
                            "methods": methods,
                            "error": {
                                "type": type(exc).__name__,
                                "message": str(exc),
                            },
                        }
                    )
            report["checks"]["transfer_acquisition"] = acquisition_attempts
            if not any(isinstance(item, dict) and item.get("selected") for item in acquisition_attempts):
                report["checks"]["live_source_availability"] = {
                    "status": "unavailable",
                    "queries_attempted": [query for query, _methods in build_transfer_acquisition_plan()],
                    "blocking": False,
                }
                raise LiveSourceUnavailableError(
                    "Live networks connected, but no safe downloadable sourced transfer was available "
                    "from the configured live-search plan."
                )

            remaining_timeout = max(1.0, args.auto_browse_timeout_seconds - args.natural_auto_browse_timeout_seconds)
            report["checks"]["auto_browse"] = wait_for_auto_browse_success(
                base_url,
                args.api_key,
                cache_dir,
                remaining_timeout,
            )
            report["checks"]["bootstrap_strategy"] = {
                "mode": "fallback-transfer-bootstrap",
            }
        report["status"] = "passed"
    except Exception as exc:
        if isinstance(exc, LiveSourceUnavailableError):
            report["status"] = "inconclusive"
            report["inconclusive_reason"] = {
                "type": type(exc).__name__,
                "message": str(exc),
            }
        else:
            pending_error = exc
            report["status"] = "failed"
            report["error"] = {
                "type": type(exc).__name__,
                "message": str(exc),
            }
    finally:
        cleanup = report["cleanup"]
        assert isinstance(cleanup, dict)
        if app is not None:
            cleanup["process_id"] = wait_for_process_id(app)
            cleanup["profile_base"] = str(profile["profile_base"])
            if args.keep_running and report["status"] == "passed":
                cleanup["app_closed"] = False
                cleanup["app_left_running"] = True
            else:
                try:
                    close_app_cleanly(app)
                    cleanup["app_closed"] = True
                    cleanup["app_left_running"] = False
                except Exception as exc:
                    cleanup["app_closed"] = False
                    cleanup["app_close_error"] = repr(exc)
                    if pending_error is None:
                        pending_error = exc
                        report["status"] = "failed"
                        report["error"] = {
                            "type": type(exc).__name__,
                            "message": str(exc),
                        }

        write_json(artifacts_dir / "result.json", report)
        harness_cli_common.publish_run_artifacts(paths)
        harness_cli_common.publish_latest_report(paths)
        harness_cli_common.cleanup_source_artifacts(paths)

    if pending_error is not None:
        raise pending_error

    if report["status"] == "inconclusive":
        print(
            "Auto-browse live validation was inconclusive because no safe sourced transfer was available. "
            f"Report directory: {paths.run_report_dir}"
        )
        return LIVE_SOURCE_UNAVAILABLE_EXIT_CODE

    if args.keep_running:
        print(
            "Auto-browse live validation passed and left the app running. "
            f"Report directory: {paths.run_report_dir} "
            f"(PID: {report.get('launched_process_id')}, profile: {profile['profile_base']})"
        )
    else:
        print(f"Auto-browse live validation passed. Report directory: {paths.run_report_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
