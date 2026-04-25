from __future__ import annotations

import importlib.util
import sys
from pathlib import Path


def load_harness_cli_common_module():
    """Loads the hyphenated harness CLI helper for focused unit tests."""

    script_path = Path(__file__).resolve().parents[2] / "scripts" / "harness-cli-common.py"
    spec = importlib.util.spec_from_file_location("harness_cli_common_for_tests", script_path)
    assert spec is not None
    assert spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules["harness_cli_common_for_tests"] = module
    spec.loader.exec_module(module)
    return module


def test_publish_directory_snapshot_skips_generated_shared_hash_payloads(tmp_path: Path) -> None:
    module = load_harness_cli_common_module()
    source = tmp_path / "source"
    destination = tmp_path / "destination"
    scenario = source / "scenario"
    payload_dir = scenario / "shared-hash-root" / "branch"
    payload_dir.mkdir(parents=True)
    (payload_dir / "large-payload.bin").write_bytes(b"x" * 1024)
    (scenario / "result.json").write_text("{}", encoding="utf-8")
    (source / "suite-result.json").write_text("{}", encoding="utf-8")

    module.publish_directory_snapshot(source, destination)

    assert (destination / "suite-result.json").is_file()
    assert (destination / "scenario" / "result.json").is_file()
    assert not (destination / "scenario" / "shared-hash-root").exists()
