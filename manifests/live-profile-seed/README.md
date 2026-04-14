# Live Profile Seed

This directory stores the deterministic test-only profile baseline for live named-pipe runs.

The `config` subtree is shaped like an eMule profile so the live harness can copy it directly into a fresh working folder.

The seed is intentionally minimal:

- `preferences.ini`
- `preferences.dat`
- `nodes.dat`
- `server.met`

The seeded `preferences.ini` must stay limited to the non-default settings that the live harness truly needs before runtime overrides are applied.

The seeded `preferences.dat` carries the deterministic maximized main-window placement used by the live UI and startup-profile harnesses.

Mutable runtime state such as logs, temp files, downloads, and rolling history files must not be committed here.
