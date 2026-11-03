"""Материалы из CC0-сканов ambientCG (через PatrickJnr/cc0-texture-pack, формат Source 2 .vtex_c).
Делает PBR-тройки *_BC / *_N / *_ORM 2048² для генератора района + атлас листвы + фасады со сканами стен."""
import glob
import math
import sys
from pathlib import Path

import numpy as np
from PIL import Image
from scipy import ndimage

sys.path.insert(0, str(Path(__file__).parent))
import vtex
from common import normal_from_height, tile_noise

import os
PACK = Path(os.environ.get("HAN_CC0_PACK", Path(__file__).resolve().parents[4] / "assets_bin" / "cc0-texture-pack")) / "textures"
CREDITS = []


def _files(name, kind):
    return sorted(glob.glob(str(PACK / name[0] / f"{name}_{kind}_*.vtex_c")))


def load(name, kind="color", variant=None, mip=0):
    fs = _files(name, kind)
    if not fs:
        return None
    best = None
    for f in fs:
        try:
            im, meta = vtex.decode(f, mip)
        except Exception:
            continue
        if variant == "alpha":
            if best is None or im[..., 3].std() > best[..., 3].std():
                best = im
        else:
            best = im
            break
    if best is not None and name not in [c[0] for c in CREDITS]:
        CREDITS.append((name, kind))
    return best


def to_size(a, n):
    if a.shape[0] == n and a.shape[1] == n:
        return a
    return np.asarray(Image.fromarray(a).resize((n, n), Image.LANCZOS))


def rough_from(col, base, k=0.25):
    lum = col[..., :3].astype(np.float32).mean(-1) / 255
    lp = ndimage.gaussian_filter(lum, 24, mode="wrap")
    return np.clip(base + (lp - lum) * k * 2.5 + (0.5 - lum) * k * 0.4, 0.05, 1)


def pbr(out, name, scan, n=2048, rough=0.85, rk=0.25, metal=0.0, tint=(1, 1, 1), gain=1.0, nflip_y=False, n_strength=1.0, rough_map=None):
    col = load(scan, "color")
    if col is None:
        print("нет скана", scan); return False
    col = to_size(col[..., :3], n).astype(np.float32)
    col = np.clip(col * np.array(tint) * gain, 0, 255)
    nm, rB = scan_normal(scan, n, n_strength, with_rough=True)
    if nm is None:
        lum = col.mean(-1) / 255
        nm = normal_from_height(lum - ndimage.gaussian_filter(lum, 8, mode="wrap") + 0.5, 1.5).astype(np.float32)
    ao = load(scan, "ao")
    ao = to_size(ao[..., 0], n).astype(np.float32) / 255 if ao is not None else np.ones((n, n), np.float32)
    rg = rough_map if rough_map is not None else rough_from(col, rough, rk)
    if rB is not None and rough_map is None:
        rg = np.clip(0.5 * rg + 0.5 * np.maximum(rB, rough - 0.35), 0.05, 1)
    orm = np.stack([ao, rg, np.full_like(rg, metal)], -1)
    Image.fromarray(col.astype(np.uint8)).save(Path(out) / f"{name}_BC.png")
    Image.fromarray(np.clip(nm, 0, 255).astype(np.uint8)).save(Path(out) / f"{name}_N.png")
    Image.fromarray((orm * 255 + 0.5).astype(np.uint8)).save(Path(out) / f"{name}_ORM.png")
    return True


def scan_normal(scan, n, strength=1.0, with_rough=False):
    """Нормали Source 2: R=X, G=Y (DirectX, вниз), B — шероховатость. Переводим в OpenGL RGB."""
    nm = load(scan, "normal")
    if nm is None:
        return (None, None) if with_rough else None
    nm = to_size(nm, n).astype(np.float32)
    x = nm[..., 0] / 127.5 - 1; y = -(nm[..., 1] / 127.5 - 1)
    x *= strength; y *= strength
    z = np.sqrt(np.clip(1 - x * x - y * y, 0.02, 1))
    l = np.sqrt(x * x + y * y + z * z)
    rgb = np.stack([x / l, y / l, z / l], -1) * 127.5 + 127.5
    rough = nm[..., 2] / 255.0
    if with_rough:
        return rgb, rough
    return rgb


