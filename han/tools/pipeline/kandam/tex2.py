"""Фотореалистичные процедурные материалы 2K для района «Кандам»: многослойные (зерно, трещины, заплатки, пятна, мох, потёки, износ).
Всё создаётся кодом — своя работа; чужих сканов нет."""
import math
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage

import assets
from common import blur, normal_from_height, save_png, tile_noise, to_u8

N = 2048


# ------------------------------------------------------------------ базовые кирпичики
def lowfreq(n, beta, seed):
    """Бесшовный низкочастотный шум: считаем на 512 и растягиваем (быстро)."""
    m = 512
    a = tile_noise(m, beta, seed)
    if n == m:
        return a.astype(np.float32)
    z = ndimage.zoom(np.pad(a, 8, mode="wrap"), n / m, order=3)
    off = int(8 * n / m)
    return z[off:off + n, off:off + n].astype(np.float32)


def grain(n, seed, sigma=0.8):
    a = np.random.default_rng(seed).random((n, n)).astype(np.float32)
    a = ndimage.gaussian_filter(a, sigma, mode="wrap")
    return ((a - a.min()) / (a.max() - a.min() + 1e-9)).astype(np.float32)


def draw_wrap(n, fn):
    """Рисует в 'L'-холст с оборачиванием по краям (бесшовно). fn(draw, dx, dy)."""
    im = Image.new("L", (n, n), 0)
    d = ImageDraw.Draw(im)
    for dx in (-n, 0, n):
        for dy in (-n, 0, n):
            fn(d, dx, dy)
    return np.asarray(im, dtype=np.float32) / 255.0


def cracks(n, count, seed, width=2, length=500, branch=0.35):
    rng = np.random.default_rng(seed)
    lines = []
    for _ in range(count):
        x, y = rng.uniform(0, n, 2)
        ang = rng.uniform(0, 2 * math.pi)
        pts = [(x, y)]
        for k in range(int(length / 14)):
            ang += rng.normal(0, 0.35)
            x += math.cos(ang) * 14
            y += math.sin(ang) * 14
            pts.append((x, y))
            if rng.random() < branch / 6:
                bx, by, ba = x, y, ang + rng.choice([-1, 1]) * rng.uniform(0.5, 1.1)
                bp = [(bx, by)]
                for _ in range(rng.integers(3, 9)):
                    ba += rng.normal(0, 0.4)
                    bx += math.cos(ba) * 11
                    by += math.sin(ba) * 11
                    bp.append((bx, by))
                lines.append((bp, max(1, width - 1)))
        lines.append((pts, width))

    def fn(d, dx, dy):
        for pts, w in lines:
            d.line([(px + dx, py + dy) for px, py in pts], fill=255, width=int(w))

    m = draw_wrap(n, fn)
    return ndimage.gaussian_filter(m, 0.9, mode="wrap")


def blobs(n, count, seed, rmin, rmax, soft=18):
    rng = np.random.default_rng(seed)
    items = [(rng.uniform(0, n), rng.uniform(0, n), rng.uniform(rmin, rmax), rng.uniform(0.5, 1.0), rng.uniform(0.6, 1.4)) for _ in range(count)]

    def fn(d, dx, dy):
        for x, y, r, a, e in items:
            d.ellipse([x - r + dx, y - r * e + dy, x + r + dx, y + r * e + dy], fill=int(255 * a))

    m = draw_wrap(n, fn)
    return ndimage.gaussian_filter(m, soft, mode="wrap")


def dots(n, count, seed, rmin, rmax):
    rng = np.random.default_rng(seed)
    items = [(rng.uniform(0, n), rng.uniform(0, n), rng.uniform(rmin, rmax)) for _ in range(count)]

    def fn(d, dx, dy):
        for x, y, r in items:
            d.ellipse([x - r + dx, y - r + dy, x + r + dx, y + r + dy], fill=255)

    return ndimage.gaussian_filter(draw_wrap(n, fn), 1.0, mode="wrap")


def streaks_v(n, seed, amp=1.0, length=0.08, width=1.4):
    rng = np.random.default_rng(seed)
    s = ndimage.gaussian_filter(rng.random((n, n)), (n * length, width), mode="wrap")
    return ((s - s.min()) / (s.max() - s.min() + 1e-9) * amp).astype(np.float32)


def put(out, name, alb, hgt, rough, metal=None, nstrength=2.0):
    n = alb.shape[0]
    save_png(out / f"{name}_BC.png", to_u8(alb))
    save_png(out / f"{name}_N.png", normal_from_height(hgt.astype(np.float32), nstrength))
    m = np.zeros_like(rough) if metal is None else metal
    save_png(out / f"{name}_ORM.png", to_u8(np.stack([np.ones_like(rough), rough, m], -1)))


