"""Процедурные текстуры (фасады, асфальт, плитка...) и атласы вывесок / LED-экранов для района «Кандам».
Все изображения создаются кодом — своя работа, чужих текстур и логотипов нет. Бренды — только вымышленные (из data/city/brands.json проекта + новые)."""
import importlib.util
import math
import random
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter
from scipy import ndimage

from common import blur, normal_from_height, save_png, tile_noise, to_u8
from font_paths import font_path

def font(size, bold=True):
    path, index = font_path(bold)
    result = ImageFont.truetype(str(path), int(size), index=index)
    if path.suffix.lower() == '.ttf':
        try:
            result.set_variation_by_name('Bold' if bold else 'Medium')
        except (OSError, ValueError):
            pass  # обычный TTF без осей вариаций
    return result


# ============================================================================ текстуры
class Canvas:
    """Холст тайла в метрах: x вправо, y вниз от верха тайла."""

    def __init__(self, w_px, h_px, w_m, h_m, base_albedo=(0.5, 0.5, 0.5), rough=0.8, metal=0.0):
        self.W, self.H, self.wm, self.hm = w_px, h_px, w_m, h_m
        self.alb = np.ones((h_px, w_px, 3), np.float32) * np.array(base_albedo, np.float32)
        self.hgt = np.full((h_px, w_px), 0.5, np.float32)
        self.rgh = np.full((h_px, w_px), rough, np.float32)
        self.met = np.full((h_px, w_px), metal, np.float32)
        self.emi = np.zeros((h_px, w_px, 3), np.float32)

    def _sl(self, x0, y0, x1, y1):
        ix0 = int(round(x0 / self.wm * self.W))
        ix1 = int(round(x1 / self.wm * self.W))
        iy0 = int(round(y0 / self.hm * self.H))
        iy1 = int(round(y1 / self.hm * self.H))
        return slice(max(iy0, 0), min(iy1, self.H)), slice(max(ix0, 0), min(ix1, self.W))

    def rect(self, x0, y0, x1, y1, alb=None, hgt=None, rgh=None, met=None, emi=None):
        s = self._sl(x0, y0, x1, y1)
        if alb is not None:
            self.alb[s] = np.asarray(alb, np.float32)
        if hgt is not None:
            self.hgt[s] = hgt
        if rgh is not None:
            self.rgh[s] = rgh
        if met is not None:
            self.met[s] = met
        if emi is not None:
            self.emi[s] = np.asarray(emi, np.float32)

    def mul(self, x0, y0, x1, y1, k):
        s = self._sl(x0, y0, x1, y1)
        self.alb[s] *= k

    def save(self, outdir, name, normal_strength=2.0, with_emissive=False):
        outdir = Path(outdir)
        save_png(outdir / f"{name}_BC.png", to_u8(self.alb))
        save_png(outdir / f"{name}_N.png", normal_from_height(blur(self.hgt, 0.6), normal_strength))
        orm = np.stack([np.ones_like(self.rgh), self.rgh, self.met], axis=-1)
        save_png(outdir / f"{name}_ORM.png", to_u8(orm))
        if with_emissive:
            save_png(outdir / f"{name}_E.png", to_u8(self.emi))


def _streaks(n, seed, amp=0.12):
    rng = np.random.default_rng(seed)
    s = ndimage.gaussian_filter(rng.random((n, n)), (n * 0.05, 1.3), mode="wrap")
    s = (s - s.min()) / (s.max() - s.min() + 1e-9)
    return 1.0 - amp * s


def tex_asphalt(out, n=1024):
    g1 = tile_noise(n, 1.4, 1)
    grain = np.random.default_rng(2).random((n, n)).astype(np.float32)
    grain = ndimage.gaussian_filter(grain, 0.7, mode="wrap")
    grain = (grain - grain.min()) / (grain.max() - grain.min())
    patch = tile_noise(n, 2.6, 3)
    v = 0.36 + (g1 - 0.5) * 0.05 + (grain - 0.5) * 0.10 + (patch - 0.5) * 0.06
    alb = np.stack([v * 0.98, v, v * 1.03], axis=-1)
    # светлые крупные камешки
    peb = (np.random.default_rng(4).random((n, n)) > 0.9965).astype(np.float32)
    peb = ndimage.gaussian_filter(peb, 1.1, mode="wrap") * 6
    alb += peb[..., None] * 0.05
    rough = 0.84 - (grain - 0.5) * 0.25 - peb * 0.1
    h = grain * 0.7 + g1 * 0.3
    save_png(out / "asphalt_BC.png", to_u8(alb))
    save_png(out / "asphalt_N.png", normal_from_height(h, 0.9))
    save_png(out / "asphalt_ORM.png", to_u8(np.stack([np.ones((n, n)), rough, np.zeros((n, n))], -1)))


