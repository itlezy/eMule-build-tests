# eMule Shared Tests

This repository is a shared workspace-level test asset for the `eMule-build` and `eMule-build-oracle` workspaces.

It owns:

- the standalone `emule-tests.vcxproj` project
- shared doctest sources and support headers
- parity and divergence suites for live dev-vs-oracle comparison
- workspace-level build and live-diff scripts
- fixture, manifest, and report directories for future protocol coverage

The project is built against the local `eMule` checkout in whichever workspace invokes it. It is intentionally not a runtime dependency like the `eMule-*` third-party submodules.

Current suite model:

- `parity`: cases that must pass in both the `eMule-build` and `eMule-build-oracle` workspaces
- `divergence`: cases that are expected to pass on `eMule-build` and fail on the pre-refactor `eMule-build-oracle` workspace
