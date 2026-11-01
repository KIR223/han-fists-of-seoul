#!/usr/bin/env python3
"""Загрузка данных OpenStreetMap для города (E10): дороги, здания, парки и вода в рамке → assets_src/osm/<имя>.osm.

    python han/tools/pipeline/han_osm_fetch.py                       # Мапо (Хондэ), файл assets_src/osm/mapo.osm
    python han/tools/pipeline/han_osm_fetch.py --bbox 37.55 126.91 37.57 126.94 --out assets_src/osm/test.osm

Данные © участники OpenStreetMap, лицензия ODbL 1.0 (https://www.openstreetmap.org/copyright). Файлы в Git не кладутся
(они в .gitignore): каждый запускает загрузку сам, рядом пишется файл <имя>.osm.license.txt с указанием источника и даты.
Из данных в игру берётся только геометрия; названия и бренды из тегов не используются.
Только HTTPS, лимит размера ответа, вежливые запросы (User-Agent проекта, рамка не больше MAX_SPAN градусов).
"""
from __future__ import annotations

import argparse
import datetime
import os
import sys
import urllib.error
import urllib.request
from pathlib import Path

DEFAULT_SERVER = "https://overpass-api.de/api/interpreter"
USER_AGENT = "han-fists-of-seoul-dev/0.1 (game prototype; https://github.com/KIR223/han-fists-of-seoul)"
MAX_BYTES = 200 * 1024 * 1024
MAX_SPAN = 0.03  # градусов по каждой стороне рамки (~3 км): больше — ходите по кускам
PRESETS = {"mapo": (37.5545, 126.9190, 37.5600, 126.9270)}  # юг, запад, север, восток: район Хондэ


class FetchError(Exception):
    pass


def validate_bbox(bbox, allow_large=False):
    south, west, north, east = bbox
    if not (-90 <= south < north <= 90 and -180 <= west < east <= 180):
        raise FetchError(f"неверная рамка: {bbox} (нужно юг < север, запад < восток в градусах)")
    if not allow_large and (north - south > MAX_SPAN or east - west > MAX_SPAN):
        raise FetchError(f"рамка больше {MAX_SPAN}° по стороне: скачивайте частями или добавьте --allow-large")
    return bbox


def build_query(bbox) -> str:
    s, w, n, e = bbox
    box = f"({s},{w},{n},{e})"
    return ("[out:xml][timeout:90];("
            f'way["highway"]{box};way["building"]{box};way["natural"="water"]{box};way["waterway"="riverbank"]{box};'
            f'way["leisure"="park"]{box};way["leisure"="garden"]{box};way["landuse"="grass"]{box};way["landuse"="forest"]{box};'
            ");(._;>;);out body;")


def download(server: str, query: str, timeout: float = 120.0) -> bytes:
    if not server.lower().startswith("https://"):
        raise FetchError("разрешён только HTTPS-сервер")
    from urllib.parse import urlencode
    request = urllib.request.Request(server, data=urlencode({"data": query}).encode("utf-8"),
                                     headers={"User-Agent": USER_AGENT, "Accept": "*/*"})
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            chunks, total = [], 0
            while True:
                block = response.read(1 << 20)
                if not block:
                    break
                total += len(block)
                if total > MAX_BYTES:
                    raise FetchError(f"ответ больше {MAX_BYTES // (1024 * 1024)} МБ: сузьте рамку")
                chunks.append(block)
            return b"".join(chunks)
    except urllib.error.HTTPError as e:
        raise FetchError(f"сервер ответил {e.code} {e.reason}") from e
    except urllib.error.URLError as e:
        raise FetchError(f"нет связи с сервером: {e.reason}") from e


def check_osm(data: bytes) -> int:
    """Грубая проверка ответа: это XML OSM и в нём есть линии. Возвращает число линий."""
    head = data[:2000].decode("utf-8", errors="replace")
    if "<osm" not in head:
        raise FetchError("ответ не похож на файл OSM (возможно, сервер вернул ошибку): " + head[:120].replace("\n", " "))
    if b"<remark>" in data[-2000:] and b"runtime error" in data[-2000:]:
        raise FetchError("сервер оборвал выполнение запроса (слишком большая рамка или перегрузка)")
    ways = data.count(b"<way ")
    if ways == 0:
        raise FetchError("в рамке нет ни одной линии (дорог, зданий): проверьте координаты")
    return ways


def write_result(data: bytes, out: Path, bbox, server: str) -> None:
    out.parent.mkdir(parents=True, exist_ok=True)
    tmp = out.with_suffix(out.suffix + ".part")
    tmp.write_bytes(data)
    os.replace(tmp, out)  # файл появляется целиком или не появляется
    note = (f"Данные OpenStreetMap, © участники OpenStreetMap, лицензия ODbL 1.0: https://www.openstreetmap.org/copyright\n"
            f"Источник: {server}\nРамка (юг запад север восток): {' '.join(str(v) for v in bbox)}\n"
            f"Скачано: {datetime.datetime.now(datetime.timezone.utc).strftime('%Y-%m-%d %H:%M UTC')}\n"
            "В игру берётся только геометрия; названия и бренды из тегов не используются.\n")
    out.with_name(out.name + ".license.txt").write_text(note, encoding="utf-8")


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description="Загрузка данных OpenStreetMap (Overpass API)")
    parser.add_argument("--preset", choices=sorted(PRESETS), default="mapo")
    parser.add_argument("--bbox", nargs=4, type=float, metavar=("ЮГ", "ЗАПАД", "СЕВЕР", "ВОСТОК"))
    parser.add_argument("--out", type=Path)
    parser.add_argument("--server", default=DEFAULT_SERVER)
    parser.add_argument("--allow-large", action="store_true")
    parser.add_argument("--force", action="store_true", help="перезаписать существующий файл")
    args = parser.parse_args(argv)
    try:
        bbox = validate_bbox(tuple(args.bbox) if args.bbox else PRESETS[args.preset], args.allow_large)
        out = args.out or Path("assets_src") / "osm" / f"{args.preset if not args.bbox else 'custom'}.osm"
        if out.exists() and not args.force:
            print(f"{out} уже есть (--force — скачать заново)")
            return 0
        data = download(args.server, build_query(bbox))
        ways = check_osm(data)
        write_result(data, out, bbox, args.server)
    except FetchError as e:
        print(f"ОШИБКА: {e}", file=sys.stderr)
        return 1
    print(f"готово: {out} ({len(data) // 1024} КБ, линий {ways}). © участники OpenStreetMap (ODbL)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
