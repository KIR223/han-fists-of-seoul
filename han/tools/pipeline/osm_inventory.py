#!/usr/bin/env python3
"""Инвентаризация OSM-карты города по ячейкам (этап 01 плана): что есть в источнике и где пусто.

    python han/tools/pipeline/osm_inventory.py assets_src/osm/seoul_connected.osm --out build/metrics/osm_inventory.json \\
        [--origin 37.557 126.914] [--cell 256] [--bounds -3000 -2500 17500 9800] [--png build/metrics/osm_density.png]

Для каждой ячейки считаются здания, дороги, парки и вода (по первой точке пути), высотные здания, число зданий с высотой/этажностью.
Ячейка входит в запекание, если её центр внутри --bounds (как у han_citycook). Это диагностика источника: она не заменяет проверку
запечённых файлов и видимости, но отделяет «в источнике пусто» от «в источнике есть, но не дошло до экрана».
"""
from __future__ import annotations

import argparse
import collections
import json
import math
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

PARK_TAGS = {("leisure", v) for v in ("park", "garden", "pitch", "playground", "recreation_ground", "nature_reserve", "golf_course")} | \
            {("landuse", v) for v in ("grass", "forest", "recreation_ground", "village_green", "cemetery", "meadow")} | \
            {("natural", v) for v in ("wood", "scrub", "grassland")}
WATER_TAGS = {("natural", "water"), ("natural", "wetland"), ("waterway", "riverbank"), ("landuse", "reservoir")}
TALL_HEIGHT_M = 60.0
TALL_LEVELS = 20


class Projection:
    """Та же проекция, что GeoProjection в han/world/src/osm.cpp: x — на восток, z — на юг."""

    def __init__(self, lat0: float, lon0: float):
        phi = math.radians(lat0)
        self.lat0, self.lon0 = lat0, lon0
        self.m_lat = 111132.92 - 559.82 * math.cos(2 * phi) + 1.175 * math.cos(4 * phi)
        self.m_lon = 111412.84 * math.cos(phi) - 93.5 * math.cos(3 * phi)

    def __call__(self, lat: float, lon: float) -> tuple[float, float]:
        return (lon - self.lon0) * self.m_lon, -(lat - self.lat0) * self.m_lat


def parse_number(text: str | None) -> float | None:
    if not text:
        return None
    try:
        value = float(text.split()[0].replace(",", "."))
    except ValueError:
        return None
    return value if math.isfinite(value) and value > 0 else None


def classify(tags: dict[str, str]) -> str | None:
    if tags.get("building") not in (None, "no"):
        return "building"
    if "building:part" in tags:
        return "building_part"
    if "highway" in tags:
        return "road"
    if any((k, v) in WATER_TAGS for k, v in tags.items()) or tags.get("waterway") in ("river", "stream", "canal"):
        return "water"
    if any((k, v) in PARK_TAGS for k, v in tags.items()):
        return "park"
    return None


