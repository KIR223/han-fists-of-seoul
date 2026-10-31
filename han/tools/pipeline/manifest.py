"""Манифест ассетов: что скачано, откуда, по какой лицензии, как переделывать (часть 76.3, этап E20).

Файл assets_src/manifest.json лежит в Git; сами скачанные и обработанные файлы — в assets_bin/ (не в Git).
Из манифеста игра воспроизводится с нуля: han_fetch.py all скачает, проверит хэш и лицензию, переделает и сварит всё.
"""
from __future__ import annotations

import datetime
import hashlib
import json
import re
from pathlib import Path
from typing import Any

import license_rules

MANIFEST_VERSION = 1
KINDS = ("model", "texture", "font", "sound")
SOURCE_TYPES = ("direct", "polyhaven", "sketchfab", "local")
ZONES = ("A", "B")
ID_RE = re.compile(r"^[a-z0-9_]{2,64}$")
# Имена по каталогу (часть 48.1): CH_ CL_ HR_ VH_ PR_ BL_ LM_ INT_ FX_ и т. п. + латиница, цифры, подчёркивание.
NAME_RE = re.compile(r"^[A-Z]{2,4}_[A-Za-z0-9_]{1,60}$")
SHA_RE = re.compile(r"^[0-9a-f]{64}$")

# Бюджеты треугольников LOD0 по префиксу имени (часть 48.2); по умолчанию — как у среднего пропса.
BUDGET_BY_PREFIX = {"PR": 2500, "VH": 60000, "CH": 40000, "BL": 3000, "LM": 150000, "CL": 20000}

DEFAULT_PIPELINE = {
    "scale": 1.0,                 # множитель единиц (FBX в сантиметрах — 0.01)
    "pivot": "base_center",       # base_center — центр основания; none — не трогать
    "max_materials": 2,           # предупреждение, если материалов больше (слияние через запекание — вручную)
    "max_texture_size": 1024,     # большие картинки уменьшаются до этого размера
    "lods": [0.5, 0.2, 0.05],     # доли треугольников LOD1–LOD3
    "collision": True,            # выпуклая оболочка ≤ 32 вершин
    "entry": "",                  # главный файл внутри архива (пусто — найти первый .glb/.gltf/.fbx/.obj)
}


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def empty() -> dict[str, Any]:
    return {"version": MANIFEST_VERSION, "assets": []}


def load(path: Path) -> dict[str, Any]:
    if not path.exists():
        return empty()
    return json.loads(path.read_text(encoding="utf-8"))


def save(path: Path, data: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")


def find(data: dict[str, Any], asset_id: str) -> dict[str, Any] | None:
    for asset in data.get("assets", []):
        if asset.get("id") == asset_id:
            return asset
    return None


def pipeline_of(asset: dict[str, Any]) -> dict[str, Any]:
    """Настройки конвейера с подстановкой значений по умолчанию и бюджета по префиксу имени."""
    cfg = dict(DEFAULT_PIPELINE)
    prefix = str(asset.get("name", "")).split("_", 1)[0]
    cfg["budget_triangles"] = BUDGET_BY_PREFIX.get(prefix, 2500)
    cfg.update(asset.get("pipeline", {}))
    return cfg


def license_verdict(asset: dict[str, Any]) -> str:
    return license_rules.classify(str(asset.get("license", "")))


def _is_https(url: str) -> bool:
    return url.startswith("https://") and len(url) > 12


def validate(data: dict[str, Any]) -> list[str]:
    """Возвращает список проблем (пустой — манифест в порядке). Ничего не скачивает и не читает с диска."""
    errors: list[str] = []
    if data.get("version") != MANIFEST_VERSION:
        errors.append(f"неподдерживаемая версия манифеста: {data.get('version')!r}")
    assets = data.get("assets")
    if not isinstance(assets, list):
        return errors + ["поле assets должно быть списком"]

    seen_ids: set[str] = set()
    seen_names: set[str] = set()
    for index, a in enumerate(assets):
        where = f"assets[{index}] ({a.get('id', '?')})"
        asset_id = str(a.get("id", ""))
        if not ID_RE.match(asset_id):
            errors.append(f"{where}: id должен быть латиницей в нижнем регистре, цифры и _")
        if asset_id in seen_ids:
            errors.append(f"{where}: повторяющийся id")
        seen_ids.add(asset_id)

        name = str(a.get("name", ""))
        if not NAME_RE.match(name):
            errors.append(f"{where}: name должен быть по каталогу, например PR_TrashBin (префикс_Имя)")
        if name in seen_names:
            errors.append(f"{where}: повторяющееся имя {name}")
        seen_names.add(name)

        if a.get("kind") not in KINDS:
            errors.append(f"{where}: kind должен быть одним из {KINDS}")
        if a.get("zone") not in ZONES:
            errors.append(f"{where}: zone должна быть A или B")

        source = a.get("source") or {}
        stype = source.get("type")
        if stype not in SOURCE_TYPES:
            errors.append(f"{where}: source.type должен быть одним из {SOURCE_TYPES}")
        if stype in ("direct", "polyhaven", "sketchfab") and not _is_https(str(source.get("page", ""))):
            errors.append(f"{where}: source.page — ссылка на страницу ассета (https), чтобы автор и лицензия проверялись людьми")
        if stype == "direct" and not _is_https(str(source.get("url", ""))):
            errors.append(f"{where}: для direct нужен source.url (https)")
        if stype in ("polyhaven", "sketchfab") and not source.get("asset_id"):
            errors.append(f"{where}: для {stype} нужен source.asset_id")

        for field in ("author", "license", "used_in", "modified"):
            if not str(a.get(field, "")).strip():
                errors.append(f"{where}: поле {field} обязательно (нужно для docs/credits)")
        verdict = license_verdict(a)
        if verdict == license_rules.FORBIDDEN:
            errors.append(f"{where}: лицензия «{a.get('license', '')}» не разрешена (часть 76.1)")
        elif verdict == license_rules.NEEDS_OWNER and not a.get("approved_by_owner"):
            errors.append(f"{where}: лицензия «{a.get('license', '')}» только с одобрения владельца (approved_by_owner: true после решения)")

        retrieved = str(a.get("retrieved", ""))
        try:
            datetime.date.fromisoformat(retrieved)
        except ValueError:
            errors.append(f"{where}: retrieved должен быть датой ГГГГ-ММ-ДД")
        sha = str(a.get("sha256", ""))
        if sha and not SHA_RE.match(sha):
            errors.append(f"{where}: sha256 должен быть 64 шестнадцатеричных символа (или пусто до первой загрузки)")

        pipeline = a.get("pipeline", {})
        if not isinstance(pipeline, dict):
            errors.append(f"{where}: pipeline должен быть объектом")
        else:
            budget = pipeline.get("budget_triangles")
            if budget is not None and (not isinstance(budget, int) or budget <= 0):
                errors.append(f"{where}: pipeline.budget_triangles должен быть положительным целым")
            lods = pipeline.get("lods")
            if lods is not None and (not isinstance(lods, list) or any(not isinstance(x, (int, float)) or not 0 < x < 1 for x in lods)):
                errors.append(f"{where}: pipeline.lods — список долей от 0 до 1 (не включая границы)")
    return errors
