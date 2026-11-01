#!/usr/bin/env python3
"""Проверка данных игры по схемам из contracts/schemas (G1, часть 2 плана B).

    python data/_tools/validate_data.py            # все известные наборы данных
    python data/_tools/validate_data.py --quiet    # только ошибки

Что проверяется:
  • data/combat/moves/*.json       — схема move.schema.json, id совпадает с именем файла, ключи name_key есть в локализации;
  • data/people/archetypes/*.json  — схема npc_archetype.schema.json, id совпадает с именем файла;
  • data/combat/combat_rules.json  — структура правил (разбор делает движок; здесь проверка обязательных разделов);
  • data/combat/scenarios/*.json   — это сценарии: должен быть разбор JSON и хотя бы два бойца.
Код выхода: 0 — всё хорошо, 1 — есть ошибки в данных, 2 — не хватает пакета jsonschema (pip install jsonschema).
"""
from __future__ import annotations

import argparse
import csv
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCHEMAS = ROOT / "contracts" / "schemas"

# (папка с данными, схема, ключ локализации в файле или None)
SETS = [
    ("data/combat/moves", "move.schema.json", "name_key"),
    ("data/people/archetypes", "npc_archetype.schema.json", "name_key"),
]
RULES_SECTIONS = ["damage", "stamina", "balance", "knockdown", "movement", "block_by_angle", "defenses"]


def loc_keys() -> set[str]:
    """Все ключи локализации: заявки A и B и готовые таблицы data/loc/*/*.csv."""
    keys: set[str] = set()
    files = list((ROOT / "contracts" / "loc_requests").glob("*.csv")) + list((ROOT / "data" / "loc").glob("*/*.csv"))
    for f in files:
        with f.open(encoding="utf-8", newline="") as fh:
            for row in csv.reader(fh):
                if row and row[0] != "key":
                    keys.add(row[0])
    return keys


def load_json(path: Path, errors: list[str]):
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as e:
        errors.append(f"{path.relative_to(ROOT).as_posix()}: не разобрать JSON ({e})")
        return None


def validate(quiet: bool = False) -> int:
    try:
        import jsonschema
    except ImportError:
        print("Нужен пакет jsonschema: pip install jsonschema", file=sys.stderr)
        return 2
    errors: list[str] = []
    checked = 0
    keys = loc_keys()
    for folder, schema_name, key_field in SETS:
        schema = json.loads((SCHEMAS / schema_name).read_text(encoding="utf-8"))
        jsonschema.Draft202012Validator.check_schema(schema)
        validator = jsonschema.Draft202012Validator(schema)
        directory = ROOT / folder
        files = sorted(directory.glob("*.json"))
        if not files:
            errors.append(f"{folder}: нет ни одного файла данных")
        ids: set[str] = set()
        for f in files:
            doc = load_json(f, errors)
            if doc is None:
                continue
            checked += 1
            rel = f.relative_to(ROOT).as_posix()
            for e in sorted(validator.iter_errors(doc), key=lambda e: list(e.path)):
                where = "/".join(str(p) for p in e.path) or "корень"
                errors.append(f"{rel}: {where}: {e.message}")
            if doc.get("id") != f.stem:
                errors.append(f"{rel}: id «{doc.get('id')}» не совпадает с именем файла")
            if doc.get("id") in ids:
                errors.append(f"{rel}: повторяющийся id «{doc.get('id')}»")
            ids.add(str(doc.get("id")))
            if key_field and doc.get(key_field) and doc[key_field] not in keys:
                errors.append(f"{rel}: ключа локализации «{doc[key_field]}» нет ни в заявках, ни в data/loc")
    rules_path = ROOT / "data" / "combat" / "combat_rules.json"
    rules = load_json(rules_path, errors)
    if rules is not None:
        checked += 1
        for section in RULES_SECTIONS:
            if section not in rules:
                errors.append(f"data/combat/combat_rules.json: нет раздела «{section}»")
    for f in sorted((ROOT / "data" / "combat" / "scenarios").glob("*.json")):
        doc = load_json(f, errors)
        if doc is None:
            continue
        checked += 1
        if len(doc.get("fighters", [])) < 2:
            errors.append(f"{f.relative_to(ROOT).as_posix()}: в сценарии меньше двух бойцов")
    if errors:
        print("Ошибки в данных:\n  " + "\n  ".join(errors), file=sys.stderr)
        return 1
    if not quiet:
        print(f"Данные в порядке: проверено файлов {checked}")
    return 0


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Проверка данных игры по схемам")
    parser.add_argument("--quiet", action="store_true")
    sys.exit(validate(parser.parse_args().quiet))
