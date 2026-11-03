"""Корейские уличные детали: решётки у деревьев, болларды, наружные блоки кондиционеров, баки на крышах,
бетонные столбы с проводами, тактильная плитка, люки, палатки-포장마차."""
import math

import numpy as np

from trees import tube


def tree_grate(M, x, y, z, s=1.5):
    """Чугунная решётка приствольной лунки (почти везде в Сеуле)."""
    h = s / 2
    M["grate"].quad((x - h, y + 0.012, z - h), (x - h, y + 0.012, z + h), (x + h, y + 0.012, z + h), (x + h, y + 0.012, z - h),
                    uv=[(0, 0), (0, 1), (1, 1), (1, 0)], normal=np.array([0, 1, 0]))


def bollard(M, x, y, z, rnd):
    """Гранитный столбик-боллард с отражающей полосой (у переходов)."""
    if rnd.random() < 0.6:
        M["concrete"].cylinder(x, y, z, 0.17, 0.8, 10, color=(0.92, 0.9, 0.86, 1), r_top=0.15)
        M["paint_yellow"].cylinder(x, y + 0.6, z, 0.152, 0.07, 10, cap=False)
    else:
        M["metal_light"].cylinder(x, y, z, 0.08, 0.9, 8, color=(0.85, 0.85, 0.85, 1))
        M["paint_yellow"].cylinder(x, y + 0.72, z, 0.082, 0.08, 8, cap=False)


def ac_unit(M, x, y, z, nx, nz, rnd):
    """Наружный блок кондиционера на стене (решётка вентилятора наружу) + кронштейн и трубка."""
    yaw = math.atan2(nz, nx)
    w, hh, d = 0.82, 0.62, 0.3
    cx, cz = x + nx * (d / 2 + 0.06), z + nz * (d / 2 + 0.06)
    M["ac"].box(cx, y, cz, d, hh, w, yaw=yaw, uv_scale=1.0)
    M["metal_dark"].box(x + nx * 0.2, y - 0.05, z + nz * 0.2, 0.42, 0.04, w + 0.1, yaw=yaw, uv_scale=1.0)
    if rnd.random() < 0.6:  # трубка вниз по стене
        tx, tz = x + nx * 0.03 - nz * (w / 2 + 0.05), z + nz * 0.03 + nx * (w / 2 + 0.05)
        tube(M["pipe"], (tx, y + 0.2, tz), (tx, max(0.3, y - rnd.uniform(1.5, 4.0)), tz), 0.025, 0.025, 5)


def ac_rooftop(M, x, y, z, rnd):
    for i in range(rnd.randint(2, 5)):
        M["ac"].box(x + i * 1.0, y, z, 0.85, 0.7, 0.35, uv_scale=1.0)


def water_tank(M, x, y, z, rnd):
    """Цилиндрический бак (FRP) на крыше — синий, зелёный или белый, на стальной раме."""
    r = rnd.uniform(0.9, 1.5)
    h = rnd.uniform(1.6, 2.4)
    col = rnd.choice([(0.25, 0.45, 0.7, 1), (0.35, 0.55, 0.42, 1), (0.85, 0.85, 0.82, 1), (0.3, 0.5, 0.62, 1)])
    M["metal_dark"].box(x, y, z, r * 2.1, 0.5, r * 2.1, uv_scale=1.0)
    M["tank"].cylinder(x, y + 0.5, z, r, h, 14, color=col, r_top=r * 0.97)
    M["tank"].cylinder(x, y + 0.5 + h, z, r * 0.97, 0.3, 14, color=col, r_top=r * 0.35)


def utility_pole(M, x, y, z, rnd, yaw=0.0):
    """Бетонный столб 11 м с траверсой и трансформатором. Возвращает точки крепления проводов."""
    H = 11.0
    M["concrete"].cylinder(x, y, z, 0.2, H, 10, color=(0.8, 0.8, 0.78, 1), r_top=0.13)
    dx, dz = math.cos(yaw), math.sin(yaw)
    M["metal_dark"].box(x, y + H - 1.0, z, 2.6, 0.12, 0.12, yaw=yaw, uv_scale=1.0)
    M["metal_dark"].box(x, y + H - 2.2, z, 1.8, 0.1, 0.1, yaw=yaw, uv_scale=1.0)
    if rnd.random() < 0.45:
        tx, tz = x - dz * 0.45, z + dx * 0.45
        M["metal_light"].cylinder(tx, y + H - 4.2, tz, 0.3, 1.1, 10, color=(0.62, 0.64, 0.62, 1))
    # телеком-боксы и спирали кабеля
    M["metal_light"].box(x - dz * 0.24, y + 3.2, z + dx * 0.24, 0.3, 0.5, 0.2, yaw=yaw, color=(0.5, 0.52, 0.5, 1), uv_scale=1.0)
    pts = []
    for k, (hy, off) in enumerate([(H - 1.0, -1.2), (H - 1.0, 1.2), (H - 1.0, 0.0), (H - 2.2, -0.8), (H - 2.2, 0.8), (H - 3.0, 0.15), (H - 3.3, -0.1)]):
        pts.append((x + dx * off, y + hy, z + dz * off))
    return pts


def cable(M, a, b, sag=0.6, r=0.012, n=10):
    a = np.asarray(a, float); b = np.asarray(b, float)
    prev = a
    for i in range(1, n + 1):
        t = i / n
        p = a + (b - a) * t
        p[1] -= sag * 4 * t * (1 - t)
        tube(M["cable"], prev, p, r, r, 3)
        prev = p


def tactile_strip(M, x0, z0, x1, z1, y):
    """Жёлтая тактильная плитка (점자블록) — полоса по тротуару."""
    M["tactile"].flat(min(x0, x1), min(z0, z1), max(x0, x1), max(z0, z1), y + 0.006, tile=0.6)


def manhole(M, x, y, z, rnd, r=0.45):
    a = rnd.uniform(0, 2 * math.pi)
    c, s = math.cos(a) * r, math.sin(a) * r
    M["manhole"].quad((x - c + s, y, z - s - c), (x - c - s, y, z - s + c), (x + c - s, y, z + s + c), (x + c + s, y, z + s - c),
                      uv=[(0, 0), (0, 1), (1, 1), (1, 0)], normal=np.array([0, 1, 0]))


def pojangmacha(M, x, y, z, yaw, LIGHT_GLOWS):
    """Палатка уличной закусочной: оранжевый тент на каркасе, тёплый свет изнутри."""
    w, d, h = 4.0, 2.8, 2.3
    M["metal_dark"].box(x, y, z, w, 0.05, d, yaw=yaw, uv_scale=1.0)
    M["tent"].box(x, y + 0.9, z, w, h - 0.9, d, yaw=yaw, uv_scale=0.5)
    M["tent"].box(x, y + h, z, w + 0.2, 0.25, d + 0.2, yaw=yaw, uv_scale=0.5)
    LIGHT_GLOWS.append((x, z, 4.5, -1, "tent"))