# ------------------------------------------------------------------ листва
def _leaf_sprites(scan, max_n=12):
    im = load(scan, "color", variant="alpha")
    if im is None or im[..., 3].std() < 10:
        return []
    a = im[..., 3] > 128
    lab, n = ndimage.label(a)
    out = []
    for sl in ndimage.find_objects(lab):
        h, w = sl[0].stop - sl[0].start, sl[1].stop - sl[1].start
        if h * w < 4000:
            continue
        out.append(im[sl].copy())
    out.sort(key=lambda x: -x.shape[0] * x.shape[1])
    return out[:max_n]


def _ginkgo_sprite(rng, S=160):
    """Веерный лист гинкго: сектор ~150°, выемка по центру, радиальные жилки."""
    yy, xx = np.mgrid[0:S, 0:S].astype(np.float32)
    cx, cy = S / 2, S * 0.92
    dx, dy = xx - cx, cy - yy
    r = np.hypot(dx, dy) / (S * 0.85)
    ang = np.arctan2(dx, dy)
    half = math.radians(rng.uniform(62, 78))
    edge = 1.0 + 0.04 * np.sin(ang * 23 + rng.uniform(0, 6)) - 0.22 * np.exp(-(ang / 0.09) ** 2)
    m = (np.abs(ang) < half * (1 - 0.15 * (1 - r))) & (r < edge) & (r > 0.12)
    stem = (np.abs(dx) < S * 0.018) & (yy > cy - S * 0.2) & (yy < S)
    veins = 0.95 + 0.05 * np.cos(ang * 40)
    a = (m | stem).astype(np.float32)
    shade = (0.78 + 0.3 * r) * veins
    rgb = np.stack([shade] * 3, -1)
    rgb[stem & ~m] = 0.55
    a = ndimage.gaussian_filter(a, 0.7)
    return rgb, a