def tex_pavers(out, n=1024, grid=6, gap=3, tint=(1.0, 0.97, 0.95), name="sidewalk", base=0.55):
    rng = np.random.default_rng(11 if name == "sidewalk" else 12)
    cell = n // grid
    alb = np.zeros((n, n, 3), np.float32)
    hgt = np.zeros((n, n), np.float32)
    spec = ndimage.gaussian_filter(rng.random((n, n)), 0.8, mode="wrap")
    spec = (spec - spec.min()) / (spec.max() - spec.min())
    mac = tile_noise(n, 2.4, 13)
    for j in range(grid):
        for i in range(grid):
            k = base + rng.normal(0, 0.035)
            sub = np.array(tint) * k
            alb[j * cell:(j + 1) * cell, i * cell:(i + 1) * cell] = sub
            hgt[j * cell:(j + 1) * cell, i * cell:(i + 1) * cell] = 1.0
    alb *= (0.94 + 0.12 * spec[..., None] + 0.05 * (mac[..., None] - 0.5))
    mask = np.ones((n, n), np.float32)
    for k in range(grid):
        mask[k * cell:k * cell + gap, :] = 0
        mask[:, k * cell:k * cell + gap] = 0
    mask = ndimage.gaussian_filter(mask, 0.8, mode="wrap")
    alb = alb * (0.35 + 0.65 * mask[..., None])
    hgt = mask * 0.8 + spec * 0.1
    rough = 0.72 - spec * 0.2 + (1 - mask) * 0.2
    save_png(out / f"{name}_BC.png", to_u8(alb))
    save_png(out / f"{name}_N.png", normal_from_height(hgt, 2.0))
    save_png(out / f"{name}_ORM.png", to_u8(np.stack([np.ones((n, n)), rough, np.zeros((n, n))], -1)))


