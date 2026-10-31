"""Правила лицензий ассетов (часть 76.1 дизайна). Зеркало han/tools/src/license.cpp: поведение сверяется общим файлом
han/tools/pipeline/tests/license_cases.tsv, который читают тесты и C++, и Python.

classify(text) -> "allowed" | "needs-owner" | "forbidden". Неизвестное и пустое — запрещено: нет лицензии = нельзя.
"""
from __future__ import annotations

ALLOWED = "allowed"
NEEDS_OWNER = "needs-owner"
FORBIDDEN = "forbidden"

_FORBIDDEN_PARTS = (
    "bync", "bynd", "ccnc", "ccnd",              # CC-BY-NC, CC-BY-ND, CC-NC, CC-ND
    "noncommercial", "noderiv",                   # «Attribution-NonCommercial», «NoDerivs»
    "editorial", "personaluse", "gamerip", "nolicense",
)


def _normalize(text: str) -> str:
    """Нижний регистр без пробелов, дефисов и подчёркиваний: 'CC BY-NC 4.0' -> 'ccbync4.0'."""
    return "".join(ch for ch in text.lower() if ch not in " -_\t")


def classify(text: str) -> str:
    s = _normalize(text or "")
    if not s:
        return FORBIDDEN

    # Запреты проверяются первыми: «CC-BY-NC-SA» — это NC, то есть нельзя.
    if any(part in s for part in _FORBIDDEN_PARTS):
        return FORBIDDEN
    # 공공누리 (KOGL): тип 1 можно, типы 2–4 нельзя.
    if "kogl" in s or "공공누리" in s:
        return ALLOWED if ("type1" in s or "1유형" in s or "kogl1" in s) else FORBIDDEN

    if "ccbysa" in s or "ccattributionsharealike" in s or "royaltyfree" in s:
        return NEEDS_OWNER

    if "cc0" in s or "publicdomain" in s or s == "pd":
        return ALLOWED
    if "ccby" in s or "ccattribution" in s:
        return ALLOWED
    if any(part in s for part in ("bsd", "apache", "zlib", "ofl", "openfontlicense")) or s.startswith("mit"):
        return ALLOWED
    return FORBIDDEN


# Лицензии Sketchfab в API приходят короткими кодами.
_SKETCHFAB_SLUGS = {
    "cc0": "CC0",
    "by": "CC-BY 4.0",
    "by-sa": "CC-BY-SA 4.0",
    "by-nd": "CC-BY-ND 4.0",
    "by-nc": "CC-BY-NC 4.0",
    "by-nc-sa": "CC-BY-NC-SA 4.0",
    "by-nc-nd": "CC-BY-NC-ND 4.0",
    "ed": "Editorial",
    "st": "Standard (нельзя)",
}


def from_sketchfab_slug(slug: str) -> str:
    """Переводит код лицензии Sketchfab в понятный текст; незнакомый код остаётся как есть (и будет запрещён)."""
    return _SKETCHFAB_SLUGS.get((slug or "").strip().lower(), slug or "")
