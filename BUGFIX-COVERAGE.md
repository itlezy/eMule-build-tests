# Bugfix Coverage Map

Canonical app target: `release/v0.72a-bugfix`
Canonical test branch: `main`

## Automated Coverage

- Packet length parsing hardening: `src/protocol.tests.cpp`
- Blob/tag payload bounds checking: `src/protocol.tests.cpp`
- Hello and server tag bounds checking helpers: `src/protocol.tests.cpp`
- Explicit dotted IPv4 parsing: `src/protocol.tests.cpp`
- Progress denominator and clamp guards: `src/protocol.tests.cpp`, `src/part_file_numeric.tests.cpp`, `src/known_file_list.tests.cpp`
- `TryToConnect` lifetime and detach safety: `src/client_socket_lifetime.tests.cpp`
- Queue and numeric guard helpers: `src/part_file_numeric.tests.cpp`, `src/source_exchange_flow.tests.cpp`, `src/source_exchange_replay.tests.cpp`
- Orphaned known2 AICH cleanup: `src/known_file_list.tests.cpp`
- UPnP delete safety seam logic: `src/upnp_impl_minilib.tests.cpp`
- Null/resource hardening helpers used by low-risk tail fixes: `src/null_guard.tests.cpp`, `src/resource_ownership.tests.cpp`

## Manual Verification

- Clipboard ANSI export overflow fix
- `WebSocket.cpp` wide-path TLS cert/key loading
- AddFriend dialog control guards
- ArchivePreview dialog/state guards
- ColourPopup owner and column guards
- Safe fixed-buffer UI copy fixes
- CaptchaGenerator GDI/DC cleanup
- ToolBarCtrlX desktop DC ownership cleanup

## Current Checkpoint

- `emule-tests.exe --test-suite=parity`: passing on the canonical bugfix worktree
- OpenCppCoverage parity report:
  - report: `reports/native-coverage/<timestamp>-<build-tag>-x64-Debug`
  - line rate: `96.19%`
- Cleanroom parity validation on rebuilt canonical `main`, `release/v0.72a-build`, and `release/v0.72a-bugfix` worktrees:
  - report: `reports/native-coverage/<timestamp>-<cleanroom-tag>-x64-Debug`
  - line rate: `96.19%`