def leaf_atlas(out, W=2048, H=512):
    """4 ячейки: гинкго жёлтый, гинкго зелёный, дзелква осенняя, дзелква зелёная. Ветки-побеги рисуются тоже."""
    rng = np.random.default_rng(77)
    S = H
    atlas = np.zeros((H, W, 4), np.float32)
    zel = []
    for sc in ["leafset003", "leafset005", "leafset001", "leafset016", "leafset011", "leafset006", "leafset004"]:
        zel += _leaf_sprites(sc)
    palettes = [
        [(0.95, 0.78, 0.18), (0.98, 0.84, 0.3), (0.88, 0.66, 0.12), (0.92, 0.8, 0.35), (0.8, 0.72, 0.25)],  # гинкго осень
        [(0.42, 0.58, 0.2), (0.5, 0.65, 0.24), (0.36, 0.5, 0.18), (0.55, 0.62, 0.22)],
        [(0.72, 0.38, 0.14), (0.62, 0.3, 0.12), (0.8, 0.5, 0.18), (0.55, 0.42, 0.16), (0.68, 0.55, 0.2)],  # дзелква осень
        [(0.3, 0.45, 0.17), (0.36, 0.52, 0.2), (0.26, 0.4, 0.15)],
    ]
    for cell in range(4):
        x0 = cell * S
        canvas = np.zeros((S, S, 4), np.float32)
        # веточки: от низа к краям
        from PIL import ImageDraw
        tw = Image.new("L", (S, S), 0); dtw = ImageDraw.Draw(tw)
        tips = []
        for k in range(5):
            sx, sy = S * 0.5 + rng.uniform(-40, 40), S * 0.98
            ex, ey = S * (0.12 + 0.76 * rng.random()), S * (0.08 + 0.5 * rng.random())
            pts = [(sx + (ex - sx) * t + 12 * math.sin(t * 7 + k), sy + (ey - sy) * t) for t in np.linspace(0, 1, 9)]
            dtw.line(pts, fill=255, width=int(rng.uniform(3, 6)))
            tips += pts[3:]
        twa = np.asarray(tw, np.float32) / 255
        canvas[..., :3] += twa[..., None] * np.array([0.25, 0.2, 0.15])
        canvas[..., 3] = np.maximum(canvas[..., 3], twa)
        pal = palettes[cell]
        n_leaves = 150 if cell < 2 else 120
        for i in range(n_leaves):
            px, py = tips[rng.integers(len(tips))]
            px += rng.normal(0, S * 0.09); py += rng.normal(0, S * 0.07)
            if cell < 2:
                rgb, a = _ginkgo_sprite(rng, 160)
                sz = rng.uniform(46, 72)
            else:
                if not zel:
                    rgb, a = _ginkgo_sprite(rng, 160)
                else:
                    sp = zel[rng.integers(len(zel))]
                    rgb = sp[..., :3].astype(np.float32) / 255
                    lum = rgb.mean(-1, keepdims=True)
                    rgb = np.repeat(lum / max(lum.mean(), 1e-3) * 0.8, 3, -1)
                    a = sp[..., 3].astype(np.float32) / 255
                sz = rng.uniform(50, 80)
            col = np.array(pal[rng.integers(len(pal))]) * rng.uniform(0.85, 1.1)
            leaf = np.concatenate([np.clip(rgb * col, 0, 1), a[..., None]], -1)
            imL = Image.fromarray((leaf * 255).astype(np.uint8), "RGBA")
            k = sz / max(imL.size)
            imL = imL.resize((max(2, int(imL.size[0] * k)), max(2, int(imL.size[1] * k))), Image.LANCZOS)
            imL = imL.rotate(rng.uniform(-180, 180) if cell >= 2 else rng.uniform(-70, 70) + 180 * (rng.random() < 0.3), expand=True, resample=Image.BICUBIC)
            L = np.asarray(imL, np.float32) / 255
            # затенение глубины: дальние листья темнее
            L[..., :3] *= rng.uniform(0.7, 1.0)
            h, w = L.shape[:2]
            ix, iy = int(px - w / 2), int(py - h / 2)
            sx0, sy0 = max(0, -ix), max(0, -iy)
            ex_, ey_ = min(w, S - ix), min(h, S - iy)
            if ex_ <= sx0 or ey_ <= sy0:
                continue
            reg = canvas[iy + sy0:iy + ey_, ix + sx0:ix + ex_]
            src = L[sy0:ey_, sx0:ex_]
            al = src[..., 3:4]
            reg[..., :3] = reg[..., :3] * (1 - al) + src[..., :3] * al
            reg[..., 3:4] = np.maximum(reg[..., 3:4], al)
        # края ячейки прозрачные
        fade = np.ones((S, S), np.float32); b = 6
        fade[:b] = 0; fade[-b:] = 0; fade[:, :b] = 0; fade[:, -b:] = 0
        canvas[..., 3] *= fade
        # «расплываем» цвет под прозрачные пиксели (чтобы мипы не темнели)
        a = canvas[..., 3]
        filled = canvas[..., :3].copy()
        wsum = ndimage.gaussian_filter(a, 6)
        for c in range(3):
            fc = ndimage.gaussian_filter(canvas[..., c] * a, 6) / np.maximum(wsum, 1e-4)
            filled[..., c] = np.where(a > 0.02, canvas[..., c] / np.maximum(a, 1e-3) * np.minimum(a * 1.0, 1) + fc * 0, fc)
        canvas[..., :3] = np.where(a[..., None] > 0.02, canvas[..., :3], filled)
        atlas[:, x0:x0 + S] = canvas
    Image.fromarray((np.clip(atlas, 0, 1) * 255).astype(np.uint8), "RGBA").save(Path(out) / "leaves.png")


