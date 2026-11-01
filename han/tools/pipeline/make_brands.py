#!/usr/bin/env python3
"""Генератор вымышленных брендов (G38, часть 13 дизайна): data/city/brands.json, assets_src/brands/*.svg, строки локализации.

Запуск из корня репозитория: python han/tools/pipeline/make_brands.py
Логотипы — свои простые SVG (прямоугольники, круги, эллипсы, многоугольники в 64 x 64), без чужих знаков; названия — выдуманные
(проверка по han/tools/pipeline/real_brands.txt). Тексты названий (ru, ko, en) пишутся в contracts/loc_requests/A.csv, если ключа ещё нет.
"""
import csv
import json
import sys
from pathlib import Path

# id, сфера, (ru, ko, en), цвета, фигуры логотипа
C = "circle"
R = "rect"
P = "polygon"
E = "ellipse"
BRANDS = [
    ("ilsang24", "convenience", ("Ильсан 24", "일상24", "Ilsang 24"), ["#1B8F4E", "#FFD400"],
     [(C, dict(cx=32, cy=32, r=26, fill="#FFD400")), (C, dict(cx=32, cy=32, r=20, fill="#1B8F4E")), (R, dict(x=30, y=16, width=4, height=17, fill="#FFD400")), (R, dict(x=30, y=30, width=13, height=4, fill="#FFD400"))]),
    ("dalbit_mart", "convenience", ("Дальбит Март", "달빛마트", "Dalbit Mart"), ["#2B3A8C", "#F4C542"],
     [(C, dict(cx=32, cy=32, r=26, fill="#F4C542")), (C, dict(cx=40, cy=26, r=21, fill="#2B3A8C"))]),
    ("gom_coffee", "coffee", ("Ком Кофе", "곰커피", "Gom Coffee"), ["#6B3E26", "#F2E3C6"],
     [(C, dict(cx=14, cy=14, r=9, fill="#F2E3C6")), (C, dict(cx=50, cy=14, r=9, fill="#F2E3C6")), (C, dict(cx=32, cy=34, r=24, fill="#F2E3C6")), (C, dict(cx=32, cy=42, r=8, fill="#6B3E26")), (C, dict(cx=23, cy=28, r=3, fill="#6B3E26")), (C, dict(cx=41, cy=28, r=3, fill="#6B3E26"))]),
    ("coffee_bangbang", "coffee", ("Кофе Бан-Бан", "커피 뱅뱅", "Coffee Bang Bang"), ["#E83A2F", "#FFFFFF"],
     [(R, dict(x=6, y=6, width=52, height=52, rx=10, fill="#FFFFFF")), (P, dict(points="34,8 18,36 30,36 26,56 46,26 34,26", fill="#E83A2F"))]),
    ("chicken_teacher", "food", ("Чикен Тичер", "치킨 선생", "Chicken Teacher"), ["#F2A900", "#7A1E1E"],
     [(C, dict(cx=32, cy=36, r=22, fill="#FFFFFF")), (P, dict(points="22,10 32,22 42,10 38,26 26,26", fill="#7A1E1E")), (P, dict(points="26,34 38,34 32,46", fill="#F2A900")), (C, dict(cx=24, cy=30, r=3, fill="#7A1E1E")), (C, dict(cx=40, cy=30, r=3, fill="#7A1E1E"))]),
    ("crispy_ajumma", "food", ("Криспи Аджумма", "바삭 아줌마", "Crispy Ajumma"), ["#D62828", "#FFD166"],
     [(E, dict(cx=32, cy=36, rx=26, ry=20, fill="#FFD166")), (C, dict(cx=32, cy=16, r=12, fill="#D62828")), (C, dict(cx=22, cy=16, r=5, fill="#FFD166")), (C, dict(cx=42, cy=16, r=5, fill="#FFD166"))]),
    ("kimbap_station", "food", ("Кимбап Стейшн", "김밥 스테이션", "Kimbap Station"), ["#2A9D8F", "#F4F1DE"],
     [(R, dict(x=6, y=14, width=52, height=36, rx=6, fill="#F4F1DE")), (C, dict(cx=20, cy=32, r=10, fill="#2A9D8F")), (C, dict(cx=44, cy=32, r=10, fill="#2A9D8F")), (R, dict(x=26, y=8, width=12, height=48, fill="#2A9D8F"))]),
    ("bulgogi_bros", "food", ("Пульгоги Брос", "불고기 브로스", "Bulgogi Bros"), ["#1D3557", "#E63946"],
     [(P, dict(points="32,4 48,30 40,30 52,58 12,58 24,30 16,30", fill="#E63946")), (C, dict(cx=32, cy=44, r=9, fill="#F1FAEE"))]),
    ("pizza_ajumma", "food", ("Пицца Аджумма", "피자 아줌마", "Pizza Ajumma"), ["#E76F51", "#264653"],
     [(P, dict(points="8,12 56,12 32,58", fill="#F4A261")), (C, dict(cx=24, cy=22, r=5, fill="#E76F51")), (C, dict(cx=40, cy=22, r=5, fill="#E76F51")), (C, dict(cx=32, cy=38, r=5, fill="#E76F51"))]),
    ("levelup_pc", "other", ("Левел-ап ПК", "레벨업 PC", "Level Up PC"), ["#7B2CBF", "#00F5D4"],
     [(R, dict(x=8, y=10, width=48, height=34, rx=4, fill="#00F5D4")), (R, dict(x=14, y=16, width=36, height=22, fill="#7B2CBF")), (R, dict(x=26, y=44, width=12, height=8, fill="#00F5D4")), (R, dict(x=16, y=52, width=32, height=5, fill="#00F5D4"))]),
    ("singsing_room", "other", ("Син-Син Рум", "싱싱 노래방", "Sing Sing Room"), ["#FF006E", "#8338EC"],
     [(E, dict(cx=32, cy=24, rx=14, ry=18, fill="#FFFFFF")), (R, dict(x=28, y=40, width=8, height=18, fill="#FFFFFF")), (R, dict(x=18, y=54, width=28, height=5, rx=2, fill="#FFFFFF"))]),
    ("snail_queen", "other", ("Снейл Квин", "달팽이 퀸", "Snail Queen"), ["#C9ADA7", "#4A4E69"],
     [(C, dict(cx=36, cy=30, r=22, fill="#4A4E69")), (C, dict(cx=36, cy=30, r=13, fill="#C9ADA7")), (C, dict(cx=36, cy=30, r=6, fill="#4A4E69")), (R, dict(x=6, y=46, width=48, height=8, rx=4, fill="#4A4E69"))]),
    ("glow_seoul", "other", ("Глоу Сеул", "글로우 서울", "Glow Seoul"), ["#FFB5A7", "#6D6875"],
     [(C, dict(cx=32, cy=32, r=12, fill="#FFFFFF")), (P, dict(points="32,2 36,24 32,28 28,24", fill="#FFFFFF")), (P, dict(points="32,62 28,40 32,36 36,40", fill="#FFFFFF")), (P, dict(points="2,32 24,28 28,32 24,36", fill="#FFFFFF")), (P, dict(points="62,32 40,36 36,32 40,28", fill="#FFFFFF"))]),
    ("hantel", "telecom", ("Хантел", "한텔", "HanTel"), ["#0077B6", "#FFFFFF"],
     [(C, dict(cx=32, cy=32, r=26, fill="#FFFFFF")), (C, dict(cx=32, cy=32, r=18, fill="#0077B6")), (C, dict(cx=32, cy=32, r=9, fill="#FFFFFF")), (C, dict(cx=32, cy=32, r=3, fill="#0077B6"))]),
    ("pado_mobile", "telecom", ("Пандо Мобайл", "파도 모바일", "Pado Mobile"), ["#FF7B00", "#2D2D2D"],
     [(P, dict(points="4,40 16,26 28,40 40,26 52,40 60,32 60,58 4,58", fill="#2D2D2D")), (C, dict(cx=44, cy=16, r=9, fill="#FFFFFF"))]),
    ("ttang_bank", "bank", ("Тхан Банк", "땅 은행", "Ttang Bank"), ["#355E3B", "#FFD700"],
     [(R, dict(x=6, y=40, width=52, height=18, fill="#FFD700")), (P, dict(points="32,6 58,34 6,34", fill="#FFD700")), (R, dict(x=16, y=30, width=6, height=12, fill="#355E3B")), (R, dict(x=29, y=30, width=6, height=12, fill="#355E3B")), (R, dict(x=42, y=30, width=6, height=12, fill="#355E3B"))]),
    ("bori_card", "bank", ("Бори Кард", "보리카드", "Bori Card"), ["#8E7DBE", "#FFFFFF"],
     [(R, dict(x=4, y=14, width=56, height=36, rx=6, fill="#FFFFFF")), (R, dict(x=4, y=22, width=56, height=8, fill="#8E7DBE")), (R, dict(x=10, y=38, width=18, height=5, rx=2, fill="#8E7DBE"))]),
    ("hwasan_ramyeon", "food", ("Хвасан Рамён", "화산 라면", "Hwasan Ramyeon"), ["#B80C09", "#FFB400"],
     [(P, dict(points="4,58 22,22 32,34 42,18 60,58", fill="#FFB400")), (P, dict(points="28,18 32,4 38,20 33,16", fill="#B80C09"))]),
    ("pureun_bam", "beer", ("Пурын Бам", "푸른 밤", "Pureun Bam"), ["#1A3A6B", "#9AD1D4"],
     [(C, dict(cx=32, cy=32, r=26, fill="#9AD1D4")), (C, dict(cx=42, cy=24, r=20, fill="#1A3A6B")), (C, dict(cx=16, cy=48, r=3, fill="#FFFFFF")), (C, dict(cx=26, cy=54, r=2, fill="#FFFFFF"))]),
    ("haneul_lager", "beer", ("Ханыль Лагер", "하늘 라거", "Haneul Lager"), ["#5AA9E6", "#FFFFFF"],
     [(R, dict(x=14, y=12, width=30, height=44, rx=6, fill="#FFFFFF")), (R, dict(x=44, y=22, width=10, height=22, rx=5, fill="#FFFFFF")), (R, dict(x=14, y=12, width=30, height=10, rx=5, fill="#5AA9E6"))]),
    ("banana_moo", "food", ("Банана Му", "바나나 무", "Banana Moo"), ["#FFE45E", "#6B4F1D"],
     [(E, dict(cx=32, cy=34, rx=24, ry=14, fill="#FFE45E")), (C, dict(cx=22, cy=30, r=3, fill="#6B4F1D")), (C, dict(cx=42, cy=30, r=3, fill="#6B4F1D")), (E, dict(cx=32, cy=40, rx=6, ry=3, fill="#6B4F1D"))]),
    ("volt_tiger", "other", ("Вольт Тайгер", "볼트 타이거", "Volt Tiger"), ["#00C2FF", "#FFD60A"],
     [(R, dict(x=6, y=6, width=52, height=52, rx=12, fill="#FFD60A")), (P, dict(points="36,8 16,36 28,36 24,56 48,26 36,26", fill="#00C2FF"))]),
    ("kumbit_shop", "electronics", ("Кымбит Электроникс", "금빛 전자", "Kumbit Electronics"), ["#3A0CA3", "#F72585"],
     [(C, dict(cx=32, cy=32, r=26, fill="#F72585")), (R, dict(x=22, y=14, width=20, height=36, rx=4, fill="#3A0CA3")), (C, dict(cx=32, cy=44, r=3, fill="#F72585"))]),
]


