"""Запись ассета в docs/credits/<зона>.md (часть 76.4, шаг 10). Идемпотентна: повторный вызов строку не дублирует."""
from __future__ import annotations

from pathlib import Path
from typing import Any

HEADER = (
    "# Ассеты роли {zone}\n\n"
    "| Ассет | Автор | Источник (URL) | Лицензия | Где используется | Изменён? |\n"
    "|---|---|---|---|---|---|\n"
)


def _cell(text: Any) -> str:
    """Ячейка таблицы Markdown: без переводов строк и вертикальных черт."""
    return " ".join(str(text).replace("|", "/").split())


def row_for(asset: dict[str, Any]) -> str:
    source = asset.get("source", {})
    url = source.get("page") or source.get("url") or "локальный файл"
    # Первая колонка: игровое имя и путь результата; CI ищет в ней путь файла или папки.
    first = f"{asset['name']} (assets_bin/cooked/{asset['name']}.hmesh)" if asset.get("kind") == "model" else f"{asset['name']} (assets_src/{asset['id']})"
    modified = asset.get("modified", "")
    return "| " + " | ".join(
        _cell(c) for c in (first, asset.get("author", ""), url, asset.get("license", ""), asset.get("used_in", ""), modified)
    ) + " |"


def marker(asset: dict[str, Any]) -> str:
    """Подстрока, по которой строка ассета узнаётся при повторном запуске (имя в первой колонке)."""
    return f"| {_cell(asset['name'])} ("


def ensure_row(credits_dir: Path, asset: dict[str, Any]) -> bool:
    """Добавляет строку ассета в docs/credits/<зона>.md. Возвращает True, если файл изменён. Существующая строка обновляется."""
    path = credits_dir / f"{asset['zone']}.md"
    text = path.read_text(encoding="utf-8") if path.exists() else HEADER.format(zone=asset["zone"])
    lines = text.splitlines()
    new_row = row_for(asset)
    key = marker(asset)
    for i, line in enumerate(lines):
        if line.startswith(key):
            if line == new_row:
                return False
            lines[i] = new_row
            path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
            return True
    while lines and not lines[-1].strip():
        lines.pop()
    lines.append(new_row)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    return True


def has_row(credits_dir: Path, asset: dict[str, Any]) -> bool:
    path = credits_dir / f"{asset['zone']}.md"
    return path.exists() and any(line.startswith(marker(asset)) for line in path.read_text(encoding="utf-8").splitlines())