def tex_concrete(out, n=1024):
    g = tile_noise(n, 2.2, 21)
    f = tile_noise(n, 0.8, 22)
    v = 0.55 + (g - 0.5) * 0.12 + (f - 0.5) * 0.05
    v = v * _streaks(n, 23, 0.16)
    alb = np.stack([v, v * 0.995, v * 0.98], -1)
    h = f * 0.6 + g * 0.4
    for k in range(0, n, n // 4):  # швы опалубки
        alb[k:k + 2, :] *= 0.8
        h[k:k + 2, :] -= 0.3
    save_png(out / "concrete_BC.png", to_u8(alb))
    save_png(out / "concrete_N.png", normal_from_height(h, 1.6))
    save_png(out / "concrete_ORM.png", to_u8(np.stack([np.ones((n, n)), 0.9 - f * 0.2, np.zeros((n, n))], -1)))


def tex_roof(out, n=1024):
    g = tile_noise(n, 1.6, 31)
    gr = ndimage.gaussian_filter(np.random.default_rng(32).random((n, n)), 0.9, mode="wrap")
    gr = (gr - gr.min()) / (gr.max() - gr.min())
    v = 0.30 + (g - 0.5) * 0.08 + (gr - 0.5) * 0.08
    alb = np.stack([v, v * 1.01, v * 1.02], -1)
    h = gr * 0.5 + g * 0.2
    seam = np.zeros((n, n), np.float32)
    seam[:, ::n // 4] = 1
    seam = ndimage.gaussian_filter(seam, 1.5, mode="wrap")
    alb *= (1 - seam[..., None] * 0.25)
    save_png(out / "roof_BC.png", to_u8(alb))
    save_png(out / "roof_N.png", normal_from_height(h + seam, 1.4))
    save_png(out / "roof_ORM.png", to_u8(np.stack([np.ones((n, n)), 0.85 - gr * 0.15, np.zeros((n, n))], -1)))


def tex_plain(out, name, rgb, rough, metal=0.0, n=256):
    g = tile_noise(n, 2.0, abs(hash(name)) % 1000)
    v = 1 + (g - 0.5) * 0.06
    alb = np.clip(np.array(rgb)[None, None, :] * v[..., None], 0, 1)
    save_png(out / f"{name}_BC.png", to_u8(alb))
    save_png(out / f"{name}_N.png", normal_from_height(g, 0.6))
    save_png(out / f"{name}_ORM.png", to_u8(np.stack([np.ones((n, n)), np.full((n, n), rough), np.full((n, n), metal)], -1)))


def tex_glass_facade(out, n=1024, seed=41):
    """Стеклянный фасад-стена: 4 пролёта по 3 м × 3 этажа по 4 м = тайл 12 × 12 м."""
    rng = np.random.default_rng(seed)
    c = Canvas(n, n, 12, 12, base_albedo=(0.42, 0.44, 0.45), rough=0.4, metal=0.6)
    for f in range(3):
        y0 = f * 4.0
        c.rect(0, y0, 12, y0 + 0.25, alb=(0.50, 0.52, 0.53), hgt=0.9, rgh=0.45, met=0.5)             # торец плиты
        c.rect(0, y0 + 2.95, 12, y0 + 4.0, alb=(0.17, 0.19, 0.20), hgt=0.75, rgh=0.42, met=0.5)       # подоконная панель
        for p in range(8):
            x0 = p * 1.5
            tint = np.array([0.075, 0.125, 0.14]) * (0.8 + 0.5 * rng.random())
            blind = rng.random() < 0.16
            lit = rng.random()
            c.rect(x0 + 0.04, y0 + 0.25, x0 + 1.46, y0 + 2.95, alb=tint, hgt=0.45, rgh=0.06, met=0.0)
            if blind:
                frac = 0.35 + 0.65 * rng.random()
                col = np.array([0.50, 0.46, 0.38]) * (0.8 + 0.3 * rng.random())
                c.rect(x0 + 0.04, y0 + 0.25, x0 + 1.46, y0 + 0.25 + 2.7 * frac, alb=col, hgt=0.5, rgh=0.55, met=0.0)
            if lit < 0.26:
                warm = np.array([1.0, 0.74 + 0.1 * rng.random(), 0.45 + 0.1 * rng.random()]) * (0.25 + 0.5 * rng.random())
                c.rect(x0 + 0.04, y0 + 0.25, x0 + 1.46, y0 + 2.95, emi=warm)
            elif lit < 0.32:
                c.rect(x0 + 0.04, y0 + 0.25, x0 + 1.46, y0 + 2.95, emi=np.array([0.75, 0.88, 1.0]) * (0.2 + 0.3 * rng.random()))
            c.rect(x0 - 0.035, y0 + 0.25, x0 + 0.035, y0 + 4.0, alb=(0.52, 0.54, 0.55), hgt=1.0, rgh=0.35, met=0.7)  # импост
    c.alb *= _streaks(n, seed + 1, 0.10)[..., None]
    c.save(out, "facade_glass", 2.4, True)


def tex_ribbon_facade(out, n=1024, seed=42):
    """Бетонные панели с ленточными окнами: 3 пролёта по 4 м × 3 этажа по 4 м."""
    rng = np.random.default_rng(seed)
    c = Canvas(n, n, 12, 12, base_albedo=(0.70, 0.69, 0.67), rough=0.88)
    g = tile_noise(n, 2.0, seed)
    c.alb *= (0.94 + 0.12 * g)[..., None]
    c.hgt[:] = 0.5 + (g - 0.5) * 0.1
    for f in range(3):
        y0 = f * 4.0
        for b in range(3):
            x0 = b * 4.0
            c.rect(x0 + 0.35, y0 + 0.85, x0 + 3.65, y0 + 3.05, alb=(0.15, 0.17, 0.18), hgt=0.2, rgh=0.12, met=0.0)  # рама + тёмная глубина
            for k in range(2):
                xx = x0 + 0.5 + k * 1.57
                tint = np.array([0.09, 0.13, 0.15]) * (0.8 + 0.5 * rng.random())
                c.rect(xx, y0 + 0.97, xx + 1.4, y0 + 2.93, alb=tint, hgt=0.3, rgh=0.07, met=0.0)
                r = rng.random()
                if r < 0.18:
                    c.rect(xx, y0 + 0.97, xx + 1.4, y0 + 0.97 + 1.96 * (0.4 + 0.6 * rng.random()), alb=(0.62, 0.58, 0.48), hgt=0.32, rgh=0.6)
                if r < 0.3:
                    c.rect(xx, y0 + 0.97, xx + 1.4, y0 + 2.93, emi=np.array([1.0, 0.78, 0.5]) * (0.22 + 0.45 * rng.random()))
                elif r < 0.35:
                    c.rect(xx, y0 + 0.97, xx + 1.4, y0 + 2.93, emi=np.array([0.7, 0.85, 1.0]) * 0.3)
            c.rect(x0 + 0.3, y0 + 3.05, x0 + 3.7, y0 + 3.15, alb=(0.78, 0.77, 0.75), hgt=0.85)  # подоконник
            if rng.random() < 0.16:  # внешний блок кондиционера
                c.rect(x0 + 3.7, y0 + 2.2, x0 + 3.98, y0 + 3.0, alb=(0.72, 0.73, 0.74), hgt=0.7, rgh=0.5)
    c.alb *= _streaks(n, seed + 1, 0.22)[..., None]
    c.save(out, "facade_ribbon", 2.0, True)


def tex_punched_facade(out, n=1024, seed=43):
    """Плитка / штукатурка с окнами-проёмами: 4 пролёта по 3 м × 3 этажа по 4 м (нейтральная, красится цветом вершин)."""
    rng = np.random.default_rng(seed)
    c = Canvas(n, n, 12, 12, base_albedo=(0.74, 0.72, 0.68), rough=0.82)
    g = tile_noise(n, 1.8, seed)
    c.alb *= (0.92 + 0.14 * g)[..., None]
    c.hgt[:] = 0.5 + (g - 0.5) * 0.12
    # плитка 0.3 м
    step = n // 40
    for k in range(0, n, step):
        c.alb[k:k + 1, :] *= 0.88
        c.alb[:, k:k + 1] *= 0.9
    for f in range(3):
        y0 = f * 4.0
        for b in range(4):
            x0 = b * 3.0
            c.rect(x0 + 0.6, y0 + 0.9, x0 + 2.4, y0 + 2.9, alb=(0.20, 0.22, 0.23), hgt=0.15, rgh=0.2)
            c.rect(x0 + 0.68, y0 + 0.98, x0 + 2.32, y0 + 2.82, alb=np.array([0.09, 0.13, 0.15]) * (0.8 + 0.5 * rng.random()), hgt=0.28, rgh=0.07)
            c.rect(x0 + 1.47, y0 + 0.98, x0 + 1.53, y0 + 2.82, alb=(0.35, 0.37, 0.38), hgt=0.8, rgh=0.4)
            r = rng.random()
            if r < 0.28:
                c.rect(x0 + 0.68, y0 + 0.98, x0 + 2.32, y0 + 2.82, emi=np.array([1.0, 0.78, 0.5]) * (0.2 + 0.5 * rng.random()))
            elif r < 0.36:
                c.rect(x0 + 0.68, y0 + 0.98, x0 + 2.32, y0 + 2.82, emi=np.array([0.7, 0.86, 1.0]) * 0.28)
            c.rect(x0 + 0.5, y0 + 2.9, x0 + 2.5, y0 + 3.0, alb=(0.82, 0.81, 0.78), hgt=0.9)
    c.alb *= _streaks(n, seed + 1, 0.2)[..., None]
    c.save(out, "facade_punched", 2.0, True)


def tex_brick_facade(out, n=1024, seed=44):
    """Красный кирпич вилл / офистелей: тайл 6 × 6 м, 2 этажа по 3 м, окна 1,8 × 1,3 м."""
    rng = np.random.default_rng(seed)
    c = Canvas(n, n, 6, 6, base_albedo=(0.45, 0.2, 0.16), rough=0.9)
    bw, bh = 0.22, 0.075
    pxm = n / 6.0
    hgt = np.zeros((n, n), np.float32)
    for r in range(int(6 / bh) + 1):
        off = 0.0 if r % 2 == 0 else bw / 2
        for ccol in range(-1, int(6 / bw) + 2):
            x0 = ccol * bw + off
            y0 = r * bh
            col = np.array([0.46, 0.21, 0.16]) * (0.78 + 0.45 * rng.random())
            if rng.random() < 0.06:
                col = np.array([0.30, 0.15, 0.12])
            c.rect(x0 + 0.006, y0 + 0.006, x0 + bw - 0.006, y0 + bh - 0.006, alb=col, hgt=1.0)
    c.hgt[:] = np.where(c.hgt > 0.9, 1.0, 0.0)
    c.alb *= _streaks(n, seed + 1, 0.28)[..., None]
    # проёмы
    for f in range(2):
        y0 = f * 3.0
        for b in range(2):
            x0 = b * 3.0
            c.rect(x0 + 0.55, y0 + 0.9, x0 + 2.45, y0 + 2.3, alb=(0.55, 0.56, 0.55), hgt=0.7, rgh=0.4, met=0.4)     # рама
            c.rect(x0 + 0.65, y0 + 1.0, x0 + 2.35, y0 + 2.2, alb=np.array([0.10, 0.14, 0.15]) * (0.8 + 0.5 * rng.random()), hgt=0.4, rgh=0.07, met=0.0)
            c.rect(x0 + 1.5, y0 + 1.0, x0 + 1.55, y0 + 2.2, alb=(0.55, 0.56, 0.55), hgt=0.8)
            r = rng.random()
            if r < 0.34:
                c.rect(x0 + 0.65, y0 + 1.0, x0 + 2.35, y0 + 2.2, emi=np.array([1.0, 0.76, 0.46]) * (0.2 + 0.5 * rng.random()))
            c.rect(x0 + 0.45, y0 + 2.3, x0 + 2.55, y0 + 2.4, alb=(0.62, 0.6, 0.57), hgt=0.9, rgh=0.8)
    c.save(out, "facade_brick", 2.4, True)


def tex_storefront(out, n=1024, seed=45):
    """Витрины первого этажа: тайл 12 × 5 м (текстура n × n/2.4)."""
    rng = np.random.default_rng(seed)
    h_px = int(round(n * 5 / 12))
    c = Canvas(n, h_px, 12, 5, base_albedo=(0.36, 0.36, 0.37), rough=0.75)
    g = tile_noise(n, 2.0, seed)[:h_px, :]
    c.alb *= (0.9 + 0.2 * g)[..., None]
    c.rect(0, 0, 12, 1.3, alb=(0.30, 0.30, 0.31), hgt=0.55, rgh=0.8)  # поле под вывеску
    for b in range(4):
        x0 = b * 3.0
        c.rect(x0 + 0.1, 1.35, x0 + 2.9, 4.75, alb=(0.07, 0.07, 0.08), hgt=0.2, rgh=0.3, met=0.4)   # рама
        glass = np.array([0.16, 0.2, 0.21])
        c.rect(x0 + 0.2, 1.45, x0 + 2.8, 4.1, alb=glass, hgt=0.3, rgh=0.05, met=0.0)
        interior = rng.random()
        if interior < 0.72:
            warm = np.array([1.0, 0.82, 0.58]) if rng.random() < 0.7 else np.array([0.85, 0.92, 1.0])
            c.rect(x0 + 0.2, 1.45, x0 + 2.8, 4.1, emi=warm * (0.2 + 0.3 * rng.random()))
            # потолочный светильник и тёмная «полка» внутри
            c.rect(x0 + 0.2, 1.45, x0 + 2.8, 1.75, emi=warm * 0.8)
            c.rect(x0 + 0.5, 3.2, x0 + 2.5, 4.1, alb=(0.12, 0.1, 0.09), emi=warm * 0.25)
        c.rect(x0 + 0.1, 4.1, x0 + 2.9, 5.0, alb=(0.22, 0.22, 0.23), hgt=0.7, rgh=0.6)             # цоколь
        c.rect(x0 + 1.35, 1.45, x0 + 1.65, 4.75, alb=(0.07, 0.07, 0.08), hgt=0.6)                  # дверная стойка
    c.save(out, "facade_shop", 2.0, True)


def tex_glow(out, n=256):
    y, x = np.mgrid[0:n, 0:n].astype(np.float32)
    r = np.sqrt(((x - n / 2) / (n / 2)) ** 2 + ((y - n / 2) / (n / 2)) ** 2)
    a = np.clip(1 - r, 0, 1) ** 2.2
    rgba = np.zeros((n, n, 4), np.uint8)
    rgba[..., :3] = 255
    rgba[..., 3] = to_u8(a)
    save_png(out / "glow_soft.png", rgba)


def build_textures(out):
    out.mkdir(parents=True, exist_ok=True)
    tex_asphalt(out)
    tex_pavers(out, name="sidewalk", grid=6, tint=(1.0, 0.97, 0.95), base=0.56)
    tex_pavers(out, name="plaza", grid=4, gap=4, tint=(0.97, 0.96, 0.95), base=0.62)
    tex_concrete(out)
    tex_roof(out)
    tex_glass_facade(out)
    tex_ribbon_facade(out)
    tex_punched_facade(out)
    tex_brick_facade(out)
    tex_storefront(out)
    tex_glow(out)
    tex_plain(out, "metal_dark", (0.16, 0.17, 0.18), 0.45, 0.8)
    tex_plain(out, "metal_light", (0.55, 0.57, 0.58), 0.4, 0.8)
    tex_plain(out, "car_glass", (0.05, 0.07, 0.08), 0.08, 0.0)


# ============================================================================ бренды и вывески
def load_brands():
    spec = importlib.util.spec_from_file_location("mb", str(Path(__file__).resolve().parent.parent / "make_brands.py"))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    out = []
    for bid, sector, (ru, ko, en), colors, shapes in m.BRANDS:
        out.append(dict(id=bid, sector=sector, ru=ru, ko=ko, en=en, colors=colors, shapes=shapes, source="проект (data/city/brands.json)"))
    return out


# Новые вымышленные названия для района (id, сфера, ru, ko, en, цвета, фигуры логотипа в 64x64)
C, R, P, E = "circle", "rect", "polygon", "ellipse"
NEW_BRANDS = [
    dict(id="nuri_mall", sector="retail", ru="Нури Молл", ko="누리 몰", en="Nuri Mall", colors=["#0B3C5D", "#F1C40F"],
         shapes=[(R, dict(x=8, y=14, width=48, height=38, rx=6, fill="#F1C40F")), (R, dict(x=24, y=6, width=16, height=12, rx=5, fill="#0B3C5D")), (C, dict(cx=32, cy=34, r=9, fill="#0B3C5D"))]),
    dict(id="maeum_skin", sector="clinic", ru="Маым Скин", ko="마음 피부과", en="Maeum Skin", colors=["#2EC4B6", "#FFFFFF"],
         shapes=[(C, dict(cx=32, cy=32, r=26, fill="#FFFFFF")), (P, dict(points="32,52 14,30 20,18 32,26 44,18 50,30", fill="#2EC4B6"))]),
    dict(id="daebak_academy", sector="education", ru="Тэбак Академи", ko="대박 학원", en="Daebak Academy", colors=["#E63946", "#FFFFFF"],
         shapes=[(R, dict(x=8, y=10, width=48, height=44, rx=4, fill="#FFFFFF")), (P, dict(points="32,14 52,26 32,38 12,26", fill="#E63946")), (R, dict(x=28, y=36, width=8, height=14, fill="#E63946"))]),
    dict(id="dallae_hair", sector="beauty", ru="Даллэ Хэр", ko="달래 헤어", en="Dallae Hair", colors=["#8E44AD", "#F5CBA7"],
         shapes=[(C, dict(cx=32, cy=32, r=26, fill="#F5CBA7")), (E, dict(cx=32, cy=30, rx=11, ry=18, fill="#8E44AD"))]),
    dict(id="byeolttong_pocha", sector="bar", ru="Пёлтхон Поча", ko="별똥 포차", en="Byeolttong Pocha", colors=["#14213D", "#FCA311"],
         shapes=[(C, dict(cx=32, cy=32, r=26, fill="#14213D")), (P, dict(points="20,10 26,30 12,24 38,34 24,54 36,32 52,40 40,28", fill="#FCA311"))]),
    dict(id="haru_pharmacy", sector="pharmacy", ru="Хару Фарма", ko="하루 약국", en="Haru Pharmacy", colors=["#2A9D8F", "#FFFFFF"],
         shapes=[(R, dict(x=24, y=6, width=16, height=52, rx=3, fill="#FFFFFF")), (R, dict(x=6, y=24, width=52, height=16, rx=3, fill="#FFFFFF"))]),
    dict(id="myungho_group", sector="conglomerate", ru="Мёнхо Груп", ko="명호 그룹", en="Myungho Group", colors=["#0D1B2A", "#C9A227"],
         shapes=[(R, dict(x=6, y=6, width=52, height=52, rx=6, fill="#0D1B2A")), (P, dict(points="14,50 14,22 24,34 32,14 40,34 50,22 50,50", fill="#C9A227"))]),
    dict(id="liveon", sector="media", ru="ЛайвОн", ko="라이브온", en="LiveOn", colors=["#111111", "#FF2D55"],
         shapes=[(R, dict(x=6, y=14, width=52, height=36, rx=8, fill="#FF2D55")), (P, dict(points="26,22 26,42 44,32", fill="#FFFFFF"))]),
]
GENERIC = ["치킨", "호프", "노래방", "PC방", "약국", "미용실", "부동산", "학원", "한의원", "피부과", "성형외과", "고시원", "주점", "삼겹살", "냉면",
           "포차", "카페", "안경", "치과", "내과", "네일", "편의점", "국밥", "곱창", "분식", "꽃집", "세탁소", "당구장", "헬스", "스터디카페"]


def render_logo(shapes, size):
    ss = 4
    S = size * ss
    im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    k = S / 64.0
    for tag, a in shapes:
        fill = a["fill"]
        if tag == "circle":
            d.ellipse([(a["cx"] - a["r"]) * k, (a["cy"] - a["r"]) * k, (a["cx"] + a["r"]) * k, (a["cy"] + a["r"]) * k], fill=fill)
        elif tag == "ellipse":
            d.ellipse([(a["cx"] - a["rx"]) * k, (a["cy"] - a["ry"]) * k, (a["cx"] + a["rx"]) * k, (a["cy"] + a["ry"]) * k], fill=fill)
        elif tag == "rect":
            box = [a["x"] * k, a["y"] * k, (a["x"] + a["width"]) * k, (a["y"] + a["height"]) * k]
            if a.get("rx"):
                d.rounded_rectangle(box, radius=a["rx"] * k, fill=fill)
            else:
                d.rectangle(box, fill=fill)
        elif tag == "polygon":
            pts = [tuple(float(v) * k for v in p.split(",")) for p in a["points"].split()]
            d.polygon(pts, fill=fill)
    return im.resize((size, size), Image.LANCZOS)


def hex_rgb(h):
    h = h.lstrip("#")
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def lum(rgb):
    return (0.2126 * rgb[0] + 0.7152 * rgb[1] + 0.0722 * rgb[2]) / 255.0


def text_color_on(bg, light=(255, 255, 255), dark=(20, 20, 24)):
    return dark if lum(bg) > 0.55 else light


def draw_text_fit(d, text, box, fill, bold=True, max_size=200, min_size=10, stroke=0, stroke_fill=None, anchor="mm"):
    x0, y0, x1, y1 = box
    size = max_size
    while size > min_size:
        f = font(size, bold)
        bb = d.textbbox((0, 0), text, font=f, stroke_width=stroke)
        if bb[2] - bb[0] <= (x1 - x0) and bb[3] - bb[1] <= (y1 - y0):
            break
        size -= 2
    f = font(size, bold)
    d.text(((x0 + x1) / 2, (y0 + y1) / 2), text, font=f, fill=fill, anchor=anchor, stroke_width=stroke, stroke_fill=stroke_fill)


ALL_BRANDS = None


def brands():
    global ALL_BRANDS
    if ALL_BRANDS is None:
        ALL_BRANDS = load_brands() + [dict(b, source="новый для района (проверить в части 13)") for b in NEW_BRANDS]
    return ALL_BRANDS


def blade_cell(i, rnd):
    """Вертикальная вывеска 128 x 512."""
    W, H = 128, 512
    bl = brands()
    kind = rnd.random()
    if kind < 0.5:
        b = bl[i % len(bl)]
        bg = hex_rgb(b["colors"][0])
        fg = hex_rgb(b["colors"][1])
        im = Image.new("RGB", (W, H), bg)
        d = ImageDraw.Draw(im)
        d.rectangle([4, 4, W - 5, H - 5], outline=fg, width=3)
        logo = render_logo(b["shapes"], 96)
        im.paste(logo, (16, 16), logo)
        name = b["ko"].replace(" ", "")
        n = len(name)
        sz = min(70, int((H - 140) / max(n, 1)))
        col = text_color_on(bg) if lum(bg) < 0.8 else (20, 20, 24)
        for k, ch in enumerate(name):
            d.text((W / 2, 130 + sz * (k + 0.5)), ch, font=font(sz * 0.92), fill=col, anchor="mm")
        label = b["en"]
        return im, label
    else:
        word = GENERIC[(i * 7 + int(rnd.random() * 100)) % len(GENERIC)]
        palette = [(214, 40, 40), (255, 190, 11), (6, 214, 160), (17, 138, 178), (239, 71, 111), (255, 255, 255), (30, 30, 36), (131, 56, 236), (255, 127, 80)]
        bg = palette[int(rnd.random() * len(palette))]
        im = Image.new("RGB", (W, H), bg)
        d = ImageDraw.Draw(im)
        fg = text_color_on(bg)
        d.rectangle([5, 5, W - 6, H - 6], outline=fg, width=3)
        d.rectangle([5, 5, W - 6, 44], fill=fg)
        d.ellipse([W / 2 - 12, 14, W / 2 + 12, 38], fill=bg)
        chars = list(word)
        sz = min(76, int((H - 90) / max(len(chars), 1)))
        for k, ch in enumerate(chars):
            d.text((W / 2, 70 + sz * (k + 0.5)), ch, font=font(sz * 0.9), fill=fg, anchor="mm")
        return im, word


def horiz_cell(i, rnd):
    """Горизонтальная вывеска 512 x 128."""
    W, H = 512, 128
    bl = brands()
    if i < len(bl):
        b = bl[i]
        bg = hex_rgb(b["colors"][0])
        fg = hex_rgb(b["colors"][1])
        im = Image.new("RGB", (W, H), bg)
        d = ImageDraw.Draw(im)
        d.rectangle([3, 3, W - 4, H - 4], outline=fg, width=3)
        logo = render_logo(b["shapes"], 104)
        im.paste(logo, (12, 12), logo)
        col = text_color_on(bg) if lum(bg) < 0.8 else (20, 20, 24)
        draw_text_fit(d, b["ko"], (130, 8, W - 14, 74), col, max_size=62)
        draw_text_fit(d, b["en"].upper(), (130, 76, W - 14, 120), col, bold=False, max_size=34)
        return im, b["en"]
    palette = [(214, 40, 40), (255, 190, 11), (6, 214, 160), (17, 138, 178), (239, 71, 111), (240, 240, 235), (30, 30, 36), (131, 56, 236), (255, 127, 80)]
    word = GENERIC[(i * 5) % len(GENERIC)]
    suffix = rnd.choice(["", " 24시", " 본점", " 1호점", " 직영"])
    bg = palette[int(rnd.random() * len(palette))]
    im = Image.new("RGB", (W, H), bg)
    d = ImageDraw.Draw(im)
    fg = text_color_on(bg)
    d.rectangle([3, 3, W - 4, H - 4], outline=fg, width=3)
    draw_text_fit(d, word + suffix, (20, 10, W - 20, H - 10), fg, max_size=88)
    return im, word + suffix


def _led_bg(W, H, c1, c2, angle=0.0, rnd=None):
    y, x = np.mgrid[0:H, 0:W].astype(np.float32)
    t = ((x / W) * math.cos(angle) + (y / H) * math.sin(angle))
    t = (t - t.min()) / (t.max() - t.min() + 1e-6)
    c1 = np.array(c1, np.float32)
    c2 = np.array(c2, np.float32)
    arr = c1[None, None, :] * (1 - t[..., None]) + c2[None, None, :] * t[..., None]
    return Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8))


