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