def bark(out, n=1024):
    """Кора: вертикальные трещины (дзелква/гинкго — серо-коричневая, с бороздами)."""
    rng = np.random.default_rng(9)
    base = tile_noise(n, 2.0, 91)
    # анизотропный шум — вытянут по V
    from numpy.fft import fft2, ifft2, fftfreq
    fy, fx = np.meshgrid(fftfreq(n), fftfreq(n), indexing="ij")
    spec = (np.random.default_rng(3).normal(size=(n, n)) + 1j * np.random.default_rng(4).normal(size=(n, n)))
    filt = np.exp(-((fx * n / 40) ** 2 + (fy * n / 4) ** 2))
    furrow = np.real(ifft2(spec * filt)); furrow = (furrow - furrow.min()) / (np.ptp(furrow) + 1e-6)
    ridges = np.abs(furrow - 0.5) * 2
    fine = tile_noise(n, 0.6, 92)
    hgt = 1 - ridges ** 0.7 * 0.8 + (fine - 0.5) * 0.25
    col = np.array([0.42, 0.37, 0.32]) * (0.55 + 0.6 * hgt[..., None]) * (0.85 + 0.3 * base[..., None])
    lich = ndimage.gaussian_filter((tile_noise(n, 3.0, 93) > 0.72).astype(np.float32), 2, mode="wrap")
    col = col * (1 - 0.35 * lich[..., None]) + lich[..., None] * np.array([0.5, 0.55, 0.45]) * 0.35
    Image.fromarray((np.clip(col, 0, 1) * 255).astype(np.uint8)).save(Path(out) / "bark_BC.png")
    Image.fromarray(normal_from_height(hgt, 5.0)).save(Path(out) / "bark_N.png")
    orm = np.stack([0.6 + 0.4 * hgt, 0.92 - 0.1 * hgt, np.zeros_like(hgt)], -1)
    Image.fromarray((np.clip(orm, 0, 1) * 255).astype(np.uint8)).save(Path(out) / "bark_ORM.png")


# ------------------------------------------------------------------ фасады: стены из сканов + процедурные окна
def _tiled(img, n, tile_m, scan_m):
    """Ресемпл скана (scan_m метров на текстуру) в тайл n×n пикселей на tile_m метров."""
    reps = tile_m / scan_m
    k = int(round(reps)) if reps >= 1 else 1
    big = np.tile(img, (k, k, 1)) if img.ndim == 3 else np.tile(img, (k, k))
    return to_size(big.astype(np.uint8) if big.dtype != np.uint8 else big, n)


def _wall_canvas(scan, n, tile_m, scan_m, tint=(1, 1, 1), rough=0.85):
    import assets
    c = assets.Canvas(n, n, tile_m, tile_m, rough=rough)
    col = load(scan, "color")[..., :3]
    col = _tiled(col, n, tile_m, scan_m).astype(np.float32) / 255 * np.array(tint)
    c.alb[:] = col
    nm = scan_normal(scan, 2048)
    c._scan_n = _tiled(np.clip(nm, 0, 255).astype(np.uint8), n, tile_m, scan_m).astype(np.float32) if nm is not None else None
    c._wall = np.ones((n, n), bool)
    c.rgh[:] = rough_from((col * 255), rough, 0.2)
    orig = c.rect

    def rect(x0, y0, x1, y1, **kw):
        if "alb" in kw or "hgt" in kw:
            c._wall[c._sl(x0, y0, x1, y1)] = False
        orig(x0, y0, x1, y1, **kw)
    c.rect = rect
    return c


def _save_wall(c, out, name, nstr):
    from common import blur
    c.save(out, name, nstr, True)
    if c._scan_n is not None:
        p = Path(out) / f"{name}_N.png"
        proc = np.asarray(Image.open(p)).astype(np.float32)
        w = c._wall[..., None]
        Image.fromarray(np.where(w, c._scan_n, proc).astype(np.uint8)).save(p)