# ------------------------------------------------------------------ поверхности
def asphalt(out, n=N):
    fine = grain(n, 1, 0.7)
    mid = grain(n, 2, 2.0)
    macro = lowfreq(n, 2.5, 3)
    macro2 = lowfreq(n, 1.6, 4)
    stones = ndimage.gaussian_filter((grain(n, 5, 1.0) > 0.86).astype(np.float32), 1.0, mode="wrap")
    dark_stones = ndimage.gaussian_filter((grain(n, 6, 1.0) > 0.9).astype(np.float32), 1.1, mode="wrap")
    base = 0.30 + (fine - 0.5) * 0.15 + (mid - 0.5) * 0.10 + (macro - 0.5) * 0.09 + stones * 0.20 - dark_stones * 0.10
    # битумные заплатки (ровнее, темнее, с каймой-швом)
    rng = np.random.default_rng(7)
    patch = np.zeros((n, n), np.float32)
    seam = np.zeros((n, n), np.float32)
    for _ in range(6):
        w, h = rng.integers(220, 700), rng.integers(180, 520)
        x, y = rng.integers(0, n - w), rng.integers(0, n - h)
        patch[y:y + h, x:x + w] = 1
        seam[y:y + 3, x:x + w] = 1
        seam[y + h - 3:y + h, x:x + w] = 1
        seam[y:y + h, x:x + 3] = 1
        seam[y:y + h, x + w - 3:x + w] = 1
    patch = ndimage.gaussian_filter(patch, 2.5)
    base = base * (1 - patch * 0.08) + patch * ndimage.gaussian_filter(base, 3.0) * 0.0
    base = np.where(patch > 0.5, ndimage.gaussian_filter(base, 2.0), base)
    cr = np.clip(cracks(n, 16, 8, 2, 700) + cracks(n, 5, 9, 4, 600) * 0.9 + seam * 0.8, 0, 1)
    oil = blobs(n, 14, 10, 40, 110, 22)
    oil = np.clip(oil * 1.6, 0, 1)
    rubber = blobs(n, 5, 11, 160, 320, 60) * 0.5
    alb = base * (1 - 0.65 * cr) * (1 - 0.34 * oil) * (1 - 0.12 * rubber)
    tint = np.array([0.985, 1.0, 1.025], np.float32)
    alb = np.clip(alb[..., None] * tint, 0, 1)
    rough = 0.88 - (fine - 0.5) * 0.30 - stones * 0.14 - patch * 0.12 - oil * 0.42 + cr * 0.08 + rubber * 0.06
    hgt = fine * 0.5 + mid * 0.3 + stones * 0.9 + macro * 0.2 - cr * 1.1 + patch * 0.1
    put(out, "asphalt", alb, hgt, np.clip(rough, 0.25, 1), nstrength=1.7)


def pavers(out, name="sidewalk", n=N, grid=6, gap=7, base=0.58, seed=20):
    rng = np.random.default_rng(seed)
    cell = n // grid
    alb = np.zeros((n, n, 3), np.float32)
    hgt = np.zeros((n, n), np.float32)
    yy, xx = np.mgrid[0:cell, 0:cell].astype(np.float32) / cell
    for j in range(grid):
        for i in range(grid):
            k = base + rng.normal(0, 0.018)
            tint = np.array([1.0, 0.965, 0.95]) + rng.normal(0, 0.012, 3)
            alb[j * cell:(j + 1) * cell, i * cell:(i + 1) * cell] = (np.array(tint) * k)
            gx, gy = rng.normal(0, 0.05, 2)
            hgt[j * cell:(j + 1) * cell, i * cell:(i + 1) * cell] = 0.8 + gx * xx + gy * yy
    spk = grain(n, seed + 1, 0.55)
    light = (spk > 0.80).astype(np.float32)
    darkk = (spk < 0.14).astype(np.float32)
    pink = (grain(n, seed + 2, 0.6) > 0.88).astype(np.float32)
    alb *= 1 + light[..., None] * 0.28 - darkk[..., None] * 0.30
    alb += pink[..., None] * np.array([0.05, 0.015, 0.02], np.float32)
    macro = lowfreq(n, 2.4, seed + 3)
    alb *= (0.9 + 0.2 * macro)[..., None]
    jm = np.ones((n, n), np.float32)
    for k in range(grid):
        jm[k * cell:k * cell + gap, :] = 0
        jm[:, k * cell:k * cell + gap] = 0
    jm = ndimage.gaussian_filter(jm, 1.6, mode="wrap")
    moss = lowfreq(n, 1.8, seed + 4)
    moss = np.clip((moss - 0.55) * 4, 0, 1) * (1 - jm)
    joint_col = np.array([0.15, 0.155, 0.13], np.float32)
    alb = alb * jm[..., None] + joint_col * (1 - jm[..., None])
    alb = alb * (1 - moss[..., None] * 0.5) + np.array([0.12, 0.2, 0.08], np.float32) * moss[..., None] * 0.5
    stain = blobs(n, 18, seed + 5, 60, 170, 28)
    alb *= (1 - 0.30 * np.clip(stain * 1.7, 0, 1))[..., None]
    gum = dots(n, 70, seed + 6, 4, 9)
    alb *= (1 - 0.55 * gum)[..., None]
    cr = cracks(n, 4, seed + 7, 2, 350, 0.1)
    alb *= (1 - 0.7 * cr)[..., None]
    wear = lowfreq(n, 2.0, seed + 8)
    rough = 0.74 - spk * 0.16 + (1 - jm) * 0.22 - wear * 0.18 - stain * 0.1
    hgt = hgt * 0.6 * jm + spk * 0.08 - cr * 0.5 + (1 - jm) * -0.4
    put(out, name, np.clip(alb, 0, 1), hgt, np.clip(rough, 0.3, 1), nstrength=1.8)