def inventory(osm_path: Path, projection: Projection, cell: float, bounds: tuple[float, float, float, float]) -> dict:
    nodes: dict[int, tuple[float, float]] = {}
    cells: dict[tuple[int, int], collections.Counter] = collections.defaultdict(collections.Counter)
    totals: collections.Counter = collections.Counter()
    missing_node_ways = 0
    tall: list[tuple[float, float, float, float]] = []  # (высота или этажи*3.2, x, z, источник: 1 высота, 0 этажи)
    for _, el in ET.iterparse(osm_path, events=("end",)):
        if el.tag == "node":
            nodes[int(el.get("id"))] = projection(float(el.get("lat")), float(el.get("lon")))
            el.clear()
        elif el.tag == "way":
            tags = {t.get("k"): t.get("v") for t in el.findall("tag")}
            kind = classify(tags)
            if kind:
                points = [nodes[int(n.get("ref"))] for n in el.findall("nd") if int(n.get("ref")) in nodes]
                refs = len(el.findall("nd"))
                if len(points) < refs:
                    missing_node_ways += 1
                if points:
                    x, z = points[0]
                    key = (math.floor(x / cell), math.floor(z / cell))
                    cells[key][kind] += 1
                    totals[kind] += 1
                    if kind == "building":
                        height = parse_number(tags.get("height"))
                        levels = parse_number(tags.get("building:levels"))
                        if height is not None:
                            cells[key]["with_height"] += 1
                        elif levels is not None:
                            cells[key]["with_levels"] += 1
                        else:
                            cells[key]["no_height_info"] += 1
                        estimate = height if height is not None else (levels * 3.2 if levels is not None else None)
                        if estimate is not None and (height is not None and height >= TALL_HEIGHT_M or levels is not None and levels >= TALL_LEVELS):
                            cells[key]["tall"] += 1
                            tall.append((estimate, x, z, 1.0 if height is not None else 0.0))
            el.clear()
        elif el.tag == "relation":
            totals["relation"] += 1
            el.clear()
    min_x, min_z, max_x, max_z = bounds
    region, empty_in_region, with_content_outside = [], [], []
    for cx in range(math.floor(min_x / cell) - 1, math.ceil(max_x / cell) + 1):
        for cz in range(math.floor(min_z / cell) - 1, math.ceil(max_z / cell) + 1):
            center = ((cx + 0.5) * cell, (cz + 0.5) * cell)
            inside = min_x <= center[0] <= max_x and min_z <= center[1] <= max_z
            counts = cells.get((cx, cz), collections.Counter())
            content = counts["building"] + counts["road"] + counts["park"] + counts["water"] + counts["building_part"]
            if inside:
                region.append((cx, cz))
                if content == 0:
                    empty_in_region.append((cx, cz))
            elif content:
                with_content_outside.append((cx, cz))
    tall.sort(reverse=True)
    return {
        "source": str(osm_path), "cell_m": cell, "bounds": list(bounds),
        "totals": dict(totals), "ways_with_missing_nodes": missing_node_ways,
        "cells_in_bake_region": len(region), "cells_without_osm_content_in_region": len(empty_in_region),
        "cells_with_content_outside_region": len(with_content_outside),
        "buildings_height_info": {"with_height": sum(c["with_height"] for c in cells.values()), "with_levels": sum(c["with_levels"] for c in cells.values()),
                                  "none": sum(c["no_height_info"] for c in cells.values())},
        "tall_buildings": {"threshold": f"height>={TALL_HEIGHT_M} m or levels>={TALL_LEVELS}", "count": len(tall),
                           "top": [{"est_height_m": round(h, 1), "x": round(x), "z": round(z), "from_height_tag": bool(s)} for h, x, z, s in tall[:15]]},
        "empty_cells_sample": [list(c) for c in empty_in_region[:40]],
        "_cells": cells,
    }


def write_png(report: dict, path: Path, bounds: tuple[float, float, float, float], cell: float) -> None:
    from PIL import Image  # необязательная зависимость диагностики
    cells = report["_cells"]
    min_cx, max_cx = math.floor(bounds[0] / cell), math.ceil(bounds[2] / cell)
    min_cz, max_cz = math.floor(bounds[1] / cell), math.ceil(bounds[3] / cell)
    image = Image.new("RGB", (max_cx - min_cx, max_cz - min_cz), (16, 16, 24))
    peak = max((c["building"] for c in cells.values()), default=1) or 1
    for (cx, cz), c in cells.items():
        if not (min_cx <= cx < max_cx and min_cz <= cz < max_cz):
            continue
        r = min(255, int(40 + 215 * c["building"] / peak))
        g = min(255, 60 + 8 * c["road"] // 4)
        b = 220 if c["water"] else (90 if c["park"] else 30)
        image.putpixel((cx - min_cx, cz - min_cz), (r, g, b))
    path.parent.mkdir(parents=True, exist_ok=True)
    image.resize((image.width * 8, image.height * 8), Image.NEAREST).save(path)


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("osm", type=Path)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--origin", type=float, nargs=2, default=(37.557, 126.914))
    ap.add_argument("--cell", type=float, default=256.0)
    ap.add_argument("--bounds", type=float, nargs=4, default=(-3000, -2500, 17500, 9800))
    ap.add_argument("--png", type=Path)
    args = ap.parse_args(argv)
    report = inventory(args.osm, Projection(*args.origin), args.cell, tuple(args.bounds))
    if args.png:
        write_png(report, args.png, tuple(args.bounds), args.cell)
    report.pop("_cells")
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps({k: report[k] for k in ("totals", "cells_in_bake_region", "cells_without_osm_content_in_region", "cells_with_content_outside_region",
                                               "buildings_height_info")}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    sys.exit(main())
