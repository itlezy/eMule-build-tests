from __future__ import annotations

from pathlib import Path

from emule_test_harness.live_seed_sources import (
    EMULE_SECURITY_HOME_URL,
    default_seed_sources,
    refresh_seed_files,
)


def test_default_live_seed_sources_use_emule_security() -> None:
    sources = default_seed_sources()

    assert EMULE_SECURITY_HOME_URL == "https://emule-security.org/"
    assert {source.file_name for source in sources} == {"server.met", "nodes.dat"}
    assert all(source.url.startswith("https://upd.emule-security.org/") for source in sources)


def test_refresh_seed_files_writes_payloads_and_reports_hashes(tmp_path: Path) -> None:
    payloads = {
        "https://upd.emule-security.org/server.met": b"s" * 80,
        "https://upd.emule-security.org/nodes.dat": b"n" * 96,
    }

    def fake_fetch(url: str, _timeout_seconds: float) -> bytes:
        return payloads[url]

    summary = refresh_seed_files(tmp_path, fetch_bytes=fake_fetch)

    assert (tmp_path / "server.met").read_bytes() == b"s" * 80
    assert (tmp_path / "nodes.dat").read_bytes() == b"n" * 96
    assert summary["source_home_url"] == EMULE_SECURITY_HOME_URL
    files = summary["files"]
    assert isinstance(files, list)
    assert {entry["file_name"]: entry["bytes"] for entry in files} == {
        "server.met": 80,
        "nodes.dat": 96,
    }
