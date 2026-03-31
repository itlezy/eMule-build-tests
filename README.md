# eMule Shared Tests

This repository is a shared workspace-level test asset for the `eMule-build` and `eMule-build-oracle` workspaces.

Expected fixed layout:

- `C:\prj\p2p\eMule\eMulebb\eMule-build`
- `C:\prj\p2p\eMule\eMulebb\eMule-build-oracle`
- `C:\prj\p2p\eMule\eMulebb\eMule-build-tests`

It owns:

- the standalone `emule-tests.vcxproj` project
- shared doctest sources and support headers
- parity and divergence suites for live dev-vs-oracle comparison
- workspace-level build and live-diff scripts
- fixture, manifest, and report directories for future protocol coverage

The project is built against the local `eMule` checkout in whichever workspace invokes it. It is intentionally not a runtime dependency like the `eMule-*` third-party submodules, and it is no longer embedded as a `tests/` submodule inside each workspace.

Current suite model:

- `parity`: cases that must pass in both the `eMule-build` and `eMule-build-oracle` workspaces
- `divergence`: cases that are expected to pass on `eMule-build` and fail on the pre-refactor `eMule-build-oracle` workspace

Standalone probe mode:

- `build\<tag>\x64\Debug\emule-tests.exe --hash-probe "<full file path>"` runs an isolated non-UI file scan
- by default it executes a buffered scan first and then the shared `MappedFileReader` path
- use `--reader buffered`, `--reader mapped`, or `--reader both` to narrow the probe
- use `--byte-limit <N>` to cap the scan length and `--progress-mib <N>` to control progress output
- `build\<tag>\x64\Debug\emule-tests.exe --full-hash-probe "<full file path>"` runs the offline MD4 plus AICH hashing pipeline without launching `emule.exe`
- the full-hash mode also supports `--reader buffered|mapped|both` and `--progress-mib <N>`
- use the full-hash mode when you need to separate raw file access from higher-level `CKnownFile::CreateFromFile` work such as metadata extraction, known-file registration, or UI progress handling
