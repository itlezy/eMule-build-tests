"""Operator-facing aggregate live UI, REST, and live-wire E2E runner."""

from __future__ import annotations

import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

from emule_test_harness import live_e2e_suite

import harness_cli_common


def main() -> int:
    """Runs the aggregate live E2E suite and returns a process exit code."""

    parser = live_e2e_suite.build_parser()
    args = parser.parse_args()
    summary = live_e2e_suite.run_live_e2e_suite(args, harness_cli_common)
    return 0 if summary["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
