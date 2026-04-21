# eMule Shared Tests

This repository is the shared test harness for the canonical 0.72a workspace rooted at `EMULE_WORKSPACE_ROOT`.

This repo assumes the canonical workspace created by `eMulebb-setup`.

Minimum expected roots:

- `EMULE_WORKSPACE_ROOT\repos\eMule-build`
- `EMULE_WORKSPACE_ROOT\repos\eMule-build-tests`
- `EMULE_WORKSPACE_ROOT\repos\eMule-remote`
- `EMULE_WORKSPACE_ROOT\workspaces\v0.72a`

Use `eMulebb-setup\README.md` for the full workspace topology and materialization
contract.

It owns:

- the standalone `emule-tests.vcxproj` project
- shared doctest sources and support headers
- parity and divergence suites for live workspace-to-workspace comparison
- workspace-level build and live-diff scripts
- fixture, manifest, and report directories for future protocol coverage
- the deterministic live-profile seed used by the live REST E2E lane, named-pipe live harness, and live UI regressions

The project is built against the canonical app checkout resolved from the invoking workspace manifest. It is intentionally not a runtime dependency like the `eMule-*` third-party dependencies, and it is no longer embedded as a `tests/` submodule inside each workspace.

The harness also supports an explicit `-AppRoot` override when validating a cleanroom rebuild before promotion.

Supported branch:

- `main`

Oracle workspaces may be edited when the change is strictly to enable tests, seams, logging, tracing, or debugging. They are not feature-development branches.

Current suite model:

- `parity`: cases that must pass in both selected workspaces
- `divergence`: cases that are expected to differ between two explicitly chosen workspace roots
- focused comparison suites may be added when a specific branch-to-branch audit needs a tighter signal than the repo-wide `divergence` bucket

Bugfix core comparison workflow:

- `scripts\run-bugfix-core-coverage.ps1` is the operator-facing wrapper for the canonical `main` vs `release/v0.72a-bugfix` comparison
- it runs native coverage for `app\eMule-main` with `parity` and `bugfix-core-divergence`
- it runs the focused `bugfix-core-divergence` suite for main-only queue-scoring and persistence behavior
- it runs native coverage for `app\eMule-v0.72a-bugfix` with `parity`
- it runs `scripts\run-live-diff.ps1` against those two app roots and keeps the suite-level pass/fail split explicit
- the wrapper writes a combined summary under `reports\bugfix-core-coverage`

Current critical comparison slices:

- upload queue entry access parity: `src\upload_queue.tests.cpp`
- upload queue/scoring divergence and FEAT-023 consumer helpers: `src\bugfix_core_divergence.tests.cpp`, `src\upload_score.tests.cpp`
- protocol receive replay parity with fragmented temp-file streams: `src\protocol_receive_flow.tests.cpp`
- long-path and part/met persistence IO: `src\long_path_fs_parity.tests.cpp`, `src\part_file_persistence.tests.cpp`
- core socket IO guards: `src\socket_io.tests.cpp`, `src\emsocket_send.tests.cpp`, `src\async_socket_ex.tests.cpp`

Script inventory:

- Python 3 is the canonical runtime for live/UI harnesses in this repo
- `python -m pip install -e .[dev]` installs the fast pytest harness dependencies
- `python -m pip install -e .[dev,live]` also installs the Win32 live/UI automation dependencies
- default pytest collection is intentionally fast and excludes `native` and `live` marked tests
- retained `*.ps1` scripts require `pwsh 7.6+` and are kept only for build/report/coverage flows or narrowly Windows-specific utilities
- canonical Python entrypoints are documented below; retained PowerShell utilities and targeted diagnostics are listed here so maintenance scope stays explicit

