from __future__ import annotations

import importlib.util
import sys
from pathlib import Path


def load_auto_browse_module():
    """Loads the hyphenated auto-browse script for focused unit tests."""

    script_path = Path(__file__).resolve().parents[2] / "scripts" / "auto-browse-live.py"
    spec = importlib.util.spec_from_file_location("auto_browse_live_for_tests", script_path)
    assert spec is not None
    assert spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules["auto_browse_live_for_tests"] = module
    spec.loader.exec_module(module)
    return module


def test_transfer_acquisition_plan_covers_bootstrap_and_public_queries() -> None:
    module = load_auto_browse_module()

    plan = module.build_transfer_acquisition_plan()

    assert plan[0] == (module.BOOTSTRAP_TRANSFER_HASH, list(module.BOOTSTRAP_SEARCH_METHODS))
    assert [query for query, _methods in plan[1:]] == list(module.FALLBACK_SEARCH_QUERIES)
    assert all("server" in methods and "kad" in methods for _query, methods in plan)
    assert module.LIVE_SOURCE_UNAVAILABLE_EXIT_CODE == 2
    assert module.DEFAULT_NATURAL_AUTO_BROWSE_TIMEOUT_SECONDS < module.DEFAULT_FALLBACK_AUTO_BROWSE_TIMEOUT_SECONDS


def test_selected_transfer_source_summary_is_compact() -> None:
    module = load_auto_browse_module()
    sources = [
        {"userHash": "a"},
        {"userHash": "b"},
        {"userHash": "c"},
        {"userHash": "d"},
        {"userHash": "e"},
        {"userHash": "f"},
    ]

    summary = module.summarize_selected_transfer_sources(
        [
            {"query": "ubuntu", "error": {"type": "RuntimeError"}},
            {
                "selected": {
                    "query": "linux",
                    "method": "automatic",
                    "search_id": "6",
                    "result": {
                        "hash": "b05c1075089e1de58a13de1b77ba4b2a",
                        "name": "linux.tar.gz",
                        "size": 68082727,
                    },
                    "sources_ready": {
                        "sources": sources
                    },
                }
            },
        ]
    )

    assert summary["query"] == "linux"
    assert summary["method"] == "automatic"
    assert summary["source_count"] == 6
    assert len(summary["sources"]) == 5
    assert summary["hash"] == "b05c1075089e1de58a13de1b77ba4b2a"


def test_selected_transfer_sources_returns_full_source_list() -> None:
    module = load_auto_browse_module()
    sources = [{"userHash": "a"}, {"userHash": "b"}, {"userHash": "c"}, {"userHash": "d"}, {"userHash": "e"}, {"userHash": "f"}]

    selected_sources = module.get_selected_transfer_sources([{"selected": {"sources_ready": {"sources": sources}}}])

    assert selected_sources == sources


def test_source_browse_candidates_prefer_reachable_emule_sources() -> None:
    module = load_auto_browse_module()

    candidates = module.iter_source_browse_candidates(
        [
            {"userHash": "0" * 32, "ip": "", "port": 0},
            {
                "userHash": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                "ip": "",
                "port": 4662,
                "lowId": True,
                "clientSoftware": "",
            },
            {
                "userHash": "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
                "ip": "1.2.3.4",
                "port": 4662,
                "lowId": False,
                "clientSoftware": "eMule v0.70b",
            },
            {
                "userHash": "cccccccccccccccccccccccccccccccc",
                "ip": "1.2.3.5",
                "port": 4662,
                "viewSharedFiles": False,
            },
        ]
    )

    assert len(candidates) == 2
    assert candidates[0]["selector"]["userHash"] == "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
    assert candidates[0]["selector"]["ip"] == "1.2.3.4"


def test_record_phase_writes_partial_report(tmp_path: Path, capsys) -> None:
    module = load_auto_browse_module()
    report: dict[str, object] = {"status": "failed"}

    module.record_phase(tmp_path, report, "network_ready")

    assert report["current_phase"] == "network_ready"
    assert report["phase_history"] and report["phase_history"][0]["phase"] == "network_ready"
    assert (tmp_path / "result.partial.json").is_file()
    assert "auto-browse-live phase: network_ready" in capsys.readouterr().out


def test_record_auto_browse_observation_keeps_recent_partial_state(tmp_path: Path) -> None:
    module = load_auto_browse_module()
    report: dict[str, object] = {"checks": {}}

    for index in range(25):
        module.record_auto_browse_observation(
            tmp_path,
            report,
            "natural_auto_browse_progress",
            {
                "observed_at": float(index),
                "auto_count": 0,
                "success_count": 0,
                "cache_files": [],
            },
        )

    progress = report["checks"]["natural_auto_browse_progress"]
    assert len(progress["observations"]) == 20
    assert progress["observations"][0]["observed_at"] == 5.0
    assert progress["last_observation"]["observed_at"] == 24.0
    assert (tmp_path / "result.partial.json").is_file()