def facade_brick(out, scan="bricks017", n=2048, seed=44):
    rng = np.random.default_rng(seed)
    c = _wall_canvas(scan, n, 6.0, 2.0, rough=0.9)
    for f in range(2):
        y0 = f * 3.0
        for b in range(2):
            x0 = b * 3.0
            c.rect(x0 + 0.5, y0 + 0.85, x0 + 2.5, y0 + 0.95, alb=(0.58, 0.56, 0.53), hgt=0.85, rgh=0.8)   # перемычка
            c.rect(x0 + 0.55, y0 + 0.95, x0 + 2.45, y0 + 2.3, alb=(0.62, 0.63, 0.62), hgt=0.7, rgh=0.4, met=0.5)
            c.rect(x0 + 0.62, y0 + 1.02, x0 + 2.38, y0 + 2.23, alb=np.array([0.09, 0.12, 0.13]) * (0.8 + 0.5 * rng.random()), hgt=0.35, rgh=0.06)
            c.rect(x0 + 1.47, y0 + 1.02, x0 + 1.53, y0 + 2.23, alb=(0.62, 0.63, 0.62), hgt=0.8, met=0.5)
            if rng.random() < 0.3:  # решётка на окне (типично для вилл)
                for k in range(6):
                    xx = x0 + 0.62 + k * 0.33
                    c.rect(xx, y0 + 1.02, xx + 0.025, y0 + 2.23, alb=(0.75, 0.75, 0.72), hgt=0.95, met=0.8, rgh=0.4)
            r = rng.random()
            if r < 0.34:
                c.rect(x0 + 0.62, y0 + 1.02, x0 + 2.38, y0 + 2.23, emi=np.array([1.0, 0.76, 0.46]) * (0.2 + 0.5 * rng.random()))
            elif r < 0.42:
                c.rect(x0 + 0.62, y0 + 1.02, x0 + 2.38, y0 + 2.23, emi=np.array([0.75, 0.88, 1.0]) * 0.3)
            c.rect(x0 + 0.45, y0 + 2.3, x0 + 2.55, y0 + 2.42, alb=(0.66, 0.64, 0.6), hgt=0.95, rgh=0.8)    # подоконник
    _save_wall(c, out, "facade_brick", 2.4)


def facade_punched(out, scan="tiles097", n=2048, seed=43):
    rng = np.random.default_rng(seed)
    c = _wall_canvas(scan, n, 12.0, 1.5, rough=0.5)
    for f in range(3):
        y0 = f * 4.0
        for b in range(4):
            x0 = b * 3.0
            c.rect(x0 + 0.6, y0 + 0.9, x0 + 2.4, y0 + 2.9, alb=(0.24, 0.26, 0.27), hgt=0.15, rgh=0.25, met=0.6)
            c.rect(x0 + 0.68, y0 + 0.98, x0 + 2.32, y0 + 2.82, alb=np.array([0.08, 0.12, 0.14]) * (0.8 + 0.5 * rng.random()), hgt=0.28, rgh=0.06)
            c.rect(x0 + 1.47, y0 + 0.98, x0 + 1.53, y0 + 2.82, alb=(0.35, 0.37, 0.38), hgt=0.8, rgh=0.4, met=0.6)
            if rng.random() < 0.3:  # жалюзи / шторы
                c.rect(x0 + 0.68, y0 + 0.98, x0 + 2.32, y0 + 0.98 + rng.uniform(0.4, 1.6), alb=np.array([0.75, 0.72, 0.66]) * rng.uniform(0.6, 1.0), hgt=0.3, rgh=0.8)
            r = rng.random()
            if r < 0.28:
                c.rect(x0 + 0.68, y0 + 0.98, x0 + 2.32, y0 + 2.82, emi=np.array([1.0, 0.78, 0.5]) * (0.2 + 0.5 * rng.random()))
            elif r < 0.38:
                c.rect(x0 + 0.68, y0 + 0.98, x0 + 2.32, y0 + 2.82, emi=np.array([0.7, 0.86, 1.0]) * 0.3)
            c.rect(x0 + 0.5, y0 + 2.9, x0 + 2.5, y0 + 3.02, alb=(0.8, 0.79, 0.76), hgt=0.9)
    # потёки под подоконниками
    st = ndimage.gaussian_filter(np.random.default_rng(5).random((n, n)).astype(np.float32), (40, 1.5), mode="wrap")
    st = (st - st.min()) / (np.ptp(st) + 1e-6)
    c.alb *= (0.86 + 0.18 * st)[..., None]
    _save_wall(c, out, "facade_punched", 2.0)


