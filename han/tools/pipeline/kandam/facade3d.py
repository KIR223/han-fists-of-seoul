"""Объёмные фасады сеульских малоэтажек, вилл и офистелей: стена собирается из кусков вокруг проёмов,
окна утоплены (откосы, рама, импост, подоконник с отливом), закрытые остеклённые балконы-выступы,
корзины кондиционеров, решётки первых этажей, межэтажные пояса и карниз; офисы — ленточное остекление.
Координаты стены: t — вдоль (м от точки a), y — высота, o — наружу по нормали n."""
import math

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

# ------------------------------------------------------------------ атлас интерьеров окон (4 × 4)
CELLS = {"curtain": [0, 1, 2], "blinds": [3, 4], "room_lit": [5, 6, 7], "dark": [8, 9], "office": [10, 11],
         "balcony": [12, 13, 14], "office_dark": [15]}


def interior_atlas(out, S=512):
    """Атлас 2048² (16 ячеек 512²): шторы, жалюзи, освещённые комнаты, тёмные, офис, балконы с бельём. + эмиссия."""
    rng = np.random.default_rng(12)
    W = S * 4
    bc = Image.new("RGB", (W, W)); em = Image.new("RGB", (W, W))
    for idx in range(16):
        cx, cy = (idx % 4) * S, (idx // 4) * S
        a = Image.new("RGB", (S, S)); e = Image.new("RGB", (S, S))
        da, de = ImageDraw.Draw(a), ImageDraw.Draw(e)
        kind = [k for k, v in CELLS.items() if idx in v][0]
        warm = tuple(int(c) for c in (np.array([255, 214, 160]) if rng.random() < 0.7 else np.array([225, 238, 255])))

        def grad(draw, c_top, c_bot, dr=None, k=1.0):
            for y in range(S):
                t = y / S
                c = tuple(int((c_top[i] * (1 - t) + c_bot[i] * t) * k) for i in range(3))
                draw.line([(0, y), (S, y)], fill=c)
        if kind == "curtain":
            base = np.array(rng.choice([[200, 190, 170], [180, 170, 150], [150, 160, 170], [210, 205, 195], [160, 120, 100]]))
            lit = rng.random() < 0.6
            for x in range(S):
                f = 0.78 + 0.22 * math.sin(x / S * math.pi * rng.uniform(14, 22)) ** 2
                c = tuple(int(v * f) for v in base)
                da.line([(x, 0), (x, S)], fill=c)
                if lit:
                    de.line([(x, 0), (x, S)], fill=tuple(int(v * f * 0.75 * w / 255) for v, w in zip(base, warm)))
            if rng.random() < 0.5:  # штора отодвинута — щель в комнату
                g = int(rng.uniform(0.3, 0.6) * S)
                da.rectangle([g, 0, g + S // 6, S], fill=(40, 35, 30))
                if lit:
                    de.rectangle([g, 0, g + S // 6, S], fill=tuple(int(w * 0.8) for w in warm))
        elif kind == "blinds":
            lit = rng.random() < 0.5
            for y in range(0, S, 14):
                da.rectangle([0, y, S, y + 10], fill=(205, 203, 196)); da.rectangle([0, y + 10, S, y + 14], fill=(120, 118, 112))
                if lit:
                    de.rectangle([0, y + 10, S, y + 14], fill=warm)
                    de.rectangle([0, y, S, y + 10], fill=tuple(int(w * 0.35) for w in warm))
        elif kind in ("room_lit", "dark"):
            lit = kind == "room_lit"
            wallc = np.array(rng.choice([[230, 225, 210], [215, 220, 225], [235, 215, 190]]))
            grad(da, wallc * 0.55, wallc * 0.35)
            if lit:
                grad(de, np.array(warm) * 0.95, np.array(warm) * 0.45)
                de.ellipse([S * 0.35, -S * 0.1, S * 0.65, S * 0.12], fill=(255, 250, 235))
            # мебель-силуэты: шкаф, полка, растение, телевизор
            for _ in range(rng.integers(2, 5)):
                x0 = int(rng.uniform(0, S * 0.8)); w = int(rng.uniform(S * 0.12, S * 0.35)); h = int(rng.uniform(S * 0.2, S * 0.6))
                c = tuple(int(v) for v in rng.integers(25, 70, 3))
                da.rectangle([x0, S - h, x0 + w, S], fill=c)
                if lit:
                    de.rectangle([x0, S - h, x0 + w, S], fill=tuple(int(v * 0.35) for v in warm))
            if lit and rng.random() < 0.5:
                da.rectangle([S * 0.55, S * 0.45, S * 0.85, S * 0.62], fill=(15, 15, 20))
                de.rectangle([S * 0.56, S * 0.46, S * 0.84, S * 0.61], fill=(120, 160, 220))
            if not lit:
                e = Image.new("RGB", (S, S)); de = ImageDraw.Draw(e)
        elif kind in ("office", "office_dark"):
            lit = kind == "office"
            grad(da, (90, 95, 100), (60, 62, 66))
            if lit:
                grad(de, (235, 240, 245), (130, 135, 140))
            for k in range(4):  # потолочные светильники
                y = int(S * 0.04 + k * 9)
                for x in range(0, S, S // 3):
                    da.rectangle([x + 20, y, x + S // 3 - 20, y + 5], fill=(220, 225, 230))
                    de.rectangle([x + 20, y, x + S // 3 - 20, y + 5], fill=(255, 255, 255) if lit else (40, 40, 40))
            for x in range(0, S, 90):  # перегородки и мониторы
                da.rectangle([x, int(S * 0.65), x + 70, S], fill=(70, 72, 76))
                da.rectangle([x + 20, int(S * 0.55), x + 50, int(S * 0.64)], fill=(20, 20, 25))
                if lit:
                    de.rectangle([x + 22, int(S * 0.56), x + 48, int(S * 0.63)], fill=(150, 190, 240))
        elif kind == "balcony":
            lit = rng.random() < 0.55
            grad(da, (150, 145, 135), (110, 105, 98))
            if lit:
                grad(de, np.array(warm) * 0.6, np.array(warm) * 0.3)
            # сушилка с бельём, коробки, растения
            da.line([(S * 0.05, S * 0.22), (S * 0.95, S * 0.22)], fill=(200, 200, 200), width=4)
            x = int(S * 0.08)
            while x < S * 0.9:
                w = int(rng.uniform(S * 0.06, S * 0.14)); h = int(rng.uniform(S * 0.15, S * 0.4))
                c = tuple(int(v) for v in rng.choice([[230, 230, 230], [60, 80, 140], [200, 60, 60], [240, 220, 150], [90, 90, 95], [180, 200, 220]]))
                da.rectangle([x, int(S * 0.22), x + w, int(S * 0.22) + h], fill=c)
                x += w + int(rng.uniform(4, 30))
            for _ in range(rng.integers(1, 4)):
                x0 = int(rng.uniform(0, S * 0.8))
                da.rectangle([x0, int(S * 0.8), x0 + int(S * 0.15), S], fill=(150, 120, 80))
            if rng.random() < 0.6:
                x0 = int(rng.uniform(0, S * 0.7))
                da.ellipse([x0, int(S * 0.6), x0 + int(S * 0.2), int(S * 0.85)], fill=(50, 90, 45))
                da.rectangle([x0 + int(S * 0.06), int(S * 0.82), x0 + int(S * 0.14), S], fill=(140, 80, 50))
        a = a.filter(ImageFilter.GaussianBlur(1.2)); e = e.filter(ImageFilter.GaussianBlur(1.5))
        bc.paste(a, (cx, cy)); em.paste(e, (cx, cy))
    bc.save(f"{out}/win_interior_BC.png"); em.save(f"{out}/win_interior_E.png")


def cell_uv(idx):
    c, r = idx % 4, idx // 4
    p = 0.004
    return c / 4 + p, r / 4 + p, (c + 1) / 4 - p, (r + 1) / 4 - p


# ------------------------------------------------------------------ геометрия
class WallFrame:
    def __init__(self, a, b, n):
        self.a = np.array([a[0], a[1]], float)
        d = np.array([b[0] - a[0], b[1] - a[1]], float)
        self.L = float(np.linalg.norm(d))
        self.d = d / max(self.L, 1e-9)
        self.n = np.array(n, float)

    def P(self, t, y, o=0.0):
        p = self.a + self.d * t + self.n * o
        return (float(p[0]), float(y), float(p[1]))


def _q(mesh, fr, pts, nrm_local, uv, color):
    """Квадрат с правильной обмоткой под заданную нормаль (в мировых координатах)."""
    p = [np.asarray(x) for x in pts]
    nn = np.cross(p[1] - p[0], p[3] - p[0])
    if np.dot(nn, nrm_local) < 0:
        pts = [pts[1], pts[0], pts[3], pts[2]]
        uv = [uv[1], uv[0], uv[3], uv[2]]
    mesh.quad(*pts, uv=uv, color=color, normal=np.asarray(nrm_local) / (np.linalg.norm(nrm_local) + 1e-9))


def face(mesh, fr, t0, t1, y0, y1, o, tile=(2.0, 2.0), color=(1, 1, 1, 1), uoff=0.0, uv=None):
    """Плоскость фасада (смотрит наружу) от t0 до t1, y0..y1 на смещении o."""
    if t1 - t0 < 1e-4 or y1 - y0 < 1e-4:
        return
    nw = np.array([fr.n[0], 0, fr.n[1]])
    if uv is None:
        uv = [((t0 + uoff) / tile[0], -y0 / tile[1]), ((t1 + uoff) / tile[0], -y0 / tile[1]),
              ((t1 + uoff) / tile[0], -y1 / tile[1]), ((t0 + uoff) / tile[0], -y1 / tile[1])]
    _q(mesh, fr, [fr.P(t0, y0, o), fr.P(t1, y0, o), fr.P(t1, y1, o), fr.P(t0, y1, o)], nw, uv, color)


def side(mesh, fr, t, y0, y1, o0, o1, sign, tile=(2.0, 2.0), color=(1, 1, 1, 1)):
    """Боковая грань (перпендикулярна стене) в точке t; sign: +1 нормаль вдоль d, -1 против."""
    nw = np.array([fr.d[0], 0, fr.d[1]]) * sign
    uv = [(o0 / tile[0], -y0 / tile[1]), (o1 / tile[0], -y0 / tile[1]), (o1 / tile[0], -y1 / tile[1]), (o0 / tile[0], -y1 / tile[1])]
    _q(mesh, fr, [fr.P(t, y0, o0), fr.P(t, y0, o1), fr.P(t, y1, o1), fr.P(t, y1, o0)], nw, uv, color)


def horiz(mesh, fr, t0, t1, y, o0, o1, up, tile=(2.0, 2.0), color=(1, 1, 1, 1)):
    nw = np.array([0, 1.0 if up else -1.0, 0])
    uv = [(t0 / tile[0], o0 / tile[1]), (t1 / tile[0], o0 / tile[1]), (t1 / tile[0], o1 / tile[1]), (t0 / tile[0], o1 / tile[1])]
    _q(mesh, fr, [fr.P(t0, y, o0), fr.P(t1, y, o0), fr.P(t1, y, o1), fr.P(t0, y, o1)], nw, uv, color)


def obox(mesh, fr, t0, t1, y0, y1, o0, o1, tile=(1.0, 1.0), color=(1, 1, 1, 1), back=False, bottom=True, faces="FSTB"):
    """faces: F — фронт, S — бока, T — верх, B — низ (лишние грани не строим — экономия треугольников)."""
    if "F" in faces:
        face(mesh, fr, t0, t1, y0, y1, o1, tile, color)
    if "S" in faces:
        side(mesh, fr, t0, y0, y1, o0, o1, -1, tile, color)
        side(mesh, fr, t1, y0, y1, o0, o1, +1, tile, color)
    if "T" in faces:
        horiz(mesh, fr, t0, t1, y1, o0, o1, True, tile, color)
    if bottom and "B" in faces:
        horiz(mesh, fr, t0, t1, y0, o0, o1, False, tile, color)


# ------------------------------------------------------------------ окно / балкон
def window(M, fr, t0, t1, y0, y1, rnd, wall_key, wall_tile, wall_col, frame_col, depth=0.2, interior=None, bars=False, sill=True, mullions=1, transom=False):
    w = M["win"]
    # откосы (материал стены, слегка темнее), низ — внутренний подоконник
    dark = tuple(c * 0.82 for c in wall_col[:3]) + (1,)
    side(M[wall_key], fr, t0, y0, y1, -depth, 0, +1, wall_tile, dark)
    side(M[wall_key], fr, t1, y0, y1, -depth, 0, -1, wall_tile, dark)
    horiz(M[wall_key], fr, t0, t1, y1, -depth, 0, False, wall_tile, tuple(c * 0.7 for c in wall_col[:3]) + (1,))
    horiz(M[wall_key], fr, t0, t1, y0, -depth, 0, True, wall_tile, dark)
    # стекло с интерьером
    idx = interior if interior is not None else rnd.choice(CELLS[rnd.choice(["curtain", "curtain", "blinds", "room_lit", "dark", "dark", "room_lit"])])
    u0, v0, u1, v1 = cell_uv(idx)
    face(w, fr, t0, t1, y0, y1, -depth + 0.02, uv=[(u0, v1), (u1, v1), (u1, v0), (u0, v0)])
    # рама: контур + импосты (тонкие коробки из алюминия)
    f = M["frame"]; fw = 0.055; o0, o1 = -depth + 0.015, -depth + 0.075
    obox(f, fr, t0, t1, y0, y0 + fw, o0, o1, color=frame_col, faces="F")
    obox(f, fr, t0, t1, y1 - fw, y1, o0, o1, color=frame_col, faces="F")
    obox(f, fr, t0, t0 + fw, y0, y1, o0, o1, color=frame_col, faces="F")
    obox(f, fr, t1 - fw, t1, y0, y1, o0, o1, color=frame_col, faces="F")
    for k in range(1, mullions + 1):
        tm = t0 + (t1 - t0) * k / (mullions + 1)
        obox(f, fr, tm - 0.03, tm + 0.03, y0, y1, o0, o1 + 0.02, color=frame_col, faces="F")
    if transom:
        ym = y1 - (y1 - y0) * 0.28
        obox(f, fr, t0, t1, ym - 0.025, ym + 0.025, o0, o1, color=frame_col, faces="F")
    if sill:  # наружный отлив
        obox(M["frame"], fr, t0 - 0.06, t1 + 0.06, y0 - 0.05, y0, -0.02, 0.09, color=(0.72, 0.72, 0.7, 1), faces="FT")
    if bars:  # решётка (типично для 1-2 этажа вилл)
        bar = M["bars"]
        nb = max(3, int((t1 - t0) / 0.18))
        for k in range(nb + 1):
            tb = t0 + 0.05 + (t1 - t0 - 0.1) * k / nb
            obox(bar, fr, tb - 0.012, tb + 0.012, y0 + 0.02, y1 - 0.02, 0.04, 0.065, faces="F")
        for yb in (y0 + 0.12, y1 - 0.12):
            obox(bar, fr, t0, t1, yb - 0.015, yb + 0.015, 0.035, 0.07, faces="F")
        obox(bar, fr, t0 - 0.03, t0, y0, y1, 0.0, 0.07, faces="F"); obox(bar, fr, t1, t1 + 0.03, y0, y1, 0.0, 0.07, faces="F")


def balcony(M, fr, t0, t1, y0, fh, rnd, wall_key, wall_tile, wall_col, frame_col, depth=1.05):
    """Закрытый (остеклённый) балкон-выступ, как у корейских квартир и вилл."""
    slab = M["slab"]
    para_h = 0.95 if rnd.random() < 0.7 else 0.5
    pcol = wall_col if rnd.random() < 0.6 else (0.86, 0.86, 0.84, 1)
    # плита снизу и сверху
    obox(slab, fr, t0, t1, y0 - 0.18, y0, 0, depth, tile=(2, 2), color=(0.78, 0.78, 0.76, 1))
    # парапет (стена) спереди и по бокам
    face(M[wall_key], fr, t0, t1, y0, y0 + para_h, depth, wall_tile, pcol)
    side(M[wall_key], fr, t0, y0, y0 + para_h, 0, depth, -1, wall_tile, pcol)
    side(M[wall_key], fr, t1, y0, y0 + para_h, 0, depth, +1, wall_tile, pcol)
    horiz(M[wall_key], fr, t0, t1, y0 + para_h, depth - 0.12, depth, True, wall_tile, pcol)
    # остекление
    yg0, yg1 = y0 + para_h, y0 + fh - 0.2
    idx = rnd.choice(CELLS["balcony"] + CELLS["curtain"][:1] + CELLS["blinds"][:1])
    u0, v0, u1, v1 = cell_uv(idx)
    face(M["win"], fr, t0 + 0.02, t1 - 0.02, yg0, yg1, depth - 0.08, uv=[(u0, v1), (u1, v1), (u1, v0), (u0, v0)])
    # боковые стёкла — тот же интерьер, полоска ячейки
    for tt, sg in ((t0, -1), (t1, +1)):
        nw = np.array([fr.d[0], 0, fr.d[1]]) * sg
        _q(M["win"], fr, [fr.P(tt, yg0, 0.05), fr.P(tt, yg0, depth - 0.08), fr.P(tt, yg1, depth - 0.08), fr.P(tt, yg1, 0.05)], nw,
           [(u0, v1), (u0 + (u1 - u0) * 0.3, v1), (u0 + (u1 - u0) * 0.3, v0), (u0, v0)], (0.8, 0.8, 0.8, 1))
    f = M["frame"]
    n_m = max(2, int((t1 - t0) / 0.9))
    for k in range(n_m + 1):
        tm = t0 + (t1 - t0) * k / n_m
        obox(f, fr, max(t0, tm - 0.035), min(t1, tm + 0.035), yg0, yg1, depth - 0.1, depth - 0.02, color=frame_col, faces="FS")
    obox(f, fr, t0, t1, yg0, yg0 + 0.06, depth - 0.1, depth - 0.01, color=frame_col, faces="FT")
    obox(f, fr, t0, t1, yg1 - 0.06, yg1, depth - 0.1, depth - 0.01, color=frame_col, faces="FB")
    # перемычка/плита сверху
    obox(slab, fr, t0, t1, yg1, y0 + fh - 0.18, 0, depth, tile=(2, 2), color=(0.8, 0.8, 0.78, 1), bottom=True)
    # иногда снаружи — корзина кондиционера на парапете
    if rnd.random() < 0.35:
        tc = rnd.uniform(t0 + 0.2, t1 - 1.0)
        obox(M["bars"], fr, tc, tc + 0.9, y0 + 0.05, y0 + 0.7, depth, depth + 0.42)
        obox(M["ac"], fr, tc + 0.05, tc + 0.85, y0 + 0.08, y0 + 0.66, depth + 0.02, depth + 0.36)


def ac_cage(M, fr, t, y, rnd):
    # корзина: рама из прутьев + блок внутри
    for (a0, a1) in ((t, t + 0.04), (t + 0.91, t + 0.95)):
        obox(M["bars"], fr, a0, a1, y - 0.05, y + 0.65, 0.0, 0.48, faces="FS")
    for k in range(6):
        tb = t + 0.1 + k * 0.15
        obox(M["bars"], fr, tb, tb + 0.02, y - 0.05, y + 0.65, 0.46, 0.48, faces="F")
    obox(M["bars"], fr, t, t + 0.95, y - 0.05, y - 0.01, 0.0, 0.48, faces="FTB")
    obox(M["ac"], fr, t + 0.06, t + 0.89, y, y + 0.6, 0.03, 0.4, faces="FST")


def cornice(M, fr, y, h, o, key, col=(0.85, 0.85, 0.82, 1), ext=0.0):
    obox(M[key], fr, -ext, fr.L + ext, y, y + h, 0.0, o, tile=(2, 2), color=col)


# ------------------------------------------------------------------ сборка стены
def facade(M, a, b, n, y_start, y_end, floor_h, style, rnd, wall_key, wall_tile, wall_col, public=True, front=False):
    """style: villa | tile | officetel | ribbon. Возвращает число окон."""
    fr = WallFrame(a, b, n)
    L = fr.L
    if L < 3:
        face(M[wall_key], fr, 0, L, y_start, y_end, 0, wall_tile, wall_col)
        return 0
    frame_col = rnd.choice([(0.82, 0.82, 0.8, 1), (0.82, 0.82, 0.8, 1), (0.25, 0.25, 0.26, 1), (0.55, 0.5, 0.45, 1)])
    nfl = max(1, int(round((y_end - y_start) / floor_h)))
    floor_h = (y_end - y_start) / nfl
    count = 0
    if style == "ribbon":
        mod = 1.5
        nb = max(1, int((L - 1.2) / mod)); m0 = (L - nb * mod) / 2
        for f in range(nfl):
            fy = y_start + f * floor_h
            yb, yt = fy + 1.0, fy + floor_h - 0.45
            # спандрел выступает: ниже ленты и выше
            face(M[wall_key], fr, 0, L, fy, yb, 0.0, wall_tile, wall_col)
            face(M[wall_key], fr, 0, L, yt, fy + floor_h, 0.0, wall_tile, wall_col)
            face(M[wall_key], fr, 0, m0, yb, yt, 0.0, wall_tile, wall_col)
            face(M[wall_key], fr, L - m0, L, yb, yt, 0.0, wall_tile, wall_col)
            horiz(M[wall_key], fr, m0, L - m0, yt, -0.3, 0, False, wall_tile, tuple(c * 0.7 for c in wall_col[:3]) + (1,))
            horiz(M[wall_key], fr, m0, L - m0, yb, -0.3, 0, True, wall_tile, wall_col)
            side(M[wall_key], fr, m0, yb, yt, -0.3, 0, +1, wall_tile, wall_col)
            side(M[wall_key], fr, L - m0, yb, yt, -0.3, 0, -1, wall_tile, wall_col)
            lit_row = rnd.random()
            for k in range(nb):
                t0 = m0 + k * mod
                idx = rnd.choice(CELLS["office"] if rnd.random() < (0.55 if lit_row > 0.3 else 0.15) else CELLS["office_dark"] + CELLS["blinds"])
                u0, v0, u1, v1 = cell_uv(idx)
                face(M["win"], fr, t0, t0 + mod, yb, yt, -0.28, uv=[(u0, v1), (u1, v1), (u1, v0), (u0, v0)])
                obox(M["frame"], fr, t0 - 0.03, t0 + 0.03, yb, yt, -0.3, -0.18, color=(0.3, 0.32, 0.33, 1), faces="F")
                count += 1
            # вертикальные ламели-рёбра каждые 3 м
            for k in range(0, nb + 1, 2):
                tl = m0 + k * mod
                obox(M["frame"], fr, tl - 0.05, tl + 0.05, yb - 0.02, yt + 0.02, -0.02, 0.35, color=(0.62, 0.64, 0.65, 1), faces="FST")
        cornice(M, fr, y_end, 0.7, 0.35, wall_key, wall_col)
        return count

    # стили с отдельными окнами
    if style == "villa":
        bay = 3.0; ww, wh, sill_h = (1.8, 1.3, 0.95)
    elif style == "tile":
        bay = 3.0; ww, wh, sill_h = (1.6, 1.45, 0.9)
    else:  # officetel
        bay = 2.6; ww, wh, sill_h = (1.9, 2.0, 0.75)
    nb = max(1, int((L - 0.8) / bay)); m0 = (L - nb * bay) / 2
    # план этажа: какие пролёты — балконы (одинаково по вертикали — как в реальности)
    plan = []
    for k in range(nb):
        r = rnd.random()
        if style in ("villa", "tile") and (front or public) and r < 0.32 and nb >= 2:
            plan.append("balcony")
        elif r < 0.1:
            plan.append("blank")
        else:
            plan.append("window")
    wide = {k: (rnd.random() < 0.25) for k in range(nb)}
    for f in range(nfl):
        fy = y_start + f * floor_h
        yb, yt = fy + sill_h, min(fy + sill_h + wh, fy + floor_h - 0.3)
        opens = []
        for k, kind in enumerate(plan):
            bt0 = m0 + k * bay
            if kind == "window":
                w_ = min(bay - 0.5, ww * (1.35 if wide[k] else 1.0))
                c = bt0 + bay / 2
                opens.append((c - w_ / 2, c + w_ / 2, k))
        # куски стены: под окнами, над окнами, простенки
        face(M[wall_key], fr, 0, L, fy, yb, 0, wall_tile, wall_col)
        face(M[wall_key], fr, 0, L, yt, fy + floor_h, 0, wall_tile, wall_col)
        cur = 0.0
        for (t0, t1, k) in opens:
            face(M[wall_key], fr, cur, t0, yb, yt, 0, wall_tile, wall_col)
            cur = t1
        face(M[wall_key], fr, cur, L, yb, yt, 0, wall_tile, wall_col)
        for (t0, t1, k) in opens:
            bars = style == "villa" and f == 0 and rnd.random() < 0.7
            window(M, fr, t0, t1, yb, yt, rnd, wall_key, wall_tile, wall_col, frame_col,
                   depth=0.22 if style != "officetel" else 0.16, bars=bars, mullions=1 if (t1 - t0) < 2.2 else 2,
                   transom=style == "officetel")
            count += 1
            # кондиционер под окном
            if rnd.random() < (0.22 if style != "officetel" else 0.12) and f < nfl:
                ac_cage(M, fr, (t0 + t1) / 2 - 0.47, fy + 0.15, rnd)
            # козырёк-«бровь» над окном у плиточных домов
            if style == "tile" and rnd.random() < 0.4:
                obox(M["slab"], fr, t0 - 0.15, t1 + 0.15, yt + 0.05, yt + 0.13, 0, 0.35, color=(0.8, 0.8, 0.78, 1))
        for k, kind in enumerate(plan):
            bt0 = m0 + k * bay
            if kind == "balcony":
                balcony(M, fr, bt0 + 0.2, bt0 + bay - 0.2, fy + 0.18, floor_h, rnd, wall_key, wall_tile, wall_col, frame_col)
        # межэтажный пояс
        if style in ("villa", "tile") and f > 0:
            cornice(M, fr, fy - 0.06, 0.12, 0.06, wall_key, (0.82, 0.82, 0.8, 1) if style == "villa" else wall_col)
    # венчающий карниз
    cornice(M, fr, y_end - 0.1, 0.35, 0.22 if style != "officetel" else 0.12, "slab", (0.85, 0.85, 0.83, 1), ext=0.22)
    return count
