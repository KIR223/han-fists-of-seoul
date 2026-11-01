#!/usr/bin/env python3
"""Генератор data/world/metro/line2.json (G17): кольцевая линия 2 сеульского метро — реальные названия станций, цвет линии, приблизительные координаты.

Запуск из корня репозитория: python han/tools/pipeline/make_metro.py
Координаты станций — широта и долгота с точностью до сотен метров (для игры достаточно); в файл пишутся и метры карты (проекция как у GeoProjection,
начало координат — центр Мапо 37.5570, 126.9140). Названия на ru, ko, en пишутся в contracts/loc_requests/A.csv, если ключа ещё нет.
"""
import csv
import json
import math
from pathlib import Path

ORIGIN = (37.5570, 126.9140)
phi = math.radians(ORIGIN[0])
M_LAT = 111132.92 - 559.82 * math.cos(2 * phi) + 1.175 * math.cos(4 * phi)
M_LON = 111412.84 * math.cos(phi) - 93.5 * math.cos(3 * phi)

# id, lat, lon, ru, ko, en — по ходу поезда «по часовой стрелке» (внешнее кольцо)
STATIONS = [
    ("city_hall", 37.5657, 126.9769, "Сичхон (Ратуша)", "시청", "City Hall"),
    ("euljiro_1ga", 37.5660, 126.9826, "Ыльчжиро-ибку", "을지로입구", "Euljiro 1-ga"),
    ("euljiro_3ga", 37.5662, 126.9926, "Ыльчжиро-самга", "을지로3가", "Euljiro 3-ga"),
    ("euljiro_4ga", 37.5667, 126.9981, "Ыльчжиро-сага", "을지로4가", "Euljiro 4-ga"),
    ("dongdaemun_dhcp", 37.5651, 127.0079, "Тондэмун Ёксамунхвагонвон", "동대문역사문화공원", "Dongdaemun History & Culture Park"),
    ("sindang", 37.5656, 127.0174, "Синдан", "신당", "Sindang"),
    ("sangwangsimni", 37.5644, 127.0293, "Санван-симни", "상왕십리", "Sangwangsimni"),
    ("wangsimni", 37.5612, 127.0371, "Ванъсимни", "왕십리", "Wangsimni"),
    ("hanyang_univ", 37.5555, 127.0439, "Ханян-тэ", "한양대", "Hanyang Univ."),
    ("ttukseom", 37.5474, 127.0473, "Ттуксом", "뚝섬", "Ttukseom"),
    ("seongsu", 37.5445, 127.0559, "Сонсу", "성수", "Seongsu"),
    ("konkuk_univ", 37.5404, 127.0693, "Конгук-тэибку", "건대입구", "Konkuk Univ."),
    ("gangbyeon", 37.5351, 127.0945, "Кангбён", "강변", "Gangbyeon"),
    ("jamsilnaru", 37.5207, 127.1037, "Чамсиллару", "잠실나루", "Jamsilnaru"),
    ("jamsil", 37.5133, 127.1001, "Чамсиль", "잠실", "Jamsil"),
    ("sports_complex", 37.5110, 127.0735, "Чонхап-ундонджан", "종합운동장", "Sports Complex"),
    ("samseong", 37.5088, 127.0631, "Самсон", "삼성", "Samseong"),
    ("seolleung", 37.5045, 127.0490, "Соллын", "선릉", "Seolleung"),
    ("yeoksam", 37.5006, 127.0364, "Ёксам", "역삼", "Yeoksam"),
    ("gangnam", 37.4979, 127.0276, "Каннам", "강남", "Gangnam"),
    ("gyodae", 37.4934, 127.0140, "Кёдэ", "교대", "Seoul Nat'l Univ. of Education"),
    ("seocho", 37.4916, 127.0078, "Сочхо", "서초", "Seocho"),
    ("bangbae", 37.4814, 126.9976, "Пангбэ", "방배", "Bangbae"),
    ("sadang", 37.4766, 126.9816, "Садан", "사당", "Sadang"),
    ("nakseongdae", 37.4770, 126.9637, "Наксондэ", "낙성대", "Nakseongdae"),
    ("snu", 37.4812, 126.9527, "Соултэибку", "서울대입구", "Seoul Nat'l Univ."),
    ("bongcheon", 37.4824, 126.9418, "Пончхон", "봉천", "Bongcheon"),
    ("sillim", 37.4842, 126.9296, "Синним", "신림", "Sillim"),
    ("sindaebang", 37.4872, 126.9131, "Синтэбан", "신대방", "Sindaebang"),
    ("guro_digital", 37.4853, 126.9014, "Куро Диджитал Тандзи", "구로디지털단지", "Guro Digital Complex"),
    ("daerim", 37.4927, 126.8957, "Тэрим", "대림", "Daerim"),
    ("sindorim", 37.5089, 126.8913, "Синдорим", "신도림", "Sindorim"),
    ("mullae", 37.5179, 126.8949, "Муллэ", "문래", "Mullae"),
    ("yeongdeungpo_gu_office", 37.5254, 126.8967, "Ёндынпхо-куччхон", "영등포구청", "Yeongdeungpo-gu Office"),
    ("dangsan", 37.5348, 126.9029, "Тансан", "당산", "Dangsan"),
    ("hapjeong", 37.5496, 126.9139, "Хапчон", "합정", "Hapjeong"),
    ("hongik_univ", 37.5568, 126.9245, "Хондэибку", "홍대입구", "Hongik Univ."),
    ("sinchon", 37.5551, 126.9367, "Синчхон", "신촌", "Sinchon"),
    ("ewha_univ", 37.5568, 126.9464, "Идэ", "이대", "Ewha Womans Univ."),
    ("ahyeon", 37.5573, 126.9560, "Ахён", "아현", "Ahyeon"),
    ("chungjeongno", 37.5601, 126.9636, "Чхунджонно", "충정로", "Chungjeongno"),
]


