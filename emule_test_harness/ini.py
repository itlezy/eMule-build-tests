"""Small INI text helpers shared by Python harness tests."""

from __future__ import annotations

import re


def parse_ini_values(text: str) -> dict[str, str]:
    """Parses simple top-level INI key/value rows into a dictionary."""

    values: dict[str, str] = {}
    for raw_line in text.splitlines():
        line = raw_line.strip()
        if not line or line.startswith("[") or line.startswith(";") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        values[key.strip()] = value.strip()
    return values


def patch_ini_value(text: str, key: str, value: str) -> str:
    """Upserts one simple INI key while preserving existing surrounding text."""

    pattern = re.compile(rf"(?im)^(?P<key>{re.escape(key)})=.*$")
    replacement = f"{key}={value}"
    if pattern.search(text):
        return pattern.sub(replacement, text)
    suffix = "" if text.endswith("\n") else "\r\n"
    return f"{text}{suffix}{replacement}\r\n"
