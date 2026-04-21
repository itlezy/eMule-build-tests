from __future__ import annotations

from emule_test_harness.ini import parse_ini_values, patch_ini_value


def test_parse_ini_values_ignores_comments_sections_and_blank_lines() -> None:
    values = parse_ini_values(
        """
; comment
[Section]
Nick = eMule
Port=4662

InvalidLine
"""
    )

    assert values == {"Nick": "eMule", "Port": "4662"}


def test_patch_ini_value_replaces_existing_key_case_insensitively() -> None:
    patched = patch_ini_value("Nick=old\r\nPort=1\r\n", "nick", "new")

    assert "nick=new" in patched
    assert "Nick=old" not in patched
    assert "Port=1" in patched


def test_patch_ini_value_appends_missing_key_with_crlf() -> None:
    assert patch_ini_value("Nick=eMule", "Port", "4662") == "Nick=eMule\r\nPort=4662\r\n"