def to_local(lat, lon):
    return [round((lon - ORIGIN[1]) * M_LON, 1), round(-(lat - ORIGIN[0]) * M_LAT, 1)]


def main():
    out = Path("data/world/metro")
    out.mkdir(parents=True, exist_ok=True)
    stations = []
    rows = {}
    for sid, lat, lon, ru, ko, en in STATIONS:
        key = "world.metro.station." + sid
        stations.append({"id": sid, "name_key": key, "lat": lat, "lon": lon, "pos": to_local(lat, lon)})
        rows[key] = (ru, ko, en)
    doc = {
        "id": "line2", "name_key": "world.metro.line2", "color": "#00A84D", "circular": True,
        "max_speed": 22.0, "acceleration": 1.0, "dwell_seconds": 25.0,
        "service": {"first_hour": 5.5, "last_hour": 24.5, "headway_peak": 150.0, "headway_day": 240.0, "headway_night": 420.0,
                    "peak_hours": [[7.0, 9.5], [17.0, 20.0]], "night_hours": [[21.5, 24.5], [5.5, 6.5]]},
        "stations": stations,
    }
    (out / "line2.json").write_text(json.dumps(doc, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    csv_path = Path("contracts/loc_requests/A.csv")
    existing = {row[0] for row in csv.reader(csv_path.read_text(encoding="utf-8").splitlines()) if row}
    extra = {"world.metro.line2": ("Линия 2 (зелёная, кольцевая)", "2호선", "Line 2 (Green, circular)")}
    with csv_path.open("a", encoding="utf-8", newline="") as f:
        w = csv.writer(f, lineterminator="\n")
        for key, (ru, ko, en) in {**extra, **rows}.items():
            if key not in existing:
                w.writerow([key, ru, ko, en, "метро: " + ("линия" if key == "world.metro.line2" else "станция")])
    print("станций:", len(stations))


if __name__ == "__main__":
    main()
