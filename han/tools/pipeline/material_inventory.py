#!/usr/bin/env python3
"""Реестр материалов города (этап 07): для каждого слота из han/render/apps/city_common.h — какая папка набора реально используется
(основная из библиотеки владельца, свободный аналог или ничего), какие карты есть (альбедо/нормаль/шероховатость), размер, число мипов,
цветовое пространство (sRGB/линейное по vkFormat KTX2) и оценка VRAM. Пропавшая карта или слот без набора — «fallback» (плоский цвет).

  python han/tools/pipeline/material_inventory.py [--cooked assets_bin/cooked] [--json out.json]
Код возврата 0 всегда; строка «fallback» в итоге говорит, сколько слотов падает в плоский цвет.
"""
import argparse
import json
import re
import struct
import sys
from pathlib import Path

SLOT_RE = re.compile(r'\{world::GeoMaterial::(\w+), (nullptr|"([\w]+)"), \{[^}]*\}, ([0-9.]+)f(?:, "([\w]+)")?\}')
SRGB_FORMATS = {43, 50, 132, 134, 136, 138, 146}  # R8G8B8A8_SRGB, B8G8R8A8_SRGB, BC1/2/3/7 SRGB


def read_ktx2(path: Path):
    data = path.read_bytes()[:80]
    if len(data) < 48 or data[:12] != b"\xabKTX 20\xbb\r\n\x1a\n":
        return None
    vk_format, _type_size, width, height, _depth, _layers, _faces, levels, supercompression = struct.unpack_from("<9I", data, 12)
    return {"vk_format": vk_format, "width": width, "height": height, "levels": levels, "supercompression": supercompression}


def vram_bytes(info):
    # RGBA8 без сжатия — верхняя оценка; мипы добавляют треть.
    return int(info["width"] * info["height"] * 4 * (4 / 3 if info["levels"] > 1 else 1))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cooked", default="assets_bin/cooked")
    ap.add_argument("--header", default="han/render/apps/city_common.h")
    ap.add_argument("--json", default="")
    a = ap.parse_args()
    cooked = Path(a.cooked)
    text = Path(a.header).read_text(encoding="utf-8")
    rows = []
    for m in SLOT_RE.finditer(text):
        material, folder, alt = m.group(1), m.group(3), m.group(5)
        row = {"material": material, "primary": folder, "alt": alt, "used": None, "status": "", "maps": {}}
        if folder is None:
            row["status"] = "процедурный (код)"
            rows.append(row)
            continue
        for candidate in (folder, alt):
            if candidate and (cooked / candidate / "diff.ktx2").exists():
                row["used"] = candidate
                break
        if row["used"] is None:
            row["status"] = "fallback: плоский цвет"
            rows.append(row)
            continue
        total = 0
        for name in ("diff", "nor", "rough"):
            f = cooked / row["used"] / f"{name}.ktx2"
            info = read_ktx2(f) if f.exists() else None
            if info:
                info["srgb"] = info["vk_format"] in SRGB_FORMATS
                total += vram_bytes(info)
            row["maps"][name] = info
        row["vram_mb"] = round(total / 1048576, 2)
        missing = [n for n, i in row["maps"].items() if not i]
        problems = []
        if missing:
            problems.append("нет карт: " + ", ".join(missing))
        d = row["maps"]["diff"]
        if d and not d["srgb"]:
            problems.append("альбедо не sRGB")
        for n in ("nor", "rough"):
            i = row["maps"][n]
            if i and i["srgb"]:
                problems.append(f"{n} в sRGB (должна быть линейной)")
        if d and d["levels"] <= 1:
            problems.append("нет мипов")
        row["status"] = ("свободный аналог" if row["used"] == alt else ("набор проекта" if not alt else "библиотека владельца")) + ("; " + "; ".join(problems) if problems else "")
        rows.append(row)
    print(f"{'материал':<14}{'набор':<44}{'размер':<10}{'мипы':<6}{'МБ':<7}состояние")
    fallbacks = 0
    vram = 0.0
    for r in rows:
        d = (r["maps"].get("diff") or {})
        size = f"{d.get('width', 0)}x{d.get('height', 0)}" if d else "-"
        if r["status"].startswith("fallback"):
            fallbacks += 1
        vram += r.get("vram_mb", 0)
        print(f"{r['material']:<14}{(r['used'] or r['primary'] or '-')[:42]:<44}{size:<10}{d.get('levels', '-')!s:<6}{r.get('vram_mb', '-')!s:<7}{r['status']}")
    print(f"слотов {len(rows)}, fallback {fallbacks}, оценка VRAM текстур слотов {vram:.1f} МБ (RGBA8, верхняя оценка)")
    if a.json:
        Path(a.json).write_text(json.dumps(rows, ensure_ascii=False, indent=2), encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