def facade_ribbon(out, scan="plaster004", n=2048, seed=42):
    """Офисный фасад: ленты бетона/штукатурки + сплошные ленты стекла (3 этажа по 4 м)."""
    rng = np.random.default_rng(seed)
    c = _wall_canvas(scan, n, 12.0, 2.0, tint=(1.15, 1.13, 1.1), rough=0.8)
    for f in range(3):
        y0 = f * 4.0
        c.rect(0, y0 + 1.1, 12, y0 + 3.3, alb=(0.18, 0.2, 0.21), hgt=0.2, rgh=0.2, met=0.6)
        for k in range(8):
            x0 = k * 1.5
            tone = np.array([0.08, 0.11, 0.13]) * (0.8 + 0.5 * rng.random())
            c.rect(x0 + 0.04, y0 + 1.15, x0 + 1.46, y0 + 3.25, alb=tone, hgt=0.3, rgh=0.04)
            r = rng.random()
            if r < 0.32:
                c.rect(x0 + 0.04, y0 + 1.15, x0 + 1.46, y0 + 3.25, emi=np.array([0.85, 0.9, 1.0]) * (0.25 + 0.4 * rng.random()))
            elif r < 0.4:
                c.rect(x0 + 0.04, y0 + 1.15, x0 + 1.46, y0 + 3.25, emi=np.array([1.0, 0.8, 0.55]) * 0.35)
    _save_wall(c, out, "facade_ribbon", 2.0)


