# eMule Shared Tests

This repository is a shared workspace-level test asset for the `eMule-build` workspace and the oracle workspaces.

Expected fixed layout:

- `C:\prj\p2p\eMule\eMulebb\eMule-build`
- `C:\prj\p2p\eMule\eMulebb\eMule-build-oracle-v0.72a-oracle`
- `C:\prj\p2p\eMule\eMulebb\eMule-build-oracle-v0.60d-oracle`
- `C:\prj\p2p\eMule\eMulebb\eMule-build-tests`

It owns:

- the standalone `emule-tests.vcxproj` project
- shared doctest sources and support headers
- parity and divergence suites for live dev-vs-oracle comparison
- workspace-level build and live-diff scripts
- fixture, manifest, and report directories for future protocol coverage
- the deterministic live-profile seed used by the named-pipe live harness

The project is built against the local `eMule` checkout in whichever workspace invokes it. It is intentionally not a runtime dependency like the `eMule-*` third-party submodules, and it is no longer embedded as a `tests/` submodule inside each workspace.

Supported oracle workspaces:

- `eMule-build-oracle-v0.72a-oracle`
- `eMule-build-oracle-v0.60d-oracle`

Oracle workspaces may be edited when the change is strictly to enable tests, seams, logging, tracing, or debugging. They are not feature-development branches.

Current suite model:

- `parity`: cases that must pass in both the `eMule-build` workspace and the selected oracle workspace
- `divergence`: cases that are expected to pass on `eMule-build` and fail on the selected pre-refactor oracle workspace

Oracle build and launch quick reference:

- `eMule-build-oracle-v0.72a-oracle`: use `pwsh -File .\workspace.ps1 validate`, `build-libs`, `build-app`, and `run-binary`; the `.cmd` wrappers are compatibility shims over `workspace.ps1`
- `eMule-build-oracle-v0.60d-oracle`: use the legacy root scripts `003_build_MSBuild_ALL_libs*.cmd`, `build_MSBuild_eMule*.cmd`, and `launch_binary_eMule*.cmd`
- GUI launch wrappers are acceptable for quick manual runs. For oracle workspaces, deliberate test/debug launches should stay on the supported wrapper flows because direct `emule.exe -c <profile-dir>` launching is only available on the latest dev branch
- for live-diff runs, point `-OracleWorkspaceRoot` at the oracle you want to compare against; the default remains `C:\prj\p2p\eMule\eMulebb\eMule-build-oracle-v0.72a-oracle`

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

Tracked-file privacy guard:

- `scripts\guard-tracked-files.ps1` fails when tracked files contain local user-home paths or personal-identifier filename leaks derived from the current environment or an untracked local override file
- `scripts\build-emule-tests.ps1` runs that guard by default before building
- the same guard is enforced in GitHub Actions for pushes and pull requests
