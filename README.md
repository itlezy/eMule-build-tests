# eMule Shared Tests

This repository is a shared workspace-level test asset for the `eMulebb` and `eMulebb-oracle` workspaces.

It owns:

- the standalone `emule-tests.vcxproj` project
- shared doctest sources and support headers
- workspace-level build and live-diff scripts
- fixture, manifest, and report directories for future protocol coverage

The project is built against the local `eMule` checkout in whichever workspace invokes it. It is intentionally not a runtime dependency like the `eMule-*` third-party submodules.
