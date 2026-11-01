#!/usr/bin/env python3
"""Генератор data/world/districts/*.json из контуров в широте и долготе (проекция та же, что GeoProjection в han_world).

Запуск из корня репозитория: python han/tools/pipeline/make_districts.py
Контуры приблизительные (кварталы по реальной географии Мапо); границы можно править здесь и пересоздавать файлы.
Начало координат игры — центр выгрузки OSM «Мапо»: широта 37.5570, долгота 126.9140.
"""
import json
import math
from pathlib import Path

ORIGIN = (37.5570, 126.9140)
phi = math.radians(ORIGIN[0])
M_LAT = 111132.92 - 559.82 * math.cos(2 * phi) + 1.175 * math.cos(4 * phi)
M_LON = 111412.84 * math.cos(phi) - 93.5 * math.cos(3 * phi)


def to_local(lat, lon):
    return [round((lon - ORIGIN[1]) * M_LON, 1), round(-(lat - ORIGIN[0]) * M_LAT, 1)]


def rect(south, west, north, east):
    return [to_local(north, west), to_local(north, east), to_local(south, east), to_local(south, west)]  # по часовой на карте


DISTRICTS = [
    {"id": "mangwon", "numeric_id": 1, "kind": "city_district", "bounds": rect(37.5520, 126.8990, 37.5640, 126.9090), "danger": 1,
     "tags": ["market", "residential", "start"],
     "ambience": {"lut": "market_amber", "saturation": 1.1, "contrast": 1.15, "neon": 0.7, "lamp_color": [1.0, 0.72, 0.40], "crowd_density": 1.0,
                  "music_key": "world.music.mangwon_market"}},
    {"id": "hapjeong", "numeric_id": 2, "kind": "city_district", "bounds": rect(37.5490, 126.9090, 37.5600, 126.9160), "danger": 1,
     "tags": ["crossroads", "cafes", "start"],
     "ambience": {"lut": "neutral", "saturation": 1.0, "contrast": 1.2, "neon": 1.0, "lamp_color": [1.0, 0.80, 0.55], "crowd_density": 1.1,
                  "music_key": "world.music.hapjeong"}},
    {"id": "hongdae", "numeric_id": 3, "kind": "city_district", "bounds": rect(37.5500, 126.9160, 37.5590, 126.9280), "danger": 2,
     "tags": ["youth", "clubs", "buskers", "start"],
     "ambience": {"lut": "pink_violet", "saturation": 1.35, "contrast": 1.15, "neon": 1.8, "lamp_color": [1.0, 0.62, 0.55], "crowd_density": 1.6,
                  "music_key": "world.music.hongdae_buskers"}},
    {"id": "yeonnam", "numeric_id": 4, "kind": "city_district", "bounds": rect(37.5590, 126.9180, 37.5660, 126.9300), "danger": 0,
     "tags": ["cafes", "park", "start"],
     "ambience": {"lut": "warm_cafe", "saturation": 1.2, "contrast": 1.1, "neon": 0.9, "lamp_color": [1.0, 0.78, 0.50], "crowd_density": 1.1,
                  "music_key": "world.music.yeonnam"}},
]


def main():
    out = Path("data/world/districts")
    out.mkdir(parents=True, exist_ok=True)
    for d in DISTRICTS:
        doc = {"id": d["id"], "numeric_id": d["numeric_id"], "name_key": "world.district." + d["id"], "kind": d["kind"], "bounds": d["bounds"],
               "sacred_allowed": False, "danger": d["danger"], "tags": d["tags"], "ambience": d["ambience"]}
        (out / (d["id"] + ".json")).write_text(json.dumps(doc, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        print("записан", out / (d["id"] + ".json"))


if __name__ == "__main__":
    main()
