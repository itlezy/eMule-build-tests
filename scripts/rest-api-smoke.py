"""Runs a lightweight live smoke suite against the in-process eMule REST API."""

from __future__ import annotations

import argparse
import hashlib
import json
import socket
import time
import urllib.error
import urllib.request
from pathlib import Path

from emule_live_profile_common import (
    close_app_cleanly,
    launch_app,
    patch_ini_value,
    prepare_profile_base,
    wait_for,
    wait_for_main_window,
    write_json,
)


def compute_emule_md5(text: str) -> str:
    """Matches eMule's CString-based MD5 hashing for Web API keys."""

    return hashlib.md5(text.encode("utf-16le")).hexdigest().upper()


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


def configure_webserver_profile(config_dir: Path, app_exe: Path, api_key: str, port: int) -> None:
    """Enables the WebServer listener and REST API key inside the temp profile."""

    preferences_path = config_dir / "preferences.ini"
    text = preferences_path.read_text(encoding="utf-8", errors="ignore")
    text = patch_ini_value(text, "ConfirmExit", "0")
    template_path = app_exe.parent.parent.parent / "webinterface" / "eMule.tmpl"
    text = patch_ini_value(text, "WebTemplateFile", str(template_path))
    for key, value in (
        ("Password", ""),
        ("PasswordLow", ""),
        ("ApiKey", compute_emule_md5(api_key)),
        ("BindAddr", "127.0.0.1"),
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
    preferences_path.write_text(text, encoding="utf-8", newline="\r\n")


def http_request(base_url: str, path: str, *, method: str = "GET", api_key: str | None = None, json_body=None) -> dict[str, object]:
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
        with urllib.request.urlopen(request, timeout=5.0) as response:
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


def wait_for_rest_ready(base_url: str, api_key: str) -> dict[str, object]:
    """Polls until the live REST listener answers the version route."""

    def resolve():
        try:
            result = http_request(base_url, "/api/v1/app/version", api_key=api_key)
        except OSError:
            return None
        if int(result["status"]) != 200:
            return None
        return result

    return wait_for(resolve, timeout=45.0, interval=0.5, description="REST API readiness")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--app-exe", required=True)
    parser.add_argument("--seed-config-dir", required=True)
    parser.add_argument("--artifacts-dir", required=True)
    parser.add_argument("--api-key", default="rest-smoke-test-key")
    args = parser.parse_args()

    app_exe = Path(args.app_exe).resolve()
    seed_config_dir = Path(args.seed_config_dir).resolve()
    artifacts_dir = Path(args.artifacts_dir).resolve()
    artifacts_dir.mkdir(parents=True, exist_ok=True)

    port = choose_listen_port()
    base_url = f"http://127.0.0.1:{port}"
    profile = prepare_profile_base(seed_config_dir, artifacts_dir, shared_dirs=[])
    configure_webserver_profile(Path(profile["config_dir"]), app_exe, args.api_key, port)

    app = None
    report = {
        "base_url": base_url,
        "port": port,
        "checks": {},
        "status": "failed",
    }

    try:
        app = launch_app(app_exe, Path(profile["profile_base"]))
        main_window = wait_for_main_window(app)
        report["main_window_title"] = main_window.window_text()

        ready = wait_for_rest_ready(base_url, args.api_key)
        report["checks"]["ready"] = ready

        no_key = http_request(base_url, "/api/v1/app/version")
        assert no_key["status"] == 401
        assert isinstance(no_key["json"], dict)
        assert no_key["json"]["error"] == "UNAUTHORIZED"
        report["checks"]["missing_key"] = no_key

        wrong_key = http_request(base_url, "/api/v1/app/version", api_key="wrong-key")
        assert wrong_key["status"] == 401
        assert isinstance(wrong_key["json"], dict)
        assert wrong_key["json"]["error"] == "UNAUTHORIZED"
        report["checks"]["wrong_key"] = wrong_key

        version = http_request(base_url, "/api/v1/app/version", api_key=args.api_key)
        assert version["status"] == 200
        assert isinstance(version["json"], dict)
        assert version["json"]["appName"] == "eMule"
        assert "version" in version["json"]
        report["checks"]["app_version"] = version

        stats = http_request(base_url, "/api/v1/stats/global", api_key=args.api_key)
        assert stats["status"] == 200
        assert isinstance(stats["json"], dict)
        assert "connected" in stats["json"]
        report["checks"]["stats_global"] = stats

        log_entries = http_request(base_url, "/api/v1/log?limit=1", api_key=args.api_key)
        assert log_entries["status"] == 200
        assert isinstance(log_entries["json"], list)
        assert len(log_entries["json"]) <= 1
        report["checks"]["log_limit"] = log_entries

        kad_disconnect = http_request(base_url, "/api/v1/kad/disconnect", method="POST", api_key=args.api_key, json_body={})
        assert kad_disconnect["status"] == 200
        assert isinstance(kad_disconnect["json"], dict)
        assert "running" in kad_disconnect["json"]
        report["checks"]["kad_disconnect"] = kad_disconnect

        html_root = http_request(base_url, "/")
        assert html_root["status"] == 200
        assert "text/html" in str(html_root["content_type"]).lower()
        assert "<html" in str(html_root["body_text"]).lower()
        report["checks"]["html_root"] = {
            "status": html_root["status"],
            "content_type": html_root["content_type"],
            "body_preview": str(html_root["body_text"])[:200],
        }

        report["status"] = "passed"
    finally:
        write_json(artifacts_dir / "result.json", report)
        if app is not None:
            close_app_cleanly(app)


if __name__ == "__main__":
    main()
