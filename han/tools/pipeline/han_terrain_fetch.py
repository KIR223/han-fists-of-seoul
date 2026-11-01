#!/usr/bin/env python3
"""Рельеф карты (E17): скачивает открытые тайлы высот Terrarium (AWS Open Data «Terrain Tiles»: SRTM и другие открытые источники),
пересчитывает в регулярную сетку в метрах карты (проекция GeoProjection, начало — центр Мапо) и пишет компактный файл .hht.

    python han/tools/pipeline/han_terrain_fetch.py                       # вся игровая зона (кольцо метро и Мапо), шаг 25 м
    python han/tools/pipeline/han_terrain_fetch.py --bounds -3000 -3000 6000 4000 --cell 15 --out assets_bin/terrain/mapo.hht

Формат .hht (little-endian): "HHT1", int32 width, int32 height, float32 minX, minZ, cell, float32[width*height] высоты (м над уровнем моря), строки по z.
Файл в Git не кладётся (assets_bin/); данные Terrain Tiles: открытая лицензия, источники SRTM, ASTER и др. — атрибуция в docs/credits/A.md.
"""
import argparse
import io
import math
import struct
import sys
import urllib.request
from pathlib import Path

ORIGIN = (37.5570, 126.9140)
URL = "https://s3.amazonaws.com/elevation-tiles-prod/terrarium/{z}/{x}/{y}.png"
UA = "han-fists-of-seoul/0.1 (terrain fetch)"

phi = math.radians(ORIGIN[0])
M_LAT = 111132.92 - 559.82 * math.cos(2 * phi) + 1.175 * math.cos(4 * phi)
M_LON = 111412.84 * math.cos(phi) - 93.5 * math.cos(3 * phi)


def to_geo(x, z):
    return ORIGIN[0] - z / M_LAT, ORIGIN[1] + x / M_LON


def tile_of(lat, lon, zoom):
    n = 2 ** zoom
    x = (lon + 180.0) / 360.0 * n
    y = (1.0 - math.asinh(math.tan(math.radians(lat))) / math.pi) / 2.0 * n
    return x, y  # дробные координаты в тайлах


def fetch_tile(z, x, y, cache):
    key = (z, x, y)
    if key in cache:
        return cache[key]
    from PIL import Image
    req = urllib.request.Request(URL.format(z=z, x=x, y=y), headers={"User-Agent": UA})
    with urllib.request.urlopen(req, timeout=60) as r:
        data = r.read()
    im = Image.open(io.BytesIO(data)).convert("RGB")
    cache[key] = im
    return im


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bounds", type=float, nargs=4, default=[-3000, -2500, 17500, 9800], metavar=("MINX", "MINZ", "MAXX", "MAXZ"), help="область в метрах карты")
    ap.add_argument("--cell", type=float, default=25.0, help="шаг сетки, м")
    ap.add_argument("--zoom", type=int, default=12, help="масштаб тайлов Terrarium (12 — около 30 м на пиксель)")
    ap.add_argument("--out", default="assets_bin/terrain/seoul.hht")
    args = ap.parse_args()
    minx, minz, maxx, maxz = args.bounds
    w = int((maxx - minx) / args.cell) + 1
    h = int((maxz - minz) / args.cell) + 1
    cache = {}
    heights = []
    for j in range(h):
        row = []
        for i in range(w):
            lat, lon = to_geo(minx + i * args.cell, minz + j * args.cell)
            tx, ty = tile_of(lat, lon, args.zoom)
            # билинейная выборка по пикселям мозаики
            fx, fy = tx * 256.0 - 0.5, ty * 256.0 - 0.5
            x0, y0 = math.floor(fx), math.floor(fy)
            acc = 0.0
            for dy in (0, 1):
                for dx in (0, 1):
                    px, py = x0 + dx, y0 + dy
                    tile = fetch_tile(args.zoom, px // 256, py // 256, cache)
                    r, g, b = tile.getpixel((px % 256, py % 256))
                    elev = r * 256.0 + g + b / 256.0 - 32768.0
                    acc += elev * (1 - abs(fx - px)) * (1 - abs(fy - py))
            row.append(acc)
        heights.append(row)
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    with out.open("wb") as f:
        f.write(b"HHT1")
        f.write(struct.pack("<iifff", w, h, minx, minz, args.cell))
        for row in heights:
            f.write(struct.pack("<%df" % w, *row))
    flat = [v for row in heights for v in row]
    print("записано %s: %dx%d, шаг %.0f м, тайлов %d, высоты от %.0f до %.0f м" % (out, w, h, args.cell, len(cache), min(flat), max(flat)))


if __name__ == "__main__":
    main()