def far_facade(out, scans=("facade012", "facade013", "facade014", "facade015", "facade016"), n=2048):
    """Атлас фотофасадов для дальнего города (5 полос по U) + маска горящих окон для ночи."""
    W = 2048
    cols, ems = [], []
    rng = np.random.default_rng(31)
    for s in scans:
        im = load(s, "color")
        if im is None:
            continue
        im = to_size(im[..., :3], 1024).astype(np.float32)
        lum = im.mean(-1) / 255
        loc = ndimage.uniform_filter(lum, 25)
        win = (lum < loc * 0.85).astype(np.float32)
        blocks = rng.random((64, 64)) < 0.3
        bm = np.kron(blocks, np.ones((16, 16)))[:1024, :1024]
        warm = np.where(rng.random((64, 64)) < 0.7, 1, 0)
        wm = np.kron(warm, np.ones((16, 16)))[:1024, :1024]
        e = (win * bm)[..., None] * (wm[..., None] * np.array([1.0, 0.75, 0.45]) + (1 - wm[..., None]) * np.array([0.7, 0.85, 1.0])) * 0.8
        cols.append(im); ems.append(e)
    k = len(cols)
    atlas = np.concatenate(cols, 1); em = np.concatenate(ems, 1)
    Image.fromarray(atlas.astype(np.uint8)).resize((W, 1024 * 2048 // W if False else 1024)).save(Path(out) / "far_facade_BC.png")
    Image.fromarray((np.clip(em, 0, 1) * 255).astype(np.uint8)).resize((W, 1024)).save(Path(out) / "far_facade_E.png")
    return k


def manhole_tex(out, scan="manholecover003", n=1024):
    im = load(scan, "color")
    im = to_size(im[..., :3], n).astype(np.float32)
    yy, xx = np.mgrid[0:n, 0:n]
    r = np.hypot(xx - n / 2, yy - n / 2) / (n / 2)
    a = (r < 0.97).astype(np.float32)
    Image.fromarray(np.concatenate([im, a[..., None] * 255], -1).astype(np.uint8), "RGBA").save(Path(out) / "manhole.png")


def tree_grate_tex(out, n=512):
    """Квадратная чугунная решётка с круглым отверстием под ствол (альфа)."""
    yy, xx = np.mgrid[0:n, 0:n].astype(np.float32) / n
    r = np.hypot(xx - 0.5, yy - 0.5)
    ring = (np.abs((r * 18) % 1 - 0.5) > 0.32) | (r > 0.47)
    spokes = (np.abs(((np.arctan2(yy - 0.5, xx - 0.5) / (2 * math.pi)) * 24) % 1 - 0.5) > 0.4)
    metal = (ring | spokes) & (r > 0.13)
    border = (xx < 0.03) | (xx > 0.97) | (yy < 0.03) | (yy > 0.97)
    a = (metal | border).astype(np.float32)
    col = np.stack([0.17 + 0.05 * a] * 3, -1) * (0.9 + 0.2 * tile_noise(n, 1.0, 3)[..., None])
    col[..., 0] *= 1.08
    Image.fromarray(np.concatenate([np.clip(col, 0, 1), a[..., None]], -1).__mul__(255).astype(np.uint8), "RGBA").save(Path(out) / "tree_grate.png")


def build_all(out):
    out = Path(out); out.mkdir(parents=True, exist_ok=True)
    pbr(out, "asphalt", "asphalt010", rough=0.88, rk=0.3, gain=0.95)
    pbr(out, "sidewalk", "pavingstones030", rough=0.85, rk=0.25, tint=(0.96, 0.95, 0.97))
    pbr(out, "plaza", "pavingstones029", rough=0.7, rk=0.3, gain=1.12, tint=(1.0, 1.0, 1.02))
    pbr(out, "concrete", "concrete008", rough=0.9, rk=0.2)
    pbr(out, "roof", "concrete015", rough=0.75, rk=0.2, gain=1.25)
    pbr(out, "tactile", "tactilepaving001", rough=0.7, rk=0.2)
    pbr(out, "metal_light", "metal001", rough=0.45, metal=0.9)
    pbr(out, "metal_dark", "metal009", rough=0.5, metal=0.85, gain=0.45)
    pbr(out, "wall_brick", "bricks017", rough=0.9, rk=0.2)
    pbr(out, "wall_tile", "tiles097", rough=0.45, rk=0.2)
    pbr(out, "wall_plaster", "plaster004", rough=0.85, rk=0.2, gain=1.18)
    import facade3d
    facade3d.interior_atlas(out)
    facade_brick(out)
    facade_punched(out)
    facade_ribbon(out)
    far_facade(out)
    manhole_tex(out)
    tree_grate_tex(out)
    leaf_atlas(out)
    bark(out)
    return CREDITS


def storefront(out, n=2048, seed=45):
    """Витрины 1-го этажа (тайл 12 × 5 м): освещённые интерьеры — магазины, кафе, круглосуточные, закрытые жалюзи."""
    import assets
    rng = np.random.default_rng(seed)
    h_px = int(round(n * 5 / 12))
    c = assets.Canvas(n, h_px, 12, 5, base_albedo=(0.3, 0.3, 0.31), rough=0.7)
    wall = load("tiles038", "color")
    if wall is not None:
        t = _tiled(wall[..., :3], n, 12.0, 2.0)[:h_px].astype(np.float32) / 255
        c.alb[:] = t * 0.55
    c.rect(0, 0, 12, 1.3, alb=(0.2, 0.2, 0.21), hgt=0.55, rgh=0.7)  # поле под вывеску
    for b in range(4):
        x0 = b * 3.0
        c.rect(x0 + 0.08, 1.3, x0 + 2.92, 4.78, alb=(0.1, 0.1, 0.11), hgt=0.2, rgh=0.3, met=0.6)
        kind = rng.random()
        gx0, gx1, gy0, gy1 = x0 + 0.16, x0 + 2.84, 1.42, 4.3
        if kind < 0.0:  # закрытые рольставни (выключено: ночью почти все лавки открыты)
            for k in range(int((gy1 - gy0) / 0.08)):
                y = gy0 + k * 0.08
                c.rect(gx0, y, gx1, y + 0.05, alb=np.array([0.55, 0.56, 0.56]) * rng.uniform(0.85, 1.0), hgt=0.6, rgh=0.5, met=0.7)
                c.rect(gx0, y + 0.05, gx1, y + 0.08, alb=(0.35, 0.36, 0.36), hgt=0.4, met=0.7)
            continue
        warm = np.array([1.0, 0.86, 0.66]) if rng.random() < 0.55 else np.array([0.92, 0.96, 1.0])
        back = np.array(rng.choice([[0.85, 0.85, 0.83], [0.62, 0.45, 0.3], [0.8, 0.78, 0.72], [0.7, 0.75, 0.78], [0.9, 0.88, 0.8]]))
        c.rect(gx0, gy0, gx1, gy1, alb=back * 0.35, hgt=0.3, rgh=0.04)
        c.rect(gx0, gy0, gx1, gy1, emi=back * warm * rng.uniform(0.7, 0.95))
        cafe = rng.random() < 0.4
        if cafe:
            back = np.array([0.62, 0.52, 0.42]) * rng.uniform(0.85, 1.05)
            c.rect(gx0, gy0, gx1, gy1, alb=back * 0.35, emi=np.clip(back * warm * 1.1, 0, 1))
        # полки с товаром / столики / стойка
        for s_ in (range(3) if not cafe else range(1)):
            y = gy0 + 0.7 + s_ * 0.6
            c.rect(gx0 + 0.1, y, gx1 - 0.1, y + 0.05, alb=(0.15, 0.15, 0.15), emi=warm * 0.2)
            x = gx0 + 0.12
            while x < gx1 - 0.2:
                w = rng.uniform(0.05, 0.18)
                col = np.array(rng.choice([[0.9, 0.2, 0.2], [0.2, 0.5, 0.9], [0.95, 0.8, 0.2], [0.2, 0.7, 0.3], [0.95, 0.95, 0.95], [0.9, 0.5, 0.15]]))
                hh = rng.uniform(0.18, 0.45)
                c.rect(x, y - hh, x + w, y, alb=col * 0.3, emi=col * warm * 0.55)
                x += w + rng.uniform(0.0, 0.05)
        if cafe:  # столики и подвесные лампы
            for _ in range(3):
                px = rng.uniform(gx0 + 0.3, gx1 - 0.3)
                c.rect(px - 0.3, gy1 - 0.75, px + 0.3, gy1 - 0.7, alb=(0.3, 0.2, 0.12), emi=warm * 0.15)
                c.rect(px - 0.03, gy1 - 0.7, px + 0.03, gy1 - 0.25, alb=(0.1, 0.08, 0.06))
                c.rect(px - 0.1, gy0 + 0.35, px + 0.1, gy0 + 0.5, emi=warm * 2.0)
        c.rect(gx0, gy0, gx1, gy0 + 0.12, emi=warm * 1.6)                    # потолочная светлая полоса
        c.rect(gx0, gy1 - 0.25, gx1, gy1, alb=(0.25, 0.22, 0.2), emi=back * warm * 0.25)  # пол
        c.rect(x0 + 1.46, gy0, x0 + 1.54, 4.78, alb=(0.1, 0.1, 0.11), hgt=0.7, met=0.6)   # импост/дверь
        if rng.random() < 0.5:  # наклейки на стекле (часы работы, акции)
            c.rect(x0 + 0.4, 3.5, x0 + 1.1, 3.9, alb=(0.9, 0.9, 0.9), emi=np.array([1, 1, 1]) * 0.3)
    c.rect(0, 4.3, 12, 5.0, alb=(0.32, 0.32, 0.33), hgt=0.7, rgh=0.6)  # цоколь
    c.save(out, "facade_shop", 2.0, True)
