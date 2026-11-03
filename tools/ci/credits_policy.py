"""Политика CI использует классификатор конвейера; решение владельца сверяется с манифестом."""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "han" / "tools" / "pipeline"))
import license_rules  # noqa: E402

OWN = {"свой", "собственная работа", "own work", "оригинал", "unlicense"}
# MIT внутри Limited или MITunknown не является лицензией MIT.
LICENSE_START = re.compile(r"^(?:cc0|cc[- ]?by|cc attribution|mit|bsd|apache|(?:sil )?ofl|open font license|zlib|public domain|pd|kogl|공공누리|royalty free)(?=$|[\s\d.(_-])", re.I)


def license_allowed(text, approved_by_owner=False):
    text = text.strip()
    if text.casefold() in OWN:
        return True
    if not LICENSE_START.search(text):
        return False
    verdict = license_rules.classify(text)
    return verdict == license_rules.ALLOWED or (verdict == license_rules.NEEDS_OWNER and approved_by_owner)


def owner_approved(row, root=ROOT):
    manifest = Path(root) / "assets_src" / "manifest.json"
    if not manifest.exists():
        return False
    # Непрочитанный манифест не считается одобрением.
    try:
        assets = json.loads(manifest.read_text(encoding="utf-8")).get("assets", [])
    except (OSError, ValueError, AttributeError):
        return False
    return any(isinstance(asset, dict) and asset.get("approved_by_owner") is True and asset.get("license") == row[3] and
               (row[0] == asset.get("name") or row[0].startswith(str(asset.get("name")) + " (")) for asset in assets)
