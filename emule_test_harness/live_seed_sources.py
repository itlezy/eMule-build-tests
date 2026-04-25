from __future__ import annotations

import hashlib
import tempfile
import urllib.request
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

EMULE_SECURITY_HOME_URL = "https://emule-security.org/"
EMULE_SECURITY_SERVER_MET_URL = "https://upd.emule-security.org/server.met"
EMULE_SECURITY_NODES_DAT_URL = "https://upd.emule-security.org/nodes.dat"


@dataclass(frozen=True)
class LiveSeedSource:
    """Describes one live eD2K/Kad bootstrap file used by networked harness runs."""

    name: str
    url: str
    file_name: str
    minimum_bytes: int


def default_seed_sources() -> tuple[LiveSeedSource, ...]:
    """Returns the eMule Security bootstrap files used by live-wire tests."""

    return (
        LiveSeedSource(
            name="server_met",
            url=EMULE_SECURITY_SERVER_MET_URL,
            file_name="server.met",
            minimum_bytes=64,
        ),
        LiveSeedSource(
            name="nodes_dat",
            url=EMULE_SECURITY_NODES_DAT_URL,
            file_name="nodes.dat",
            minimum_bytes=64,
        ),
    )


def fetch_url_bytes(url: str, timeout_seconds: float) -> bytes:
    """Downloads one URL using a stable harness user agent."""

    request = urllib.request.Request(url, headers={"User-Agent": "eMule-build-tests/1.0"})
    with urllib.request.urlopen(request, timeout=timeout_seconds) as response:
        return response.read()


def refresh_seed_files(
    config_dir: Path,
    *,
    timeout_seconds: float = 30.0,
    sources: tuple[LiveSeedSource, ...] | None = None,
    fetch_bytes: Callable[[str, float], bytes] = fetch_url_bytes,
) -> dict[str, object]:
    """Downloads current server/node seed files into one isolated live profile."""

    resolved_config_dir = config_dir.resolve()
    resolved_config_dir.mkdir(parents=True, exist_ok=True)
    refreshed: list[dict[str, object]] = []

    for source in sources or default_seed_sources():
        payload = fetch_bytes(source.url, timeout_seconds)
        if len(payload) < source.minimum_bytes:
            raise RuntimeError(
                f"Downloaded {source.name} from {source.url} was too small: "
                f"{len(payload)} bytes, expected at least {source.minimum_bytes}."
            )

        target_path = resolved_config_dir / source.file_name
        with tempfile.NamedTemporaryFile(dir=resolved_config_dir, delete=False) as temp_file:
            temp_file.write(payload)
            temp_path = Path(temp_file.name)
        temp_path.replace(target_path)

        refreshed.append(
            {
                "name": source.name,
                "url": source.url,
                "file_name": source.file_name,
                "bytes": len(payload),
                "sha256": hashlib.sha256(payload).hexdigest(),
            }
        )

    return {
        "source_home_url": EMULE_SECURITY_HOME_URL,
        "files": refreshed,
    }
