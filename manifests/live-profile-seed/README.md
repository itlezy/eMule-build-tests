# Live Profile Seed

This directory stores the deterministic test-only profile baseline for live named-pipe runs.

The `config` subtree is shaped like an eMule profile so the live harness can copy it directly into a fresh working folder.

The seed is intentionally minimal:

- `preferences.ini`
- `nodes.dat`
- `server.met`

Mutable runtime state such as logs, temp files, downloads, and rolling history files must not be committed here.