| Path | Role | Status | Notes |
| --- | --- | --- | --- |
| `scripts\build-emule-tests.ps1` | operator-facing build wrapper | maintained | builds `emule-tests.exe`, optional run |
| `scripts\guard-tracked-files.ps1` | operator-facing guard | maintained | privacy/path leak gate before builds |
| `scripts\run-native-coverage.ps1` | operator-facing coverage wrapper | maintained | OpenCppCoverage orchestration |
| `scripts\run_live_diff.py` | operator-facing Python parity runner | maintained | Python-first live-diff implementation |
| `scripts\run-live-diff.ps1` | obsolete compatibility shim | obsolete | retained for `workspace.ps1` and legacy callers; delegates to Python |
| `scripts\obsolete\run-live-diff.ps1` | obsolete historical implementation | obsolete | old PowerShell live-diff implementation, kept for audit only |
| `scripts\run-bugfix-core-coverage.ps1` | operator-facing comparison wrapper | maintained | canonical `main` vs `bugfix` pass |
| `scripts\run-pipe-live-matrix.ps1` | operator-facing live harness wrapper | maintained | resolves the current helper from `repos\eMule-tooling` first, legacy path second |
| `scripts\publish-harness-summary.py` | shared report publisher | maintained | combines coverage, parity, and optional live status |
| `helpers\helper-opencppcoverage-bootstrap.ps1` | internal helper | maintained | resolves OpenCppCoverage install |
| `scripts\resolve-app-root.ps1` | internal helper | maintained | canonical app-root resolution from workspace manifest |
| `scripts\resolve-workspace-layout.ps1` | internal helper | maintained | canonical workspace/repo root resolution |
| `scripts\harness-cli-common.py` | internal Python helper | maintained | canonical app/report resolution for Python-first live/UI harnesses |
| `scripts\emule-live-profile-common.py` | internal Python helper | maintained | shared live-profile launch and trace helpers |
| `scripts\rest-api-smoke.py` | operator-facing Python E2E | maintained | canonical isolated REST live E2E lane |
| `scripts\auto-browse-live.py` | operator-facing Python E2E | maintained | isolated live auto-browse validation with `hide.me` bind and P2P UPnP |
| `scripts\config-stability-ui-e2e.py` | operator-facing Python E2E | maintained | long `-c` config path, settings save, relaunch, and stability regression |
| `scripts\shared-files-ui-e2e.py` | operator-facing Python E2E | maintained | real Win32 Shared Files regression |
| `scripts\startup-profile-scenarios.py` | operator-facing Python E2E | maintained | Chrome Trace startup-profile scenarios |
| `scripts\create-long-paths-tree.py` | fixture generator | maintained | deterministic long-path tree materialization |
| `scripts\diag-hash-launch.ps1` | targeted diagnostic | maintained | seeded profile + procdump launcher for hash stall investigations |
| `scripts\parse-dump.py` | targeted diagnostic | maintained | parses `diag-hash` dumps, defaults to `diag-hash-latest` |
| `scripts\resolve-rva.py` | targeted diagnostic | maintained | resolves caller-provided RVAs against a selected debug build |

Workspace quick reference:

- default canonical workspace: `EMULE_WORKSPACE_ROOT\workspaces\v0.72a`
- canonical target app paths are `app\eMule-main`, `app\eMule-v0.72a-oracle`, `app\eMule-v0.72a-build`, and `app\eMule-v0.72a-bugfix`
- for live-diff runs, point `-DevWorkspaceRoot` and `-OracleWorkspaceRoot` at the two workspace roots you want to compare
- for cleanroom validation, pass both `-WorkspaceRoot` and `-AppRoot` explicitly so reports and build tags stay tied to the selected workspace root

The sanctioned seam-enabled oracle baseline for 0.72a comparisons is
`oracle/v0.72a-build`, materialized as `app\eMule-v0.72a-oracle`. It should
stay behavior-preserving relative to `release/v0.72a-build` during normal app
execution and may accept only test-enablement changes.

Standalone probe mode:

- `build\<tag>\x64\Debug\emule-tests.exe --hash-probe "<full file path>"` runs an isolated non-UI file scan
- by default it executes a buffered scan first and then the shared `MappedFileReader` path
- use `--reader buffered`, `--reader mapped`, or `--reader both` to narrow the probe
- use `--byte-limit <N>` to cap the scan length and `--progress-mib <N>` to control progress output
- `build\<tag>\x64\Debug\emule-tests.exe --full-hash-probe "<full file path>"` runs the offline MD4 plus AICH hashing pipeline without launching `emule.exe`
- the full-hash mode also supports `--reader buffered|mapped|both` and `--progress-mib <N>`
- use the full-hash mode when you need to separate raw file access from higher-level `CKnownFile::CreateFromFile` work such as metadata extraction, known-file registration, or UI progress handling

Deterministic live-profile seed:

- `manifests\live-profile-seed\config` stores the canonical test-only profile inputs for live REST E2E and live named-pipe runs
- the seed is intentionally minimal and vendors only the config files the live harness truly depends on: `preferences.ini`, `preferences.dat`, `nodes.dat`, and `server.met`
- `preferences.ini` is an initialized profile seed; it must already carry the startup-silencing keys needed to avoid first-run UI such as the language prompt and runtime wizard
- `preferences.dat` carries the deterministic maximized main-window placement used by the live UI and startup-profile harnesses
- the helper injects only runtime-specific transport, logging, bind, temp, working-folder, and shared-directory settings per run
- runtime working folders are copied from that seed and then expanded with per-run logs, temp files, and other mutable state

