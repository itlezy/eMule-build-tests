# eMule Shared Tests

This repository is the shared test harness for the canonical 0.72a workspace rooted at `EMULE_WORKSPACE_ROOT`.

Expected canonical layout:

- `EMULE_WORKSPACE_ROOT\repos\eMule-build`
- `EMULE_WORKSPACE_ROOT\repos\eMule-build-tests`
- `EMULE_WORKSPACE_ROOT\repos\eMule-remote`
- `EMULE_WORKSPACE_ROOT\workspaces\v0.72a`

It owns:

- the standalone `emule-tests.vcxproj` project
- shared doctest sources and support headers
- parity and divergence suites for live workspace-to-workspace comparison
- workspace-level build and live-diff scripts
- fixture, manifest, and report directories for future protocol coverage
- the deterministic live-profile seed used by the named-pipe live harness

The project is built against the canonical app checkout resolved from the invoking workspace manifest. It is intentionally not a runtime dependency like the `eMule-*` third-party dependencies, and it is no longer embedded as a `tests/` submodule inside each workspace.

The harness also supports an explicit `-AppRoot` override when validating a cleanroom rebuild before promotion.

Supported branch:

- `main`

Oracle workspaces may be edited when the change is strictly to enable tests, seams, logging, tracing, or debugging. They are not feature-development branches.

Current suite model:

- `parity`: cases that must pass in both selected workspaces
- `divergence`: cases that are expected to differ between two explicitly chosen workspace roots

Workspace quick reference:

- default canonical workspace: `EMULE_WORKSPACE_ROOT\workspaces\v0.72a`
- canonical target app paths are `app\eMule-main`, `app\eMule-v0.72a-build`, and `app\eMule-v0.72a-bugfix`
- for live-diff runs, point `-DevWorkspaceRoot` and `-OracleWorkspaceRoot` at the two workspace roots you want to compare
- for cleanroom validation, pass both `-WorkspaceRoot` and `-AppRoot` explicitly so reports and build tags stay tied to the selected workspace root

Standalone probe mode:

- `build\<tag>\x64\Debug\emule-tests.exe --hash-probe "<full file path>"` runs an isolated non-UI file scan
- by default it executes a buffered scan first and then the shared `MappedFileReader` path
- use `--reader buffered`, `--reader mapped`, or `--reader both` to narrow the probe
- use `--byte-limit <N>` to cap the scan length and `--progress-mib <N>` to control progress output
- `build\<tag>\x64\Debug\emule-tests.exe --full-hash-probe "<full file path>"` runs the offline MD4 plus AICH hashing pipeline without launching `emule.exe`
- the full-hash mode also supports `--reader buffered|mapped|both` and `--progress-mib <N>`
- use the full-hash mode when you need to separate raw file access from higher-level `CKnownFile::CreateFromFile` work such as metadata extraction, known-file registration, or UI progress handling

Deterministic live-profile seed:

- `manifests\live-profile-seed\config` stores the canonical test-only profile inputs for live named-pipe runs
- the seed is intentionally minimal and currently vendors only `preferences.ini`, `nodes.dat`, and `server.met`
- `preferences.ini` intentionally contains only the minimal non-default seed values required by the live harness; the helper injects runtime-specific transport, logging, bind, and working-folder settings per run
- runtime working folders are copied from that seed and then expanded with per-run logs, temp files, and other mutable state

Canonical live harness:

- `scripts\run-pipe-live-matrix.ps1` is the operator-facing entrypoint for launch-only and full live named-pipe harness runs
- the harness stages a renamed binary copy, `eMule_v072_harness.exe`, beside the debug build output and launches that copy so processes, dumps, and cleanup are easier to identify
- the machine-readable session manifest can be requested through the shared wrapper with `-SessionManifestPath`
- normal runs retry targeted teardown and fail if any harness-launched process remains afterward; `-KeepRunning` is the only supported opt-out

Tracked-file privacy guard:

- `scripts\guard-tracked-files.ps1` fails when tracked files contain local user-home paths or personal-identifier filename leaks derived from the current environment or an untracked local override file
- `scripts\build-emule-tests.ps1` runs that guard by default before building
- the same guard is enforced in GitHub Actions for pushes and pull requests

Native seam coverage and shared reports:

- `scripts\run-native-coverage.ps1` builds `emule-tests.exe`, runs the requested doctest suites under OpenCppCoverage, and writes Cobertura plus summary outputs under `reports\native-coverage`
- `helpers\helper-opencppcoverage-bootstrap.ps1` prefers the shared install at `C:\tools\ocppcov` and falls back to a repo-managed pinned install under `tools\OpenCppCoverage`
- `scripts\run-live-diff.ps1` now writes both text and JSON parity/divergence summaries under `reports`
- `scripts\publish-harness-summary.ps1` combines native coverage, parity, and optional live-harness manifest data into one shared summary under `reports`