def led_grid(im, pitch=6):
    a = np.asarray(im).astype(np.float32)
    H, W = a.shape[:2]
    m = np.ones((H, W), np.float32)
    m[::pitch, :] = 0.72
    m[:, ::pitch] = 0.72
    return Image.fromarray(np.clip(a * m[..., None], 0, 255).astype(np.uint8))


LED_ADS = [
    # id бренда, фон1, фон2, большой текст (en), малый текст (ko), угол
    ("myungho_group", (8, 18, 38), (36, 60, 98), "MYUNGHO GROUP", "우리가 짓는 미래", 0.9),
    ("kumbit_shop", (58, 12, 163), (247, 37, 133), "KUMBIT ORBIT", "금빛 오르빗 · 가장 큰 카메라", 0.2),
    ("ttang_bank", (24, 70, 44), (200, 160, 30), "TTANG BANK", "50년 만기 대출", 1.2),
    ("volt_tiger", (0, 150, 220), (255, 214, 10), "VOLT TIGER", "잠은 약자들의 것", 0.5),
    ("liveon", (12, 12, 16), (160, 20, 60), "LIVE ON", "지금 방송 중 · 라이브온", 1.5),
    ("glow_seoul", (255, 181, 167), (150, 120, 160), "GLOW SEOUL", "필터 같은 피부", 0.4),
    ("haneul_lager", (20, 90, 170), (120, 190, 240), "HANEUL LAGER", "치킨엔 하늘 라거", 0.0),
    ("hantel", (0, 90, 150), (10, 30, 70), "HANTEL 5G", "무제한, 하지만 완전히는 아님", 1.0),
]
LED_ADS_PORTRAIT = [
    ("pado_mobile", (255, 123, 0), (45, 45, 45), "PADO MOBILE", "파도 모바일"),
    ("banana_moo", (255, 228, 94), (255, 190, 60), "BANANA MOO", "바나나 무"),
    ("snail_queen", (74, 78, 105), (201, 173, 167), "SNAIL QUEEN", "달팽이 퀸"),
    ("singsing_room", (255, 0, 110), (131, 56, 236), "SING SING ROOM", "싱싱 노래방"),
    ("levelup_pc", (123, 44, 191), (0, 245, 212), "LEVEL UP PC", "레벨업 PC"),
    ("hwasan_ramyeon", (184, 12, 9), (255, 180, 0), "HWASAN RAMYEON", "화산 라면"),
    ("crispy_ajumma", (214, 40, 40), (255, 209, 102), "CRISPY AJUMMA", "바삭 아줌마"),
    ("nuri_mall", (11, 60, 93), (241, 196, 15), "NURI MALL", "누리 몰"),
]


