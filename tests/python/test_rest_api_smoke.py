from __future__ import annotations

import importlib.util
from pathlib import Path

import pytest


def load_rest_api_smoke_module():
    """Loads the hyphenated REST smoke script for focused unit tests."""

    script_path = Path(__file__).resolve().parents[2] / "scripts" / "rest-api-smoke.py"
    spec = importlib.util.spec_from_file_location("rest_api_smoke_for_tests", script_path)
    assert spec is not None
    assert spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_nat_backend_order_accepts_upnp_first() -> None:
    module = load_rest_api_smoke_module()

    summary = module.assert_upnp_backend_order(
        [
            {"message": "NAT mapping backend mode: Automatic"},
            {"message": "Attempting NAT mapping backend 'UPnP IGD (MiniUPnP)'"},
            {"message": "Trying fallback NAT mapping backend 'PCP/NAT-PMP'"},
            {"message": "Attempting NAT mapping backend 'PCP/NAT-PMP'"},
        ]
    )

    assert summary["backend_names"] == ["UPnP IGD (MiniUPnP)", "PCP/NAT-PMP"]
    assert summary["upnp_first"] is True
    assert summary["pcp_before_upnp"] is False


def test_nat_backend_order_rejects_pcp_first() -> None:
    module = load_rest_api_smoke_module()

    with pytest.raises(AssertionError, match="Expected first NAT backend"):
        module.assert_upnp_backend_order(
            [
                {"message": "Attempting NAT mapping backend 'PCP/NAT-PMP'"},
                {"message": "Attempting NAT mapping backend 'UPnP IGD (MiniUPnP)'"},
            ]
        )


def test_nat_backend_order_requires_attempts() -> None:
    module = load_rest_api_smoke_module()

    with pytest.raises(AssertionError, match="No NAT mapping backend attempts"):
        module.assert_upnp_backend_order([{"message": "eMule Version 0.72a x64 ready"}])


def test_live_server_unavailable_is_inconclusive_exit_code() -> None:
    module = load_rest_api_smoke_module()

    assert module.LIVE_NETWORK_UNAVAILABLE_EXIT_CODE == 2
    with pytest.raises(module.LiveNetworkUnavailableError, match="No server candidates"):
        module.connect_to_live_server("http://127.0.0.1:1", "api-key", [], 1.0)


def test_missing_transfer_bulk_result_requires_per_item_error() -> None:
    module = load_rest_api_smoke_module()

    result = module.require_missing_transfer_bulk_result(
        {
            "status": 200,
            "json": {
                "results": [
                    {
                        "hash": module.REST_SURFACE_MISSING_HASH,
                        "ok": False,
                        "error": "transfer not found",
                    },
                ],
            },
        }
    )

    assert result["hash"] == module.REST_SURFACE_MISSING_HASH


def test_missing_transfer_bulk_result_rejects_success_rows() -> None:
    module = load_rest_api_smoke_module()

    with pytest.raises(AssertionError):
        module.require_missing_transfer_bulk_result(
            {
                "status": 200,
                "json": {
                    "results": [
                        {
                            "hash": module.REST_SURFACE_MISSING_HASH,
                            "ok": True,
                        },
                    ],
                },
            }
        )