Canonical live REST E2E lane:

- `scripts\rest-api-smoke.py` is the operator-facing entrypoint for the canonical isolated REST live E2E lane
- the Python runner is intentionally strict pass/fail and owns app resolution, report publication, and latest-report mirroring directly
- the lane launches `emule.exe` with explicit `-ignoreinstances -c <profile-base>` and enables WebServer REST against one per-run localhost port
- the lane requires real server-connect activity, Kad running state, network readiness, and one real live search lifecycle through the first usable network path
- `-ServerSearchCount <N>` and `-KadSearchCount <N>` upgrade the run into one stricter mixed-network scenario with exact per-network live search counts
- `-KeepRunning` leaves the launched isolated eMule instance alive after a passing run and forces artifact retention so the profile can be inspected afterward
- failure artifacts include the failing phase plus the last observed server/Kad state so live-network regressions are diagnosable

Canonical live auto-browse lane:

- `scripts\auto-browse-live.py` is the operator-facing entrypoint for the isolated remote-share auto-browse validation
- the scenario enables `AutoBrowseRemoteShares=1`, keeps REST on one per-run localhost port, and binds P2P `BindAddr` through `repos\eMule-tooling\scripts\config-bindaddr-updater.ps1`
- the default P2P bind target is the `hide.me` interface and the scenario always enables the main P2P `UPnP` setting
- the scenario relies on `Autoconnect=1` in the isolated profile and intentionally does not issue overlapping REST connect requests for eD2K or Kad
- the scenario first waits for real browse-capable clients to accumulate naturally after server+Kad autoconnect; transfer/source bootstrap is only a fallback if natural auto-browse never starts succeeding
- the transfer bootstrap path uses the persisted hash `28EAB1A0AB1B9416AAF534E27A234941` first and refuses `.exe` candidates when selecting a downloadable result
- the lane requires:
  - real eD2K server connectivity
  - Kad running state
  - acquisition of one live transfer with sources
  - at least one successful automatic remote-share browse that logs success and persists `.browsecache` output
- `--keep-running` leaves the launched isolated eMule instance alive after a passing run and forces artifact retention so the profile can be inspected afterward
- artifacts are published under `reports\auto-browse-live\...` with the same latest-report mirroring as the other Python-first live harnesses

Canonical live harness:

- `scripts\run-pipe-live-matrix.ps1` is the operator-facing entrypoint for launch-only and full live named-pipe harness runs
- the wrapper resolves `helper-runtime-pipe-live-session.ps1` from `repos\eMule-tooling\helpers` first and falls back to the legacy app-side helper path only when needed
- the harness stages a renamed binary copy, `eMule_v072_harness.exe`, beside the debug build output and launches that copy so processes, dumps, and cleanup are easier to identify
- the machine-readable session manifest can be requested through the shared wrapper with `-SessionManifestPath`
- normal runs retry targeted teardown and fail if any harness-launched process remains afterward; `-KeepRunning` is the only supported opt-out

Shared Files live UI regression:

- `scripts\shared-files-ui-e2e.py` is the operator-facing entrypoint for the real Win32 Shared Files regression
- it launches `emule.exe` with explicit `-ignoreinstances -c <profile-base>` so the run stays isolated from local user sessions
- the checked-in seed profile must stay initialized; the Python harness validates the seed keys, writes deterministic maximized window placement, and patches only per-run incoming, temp, and shared-directory paths
- the default UI run now covers two scenarios: the original three-file deterministic smoke case plus a generated recursive robustness tree under `C:\tmp\00_long_paths\shared_files_robustness`
- the regression asserts that the main window starts maximized and exercises exact default-name ordering, size ascending and descending sorts, name ascending and descending sorts after reload, selection-detail updates, reload preservation of the active descending size sort, and large-tree row-count/set/prefix checks driven by the generated manifest
- `--scenario` can be repeated on the Python entrypoint to run only `fixture-three-files` or only `generated-robustness-recursive`
- each run publishes artifacts and `ui-summary.json` under `reports\shared-files-ui-e2e\...` and refreshes `reports\shared-files-ui-e2e-latest`
- the shared `reports\harness-summary.json` now includes a `live_ui` section when that regression is run