def _led_card(W, H, ad, bmap, portrait=False):
    bid, c1, c2, big, small = ad[0], ad[1], ad[2], ad[3], ad[4]
    ang = ad[5] if len(ad) > 5 else 1.2
    im = _led_bg(W, H, c1, c2, ang)
    d = ImageDraw.Draw(im, "RGBA")
    rnd = random.Random(hash(bid) & 0xFFFF)
    # абстрактные световые полосы
    for _ in range(9):
        x = rnd.randint(-W // 4, W)
        y = rnd.randint(-H // 4, H)
        r = rnd.randint(min(W, H) // 6, min(W, H) // 2)
        col = (255, 255, 255, rnd.randint(12, 36))
        d.ellipse([x - r, y - r, x + r, y + r], fill=col)
    b = bmap.get(bid)
    fg = (255, 255, 255) if lum(c1) < 0.55 else (20, 20, 24)
    if b:
        logo = render_logo(b["shapes"], int(min(W, H) * (0.34 if not portrait else 0.7)))
        lx = (W - logo.width) // 2 if portrait else int(W * 0.07)
        ly = int(H * 0.12) if portrait else (H - logo.height) // 2 - int(H * 0.02)
        im.paste(logo, (lx, ly), logo)
    if portrait:
        draw_text_fit(d, big, (int(W * 0.06), int(H * 0.55), int(W * 0.94), int(H * 0.7)), fg, max_size=90, stroke=0)
        draw_text_fit(d, small, (int(W * 0.06), int(H * 0.74), int(W * 0.94), int(H * 0.86)), fg, max_size=70, bold=False)
    else:
        draw_text_fit(d, big, (int(W * 0.44), int(H * 0.22), int(W * 0.95), int(H * 0.56)), fg, max_size=150)
        draw_text_fit(d, small, (int(W * 0.44), int(H * 0.62), int(W * 0.95), int(H * 0.80)), fg, bold=False, max_size=80)
    return led_grid(im.convert("RGB"), 6)


def build_signs(out):
    """Возвращает описание атласов: файл, число столбцов/строк, названия ячеек."""
    out.mkdir(parents=True, exist_ok=True)
    rnd = random.Random(7)
    bmap = {b["id"]: b for b in brands()}
    info = {}
    # вертикальные: 8 x 4 ячеек по 128 x 512
    atlas = Image.new("RGB", (1024, 2048))
    labels = []
    for k in range(32):
        cell, label = blade_cell(k, rnd)
        atlas.paste(cell, ((k % 8) * 128, (k // 8) * 512))
        labels.append(label)
    atlas.save(out / "signs_blade.png", optimize=True)
    info["blade"] = dict(file="signs_blade.png", cols=8, rows=4, labels=labels)
    # горизонтальные: 4 x 16 ячеек по 512 x 128
    atlas = Image.new("RGB", (2048, 2048))
    labels = []
    for k in range(64):
        cell, label = horiz_cell(k, rnd)
        atlas.paste(cell, ((k % 4) * 512, (k // 4) * 128))
        labels.append(label)
    atlas.save(out / "signs_horiz.png", optimize=True)
    info["horiz"] = dict(file="signs_horiz.png", cols=4, rows=16, labels=labels)
    # LED: альбомные 2 x 4 по 1024 x 512
    atlas = Image.new("RGB", (2048, 2048))
    labels = []
    for k, ad in enumerate(LED_ADS):
        atlas.paste(_led_card(1024, 512, ad, bmap), ((k % 2) * 1024, (k // 2) * 512))
        labels.append(ad[3])
    atlas.save(out / "led_land.png", optimize=True)
    info["led_land"] = dict(file="led_land.png", cols=2, rows=4, labels=labels)
    # LED: портретные 4 x 2 по 512 x 1024
    atlas = Image.new("RGB", (2048, 2048))
    labels = []
    for k, ad in enumerate(LED_ADS_PORTRAIT):
        atlas.paste(_led_card(512, 1024, ad, bmap, portrait=True), ((k % 4) * 512, (k // 4) * 1024))
        labels.append(ad[3])
    atlas.save(out / "led_port.png", optimize=True)
    info["led_port"] = dict(file="led_port.png", cols=4, rows=2, labels=labels)
    return info


if __name__ == "__main__":
    import sys
    outdir = Path(sys.argv[1])
    build_textures(outdir / "textures")
    print(build_signs(outdir / "textures")["blade"]["labels"][:5])