def concrete(out, n=N):
    g = lowfreq(n, 2.2, 21)
    f = grain(n, 22, 0.9)
    pores = dots(n, 2500, 23, 1.2, 3.2)
    v = 0.56 + (g - 0.5) * 0.14 + (f - 0.5) * 0.06
    v = v * (1 - streaks_v(n, 24, 0.20)) + 0.04 * streaks_v(n, 25, 1.0)
    alb = np.stack([v, v * 0.995, v * 0.975], -1)
    h = f * 0.35 + g * 0.3 - pores * 0.7
    panel = n // 4
    for k in range(0, n, panel):  # швы опалубки
        alb[k:k + 3, :] *= 0.72
        h[k:k + 3, :] -= 0.5
        alb[:, k:k + 3] *= 0.8
        h[:, k:k + 3] -= 0.4
    tie = Image.new("L", (n, n))
    d = ImageDraw.Draw(tie)
    for ky in range(panel // 2, n, panel):
        for kx in range(panel // 4, n, panel // 2):
            d.ellipse([kx - 9, ky - 9, kx + 9, ky + 9], fill=255)
    tie = ndimage.gaussian_filter(np.asarray(tie, np.float32) / 255, 1.2)
    alb *= (1 - 0.35 * tie - 0.25 * pores)[..., None]
    h -= tie * 0.8
    cr = cracks(n, 5, 26, 1, 450, 0.2)
    alb *= (1 - 0.55 * cr)[..., None]
    h -= cr * 0.6
    stain = blobs(n, 10, 27, 90, 220, 34)
    alb *= (1 - 0.18 * stain)[..., None]
    put(out, "concrete", np.clip(alb, 0, 1), h, np.clip(0.9 - f * 0.2 - 0.1 * stain, 0.4, 1), nstrength=1.4)


def roof(out, n=N):
    g = lowfreq(n, 1.6, 31)
    gr = grain(n, 32, 0.8)
    gravel = (grain(n, 33, 0.9) > 0.78).astype(np.float32)
    gravel = ndimage.gaussian_filter(gravel, 0.9, mode="wrap")
    v = 0.30 + (g - 0.5) * 0.10 + (gr - 0.5) * 0.09 + gravel * 0.12
    alb = np.stack([v, v * 1.01, v * 1.03], -1)
    seam = np.zeros((n, n), np.float32)
    seam[:, ::n // 6] = 1
    seam[::n // 8, :] = 0.5
    seam = ndimage.gaussian_filter(seam, 2.0, mode="wrap")
    alb *= (1 + seam[..., None] * 0.18)
    pud = np.clip((lowfreq(n, 2.4, 34) - 0.62) * 6, 0, 1)
    alb *= (1 - 0.35 * pud)[..., None]
    dirt = streaks_v(n, 35, 0.3)
    alb *= (1 - 0.25 * dirt)[..., None]
    cr = cracks(n, 4, 36, 2, 300, 0.1)
    alb *= (1 - 0.5 * cr)[..., None]
    h = gr * 0.4 + gravel * 0.6 + seam * 0.8 - cr * 0.7 - pud * 0.2
    put(out, "roof", np.clip(alb, 0, 1), h, np.clip(0.88 - gravel * 0.12 - pud * 0.5, 0.2, 1), nstrength=1.5)


def bark(out, n=512):
    s = streaks_v(n, 41, 1.0, 0.15, 1.1)
    f = grain(n, 42, 1.0)
    v = 0.22 + s * 0.16 + (f - 0.5) * 0.06
    alb = np.stack([v * 1.0, v * 0.85, v * 0.7], -1)
    put(out, "bark", np.clip(alb, 0, 1), s * 0.8 + f * 0.2, np.full((n, n), 0.95, np.float32), nstrength=2.5)


# ------------------------------------------------------------------ грязь на фасадах
def install_grime():
    orig = assets.Canvas.save

    def patched(self, outdir, name, normal_strength=2.0, with_emissive=False):
        n = self.W
        seed = abs(hash(name)) % 1000
        soil = lowfreq(max(self.W, self.H), 2.3, seed)[:self.H, :self.W]
        spk = grain(max(self.W, self.H), seed + 1, 0.9)[:self.H, :self.W]
        if name == "facade_glass":
            # волнистость стекла (искажает отражения) и разводы
            wave = lowfreq(max(self.W, self.H), 3.0, seed + 2)[:self.H, :self.W]
            self.hgt += (wave - 0.5) * 0.18
            self.alb *= (0.92 + 0.16 * soil)[..., None]
            st = streaks_v(max(self.W, self.H), seed + 3, 1.0)[:self.H, :self.W]
            self.rgh += st * 0.08
        else:
            self.alb *= (0.84 + 0.30 * soil)[..., None]
            st = streaks_v(max(self.W, self.H), seed + 3, 1.0, 0.12, 1.6)[:self.H, :self.W]
            self.alb *= (1 - 0.26 * st)[..., None]
            self.hgt += (spk - 0.5) * 0.05
            self.alb *= (1 + (spk - 0.5) * 0.10)[..., None]
            self.rgh = np.clip(self.rgh + (soil - 0.5) * 0.14, 0.05, 1)
        orig(self, outdir, name, normal_strength, with_emissive)

    assets.Canvas.save = patched


# ------------------------------------------------------------------ декали, листва, шум
def decals(out):
    # 1) колеи: две полосы вдоль v, тайл по v
    w, h = 512, 1024
    n = 1024
    s = lowfreq(n, 2.0, 51)[:, :w]
    f = grain(n, 52, 1.0)[:, :w]
    u = np.linspace(0, 1, w, dtype=np.float32)[None, :]
    band = np.exp(-((u - 0.27) / 0.075) ** 2) + np.exp(-((u - 0.73) / 0.075) ** 2)
    a = np.clip(band * (0.35 + 0.5 * s) * (0.8 + 0.4 * f), 0, 0.62)
    rgba = np.zeros((h, w, 4), np.uint8)
    rgba[..., 0], rgba[..., 1], rgba[..., 2] = 14, 14, 15
    rgba[..., 3] = to_u8(a)
    save_png(out / "decal_tracks.png", rgba)
    # 2) атлас 2x2 по 1024: масло, трещины, люк, решётка
    cell = 1024
    atlas = np.zeros((2 * cell, 2 * cell, 4), np.uint8)
    atlas[..., :3] = 20
    oil = blobs(cell, 4, 61, 60, 180, 26)
    oil = np.clip(oil * 1.5, 0, 0.8) * (0.85 + 0.3 * grain(cell, 62, 1.5))
    atlas[:cell, :cell, 3] = to_u8(oil)
    atlas[:cell, :cell, 0:3] = (12, 12, 14)
    cr = cracks(cell, 7, 63, 3, 600, 0.5)
    cr = np.clip(cr * 1.1, 0, 0.9)
    atlas[:cell, cell:, 3] = to_u8(cr)
    atlas[:cell, cell:, 0:3] = (8, 8, 9)
    # люк
    im = Image.new("RGBA", (cell, cell), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.ellipse([112, 112, 912, 912], fill=(46, 44, 42, 255))
    d.ellipse([130, 130, 894, 894], fill=(78, 74, 70, 255))
    for k in range(-5, 6):
        d.line([(190 + k * 60, 200), (190 + k * 60 + 380, 824)], fill=(56, 52, 50, 255), width=18)
        d.line([(190 + k * 60 + 400, 200), (190 + k * 60 + 20, 824)], fill=(60, 56, 54, 255), width=10)
    d.ellipse([430, 430, 594, 594], fill=(52, 49, 47, 255))
    arr = np.asarray(im).astype(np.float32)
    arr[..., :3] *= (0.8 + 0.4 * grain(cell, 64, 0.8))[..., None]
    arr[..., 3] = np.where(arr[..., 3] > 0, 255, 0)
    atlas[cell:, :cell] = np.clip(arr, 0, 255).astype(np.uint8)
    # решётка водостока
    im = Image.new("RGBA", (cell, cell), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rectangle([60, 330, 964, 694], fill=(40, 38, 36, 255))
    for k in range(10):
        x0 = 100 + k * 88
        d.rectangle([x0, 350, x0 + 52, 674], fill=(4, 4, 5, 255))
    d.rectangle([60, 330, 964, 694], outline=(92, 88, 84, 255), width=12)
    arr = np.asarray(im).astype(np.float32)
    arr[..., :3] *= (0.85 + 0.3 * grain(cell, 65, 0.8))[..., None]
    atlas[cell:, cell:] = np.clip(arr, 0, 255).astype(np.uint8)
    save_png(out / "decal_misc.png", atlas)


def leaves(out):
    """Атлас листвы 1024x512: жёлтый гинкго и зелёный. Карточки-кластеры с альфой."""
    S = 512
    atlas = np.zeros((S, 2 * S, 4), np.uint8)
    rng = np.random.default_rng(71)
    for ci, (pal) in enumerate([[(214, 176, 42), (232, 196, 60), (196, 150, 30), (240, 210, 90)],
                                [(70, 120, 46), (92, 142, 58), (54, 98, 40), (110, 160, 70)]]):
        im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)
        pts = []
        for _ in range(900):
            r = abs(rng.normal(0, 0.42)) * S * 0.5
            a = rng.uniform(0, 2 * math.pi)
            pts.append((S / 2 + r * math.cos(a), S / 2 + r * math.sin(a) * 0.9))
        # ветки
        for _ in range(10):
            a = rng.uniform(0, 2 * math.pi)
            d.line([(S / 2, S / 2), (S / 2 + math.cos(a) * S * 0.38, S / 2 + math.sin(a) * S * 0.33)], fill=(58, 44, 32, 255), width=3)
        for (x, y) in sorted(pts, key=lambda p: p[1]):
            r = rng.uniform(16, 28)
            rot = rng.uniform(0, 2 * math.pi)
            col = pal[rng.integers(0, len(pal))]
            k = rng.uniform(0.78, 1.05)
            col = tuple(int(c * k) for c in col) + (255,)
            poly = [(x, y)]
            for a in np.linspace(-58, 58, 13):
                ang = math.radians(a) + rot
                rr = r * (1.0 - 0.18 * math.exp(-(a / 9) ** 2))
                poly.append((x + rr * math.cos(ang), y + rr * math.sin(ang)))
            d.polygon(poly, fill=col)
            dc = tuple(int(c * 0.78) for c in col[:3]) + (255,)
            d.line([(x, y), (x + r * 0.9 * math.cos(rot), y + r * 0.9 * math.sin(rot))], fill=dc, width=1)
        arr = np.asarray(im).copy()
        a = arr[..., 3].astype(np.float32)
        mean = arr[a > 128][:, :3].mean(axis=0)
        arr[a < 128, :3] = mean.astype(np.uint8)
        atlas[:, ci * S:(ci + 1) * S] = arr
    save_png(out / "leaves.png", atlas)


def noise_tex(out, n=512):
    r = (grain(n, 81, 1.2) * 0.5 + lowfreq(n, 1.2, 82) * 0.5)
    g = lowfreq(n, 3.0, 83)
    b = grain(n, 84, 2.5)
    save_png(out / "noise.png", to_u8(np.stack([r, g, b], -1)))


def build_all(out):
    out.mkdir(parents=True, exist_ok=True)
    install_grime()
    asphalt(out)
    pavers(out, "sidewalk", grid=6, gap=7, base=0.58, seed=20)
    pavers(out, "plaza", grid=4, gap=9, base=0.64, seed=30)
    concrete(out)
    roof(out)
    bark(out)
    assets.tex_glass_facade(out, n=N)
    assets.tex_ribbon_facade(out, n=N)
    assets.tex_punched_facade(out, n=N)
    assets.tex_brick_facade(out, n=N)
    assets.tex_storefront(out, n=N)
    assets.tex_glow(out)
    assets.tex_plain(out, "metal_dark", (0.16, 0.17, 0.18), 0.45, 0.8)
    assets.tex_plain(out, "metal_light", (0.55, 0.57, 0.58), 0.4, 0.8)
    assets.tex_plain(out, "car_glass", (0.05, 0.07, 0.08), 0.08, 0.0)
    decals(out)
    leaves(out)
    noise_tex(out)