def svg_of(shapes):
    out = ['<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64">']
    for kind, attrs in shapes:
        out.append("  <%s %s/>" % (kind, " ".join('%s="%s"' % (k, v) for k, v in attrs.items())))
    out.append("</svg>")
    return "\n".join(out) + "\n"


def main():
    blocklist = [w.strip().lower() for w in Path("han/tools/pipeline/real_brands.txt").read_text(encoding="utf-8").splitlines() if w.strip() and not w.startswith("#")]
    Path("assets_src/brands").mkdir(parents=True, exist_ok=True)
    Path("data/city").mkdir(parents=True, exist_ok=True)
    brands = []
    loc_rows = {}
    for bid, sector, (ru, ko, en), colors, shapes in BRANDS:
        flat = (bid + en).lower().replace(" ", "").replace("-", "")
        for word in blocklist:
            if word in flat:
                sys.exit("название похоже на реальный бренд (%s): %s" % (word, en))
        Path("assets_src/brands/%s.svg" % bid).write_text(svg_of(shapes), encoding="utf-8")
        brands.append({"id": bid, "name_key": "city.brand.%s" % bid, "sector": sector, "colors": colors, "logo": "brands/%s.svg" % bid,
                       "appears_in": ["city_district"], "fictional": True})
        loc_rows["city.brand.%s" % bid] = (ru, ko, en)
    Path("data/city/brands.json").write_text(json.dumps(brands, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    csv_path = Path("contracts/loc_requests/A.csv")
    existing = {row[0] for row in csv.reader(csv_path.read_text(encoding="utf-8").splitlines()) if row}
    with csv_path.open("a", encoding="utf-8", newline="") as f:
        w = csv.writer(f, lineterminator="\n")
        for key, (ru, ko, en) in loc_rows.items():
            if key not in existing:
                w.writerow([key, ru, ko, en, "название вымышленного бренда (вывески)"])
    print("брендов:", len(brands))


if __name__ == "__main__":
    main()