Config-stability live UI regression:

- `scripts\config-stability-ui-e2e.py` is the operator-facing entrypoint for long `-c` config-path startup, settings-save, and relaunch-stability coverage
- it launches `emule.exe` with explicit `-ignoreinstances -c <profile-base>` under a deliberately deep profile root so `profile-base\config\preferences.ini` exceeds normal Win32 path limits
- the default run covers `long-config-settings-roundtrip` and `long-config-shared-stress`
- the roundtrip scenario edits the real Preferences dialog, saves `OnlineSignature`, verifies `preferences.ini`, relaunches the same long-path profile, and confirms persisted UI state
- the stress scenario repeats launch, Preferences save, Shared Files activation, and clean shutdown across multiple cycles while recursively sharing the generated robustness tree under `C:\tmp\00_long_paths`
- each run publishes artifacts and `ui-summary.json` under `reports\config-stability-ui-e2e\...` and refreshes `reports\config-stability-ui-e2e-latest`

Startup-profile scenarios:

- `scripts\startup-profile-scenarios.py` builds deterministic Chrome Trace `startup-profile.trace.json` artifacts for multiple live-profile scenarios without changing app behavior
- the trace includes stable readiness, Shared Files hashing, Statistics dialog, and broadband lifecycle phase ids so Perfetto and the JSON summaries can separate startup, UI setup, queue wait, worker-thread bring-up costs, and final shared-hash drain time
- the default run covers `baseline-no-shares`, `fixture-three-files`, `long-paths-root-only`, `long-paths-recursive`, `long-path-output-root-only`, `long-path-output-recursive`, `long-path-emule-fixture-root-only`, `long-path-emule-fixture-recursive`, `shared-files-robustness-root-only`, and `shared-files-robustness-recursive`
- `--scenario` can be repeated on the Python entrypoint to run only the scenarios you want
- `scripts\create-long-paths-tree.py` now lives in this repo and materializes the generated long-path fixture trees plus `generated-fixture-manifest.json` under `C:\tmp\00_long_paths`
- the long-path scenarios target `C:\tmp\00_long_paths` by default, regenerate the repo-owned fixture tree as needed, and expand `shareddir.dat` deterministically in the recursive cases
- each scenario summary now also records shareddir payload metrics plus tree-shape metrics such as depth, longest paths, and counts beyond the Windows path thresholds
- each scenario summary includes highlighted timings, normalized derived timings, and the top slowest startup phases, and the combined summary adds direct delta comparisons between the main long-path, generated output, and Shared Files robustness root-only vs recursive variants
- each run publishes scenario artifacts plus `startup-profiles-summary.json` and `startup-profiles-wrapper-summary.json` under `reports\startup-profile-scenarios\...` and refreshes `reports\startup-profile-scenarios-latest`
- the shared `reports\harness-summary.json` now includes a `startup_profiles` section when that runner is used

Tracked-file privacy guard:

- `scripts\guard-tracked-files.ps1` fails when tracked files contain local user-home paths or personal-identifier filename leaks derived from the current environment or an untracked local override file
- `scripts\build-emule-tests.ps1` runs that guard by default before building
- the same guard is enforced in GitHub Actions for pushes and pull requests

Native seam coverage and shared reports:

- `scripts\run-native-coverage.ps1` builds `emule-tests.exe`, runs the requested doctest suites under OpenCppCoverage, and writes Cobertura plus summary outputs under `reports\native-coverage`
- `scripts\run-bugfix-core-coverage.ps1` chains the canonical `main` and `bugfix` native-coverage runs with the workspace live-diff pass and writes a combined summary under `reports\bugfix-core-coverage`
- `helpers\helper-opencppcoverage-bootstrap.ps1` uses an explicit install root when provided, otherwise discovers `OpenCppCoverage.exe` from `PATH`, and finally falls back to a repo-managed pinned install under `tools\OpenCppCoverage`
- `scripts\run_live_diff.py` writes both text and JSON parity/divergence summaries under `reports`
- `scripts\run-live-diff.ps1` is an obsolete compatibility shim that delegates to `scripts\run_live_diff.py`
- the old PowerShell live-diff implementation lives under `scripts\obsolete` for audit only
- native coverage remains on the retained PowerShell wrapper until the OpenCppCoverage bootstrap can be ported without touching build orchestration
- `scripts\publish-harness-summary.py` combines native coverage, parity, optional live-harness manifest data, optional live UI status, and optional startup-profile scenario status into one shared summary under `reports`
