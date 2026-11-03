"""Готовые помещения первых этажей (как в GTA V — магазины, в которые можно зайти):
одежда (옷가게), закусочная-분식집, круглосуточный (편의점), кафе (카페), косметика (화장품).
Каждое помещение: пол, стены, потолок со светильниками, мебель и товар, витрина с дверным проёмом.
Геометрия строится в системе стены WallFrame (t вдоль фасада, y вверх, o наружу; помещение при o < 0)."""
import math
import random

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

from facade3d import WallFrame, _q, face, side, horiz, obox

# ------------------------------------------------------------------ текстуры помещений
PRODUCT_WORDS = ["라면", "과자", "우유", "주스", "커피", "생수", "초코", "감자칩", "녹차", "콜라", "사이다", "김", "참치", "햇반",
                 "새우맛", "딸기", "바나나", "아몬드", "젤리", "껌", "두유", "요구르트", "에너지", "이온"]


def _font(size, bold=True):
    import assets
    return assets.font(size, bold)


def product_atlas(out, S=2048):
    """8 × 8 ячеек: коробки, пакеты, бутылки, стаканчики рамёна — вымышленная упаковка с хангылем."""
    rng = random.Random(5)
    cell = S // 8
    im = Image.new("RGB", (S, S), (30, 30, 32))
    d = ImageDraw.Draw(im)
    for i in range(64):
        cx, cy = (i % 8) * cell, (i // 8) * cell
        kind = i % 4
        n_items = rng.choice([3, 4, 5, 6])
        w = cell // n_items
        for k in range(n_items):
            x0 = cx + k * w + 2
            col = rng.choice([(200, 40, 40), (240, 200, 40), (40, 90, 180), (30, 150, 80), (240, 120, 30), (230, 230, 225), (120, 40, 140), (20, 20, 20), (250, 160, 190)])
            col2 = tuple(max(0, min(255, c + rng.randint(-60, 60))) for c in col)
            word = rng.choice(PRODUCT_WORDS)
            if kind == 0:  # коробки
                d.rectangle([x0, cy + 20, x0 + w - 4, cy + cell - 2], fill=col)
                d.rectangle([x0, cy + cell * 0.55, x0 + w - 4, cy + cell * 0.75], fill=col2)
            elif kind == 1:  # бутылки
                bw = w - 10
                d.rectangle([x0 + 3, cy + cell * 0.3, x0 + 3 + bw, cy + cell - 2], fill=(min(255, col[0] + 40), min(255, col[1] + 40), min(255, col[2] + 40)))
                d.rectangle([x0 + 3 + bw * 0.3, cy + cell * 0.12, x0 + 3 + bw * 0.7, cy + cell * 0.3], fill=col2)
                d.rectangle([x0 + 3 + bw * 0.25, cy + cell * 0.05, x0 + 3 + bw * 0.75, cy + cell * 0.12], fill=(240, 240, 240))
                d.rectangle([x0 + 3, cy + cell * 0.55, x0 + 3 + bw, cy + cell * 0.78], fill=col)
            elif kind == 2:  # стаканчики рамёна
                d.polygon([(x0, cy + cell * 0.35), (x0 + w - 4, cy + cell * 0.35), (x0 + w - 10, cy + cell - 2), (x0 + 6, cy + cell - 2)], fill=col)
                d.ellipse([x0 + w * 0.2, cy + cell * 0.5, x0 + w * 0.7, cy + cell * 0.8], fill=(250, 240, 210))
            else:  # пакеты снеков
                d.rounded_rectangle([x0, cy + 12, x0 + w - 4, cy + cell - 2], radius=8, fill=col)
                d.ellipse([x0 + w * 0.15, cy + cell * 0.45, x0 + w * 0.75, cy + cell * 0.8], fill=col2)
            try:
                f = _font(max(10, min(26, w // 2)))
                d.text((x0 + (w - 4) / 2, cy + cell * 0.66), word, font=f, fill=(255, 255, 255) if sum(col) < 450 else (20, 20, 20), anchor="mm")
            except Exception:
                pass
        # блик полки и тень
        d.rectangle([cx, cy + cell - 6, cx + cell, cy + cell], fill=(170, 170, 172))
    im = im.filter(ImageFilter.GaussianBlur(0.6))
    im.save(f"{out}/int_products.png")


def drinks_atlas(out, S=1024):
    """Холодильник с напитками: 4 ячейки (двери), ряды бутылок/банок, яркая подсветка."""
    rng = random.Random(8)
    im = Image.new("RGB", (S, S), (235, 240, 245))
    d = ImageDraw.Draw(im)
    half = S // 2
    for c in range(4):
        ox, oy = (c % 2) * half, (c // 2) * half
        d.rectangle([ox, oy, ox + half, oy + half], fill=(225, 232, 240))
        rows = 5
        rh = half // rows
        for r in range(rows):
            y1 = oy + (r + 1) * rh - 4
            d.rectangle([ox + 4, y1, ox + half - 4, y1 + 4], fill=(150, 155, 160))
            x = ox + 8
            can = rng.random() < 0.4
            while x < ox + half - 20:
                w = rng.randint(14, 22) if can else rng.randint(16, 26)
                col = rng.choice([(200, 30, 30), (30, 120, 200), (240, 200, 30), (30, 160, 70), (250, 140, 30), (230, 230, 230), (90, 50, 30), (180, 60, 160)])
                h = int(rh * (0.55 if can else rng.uniform(0.7, 0.85)))
                d.rectangle([x, y1 - h, x + w, y1], fill=col)
                if not can:
                    d.rectangle([x + w * 0.3, y1 - h - 8, x + w * 0.7, y1 - h], fill=(240, 240, 240))
                d.rectangle([x, y1 - h * 0.6, x + w, y1 - h * 0.35], fill=tuple(min(255, v + 70) for v in col))
                x += w + rng.randint(2, 5)
    im.save(f"{out}/int_drinks.png")
    Image.fromarray((np.asarray(im, np.float32) * 0.9).astype(np.uint8)).save(f"{out}/int_drinks_E.png")


def menu_atlas(out, S=2048):
    """2 × 2: меню 분식집 (светлый короб), меню кафе (тёмная доска), постер распродажи одежды, вывеска 24/7 и акций."""
    im = Image.new("RGB", (S, S))
    d = ImageDraw.Draw(im)
    h = S // 2
    # 분식 — жёлто-красный световой короб
    d.rectangle([0, 0, h, h], fill=(255, 238, 190))
    d.rectangle([0, 0, h, 150], fill=(200, 30, 30))
    d.text((h / 2, 75), "메뉴 MENU", font=_font(90), fill=(255, 255, 255), anchor="mm")
    items = [("떡볶이", "4,000"), ("순대", "4,500"), ("김밥", "3,000"), ("참치김밥", "4,000"), ("어묵", "1,500"), ("튀김", "3,500"),
             ("라면", "4,500"), ("쫄면", "6,000"), ("라볶이", "6,000"), ("만두", "4,000")]
    for k, (a, b) in enumerate(items):
        y = 210 + k * 78
        d.text((60, y), a, font=_font(56), fill=(60, 30, 20), anchor="lm")
        d.text((h - 60, y), b + "원", font=_font(52), fill=(190, 30, 30), anchor="rm")
    # кафе — тёмная доска
    d.rectangle([h, 0, S, h], fill=(36, 34, 32))
    d.text((h + h / 2, 90), "COFFEE", font=_font(96), fill=(240, 235, 225), anchor="mm")
    cafe = [("아메리카노", "4,500"), ("카페라떼", "5,000"), ("바닐라라떼", "5,500"), ("콜드브루", "5,000"), ("녹차라떼", "5,500"),
            ("딸기스무디", "6,000"), ("자몽에이드", "6,000"), ("크로플", "4,500")]
    for k, (a, b) in enumerate(cafe):
        y = 230 + k * 92
        d.text((h + 70, y), a, font=_font(56, False), fill=(235, 230, 220), anchor="lm")
        d.text((S - 70, y), b, font=_font(54, False), fill=(235, 200, 140), anchor="rm")
    # постер одежды
    d.rectangle([0, h, h, S], fill=(230, 228, 224))
    d.rectangle([60, h + 60, h - 60, h + 700], fill=(70, 80, 90))
    d.ellipse([h / 2 - 120, h + 120, h / 2 + 120, h + 360], fill=(210, 180, 160))
    d.rectangle([h / 2 - 200, h + 360, h / 2 + 200, h + 700], fill=(160, 40, 50))
    d.text((h / 2, h + 800), "NEW SEASON", font=_font(90), fill=(20, 20, 20), anchor="mm")
    d.text((h / 2, h + 910), "가을 신상 30% SALE", font=_font(64), fill=(190, 20, 30), anchor="mm")
    # 24 / акции
    d.rectangle([h, h, S, S], fill=(20, 120, 70))
    d.text((h + h / 2, h + 300), "24", font=_font(380), fill=(255, 255, 255), anchor="mm")
    d.text((h + h / 2, h + 620), "1+1  2+1", font=_font(130), fill=(255, 230, 60), anchor="mm")
    d.text((h + h / 2, h + 820), "행사 상품", font=_font(100), fill=(255, 255, 255), anchor="mm")
    im.save(f"{out}/int_menus.png")


def ceiling_tex(out, S=1024):
    """Потолок «армстронг» 600 × 600 мм с встроенной светодиодной панелью в каждой 4-й ячейке (+ эмиссия)."""
    a = np.full((S, S, 3), 228, np.float32); e = np.zeros((S, S, 3), np.float32)
    rng = np.random.default_rng(3)
    a += rng.normal(0, 4, (S, S, 1))
    g = S // 4
    for k in range(5):
        a[max(0, k * g - 3):k * g + 3, :] = 150
        a[:, max(0, k * g - 3):k * g + 3] = 150
    a[g + 10:2 * g - 10, g + 10:2 * g - 10] = 250
    e[g + 10:2 * g - 10, g + 10:2 * g - 10] = 255
    a[3 * g + 10:4 * g - 10, 3 * g + 10:4 * g - 10] = 250
    e[3 * g + 10:4 * g - 10, 3 * g + 10:4 * g - 10] = 255
    Image.fromarray(np.clip(a, 0, 255).astype(np.uint8)).save(f"{out}/int_ceiling.png")
    Image.fromarray(e.astype(np.uint8)).save(f"{out}/int_ceiling_E.png")


def plank_floor(out, scan_img, S=2048):
    """Деревянный пол: доски 0,18 × 1,2 м из скана древесины (тайл 2,4 м) + швы."""
    rng = np.random.default_rng(4)
    src = Image.fromarray(scan_img[..., :3]).resize((S, S))
    a = np.asarray(src, np.float32).copy()
    bw = int(S / 2.4 * 0.18)
    for i in range(0, S, bw):
        a[:, i:i + 3] *= 0.55
        off = rng.integers(0, S)
        for j in range(4):
            y = (off + j * int(S / 2.4 * 1.2)) % S
            a[y:y + 3, i:i + bw] *= 0.55
        a[:, i:i + bw] *= rng.uniform(0.85, 1.12)
    Image.fromarray(np.clip(a, 0, 255).astype(np.uint8)).save(f"{out}/int_woodfloor.png")


def build_textures(out):
    import scans
    product_atlas(out); drinks_atlas(out); menu_atlas(out); ceiling_tex(out)
    w = scans.load("wood021", "color")
    if w is not None:
        plank_floor(out, w)
    scans.pbr(out, "int_tile_floor", "tiles038", rough=0.25, rk=0.2)
    scans.pbr(out, "int_tile_white", "tiles105", rough=0.15, rk=0.1)
    scans.pbr(out, "int_terrazzo", "terrazzo002", rough=0.3, rk=0.2)
    scans.pbr(out, "int_wood", "wood037", rough=0.55, rk=0.2)
    scans.pbr(out, "int_plaster", "plaster002", rough=0.8, rk=0.1)


# ------------------------------------------------------------------ помощники
def face_in(mesh, fr, t0, t1, y0, y1, o, tile=(2, 2), color=(1, 1, 1, 1), uv=None):
    """Плоскость, смотрящая внутрь помещения (−n)."""
    nw = -np.array([fr.n[0], 0, fr.n[1]])
    if uv is None:
        uv = [(t0 / tile[0], -y0 / tile[1]), (t1 / tile[0], -y0 / tile[1]), (t1 / tile[0], -y1 / tile[1]), (t0 / tile[0], -y1 / tile[1])]
    _q(mesh, fr, [fr.P(t0, y0, o), fr.P(t1, y0, o), fr.P(t1, y1, o), fr.P(t0, y1, o)], nw, uv, color)


def cyl(mesh, fr, t, o, y0, h, r, sides=10, color=(1, 1, 1, 1), r_top=None):
    x, _, z = fr.P(t, 0, o)
    mesh.cylinder(x, y0, z, r, h, sides, color=color, r_top=r_top)


def card(mesh, fr, t0, t1, y0, y1, o, cell, grid, color=(1, 1, 1, 1)):
    """Плоскость с ячейкой атласа (grid × grid), смотрит наружу (+n — к витрине)."""
    c, r = cell % grid, cell // grid
    u0, u1 = c / grid + 0.002, (c + 1) / grid - 0.002
    v0, v1 = r / grid + 0.002, (r + 1) / grid - 0.002
    face(mesh, fr, t0, t1, y0, y1, o, color=color, uv=[(u0, v1), (u1, v1), (u1, v0), (u0, v0)])


def card_side(mesh, fr, t, y0, y1, o0, o1, sign, cell, grid, color=(1, 1, 1, 1)):
    c, r = cell % grid, cell // grid
    u0, u1 = c / grid + 0.002, (c + 1) / grid - 0.002
    v0, v1 = r / grid + 0.002, (r + 1) / grid - 0.002
    nw = np.array([fr.d[0], 0, fr.d[1]]) * sign
    _q(mesh, fr, [fr.P(t, y0, o0), fr.P(t, y0, o1), fr.P(t, y1, o1), fr.P(t, y1, o0)], nw, [(u0, v1), (u1, v1), (u1, v0), (u0, v0)], color)


GARMENT = [(0.12, 0.13, 0.16), (0.85, 0.84, 0.8), (0.55, 0.12, 0.14), (0.2, 0.3, 0.5), (0.6, 0.55, 0.45), (0.25, 0.35, 0.25),
           (0.9, 0.75, 0.55), (0.45, 0.45, 0.48), (0.75, 0.5, 0.55), (0.1, 0.1, 0.1), (0.95, 0.95, 0.95), (0.35, 0.25, 0.2)]


def _g(rnd, k=1.0):
    c = rnd.choice(GARMENT)
    j = rnd.uniform(0.9, 1.1) * k
    return (min(1, c[0] * j), min(1, c[1] * j), min(1, c[2] * j), 1)


def mannequin(M, fr, t, o, y0, rnd, top=None, bottom=None):
    skin = (0.9, 0.9, 0.88, 1)
    top = top or _g(rnd); bottom = bottom or _g(rnd, 0.7)
    cyl(M["i_paint"], fr, t, o, y0, 0.03, 0.22, 12, (0.15, 0.15, 0.15, 1))
    cyl(M["i_metal"], fr, t, o, y0 + 0.03, 0.12, 0.015, 6)
    for dt in (-0.09, 0.09):
        cyl(M["i_cloth"], fr, t + dt, o, y0 + 0.12, 0.82, 0.07, 8, bottom, r_top=0.085)
    cyl(M["i_cloth"], fr, t, o, y0 + 0.92, 0.18, 0.18, 12, bottom, r_top=0.16)
    cyl(M["i_cloth"], fr, t, o, y0 + 1.1, 0.42, 0.15, 12, top, r_top=0.19)
    obox(M["i_cloth"], fr, t - 0.22, t + 0.22, y0 + 1.44, y0 + 1.52, o - 0.1, o + 0.1, color=top)
    for dt in (-0.24, 0.24):
        cyl(M["i_cloth"], fr, t + dt, o, y0 + 0.95, 0.55, 0.045, 8, top)
    cyl(M["i_paint"], fr, t, o, y0 + 1.52, 0.08, 0.05, 8, skin)
    M["i_paint"].blob(*fr.P(t, y0 + 1.7, o), 0.1, 0.13, 0.11, seed=rnd.randrange(999), color_fn=lambda p, r: skin, subdiv=1)


# ------------------------------------------------------------------ комната
def room_shell(M, fr, W, D, H, floor_key, wall_key, y0, ceiling=True, wall_col=(1, 1, 1, 1), floor_tile=(1.2, 1.2)):
    horiz(M[floor_key], fr, 0, W, y0, -D, 0, True, floor_tile)
    face(M[wall_key], fr, 0, W, y0, y0 + H, -D, color=wall_col)
    side(M[wall_key], fr, 0, y0, y0 + H, -D, 0, +1, color=wall_col)
    side(M[wall_key], fr, W, y0, y0 + H, -D, 0, -1, color=wall_col)
    if ceiling:
        horiz(M["i_ceiling"], fr, 0, W, y0 + H, -D, 0, False, (2.4, 2.4))
    # плинтус
    obox(M["i_paint"], fr, 0, W, y0, y0 + 0.1, -D, -D + 0.02, color=(0.3, 0.3, 0.3, 1), faces="FT")


def shopfront(M, fr, W, y0, H, door_t, shop_h, frame_col=(0.12, 0.12, 0.13, 1)):
    """Витрина: стойки, транец, цоколь, стекло, дверной проём 1,8 м (открыт — можно зайти)."""
    f = M["frame"]
    gy0, gy1 = y0 + 0.35, y0 + 3.0
    posts = sorted(set([0.0, W] + [door_t, door_t + 1.8] + [x for x in np.arange(2.4, W - 1, 2.4)]))
    posts = [p for p in posts if not (door_t + 0.1 < p < door_t + 1.7)]
    for p in posts:
        obox(f, fr, max(0, p - 0.05), min(W, p + 0.05), y0, y0 + H, -0.12, 0.04, color=frame_col, faces="FS")
    obox(f, fr, 0, W, gy1, gy1 + 0.1, -0.12, 0.04, color=frame_col, faces="FTB")
    face_in(f, fr, 0, W, gy1 + 0.1, y0 + H, -0.13, color=(0.2, 0.2, 0.21, 1))
    # цоколь (кроме двери)
    for (a, b) in ((0, door_t), (door_t + 1.8, W)):
        if b - a > 0.05:
            obox(M["slab"], fr, a, b, y0, gy0, -0.12, 0.02, color=(0.25, 0.25, 0.26, 1), faces="FT")
            for (p0, p1) in zip([a] + [p for p in posts if a < p < b], [p for p in posts if a < p < b] + [b]):
                face(M["glass"], fr, p0 + 0.05, p1 - 0.05, gy0, gy1, -0.05)
    face(M["glass"], fr, 0.05, W - 0.05, gy1 + 0.1, y0 + H - 0.05, -0.06)
    # порог
    horiz(M["frame"], fr, door_t, door_t + 1.8, y0 + 0.012, -0.15, 0.05, True, color=(0.5, 0.5, 0.5, 1))


def lights_grid(M, fr, W, D, y, step=2.4):
    for t in np.arange(1.0, W - 0.5, step):
        for o in np.arange(-1.2, -D + 0.5, -step):
            obox(M["i_light"], fr, t, t + 1.2, y - 0.04, y, o - 0.3, o, faces="FSB")


# ------------------------------------------------------------------ типы помещений
def clothes(M, fr, W, D, y0, rnd, LIGHTS):
    H = 3.6
    room_shell(M, fr, W, D, H, "i_woodfloor", "i_plaster", y0, wall_col=(0.95, 0.94, 0.92, 1), floor_tile=(2.4, 2.4))
    lights_grid(M, fr, W, D, y0 + H)
    # трековые споты
    for o in (-1.5, -D + 2.0):
        obox(M["i_metal"], fr, 0.5, W - 0.5, y0 + H - 0.08, y0 + H - 0.04, o - 0.03, o + 0.03, faces="FB")
        for t in np.arange(1.0, W - 0.5, 1.2):
            cyl(M["i_paint"], fr, t, o, y0 + H - 0.3, 0.22, 0.05, 8, (0.1, 0.1, 0.1, 1))
            cyl(M["i_light"], fr, t, o, y0 + H - 0.31, 0.02, 0.04, 8)
    # витринные манекены у стекла
    for t in (1.4, 2.6, W - 2.6, W - 1.4):
        if abs(t - (W / 2)) > 1.2:
            mannequin(M, fr, t, -0.9, y0, rnd)
    # стойки с одеждой параллельно витрине (вешалки «боком» — видно рукава)
    for row_o in (-2.8, -4.6):
        for seg in ((0.8, W / 2 - 1.2), (W / 2 + 1.2, W - 0.8)):
            a, b = seg
            if b - a < 0.8:
                continue
            for tt in (a, b):
                cyl(M["i_metal"], fr, tt, row_o, y0, 1.45, 0.02, 6)
            obox(M["i_metal"], fr, a, b, y0 + 1.42, y0 + 1.45, row_o - 0.015, row_o + 0.015)
            x = a + 0.06
            while x < b - 0.05:
                c = _g(rnd)
                L = rnd.uniform(0.6, 1.0)
                obox(M["i_cloth"], fr, x, x + 0.035, y0 + 1.42 - L, y0 + 1.40, row_o - 0.24, row_o + 0.24, color=c)
                x += rnd.uniform(0.045, 0.07)
    # стол с аккуратными стопками свёрнутой одежды в центре
    tc = W / 2
    obox(M["i_wood"], fr, tc - 0.8, tc + 0.8, y0 + 0.72, y0 + 0.78, -3.9, -2.9)
    for (dt, do_) in ((-0.7, -3.8), (0.62, -3.8), (-0.7, -3.0), (0.62, -3.0)):
        obox(M["i_wood"], fr, tc + dt, tc + dt + 0.08, y0, y0 + 0.72, do_ , do_ + 0.08)
    for k in range(5):
        c = _g(rnd); hh = rnd.uniform(0.1, 0.22)
        t0 = tc - 0.7 + k * 0.3
        obox(M["i_cloth"], fr, t0, t0 + 0.26, y0 + 0.78, y0 + 0.78 + hh, -3.7, -3.35, color=c)
        obox(M["i_cloth"], fr, t0, t0 + 0.26, y0 + 0.78, y0 + 0.78 + hh * 0.8, -3.3, -2.95, color=_g(rnd))
    # задняя стена: полки со свёрнутой одеждой + вешала + постер
    for lvl in range(4):
        yy = y0 + 0.4 + lvl * 0.55
        obox(M["i_wood"], fr, 0.4, W - 0.4, yy, yy + 0.04, -D, -D + 0.45)
        x = 0.5
        while x < W - 0.8:
            obox(M["i_cloth"], fr, x, x + 0.32, yy + 0.04, yy + 0.04 + rnd.uniform(0.15, 0.3), -D + 0.05, -D + 0.38, color=_g(rnd))
            x += 0.38
    card(M["i_menus"], fr, W / 2 - 0.9, W / 2 + 0.9, y0 + 2.6, y0 + 3.4, -D + 0.02, 2, 2)
    # прилавок с кассой у боковой стены
    obox(M["i_wood"], fr, 0.2, 0.9, y0, y0 + 1.0, -D + 3.6, -D + 1.2)
    obox(M["i_metal"], fr, 0.3, 0.7, y0 + 1.0, y0 + 1.25, -D + 2.6, -D + 2.2)
    # примерочные: шторки
    for k in range(2):
        t0 = W - 0.2 - (k + 1) * 1.0
        obox(M["i_cloth"], fr, t0, t0 + 0.95, y0 + 0.2, y0 + 2.2, -D + 1.4, -D + 1.44, color=(0.3, 0.28, 0.26, 1))
    LIGHTS.append("clothes")


def bunsik(M, fr, W, D, y0, rnd, LIGHTS):
    H = 3.4
    room_shell(M, fr, W, D, H, "i_tile_floor", "i_tile_white", y0, floor_tile=(1.2, 1.2))
    lights_grid(M, fr, W, D, y0 + H)
    # стойка с ванной для теокпокки у витрины (типично: готовят прямо у окна)
    o0, o1 = -1.6, -0.7
    obox(M["i_metal"], fr, 0.4, 3.6, y0, y0 + 0.9, o0, o1)
    obox(M["i_metal"], fr, 0.45, 1.9, y0 + 0.9, y0 + 0.95, o0 + 0.05, o1 - 0.05, faces="FS")
    horiz(M["i_food_red"], fr, 0.5, 1.85, y0 + 0.93, o0 + 0.1, o1 - 0.1, True, (0.4, 0.4))   # теокпокки
    obox(M["i_metal"], fr, 2.0, 3.5, y0 + 0.9, y0 + 0.97, o0 + 0.05, o1 - 0.05, faces="FS")
    horiz(M["i_food_broth"], fr, 2.05, 3.45, y0 + 0.95, o0 + 0.1, o1 - 0.1, True, (0.4, 0.4))
    for k in range(10):  # шпажки с одэном
        t = 2.1 + k * 0.13
        cyl(M["i_wood"], fr, t, -1.15 + (k % 2) * 0.2, y0 + 0.95, 0.35, 0.006, 4)
        obox(M["i_food_fish"], fr, t - 0.04, t + 0.04, y0 + 0.97, y0 + 1.15, -1.18 + (k % 2) * 0.2, -1.12 + (k % 2) * 0.2)
    # стеклянная витринка с кимбапом
    obox(M["i_metal"], fr, 0.5, 1.8, y0 + 0.95, y0 + 1.0, -1.55, -1.3)
    for k in range(8):
        cyl(M["i_food_kimbap"], fr, 0.6 + k * 0.15, -1.42, y0 + 1.0, 0.04, 0.05, 8)
    # меню-короб на задней стене + кухня
    card(M["i_menus"], fr, 0.6, 3.8, y0 + 2.0, y0 + 3.2, -D + 0.03, 0, 2)
    obox(M["i_metal"], fr, 0.3, 4.5, y0, y0 + 0.9, -D, -D + 0.7)
    for k in range(3):
        cyl(M["i_metal"], fr, 0.9 + k * 1.2, -D + 0.35, y0 + 0.9, 0.25, 0.22, 12, (0.7, 0.7, 0.7, 1))
    # столики (нержавейка) и красные пластиковые табуреты
    for (t, o) in [(W - 2.2, -2.7), (W - 2.2, -5.0), (W / 2 - 0.6, -3.9), (W / 2 - 0.6, -6.0)]:
        if t < 0.8 or o < -D + 1.8:
            continue
        obox(M["i_metal"], fr, t - 0.45, t + 0.45, y0 + 0.72, y0 + 0.75, o - 0.35, o + 0.35)
        cyl(M["i_metal"], fr, t, o, y0, 0.72, 0.04, 8)
        for (dt, do_) in ((-0.75, -0.15), (-0.75, 0.2), (0.75, -0.15), (0.75, 0.2)):
            cyl(M["i_stool"], fr, t + dt, o + do_, y0, 0.45, 0.16, 12, r_top=0.15)
        cyl(M["i_metal"], fr, t + 0.2, o, y0 + 0.75, 0.12, 0.03, 8, (0.85, 0.85, 0.85, 1))   # стаканы/салфетница
    # холодильник с напитками у боковой стены
    fridge(M, fr, W - 0.05, -D + 0.6, -D + 2.0, y0, side_wall=True)
    # кулер с водой
    obox(M["i_paint"], fr, W - 0.6, W - 0.2, y0, y0 + 1.2, -D + 2.3, -D + 2.7, color=(0.92, 0.92, 0.92, 1))
    LIGHTS.append("bunsik")


def fridge(M, fr, t, o0, o1, y0, side_wall=False, door_cells=(0, 1)):
    """Холодильник со стеклянными дверями. side_wall: стоит у правой стены (t), смотрит вдоль −d."""
    if side_wall:
        obox(M["i_paint"], fr, t - 0.7, t, y0, y0 + 2.1, o0, o1, color=(0.85, 0.86, 0.88, 1))
        n = max(1, int((o1 - o0) / 0.7))
        for k in range(n):
            a, b = o0 + k * (o1 - o0) / n, o0 + (k + 1) * (o1 - o0) / n
            card_side(M["i_drinks"], fr, t - 0.71, y0 + 0.2, y0 + 1.95, a + 0.04, b - 0.04, -1, door_cells[k % len(door_cells)], 2)
            card_side(M["i_paint"], fr, t - 0.712, y0 + 1.95, y0 + 2.08, a, b, -1, 0, 1, (0.1, 0.4, 0.2, 1))
    else:
        obox(M["i_paint"], fr, t, t + (o1 - o0), y0, y0 + 2.1, o0, o0 + 0.7, color=(0.85, 0.86, 0.88, 1))


def convenience(M, fr, W, D, y0, rnd, LIGHTS):
    H = 3.3
    room_shell(M, fr, W, D, H, "i_terrazzo", "i_plaster", y0, wall_col=(0.97, 0.97, 0.97, 1), floor_tile=(1.5, 1.5))
    lights_grid(M, fr, W, D, y0 + H, step=1.8)
    # стена холодильников с напитками по всей задней стене
    n = max(2, int((W - 0.6) / 0.85))
    for k in range(n):
        t0 = 0.3 + k * (W - 0.6) / n; t1 = 0.3 + (k + 1) * (W - 0.6) / n
        obox(M["i_paint"], fr, t0, t1, y0, y0 + 2.2, -D, -D + 0.75, color=(0.88, 0.88, 0.9, 1), faces="T")
        card(M["i_drinks"], fr, t0 + 0.04, t1 - 0.04, y0 + 0.15, y0 + 2.0, -D + 0.7, k % 4, 2)
        obox(M["i_metal"], fr, t0, t0 + 0.05, y0, y0 + 2.2, -D + 0.68, -D + 0.76, color=(0.2, 0.2, 0.2, 1), faces="F")
        face(M["i_glass"], fr, t0 + 0.05, t1 - 0.05, y0 + 0.1, y0 + 2.1, -D + 0.76)
    obox(M["i_paint"], fr, 0.3, W - 0.3, y0 + 2.2, y0 + 2.5, -D, -D + 0.8, color=(0.1, 0.45, 0.25, 1), faces="FB")
    # стеллажи-острова (2 ряда, двусторонние) с товаром
    for row_o in (-3.0, -4.9):
        if row_o < -D + 1.6:
            continue
        a, b = 1.2, W - 2.8
        obox(M["i_paint"], fr, a, b, y0, y0 + 1.6, row_o - 0.3, row_o + 0.3, color=(0.92, 0.92, 0.93, 1))
        for lvl in range(4):
            yy = y0 + 0.12 + lvl * 0.38
            obox(M["i_paint"], fr, a, b, yy, yy + 0.03, row_o + 0.3, row_o + 0.42, color=(0.95, 0.95, 0.95, 1), faces="FT")
            x = a
            while x < b - 0.3:
                w = min(rnd.uniform(0.5, 0.8), b - x)
                card(M["i_products"], fr, x, x + w, yy + 0.03, yy + 0.33, row_o + 0.32, rnd.randrange(64), 8)
                # задняя сторона острова
                c = rnd.randrange(64); cc, rr = c % 8, c // 8
                face_in(M["i_products"], fr, x, x + w, yy + 0.03, yy + 0.33, row_o - 0.32,
                        uv=[(cc / 8 + 0.002, (rr + 1) / 8), ((cc + 1) / 8 - 0.002, (rr + 1) / 8), ((cc + 1) / 8 - 0.002, rr / 8), (cc / 8 + 0.002, rr / 8)])
                x += w
        # ценники-полоски
        for lvl in range(4):
            yy = y0 + 0.1 + lvl * 0.38
            obox(M["i_label"], fr, a, b, yy, yy + 0.04, row_o + 0.42, row_o + 0.44, faces="F")
    # касса у витрины (справа) + стойка с рамёнами и кипятильником
    obox(M["i_wood"], fr, W - 2.4, W - 0.3, y0, y0 + 0.95, -2.3, -1.4)
    obox(M["i_paint"], fr, W - 2.4, W - 0.3, y0 + 0.95, y0 + 1.0, -2.35, -1.35, color=(0.2, 0.2, 0.22, 1))
    obox(M["i_metal"], fr, W - 1.9, W - 1.5, y0 + 1.0, y0 + 1.3, -2.0, -1.7, color=(0.2, 0.2, 0.2, 1))
    card(M["i_menus"], fr, W - 2.2, W - 1.0, y0 + 2.2, y0 + 3.0, -2.6, 3, 2)
    obox(M["i_paint"], fr, 0.3, 1.0, y0, y0 + 1.6, -2.0, -0.9, color=(0.9, 0.9, 0.9, 1))
    card(M["i_products"], fr, 0.3, 1.0, y0 + 0.9, y0 + 1.5, -0.88, 2 + 8 * 4, 8)
    obox(M["i_metal"], fr, 0.4, 0.75, y0 + 1.6, y0 + 2.0, -1.6, -1.2, color=(0.85, 0.85, 0.85, 1))
    LIGHTS.append("convenience")


def cafe(M, fr, W, D, y0, rnd, LIGHTS):
    H = 3.7
    room_shell(M, fr, W, D, H, "i_woodfloor", "i_plaster", y0, wall_col=(0.85, 0.8, 0.74, 1), floor_tile=(2.4, 2.4), ceiling=True)
    # подвесные светильники над столами, тёплый свет
    pend = []
    for (t, o) in [(1.6, -1.6), (3.6, -1.6), (W - 1.6, -1.6), (1.6, -3.6), (3.6, -3.6), (W - 3.6, -3.6)]:
        if t < W - 0.8:
            pend.append((t, o))
            cyl(M["i_metal"], fr, t, o, y0 + 2.1, H - 2.1, 0.006, 4, (0.1, 0.1, 0.1, 1))
            cyl(M["i_paint"], fr, t, o, y0 + 1.95, 0.2, 0.2, 14, (0.12, 0.12, 0.12, 1), r_top=0.05)
            cyl(M["i_light"], fr, t, o, y0 + 1.9, 0.06, 0.06, 10)
            # столик + 2 стула
            cyl(M["i_wood"], fr, t, o - 0.1, y0 + 0.72, 0.04, 0.35, 16)
            cyl(M["i_metal"], fr, t, o - 0.1, y0, 0.72, 0.03, 6, (0.1, 0.1, 0.1, 1))
            for dt in (-0.55, 0.55):
                obox(M["i_wood"], fr, t + dt - 0.2, t + dt + 0.2, y0 + 0.45, y0 + 0.49, o - 0.3, o + 0.1)
                obox(M["i_wood"], fr, t + dt + (0.18 if dt > 0 else -0.22), t + dt + (0.22 if dt > 0 else -0.18), y0 + 0.49, y0 + 0.9, o - 0.3, o + 0.1)
                for (a, b) in ((-0.18, -0.28), (0.15, 0.05)):
                    cyl(M["i_metal"], fr, t + dt + a * 0.9, o + b, y0, 0.45, 0.015, 4, (0.1, 0.1, 0.1, 1))
            cyl(M["i_paint"], fr, t + 0.1, o - 0.05, y0 + 0.76, 0.1, 0.04, 10, (0.95, 0.95, 0.95, 1))    # чашка
    # барная стойка в глубине + кофемашина + витрина с десертами
    obox(M["i_wood"], fr, 0.8, W - 0.8, y0, y0 + 1.05, -D + 2.2, -D + 1.5)
    obox(M["i_terr_top"], fr, 0.75, W - 0.75, y0 + 1.05, y0 + 1.1, -D + 2.25, -D + 1.45)
    obox(M["i_metal"], fr, 1.2, 2.0, y0 + 1.1, y0 + 1.55, -D + 1.9, -D + 1.5, color=(0.8, 0.8, 0.82, 1))
    cyl(M["i_metal"], fr, 2.4, -D + 1.7, y0 + 1.1, 0.35, 0.1, 10, (0.2, 0.2, 0.2, 1))
    obox(M["i_glass"], fr, W - 2.8, W - 1.2, y0 + 1.1, y0 + 1.5, -D + 2.2, -D + 1.7)
    for k in range(6):
        cyl(M["i_food_cake"], fr, W - 2.7 + k * 0.25, -D + 1.95, y0 + 1.12, 0.09, 0.07, 10)
    card(M["i_menus"], fr, 1.0, 3.6, y0 + 2.1, y0 + 3.3, -D + 0.03, 1, 2)
    # стена-стеллаж с кофейными зёрнами/посудой
    for lvl in range(3):
        yy = y0 + 1.5 + lvl * 0.45
        obox(M["i_wood"], fr, 4.2, W - 0.6, yy, yy + 0.04, -D, -D + 0.3)
        for k in range(int((W - 4.8) / 0.25)):
            cyl(M["i_paint"], fr, 4.35 + k * 0.25, -D + 0.15, yy + 0.04, rnd.uniform(0.12, 0.28), 0.06, 8,
                rnd.choice([(0.95, 0.95, 0.95, 1), (0.3, 0.2, 0.15, 1), (0.6, 0.5, 0.4, 1)]))
    # растения в кадках
    for (t, o) in [(0.5, -0.6), (W - 0.5, -0.6), (W - 0.5, -D + 2.8)]:
        cyl(M["i_paint"], fr, t, o, y0, 0.45, 0.22, 12, (0.85, 0.85, 0.82, 1), r_top=0.25)
        import trees
        for _ in range(14):
            c = np.array(fr.P(t, y0 + 0.9 + rnd.uniform(-0.3, 0.5), o)) + np.array([rnd.uniform(-0.3, 0.3), 0, rnd.uniform(-0.3, 0.3)])
            trees.leaf_card(M["i_leaf"], c, (rnd.uniform(-1, 1), 0.5, rnd.uniform(-1, 1)), rnd.uniform(0.4, 0.6), 0.752, 0.998, rnd)
    LIGHTS.append("cafe")


def cosmetics(M, fr, W, D, y0, rnd, LIGHTS):
    H = 3.5
    room_shell(M, fr, W, D, H, "i_tile_floor", "i_plaster", y0, wall_col=(0.98, 0.97, 0.97, 1))
    lights_grid(M, fr, W, D, y0 + H, step=1.8)
    # белые глянцевые стеллажи по стенам с маленькой упаковкой
    for (t_wall, sign) in ((0.0, 1), (W, -1)):
        for lvl in range(6):
            yy = y0 + 0.3 + lvl * 0.35
            t0, t1 = (t_wall, t_wall + 0.35) if sign > 0 else (t_wall - 0.35, t_wall)
            obox(M["i_paint"], fr, t0, t1, yy, yy + 0.03, -D + 0.3, -0.8, color=(0.98, 0.98, 0.98, 1))
            for k in range(int((D - 1.1) / 0.5)):
                o_a = -0.85 - k * 0.5
                card_side(M["i_products"], fr, t1 + 0.01 if sign > 0 else t0 - 0.01, yy + 0.03, yy + 0.25, o_a - 0.46, o_a, sign, rnd.randrange(64), 8)
    # центральные столы-тестеры с зеркалами
    for o in (-2.4, -4.4):
        obox(M["i_paint"], fr, W / 2 - 1.2, W / 2 + 1.2, y0, y0 + 0.9, o - 0.45, o + 0.45, color=(0.95, 0.95, 0.95, 1))
        for k in range(12):
            cyl(M["i_paint"], fr, W / 2 - 1.1 + k * 0.2, o + 0.25, y0 + 0.9, rnd.uniform(0.06, 0.16), 0.025, 8,
                rnd.choice([(0.9, 0.3, 0.4, 1), (0.95, 0.85, 0.8, 1), (0.2, 0.2, 0.2, 1), (0.8, 0.6, 0.9, 1), (1, 1, 1, 1)]))
        obox(M["i_mirror"], fr, W / 2 - 0.3, W / 2 + 0.3, y0 + 0.9, y0 + 1.4, o - 0.05, o + 0.0, faces="FS")
    card(M["i_menus"], fr, W / 2 - 1.0, W / 2 + 1.0, y0 + 2.2, y0 + 3.2, -D + 0.02, 2, 2)
    obox(M["i_paint"], fr, W / 2 - 1.5, W / 2 + 1.5, y0, y0 + 1.0, -D + 1.6, -D + 1.0, color=(0.95, 0.85, 0.88, 1))
    LIGHTS.append("cosmetics")


BUILDERS = {"clothes": clothes, "bunsik": bunsik, "convenience": convenience, "cafe": cafe, "cosmetics": cosmetics}


def build(M, a, b, n, t0, W, D, y0, kind, shop_h, rnd, records, LIGHTS):
    """a,b,n — фасадная линия здания; помещение шириной W от t0 вглубь на D. Возвращает (t0, t1)."""
    base = WallFrame(a, b, n)
    oa = base.P(t0, 0, 0)
    ob = base.P(t0 + W, 0, 0)
    fr = WallFrame((oa[0], oa[2]), (ob[0], ob[2]), n)
    BUILDERS[kind](M, fr, W, D, y0, rnd, LIGHTS)
    door_t = W / 2 - 0.9 if kind != "bunsik" else W - 2.4
    shopfront(M, fr, W, y0, shop_h - y0, door_t, shop_h)
    door = fr.P(door_t + 0.9, y0, 0)
    corners = [fr.P(0, y0, 0), fr.P(W, y0, 0), fr.P(W, y0, -D), fr.P(0, y0, -D)]
    records.append(dict(type=kind, door=[round(v, 2) for v in door], facing=[round(float(n[0]), 3), 0, round(float(n[1]), 3)],
                        footprint=[[round(c[0], 2), round(c[2], 2)] for c in corners], floor_y=round(y0, 3), height=round(shop_h - y0, 2)))
