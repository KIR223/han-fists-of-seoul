"""Сборка района «Кандам» (вымышленный аналог делового района-небоскрёбов Сеула): раскладка, меши, GLB.
Запуск: python3 -I build.py <выходная папка>"""
import json
import math
import os
import random
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).parent))
import assets
from common import GLB, Mesh

OUT = Path(sys.argv[1])
TEX = OUT / "textures"
rnd = random.Random(20261103)

# ------------------------------------------------------------------ цвета (приглушённые, как в GTA V / Watch Dogs)
FACADE_TINTS = [(1.0, 0.97, 0.9), (0.93, 0.92, 0.9), (0.95, 0.88, 0.78), (0.86, 0.9, 0.92), (0.98, 0.92, 0.84), (0.82, 0.84, 0.8), (0.9, 0.82, 0.74), (0.88, 0.92, 0.86)]
GLASS_TINTS = [(0.85, 1.0, 1.05), (0.9, 1.0, 0.95), (1.0, 1.0, 1.0), (1.05, 0.98, 0.9), (0.8, 0.9, 1.05), (0.95, 0.95, 1.02)]
BRICK_TINTS = [(1.0, 1.0, 1.0), (0.9, 0.85, 0.8), (1.1, 0.95, 0.85), (0.8, 0.8, 0.85), (1.05, 1.0, 0.9)]
CAR_COLORS = [(0.78, 0.78, 0.78)] * 9 + [(0.06, 0.06, 0.07)] * 7 + [(0.45, 0.46, 0.48)] * 5 + [(0.1, 0.14, 0.24)] * 2 + [(0.4, 0.07, 0.07)] * 2 + [(0.9, 0.55, 0.1)] * 2

# ------------------------------------------------------------------ GLB и материалы
glb = GLB()
meshes = {}


def img(name, fname):
    rgba = fname.endswith("soft.png") or fname in ("leaves.png", "manhole.png", "tree_grate.png")
    arr = np.asarray(Image.open(TEX / fname).convert("RGBA" if rgba else "RGB"))
    return glb.add_image(name, arr, fmt="png" if (rgba or fname.startswith("signs") or fname.startswith("led") or fname.startswith("metro")) else "jpeg")


def mat_pbr(name, prefix, emissive=False, **kw):
    base = img(prefix + "_BC", prefix + "_BC.png")
    nrm = img(prefix + "_N", prefix + "_N.png")
    orm = img(prefix + "_ORM", prefix + "_ORM.png")
    em = img(prefix + "_E", prefix + "_E.png") if emissive else None
    idx = glb.add_material(name, base=base, normal=nrm, orm=orm, emissive=em, metallic=1.0, roughness=1.0,
                           extras=dict(kind="facade" if emissive else "surface", **kw.pop("extras", {})), **kw)
    meshes[name] = Mesh(name)
    return idx


def mat_flat(name, color, rough=0.7, metal=0.0, double=False, emissive=None, estrength=1.0, extras=None):
    idx = glb.add_material(name, base_factor=(*color, 1.0), metallic=metal, roughness=rough, double_sided=double,
                           emissive_factor=emissive or (0, 0, 0), emissive_strength=estrength, extras=extras or {})
    meshes[name] = Mesh(name)
    return idx


def mat_sign(name, fname, kind, estrength=1.0, double=True):
    t = img(name, fname)
    idx = glb.add_material(name, base=t, emissive=t, base_factor=(0.8, 0.8, 0.8, 1), metallic=0.0, roughness=0.45, double_sided=double,
                           emissive_factor=(1, 1, 1), emissive_strength=estrength, extras=dict(kind=kind))
    meshes[name] = Mesh(name)
    return idx


MI = {}
import os
import shutil
TEX.mkdir(parents=True, exist_ok=True)
WORK = Path(os.environ.get("KANDAM_WORK", Path(__file__).resolve().parents[4] / "assets_bin" / "kandam"))
T2 = WORK / "t2"
for f in T2.glob("*.png"):
    shutil.copy(f, TEX / f.name)
T3 = WORK / "t3"   # CC0-сканы ambientCG (scans.py) — перекрывают процедурные
for f in T3.glob("*.png"):
    shutil.copy(f, TEX / f.name)
import trees
import korea_props as KP
SIGNS = assets.build_signs(TEX)
# мини-вывеска станции метро (вымышленное название района)
im = Image.new("RGB", (1024, 256), (14, 90, 60))
d = ImageDraw.Draw(im)
d.rounded_rectangle([20, 20, 1004, 236], radius=30, outline=(255, 255, 255), width=8)
d.ellipse([50, 48, 210, 208], fill=(255, 255, 255))
d.text((130, 128), "2", font=assets.font(110), fill=(14, 90, 60), anchor="mm")
d.text((620, 100), "칸담", font=assets.font(110), fill=(255, 255, 255), anchor="mm")
d.text((620, 195), "KANDAM STATION", font=assets.font(52, False), fill=(255, 255, 255), anchor="mm")
im.save(TEX / "metro_sign.png")

MI["asphalt"] = mat_pbr("asphalt", "asphalt", extras=dict(surface="Asphalt"))
MI["sidewalk"] = mat_pbr("sidewalk", "sidewalk", extras=dict(surface="Tile"))
MI["plaza"] = mat_pbr("plaza", "plaza", extras=dict(surface="Tile"))
MI["concrete"] = mat_pbr("concrete", "concrete", extras=dict(surface="Concrete"))
MI["roof"] = mat_pbr("roof", "roof", extras=dict(surface="Concrete"))
MI["glass"] = mat_pbr("facade_glass", "facade_glass", emissive=True, extras=dict(surface="Glass"))
MI["ribbon"] = mat_pbr("facade_ribbon", "facade_ribbon", emissive=True, extras=dict(surface="Concrete"))
MI["punched"] = mat_pbr("facade_punched", "facade_punched", emissive=True, extras=dict(surface="Tile"))
MI["brick"] = mat_pbr("facade_brick", "facade_brick", emissive=True, extras=dict(surface="Concrete"))
MI["shop"] = mat_pbr("facade_shop", "facade_shop", emissive=True, extras=dict(surface="Glass"))
MI["metal_dark"] = mat_pbr("metal_dark", "metal_dark", extras=dict(surface="Metal"))
MI["metal_light"] = mat_pbr("metal_light", "metal_light", extras=dict(surface="Metal"))
MI["car_glass"] = mat_pbr("car_glass", "car_glass", extras=dict(surface="Glass"))
MI["blade"] = mat_sign("signs_blade", "signs_blade.png", "sign", 1.0)
MI["horiz"] = mat_sign("signs_horiz", "signs_horiz.png", "sign", 1.0)
MI["led_land"] = mat_sign("led_land", "led_land.png", "led", 2.2, double=False)
MI["led_port"] = mat_sign("led_port", "led_port.png", "led", 2.2, double=False)
MI["metro"] = mat_sign("metro_sign", "metro_sign.png", "sign", 1.2)
MI["paint_white"] = mat_flat("paint_white", (0.78, 0.78, 0.74), 0.55, extras=dict(surface="Asphalt"))
MI["paint_yellow"] = mat_flat("paint_yellow", (0.75, 0.56, 0.08), 0.55, extras=dict(surface="Asphalt"))
MI["paint_blue"] = mat_flat("paint_blue", (0.09, 0.19, 0.32), 0.65, extras=dict(surface="Asphalt"))
MI["foliage"] = mat_flat("foliage", (1, 1, 1), 0.85, double=True)
MI["trunk"] = mat_pbr("bark", "bark", extras=dict(surface="Wood"))
_lf = img("leaves", "leaves.png")
MI["leaf"] = glb.add_material("leaf_cards", base=_lf, base_factor=(1, 1, 1, 1), metallic=0.0, roughness=0.8, double_sided=True, alpha_mode="MASK", extras=dict(kind="foliage"))
meshes["leaf_cards"] = Mesh("leaf_cards")


def mat_cutout(name, fname, rough=0.6, metal=0.0):
    t = img(name, fname)
    idx = glb.add_material(name, base=t, base_factor=(1, 1, 1, 1), metallic=metal, roughness=rough, double_sided=True, alpha_mode="MASK", extras=dict(kind="foliage", cutout=True))
    meshes[name] = Mesh(name)
    return idx


MI["tactile"] = mat_pbr("tactile", "tactile", extras=dict(surface="Tile"))
# объёмные фасады: чистые стены из сканов + стекло с интерьерами + рамы
import facade3d as F3
MI["wall_brick"] = mat_pbr("wall_brick", "wall_brick", extras=dict(surface="Concrete"))
MI["wall_tile"] = mat_pbr("wall_tile", "wall_tile", extras=dict(surface="Tile"))
MI["wall_plaster"] = mat_pbr("wall_plaster", "wall_plaster", extras=dict(surface="Concrete"))
MI["slab"] = mat_pbr("slab", "concrete", extras=dict(surface="Concrete"))
_wb = img("win_interior_BC", "win_interior_BC.png"); _we = img("win_interior_E", "win_interior_E.png")
MI["win"] = glb.add_material("window_glass", base=_wb, emissive=_we, emissive_factor=(1, 1, 1), emissive_strength=2.2, metallic=0.0, roughness=0.06,
                             extras=dict(kind="facade", surface="Glass"))
meshes["window_glass"] = Mesh("window_glass")
MI["frame"] = mat_flat("window_frame", (1, 1, 1), 0.35, 0.6, extras=dict(surface="Metal"))
MI["bars"] = mat_flat("window_bars", (0.2, 0.2, 0.21), 0.45, 0.7, extras=dict(surface="Metal"))
# ---- готовые помещения: отдельный GLB (стримится при входе), витрина — в основном
import interiors as INT
MI["glass_clear"] = glb.add_material("storefront_glass", base_factor=(0.78, 0.86, 0.86, 0.22), metallic=0.0, roughness=0.04, double_sided=True,
                                     alpha_mode="BLEND", extras=dict(kind="glass", surface="Glass"))
meshes["storefront_glass"] = Mesh("storefront_glass")
G2 = GLB(); MI2 = {}; M2 = {}


def img2(name, fname, rgba=False):
    arr = np.asarray(Image.open(TEX / fname).convert("RGBA" if rgba else "RGB"))
    return G2.add_image(name, arr, fmt="png" if rgba else "jpeg")


def imat(key, color=(1, 1, 1), tex=None, nrm=None, orm=None, emis=None, rough=0.6, metal=0.0, estr=1.0, efac=None, alpha="OPAQUE", rgba=False):
    base = img2(key + "_b", tex, rgba) if tex else None
    nn = img2(key + "_n", nrm) if nrm else None
    oo = img2(key + "_o", orm) if orm else None
    ee = img2(key + "_e", emis) if emis else None
    ef = efac if efac is not None else ((1, 1, 1) if emis else (0, 0, 0))
    MI2[key] = G2.add_material(key, base=base, normal=nn, orm=oo, emissive=ee, base_factor=(*color, 1.0 if alpha != "BLEND" else 0.25),
                               metallic=metal, roughness=rough, emissive_factor=ef, emissive_strength=estr, double_sided=True,
                               alpha_mode=alpha, extras=dict(kind="glass" if alpha == "BLEND" else "interior"))
    M2[key] = Mesh(key)


def imat_pbr(key, prefix, rough=1.0, metal=1.0):
    imat(key, tex=prefix + "_BC.png", nrm=prefix + "_N.png", orm=prefix + "_ORM.png", rough=rough, metal=metal)


INT.build_textures(str(TEX))
imat("i_woodfloor", tex="int_woodfloor.png", rough=0.35)
imat_pbr("i_plaster", "int_plaster"); imat_pbr("i_tile_floor", "int_tile_floor"); imat_pbr("i_tile_white", "int_tile_white")
imat_pbr("i_terrazzo", "int_terrazzo"); imat_pbr("i_wood", "int_wood"); imat_pbr("i_terr_top", "int_terrazzo")
imat("i_ceiling", tex="int_ceiling.png", emis="int_ceiling_E.png", estr=1.1, rough=0.9)
imat("i_light", color=(1, 1, 1), efac=(1.0, 0.97, 0.9), estr=2.2, rough=0.5)
imat("i_metal", color=(0.8, 0.8, 0.8), rough=0.28, metal=1.0)
imat("i_paint", rough=0.45); imat("i_cloth", rough=0.95); imat("i_label", color=(0.95, 0.85, 0.2), efac=(0.4, 0.35, 0.05), rough=0.5)
imat("i_stool", color=(0.75, 0.08, 0.06), rough=0.35)
imat("i_food_red", color=(0.75, 0.12, 0.05), rough=0.25, efac=(0.08, 0.01, 0.0))
imat("i_food_broth", color=(0.55, 0.4, 0.2), rough=0.15); imat("i_food_fish", color=(0.85, 0.65, 0.4), rough=0.5)
imat("i_food_kimbap", color=(0.08, 0.1, 0.06), rough=0.5); imat("i_food_cake", color=(0.95, 0.85, 0.75), rough=0.5)
imat("i_menus", tex="int_menus.png", emis="int_menus.png", estr=0.8, rough=0.4)
imat("i_drinks", tex="int_drinks.png", emis="int_drinks_E.png", estr=0.9, rough=0.2)
imat("i_products", tex="int_products.png", rough=0.4)
imat("i_mirror", color=(0.55, 0.58, 0.62), rough=0.08, metal=1.0)
imat("i_leaf", tex="leaves.png", rgba=True, rough=0.7, alpha="MASK")
imat("i_glass", color=(0.8, 0.88, 0.9), rough=0.03, alpha="BLEND")
INTERIOR_PLAN = ["clothes", "bunsik", "convenience", "cafe", "cosmetics", "clothes", "cafe", "bunsik", "convenience"]
INTERIOR_RECORDS = []
INT_LIGHTS = []


def shop_wall(a, b, n, side, shop_h, ro, color=(1, 1, 1, 1)):
    """Нижний этаж: либо текстура витрин, либо (на проспекте) готовое помещение с прозрачной витриной."""
    L = math.hypot(b[0] - a[0], b[1] - a[1])
    on_blvd = side == "N" and abs(a[1] - 32.4) < 0.6 and -330 < min(a[0], b[0]) and max(a[0], b[0]) < 300
    if INTERIOR_PLAN and on_blvd and L > 16 and rnd.random() < 0.85:
        W = 10.0 if L > 22 else 8.5
        t0 = round(L / 2 - W / 2, 2)
        dx, dz = (b[0] - a[0]) / L, (b[1] - a[1]) / L
        pa = (a[0] + dx * t0, a[1] + dz * t0); pb = (a[0] + dx * (t0 + W), a[1] + dz * (t0 + W))
        M["shop"].wall(a, pa, 0, shop_h, 12.0, 5.0, uoff=ro, color=color)
        M["shop"].wall(pb, b, 0, shop_h, 12.0, 5.0, uoff=ro + (t0 + W) / 12.0, color=color)
        Mi = dict(M2); Mi["frame"] = M["frame"]; Mi["glass"] = M["glass_clear"]; Mi["slab"] = M["slab"]
        kind = INTERIOR_PLAN.pop(0)
        INT.build(Mi, a, b, n, t0, W, 8.0, SW + 0.02, kind, shop_h, rnd, INTERIOR_RECORDS, INT_LIGHTS)
        # тёплый свет из витрины на тротуар
        mx, mz = (pa[0] + pb[0]) / 2 + n[0] * 3, (pa[1] + pb[1]) / 2 + n[1] * 3
        LIGHT_GLOWS.append((mx, mz, 6.0, -1, "shopglow"))
        return
    M["shop"].wall(a, b, 0, shop_h, 12.0, 5.0, uoff=ro, color=color)


WALL_TILE = {"wall_brick": (2.0, 2.0), "wall_tile": (1.5, 1.5), "wall_plaster": (2.0, 2.0)}
MI["manhole"] = mat_cutout("manhole", "manhole.png", 0.45, 0.6)
MI["grate"] = mat_cutout("tree_grate", "tree_grate.png", 0.5, 0.7)
MI["ac"] = mat_flat("ac_unit", (0.82, 0.82, 0.8), 0.5, 0.2, extras=dict(surface="Metal"))
MI["tank"] = mat_flat("water_tank", (1, 1, 1), 0.45, extras=dict(surface="Plastic"))
MI["pipe"] = mat_flat("pipe", (0.85, 0.85, 0.82), 0.5)
MI["cable"] = mat_flat("cable", (0.05, 0.05, 0.05), 0.6)
MI["tent"] = mat_flat("tent_tarp", (0.85, 0.35, 0.12), 0.7, emissive=(1.0, 0.42, 0.12), estrength=1.6, double=True, extras=dict(kind="sign"))
_fb = img("far_facade_BC", "far_facade_BC.png"); _fe = img("far_facade_E", "far_facade_E.png")
MI["farf"] = glb.add_material("far_facade", base=_fb, emissive=_fe, emissive_factor=(1, 1, 1), metallic=0.0, roughness=0.8, extras=dict(kind="facade", surface="Concrete"))
meshes["far_facade"] = Mesh("far_facade")
MI["paint"] = mat_flat("car_paint", (1, 1, 1), 0.32, 0.35)
MI["lamp"] = mat_flat("lamp_head", (0.9, 0.85, 0.75), 0.4, emissive=(1.0, 0.82, 0.55), estrength=6.0, extras=dict(kind="lamp"))
MI["tail"] = mat_flat("car_taillight", (0.4, 0.02, 0.02), 0.4, emissive=(1.0, 0.04, 0.03), estrength=5.0, extras=dict(kind="lamp"))
MI["head"] = mat_flat("car_headlight", (0.9, 0.9, 0.85), 0.3, emissive=(1.0, 0.95, 0.85), estrength=6.0, extras=dict(kind="lamp"))
MI["tl_red"] = mat_flat("signal_red", (0.3, 0.02, 0.02), 0.4, emissive=(1.0, 0.05, 0.03), estrength=8.0, extras=dict(kind="lamp"))
MI["tl_green"] = mat_flat("signal_green", (0.02, 0.2, 0.08), 0.4, emissive=(0.1, 1.0, 0.45), estrength=8.0, extras=dict(kind="lamp"))
MI["warn"] = mat_flat("aviation_light", (0.3, 0.02, 0.02), 0.4, emissive=(1.0, 0.05, 0.03), estrength=10.0, extras=dict(kind="lamp"))
MI["far"] = mat_flat("distant", (1, 1, 1), 1.0, extras=dict(kind="distant"))
gl = img("glow", "glow_soft.png")
MI["glow"] = glb.add_material("glow_add", base=gl, base_factor=(1, 1, 1, 1), double_sided=True, alpha_mode="BLEND", roughness=1.0, extras=dict(kind="glow", blend="add"))
meshes["glow_add"] = Mesh("glow_add")
# ключи mesh-ов по материалам: имя материала -> Mesh (используем MI-ключи)
M = {}
for k in MI:
    nm = glb.materials[MI[k]]["name"]
    M[k] = meshes[nm]


def cell_uv(kind, idx):
    info = SIGNS[kind]
    c, r = idx % info["cols"], idx // info["cols"]
    u0, u1 = c / info["cols"], (c + 1) / info["cols"]
    v0, v1 = r / info["rows"], (r + 1) / info["rows"]
    pad_u, pad_v = 0.002, 0.002
    return u0 + pad_u, v0 + pad_v, u1 - pad_u, v1 - pad_v


def quad_uv(u0, v0, u1, v1):
    return [(u0, v1), (u1, v1), (u1, v0), (u0, v0)]


# ------------------------------------------------------------------ раскладка
X0, X1, Z0, Z1 = -360, 360, -300, 300
BLVD_HALF = 24
CROSS_X = [-270, -80, 100, 280]
CROSS_HALF = 11
SEC_Z = [-200, 200]
SEC_HALF = 10

# ровная земля под всем + широкая внешняя
M["concrete"].flat(-1100, -1100, 1100, 1100, -0.02, tile=8.0, color=(0.55, 0.55, 0.55, 1))

# дороги
M["asphalt"].flat(X0, -BLVD_HALF, X1, BLVD_HALF, 0.02, tile=4.0)
for cx in CROSS_X:
    M["asphalt"].flat(cx - CROSS_HALF, Z0, cx + CROSS_HALF, Z1, 0.021, tile=4.0, uoff=0.3, voff=0.2)
for sz in SEC_Z:
    M["asphalt"].flat(X0, sz - SEC_HALF, X1, sz + SEC_HALF, 0.022, tile=4.0, uoff=0.6, voff=0.1)

# блоки: (xa, xb) × (za, zb), ближняя к проспекту/улице сторона
x_edges = [X0] + [v for cx in CROSS_X for v in (cx - 17, cx + 17)] + [X1]
xr = [(x_edges[i], x_edges[i + 1]) for i in range(0, len(x_edges), 2)]
# строки блоков по z: (z_near, z_far, sign)
zr = [(-32, -185), (-215, -300), (32, 185), (215, 300)]
SW = 0.15  # высота тротуара


def sidewalk_rect(xa, xb, za, zb):
    """Тротуар на весь блок вместе с полосой до кромки дороги."""
    # расширяем до кромки дорог
    ex0 = xa - (6 if xa != X0 else 0)
    ex1 = xb + (6 if xb != X1 else 0)
    zlo, zhi = min(za, zb), max(za, zb)
    ez0 = zlo - (8 if zlo > -300 + 1 else 0)
    ez1 = zhi + (8 if zhi < 300 - 1 else 0)
    return ex0, ez0, ex1, ez1


plaza_rects = []
for (xa, xb) in xr:
    for (zn, zf) in zr:
        zlo, zhi = min(zn, zf), max(zn, zf)
        ex0, ez0, ex1, ez1 = sidewalk_rect(xa, xb, zn, zf)
        # у блоков рядом с проспектом полоса до проспекта 8 м; рядом с вторичной улицей — 5 м
        if abs(zn) == 32:
            ez0, ez1 = (zlo, ez1) if zn > 0 else (ez0, zhi)
            if zn > 0:
                ez0 = BLVD_HALF
                ez1 = zhi + 5
            else:
                ez1 = -BLVD_HALF
                ez0 = zlo - 5
        else:
            if zn > 0:
                ez0, ez1 = zlo - 5, zhi
            else:
                ez0, ez1 = zlo, zhi + 5
        M["sidewalk"].flat(ex0, ez0, ex1, ez1, SW, tile=2.4)
        # бордюр
        for (a, b, nrm_flip) in [((ex0, ez0), (ex0, ez1), 0), ((ex0, ez1), (ex1, ez1), 0), ((ex1, ez1), (ex1, ez0), 0), ((ex1, ez0), (ex0, ez0), 0)]:
            M["concrete"].wall(a, b, 0.0, SW, 4.0, 4.0)


# ------------------------------------------------------------------ здания
building_records = []
LIGHT_GLOWS = []  # (x, z, r, (r,g,b), a)


def edges(x0, z0, x1, z1):
    return [("W", (x0, z0), (x0, z1), (-1, 0)), ("S", (x0, z1), (x1, z1), (0, 1)), ("E", (x1, z1), (x1, z0), (1, 0)), ("N", (x1, z0), (x0, z0), (0, -1))]


def tint_color(t, jitter=0.04):
    return (t[0] * (1 + rnd.uniform(-jitter, jitter)), t[1] * (1 + rnd.uniform(-jitter, jitter)), t[2] * (1 + rnd.uniform(-jitter, jitter)), 1.0)


def ac_on_wall(a, b, n, y0, y1, floor_h, prob):
    """Наружные блоки кондиционеров под окнами (характерно для корейских малоэтажек и офистелей)."""
    length = math.hypot(b[0] - a[0], b[1] - a[1])
    if length < 4:
        return
    dx, dz = (b[0] - a[0]) / length, (b[1] - a[1]) / length
    y = y0 + 0.4
    while y < y1 - 1.0:
        t = 1.5
        while t < length - 1.5:
            if rnd.random() < prob:
                KP.ac_unit(M, a[0] + dx * t, y, a[1] + dz * t, n[0], n[1], rnd)
            t += rnd.uniform(2.5, 6.0)
        y += floor_h


def rooftop(x0, z0, x1, z1, y, kind):
    rc = rnd.choice([(0.95, 0.95, 0.95, 1), (1.2, 1.2, 1.15, 1), (0.8, 0.9, 1.1, 1), (1.1, 0.95, 0.85, 1)])
    if kind in ("low", "mid") and rnd.random() < 0.6:
        # зелёное полиуретановое покрытие крыш — визитная карточка Сеула сверху
        rc = rnd.choice([(0.55, 0.85, 0.62, 1), (0.5, 0.78, 0.6, 1), (0.6, 0.88, 0.7, 1)])
    M["roof"].flat(x0, z0, x1, z1, y, tile=6.0, color=rc)
    # парапет
    for (a, b) in [((x0, z0), (x0, z1)), ((x0, z1), (x1, z1)), ((x1, z1), (x1, z0)), ((x1, z0), (x0, z0))]:
        M["concrete"].wall(a, b, y, y + 0.9, 4.0, 4.0, color=(0.8, 0.8, 0.8, 1))
    w, d = x1 - x0, z1 - z0
    if w < 8 or d < 8:
        return
    n = rnd.randint(1, 3) if kind != "tower" else rnd.randint(3, 6)
    for _ in range(n):
        bw, bd, bh = rnd.uniform(2, min(8, w * 0.4)), rnd.uniform(2, min(8, d * 0.4)), rnd.uniform(1.5, 3.5 if kind != "tower" else 6)
        bx, bz = rnd.uniform(x0 + bw, x1 - bw), rnd.uniform(z0 + bd, z1 - bd)
        M["metal_light"].box(bx, y, bz, bw, bh, bd, uv_scale=0.25)
    if kind in ("low", "mid") and w > 8 and d > 8:
        for _ in range(rnd.randint(1, 2) if rnd.random() < (0.75 if kind == "low" else 0.4) else 0):
            KP.water_tank(M, rnd.uniform(x0 + 2, x1 - 2), y, rnd.uniform(z0 + 2, z1 - 2), rnd)
        if rnd.random() < 0.7:
            KP.ac_rooftop(M, rnd.uniform(x0 + 1, x1 - 6), y, rnd.uniform(z0 + 1, z1 - 1), rnd)
    if kind == "low":
        #옥탑방: домик на крыше с цветной крышей
        bw, bd = min(5, w * 0.4), min(4, d * 0.4)
        bx, bz = rnd.uniform(x0 + bw, x1 - bw), rnd.uniform(z0 + bd, z1 - bd)
        M["concrete"].box(bx, y, bz, bw, 2.6, bd, color=(0.85, 0.85, 0.82, 1), uv_scale=0.25)
        c = rnd.choice([(0.1, 0.4, 0.55), (0.12, 0.45, 0.3), (0.55, 0.15, 0.12)])
        M["foliage"].box(bx, y + 2.6, bz, bw + 0.4, 0.15, bd + 0.4, color=(*c, 1))
    if kind == "tower":
        M["metal_dark"].cylinder((x0 + x1) / 2, y, (z0 + z1) / 2, 0.25, rnd.uniform(14, 26), 6)
        M["warn"].box((x0 + x1) / 2, y + 20, (z0 + z1) / 2, 0.6, 0.6, 0.6)
        for (cx, cz) in [(x0 + 1, z0 + 1), (x1 - 1, z1 - 1)]:
            M["warn"].box(cx, y + 0.9, cz, 0.5, 0.5, 0.5)


def add_blade_signs(side, a, b, n, y_lo, y_hi, density):
    length = math.hypot(b[0] - a[0], b[1] - a[1])
    if length < 8:
        return
    dx, dz = (b[0] - a[0]) / length, (b[1] - a[1]) / length
    t = rnd.uniform(2.0, 4.5)
    while t < length - 2.0:
        if rnd.random() < density:
            h = rnd.uniform(4.5, 11.5)
            h = min(h, y_hi - y_lo - 0.5)
            if h > 3:
                w = h / 4.0
                ytop = rnd.uniform(y_lo + h + 0.2, y_hi - 0.3) if y_hi - 0.3 > y_lo + h + 0.2 else y_lo + h
                idx = rnd.randrange(32)
                px, pz = a[0] + dx * t, a[1] + dz * t
                p0 = (px + n[0] * 0.08, ytop - h, pz + n[1] * 0.08)
                p1 = (px + n[0] * (0.08 + w), ytop - h, pz + n[1] * (0.08 + w))
                p2 = (p1[0], ytop, p1[2])
                p3 = (p0[0], ytop, p0[2])
                M["blade"].quad(p0, p1, p2, p3, uv=quad_uv(*cell_uv("blade", idx)))
                M["metal_dark"].box(px + n[0] * 0.04, ytop - h - 0.02, pz + n[1] * 0.04, 0.06, h + 0.04, 0.06, uv_scale=1.0, top=False)
        t += rnd.uniform(5.5, 9.0)


def add_horiz_signs(a, b, n, y0, y1_height=1.2, density=0.7):
    length = math.hypot(b[0] - a[0], b[1] - a[1])
    if length < 6:
        return
    dx, dz = (b[0] - a[0]) / length, (b[1] - a[1]) / length
    t = rnd.uniform(0.5, 2.0)
    while t < length - 4.5:
        if rnd.random() < density:
            wd = y1_height * 4.0
            if t + wd > length - 0.3:
                break
            idx = rnd.randrange(64)
            p0 = (a[0] + dx * t + n[0] * 0.14, y0, a[1] + dz * t + n[1] * 0.14)
            p1 = (a[0] + dx * (t + wd) + n[0] * 0.14, y0, a[1] + dz * (t + wd) + n[1] * 0.14)
            p2 = (p1[0], y0 + y1_height, p1[2])
            p3 = (p0[0], y0 + y1_height, p0[2])
            M["horiz"].quad(p0, p1, p2, p3, uv=quad_uv(*cell_uv("horiz", idx)))
            t += wd + rnd.uniform(0.3, 3.0)
        else:
            t += rnd.uniform(3.0, 6.0)


def add_led(a, b, n, t_center, y_bottom, w, h, portrait, idx=None, frame=True):
    length = math.hypot(b[0] - a[0], b[1] - a[1])
    dx, dz = (b[0] - a[0]) / length, (b[1] - a[1]) / length
    t0 = t_center - w / 2
    kind = "led_port" if portrait else "led_land"
    idx = rnd.randrange(8) if idx is None else idx
    off = 0.35
    p0 = (a[0] + dx * t0 + n[0] * off, y_bottom, a[1] + dz * t0 + n[1] * off)
    p1 = (a[0] + dx * (t0 + w) + n[0] * off, y_bottom, a[1] + dz * (t0 + w) + n[1] * off)
    p2 = (p1[0], y_bottom + h, p1[2])
    p3 = (p0[0], y_bottom + h, p0[2])
    M[kind].quad(p0, p1, p2, p3, uv=quad_uv(*cell_uv(kind, idx)))
    if frame:
        # тёмная рама-короб за экраном
        cx = a[0] + dx * t_center + n[0] * 0.17
        cz = a[1] + dz * t_center + n[1] * 0.17
        yaw = math.atan2(dz, dx)
        M["metal_dark"].box(cx, y_bottom - 0.25, cz, w + 0.5, h + 0.5, 0.34, yaw=-yaw if False else math.atan2(dz, dx) * 1.0, uv_scale=0.25, top=True, sides=True)
    # свет экрана на тротуар
    gx = a[0] + dx * t_center + n[0] * (min(w, h) * 0.6 + 3)
    gz = a[1] + dz * t_center + n[1] * (min(w, h) * 0.6 + 3)
    LIGHT_GLOWS.append((gx, gz, min(30, 6 + w * 0.5), idx, kind))


def building(lot, kind, front_side, pub):
    """lot=(x0,z0,x1,z1). kind: tower|mid|low. pub: dict сторона -> общедоступна."""
    x0, z0, x1, z1 = lot
    x0, z0, x1, z1 = x0 + 0.4, z0 + 0.4, x1 - 0.4, z1 - 0.4
    w, d = x1 - x0, z1 - z0
    shop_h = 5.0 if kind != "low" else 4.5
    rec = dict(kind=kind, x0=x0, z0=z0, x1=x1, z1=z1)
    if kind == "tower":
        floors = rnd.randint(14, 42)
        pod_h = 17.0
        ht = pod_h + floors * 4.0
        rec["height"] = ht
        # подиум
        tint = tint_color(rnd.choice(FACADE_TINTS))
        pm = "ribbon" if rnd.random() < 0.6 else "punched"
        for side, a, b, n in edges(x0, z0, x1, z1):
            ro = rnd.randrange(4) / 4.0
            shop_wall(a, b, n, side, shop_h, ro)
            M[pm].wall(a, b, shop_h, pod_h, 12.0, 12.0, uoff=ro, voff=(1 / 3.0) * rnd.randrange(3), ybase=shop_h, color=tint)
            if pub[side]:
                add_horiz_signs(a, b, n, 3.7, 1.2, 0.7)
                # козырёк
                cx, cz = (a[0] + b[0]) / 2, (a[1] + b[1]) / 2
        # LED на подиуме (экраны)
        for side, a, b, n in edges(x0, z0, x1, z1):
            if pub[side]:
                length = math.hypot(b[0] - a[0], b[1] - a[1])
                if length > 26 and rnd.random() < 0.8:
                    add_led(a, b, n, length * rnd.uniform(0.35, 0.65), shop_h + 1.2, min(length * 0.5, 22), 9.5, False)
        rooftop(x0, z0, x1, z1, pod_h, "mid")
        # башня
        inset_front = 6 + rnd.uniform(0, 6)
        tx0, tx1, tz0, tz1 = x0 + 5, x1 - 5, z0 + 5, z1 - 5
        if front_side == "S":
            tz1 = z1 - inset_front
        elif front_side == "N":
            tz0 = z0 + inset_front
        gt = tint_color(rnd.choice(GLASS_TINTS), 0.03)
        stepped = rnd.random() < 0.5
        stack = [(tx0, tz0, tx1, tz1, pod_h, ht * (0.72 if stepped else 1.0))]
        if stepped:
            sx0, sx1 = tx0 + (tx1 - tx0) * 0.2, tx1 - (tx1 - tx0) * 0.2
            sz0, sz1 = tz0 + (tz1 - tz0) * 0.2, tz1 - (tz1 - tz0) * 0.2
            stack.append((sx0, sz0, sx1, sz1, ht * 0.72, ht))
        for (a0, b0, a1, b1, ylo, yhi) in stack:
            ylo_a = ylo
            hh = yhi - ylo
            hh = max(4.0, round(hh / 4.0) * 4.0)
            yhi = ylo + hh
            for side, a, b, n in edges(a0, b0, a1, b1):
                M["glass"].wall(a, b, ylo, yhi, 12.0, 12.0, uoff=rnd.randrange(4) / 4.0, voff=0.0, ybase=ylo, color=gt)
                length = math.hypot(b[0] - a[0], b[1] - a[1])
                if ylo > 30 and length > 24 and rnd.random() < 0.28 and pub.get(side, False):
                    add_led(a, b, n, length * rnd.uniform(0.3, 0.7), ylo + rnd.uniform(4, max(5, hh * 0.5)), 10, 20, True)
            rooftop(a0, b0, a1, b1, yhi, "tower")
        rec["height"] = stack[-1][5]
    elif kind == "mid":
        floors = rnd.randint(6, 13)
        hh = shop_h + (floors - 1) * 4.0
        rec["height"] = hh
        use_glass = rnd.random() < 0.18
        fm = "glass" if use_glass else rnd.choice(["ribbon", "punched", "punched"])
        tint = tint_color(rnd.choice(GLASS_TINTS if use_glass else FACADE_TINTS))
        rnd_wall_mid = rnd.choice(["wall_tile", "wall_plaster"])
        for side, a, b, n in edges(x0, z0, x1, z1):
            ro = rnd.randrange(4) / 4.0
            shop_wall(a, b, n, side, shop_h, ro)
            if fm == "glass":
                M[fm].wall(a, b, shop_h, hh, 12.0, 12.0, uoff=ro, voff=(1 / 3.0) * rnd.randrange(3), ybase=shop_h, color=tint)
            else:
                wk = "wall_plaster" if fm == "ribbon" else rnd_wall_mid
                F3.facade(M, a, b, n, shop_h, hh, 4.0, "ribbon" if fm == "ribbon" else "officetel", rnd, wk, WALL_TILE[wk], tint, public=pub[side])
            if pub[side]:
                add_horiz_signs(a, b, n, 3.7, 1.2, 0.75)
                add_blade_signs(side, a, b, n, shop_h - 0.2, hh - 1.0, 0.75)
                length = math.hypot(b[0] - a[0], b[1] - a[1])
                if hh > 30 and length > 24 and rnd.random() < 0.18:
                    add_led(a, b, n, length * rnd.uniform(0.3, 0.7), shop_h + rnd.uniform(3, hh * 0.4), 8, 16, True)
        for side, a, b, n in edges(x0, z0, x1, z1):
            if not pub[side] and fm != "glass":
                ac_on_wall(a, b, n, shop_h, hh, 4.0, 0.05)
        rooftop(x0, z0, x1, z1, hh, "mid")
        if rnd.random() < 0.3:  # рекламный щит на крыше
            for side, a, b, n in edges(x0, z0, x1, z1):
                if pub[side] and math.hypot(b[0] - a[0], b[1] - a[1]) > 16:
                    length = math.hypot(b[0] - a[0], b[1] - a[1])
                    dx, dz = (b[0] - a[0]) / length, (b[1] - a[1]) / length
                    t = length * 0.5 - 7
                    wd, ht_ = 14.0, 3.5
                    idx = rnd.randrange(64)
                    off = 0.8
                    p0 = (a[0] + dx * t + n[0] * off, hh + 1.2, a[1] + dz * t + n[1] * off)
                    p1 = (a[0] + dx * (t + wd) + n[0] * off, hh + 1.2, a[1] + dz * (t + wd) + n[1] * off)
                    p2 = (p1[0], hh + 1.2 + ht_, p1[2])
                    p3 = (p0[0], hh + 1.2 + ht_, p0[2])
                    M["horiz"].quad(p0, p1, p2, p3, uv=quad_uv(*cell_uv("horiz", idx)))
                    mx, mz = a[0] + dx * (t + wd / 2) + n[0] * (off - 0.2), a[1] + dz * (t + wd / 2) + n[1] * (off - 0.2)
                    M["metal_dark"].box(mx, hh, mz, wd + 0.3, 1.2 + ht_, 0.3, yaw=math.atan2(dz, dx), uv_scale=0.25)
                    break
    else:  # low
        floors = rnd.randint(3, 6)
        hh = shop_h + (floors - 1) * 3.0
        rec["height"] = hh
        tint = tint_color(rnd.choice(BRICK_TINTS), 0.06)
        use_tile = rnd.random() < 0.3
        fm = "punched" if use_tile else "brick"
        tc = tint_color(rnd.choice(FACADE_TINTS)) if use_tile else tint
        for side, a, b, n in edges(x0, z0, x1, z1):
            ro = rnd.randrange(4) / 4.0
            if pub[side]:
                shop_wall(a, b, n, side, shop_h, ro)
                add_horiz_signs(a, b, n, 3.4, 1.1, 0.8)
                add_blade_signs(side, a, b, n, shop_h - 0.2, hh - 0.8, 0.95)
            else:
                (M["brick"] if not use_tile else M["punched"]).wall(a, b, 0, shop_h, 6.0 if not use_tile else 12.0, 6.0 if not use_tile else 12.0, uoff=ro, color=tc)
            wk = "wall_tile" if use_tile else "wall_brick"
            F3.facade(M, a, b, n, shop_h, hh, 3.0, "tile" if use_tile else "villa", rnd, wk, WALL_TILE[wk], tc, public=pub[side], front=pub[side])
        for side, a, b, n in edges(x0, z0, x1, z1):
            ac_on_wall(a, b, n, shop_h, hh, 3.0, 0.03 if pub[side] else 0.08)
        rooftop(x0, z0, x1, z1, hh, "low")
    building_records.append(rec)


# ------------------------------------------------------------------ квартал → участки
POLES_DBG = []


def make_block(xa, xb, zn, zf):
    sign = 1 if zf > zn else -1
    depth = abs(zf - zn)
    front_block = abs(zn) == 32
    rows = []
    if front_block:
        rows = [(0, 62, "front"), (70, 108, "mid"), (116, depth, "back")]
    else:
        rows = [(0, 40, "back"), (48, depth, "back")]
    mid_block = (xa == -63)
    for ri, (t0, t1, role) in enumerate(rows):
        x = xa
        # лотки по ширине
        while x < xb - 1:
            wlot = rnd.uniform(22, 54)
            if xb - (x + wlot) < 22:
                wlot = xb - x
            lx0, lx1 = x, x + wlot
            z_a, z_b = zn + sign * t0, zn + sign * t1
            zlo, zhi = min(z_a, z_b), max(z_a, z_b)
            lot = (lx0, zlo, lx1, zhi)
            front_side = "S" if sign < 0 else "N"
            pub = {"W": lx0 == xa, "E": lx1 == xb, "S": True, "N": True}
            if sign < 0:
                pub["N"] = True
            skip = False
            if mid_block and front_block and zn < 0 and ri == 0:
                # площадь Медиа-плаза: вместо первого ряда
                skip = True
            if not skip:
                if role == "front":
                    kind = "tower" if rnd.random() < (0.75 if rnd.random() < 0.8 else 0.4) else "mid"
                    if wlot < 30 and kind == "tower":
                        kind = "mid"
                elif role == "mid":
                    kind = "mid" if rnd.random() < 0.75 else "low"
                else:
                    kind = "low" if (rnd.random() < 0.8) else "mid"
                building(lot, kind, front_side, pub)
            x += wlot
    # переулки: бетонное покрытие между рядами
    for i in range(len(rows) - 1):
        ta, tb = rows[i][1], rows[i + 1][0]
        z_a, z_b = zn + sign * ta, zn + sign * tb
        M["concrete"].flat(xa, min(z_a, z_b), xb, max(z_a, z_b), SW + 0.01, tile=4.0, color=(0.7, 0.7, 0.7, 1))
        for gx in np.arange(xa + 6, xb - 4, 9.0):
            LIGHT_GLOWS.append((float(gx), (z_a + z_b) / 2, 7.5, 0, "alley"))
        # бетонные столбы и провода над переулком
        zc = (z_a + z_b) / 2
        side_z = zc + rnd.choice([-1, 1]) * (abs(z_b - z_a) / 2 - 0.6)
        poles = []
        for px in np.arange(xa + 8 + rnd.uniform(0, 6), xb - 4, rnd.uniform(26, 34)):
            poles.append(KP.utility_pole(M, float(px), SW, side_z, rnd, yaw=math.pi / 2))
            POLES_DBG.append((float(px), side_z))
        for p_a, p_b in zip(poles, poles[1:]):
            for k in range(len(p_a)):
                KP.cable(M, p_a[k], p_b[k], sag=rnd.uniform(0.35, 0.9), r=0.012 if k < 3 else 0.018)
        # провода от столбов к домам через переулок
        for p in poles:
            for _ in range(rnd.randint(1, 3)):
                q = (p[0][0] + rnd.uniform(-6, 6), p[0][1] - rnd.uniform(2, 5), zc - (side_z - zc) * 0.98)
                KP.cable(M, p[rnd.randrange(len(p))], q, sag=0.5, r=0.01)
        # палатки-포장마차 (ночные закусочные) в некоторых переулках
        if rnd.random() < 0.35:
            for _ in range(rnd.randint(1, 3)):
                KP.pojangmacha(M, rnd.uniform(xa + 8, xb - 8), SW + 0.01, zc - (side_z - zc) * 0.35, 0.0, LIGHT_GLOWS)


for (xa, xb) in xr:
    for (zn, zf) in zr:
        if xb - xa < 30:
            continue
        make_block(xa, xb, zn, zf)

# ------------------------------------------------------------------ площадь «Медиа-плаза» и главный LED-фасад
PX0, PX1 = -63, 83
PZ_N = -32
PZ_S = -32 - 62
M["plaza"].flat(PX0 + 2, PZ_S, PX1 - 2, PZ_N, SW + 0.02, tile=4.8)
# башня-экран за площадью
tx0, tx1 = -45, 65
tz0, tz1 = PZ_S - 40, PZ_S - 2
H_T = 148.0
tint = tint_color((0.95, 1.0, 1.05), 0.0)
rec = dict(kind="tower", x0=tx0, z0=tz0, x1=tx1, z1=tz1, height=H_T)
building_records.append(rec)
for side, a, b, n in edges(tx0, tz0, tx1, tz1):
    M["shop"].wall(a, b, 0, 5.0, 12.0, 5.0, uoff=0.25)
    M["glass"].wall(a, b, 5.0, H_T, 12.0, 12.0, uoff=0.0, ybase=5.0, color=tint)
    if side == "S":
        # гигантский экран по фасаду (над подиумом)
        add_led(a, b, n, (tx1 - tx0) * 0.5, 14, 84, 42, False, idx=0, frame=True)
        add_led(a, b, n, (tx1 - tx0) * 0.5 + 56, 60, 12, 36, True, idx=3)
        add_led(a, b, n, (tx1 - tx0) * 0.5 - 56, 60, 12, 36, True, idx=1)
rooftop(tx0, tz0, tx1, tz1, H_T, "tower")
# изогнутый угловой LED (дуга) на юго-западном углу башни — «изгиб» как у современных медиафасадов
cx, cz, R_ARC, h_arc, y_arc = tx0 - 2, PZ_S - 2 + 14, 15.0, 30.0, 10.0
segs = 14
for i in range(segs):
    a0 = math.radians(180 + 90 * i / segs)
    a1 = math.radians(180 + 90 * (i + 1) / segs)
    p0 = (cx + R_ARC * math.cos(a0), y_arc, cz - R_ARC * math.sin(a0) * -1)
    p1 = (cx + R_ARC * math.cos(a1), y_arc, cz - R_ARC * math.sin(a1) * -1)
    u0, v0, u1, v1 = cell_uv("led_land", 4)
    uu0 = u0 + (u1 - u0) * i / segs
    uu1 = u0 + (u1 - u0) * (i + 1) / segs
    M["led_land"].quad(p1, p0, (p0[0], y_arc + h_arc, p0[2]), (p1[0], y_arc + h_arc, p1[2]), uv=[(uu1, v1), (uu0, v1), (uu0, v0), (uu1, v0)])
LIGHT_GLOWS.append((cx - 8, cz + 6, 26, 4, "led_land"))
# скамьи и деревья на площади
PLAZA_TREES = [(PX0 + 14 + i * 16, z) for i in range(8) for z in (PZ_N - 8, PZ_S + 10)]

# ------------------------------------------------------------------ уличная инфраструктура
def tree(x, z, season_mix=0.6, y0=None, grate=True):
    """Гинкго (в основном, как на улицах Сеула) или дзелква. Ствол и ветки уходят в крону."""
    y0 = SW if y0 is None else y0
    species = "ginkgo" if rnd.random() < 0.72 else "zelkova"
    yellow = rnd.random() < season_mix
    trees.street_tree(M, x, y0, z, rnd, species=species, yellow=yellow, scale=rnd.uniform(0.9, 1.1))
    if grate:
        KP.tree_grate(M, x, y0, z)


def lamp(x, z, yaw_out):
    M["metal_dark"].cylinder(x, SW, z, 0.12, 9.0, 6)
    ax, az = x + math.cos(yaw_out) * 1.7, z + math.sin(yaw_out) * 1.7
    M["metal_dark"].box((x + ax) / 2, SW + 8.9, (z + az) / 2, 1.9, 0.12, 0.12, yaw=-yaw_out, uv_scale=1.0)
    M["lamp"].box(ax, SW + 8.78, az, 0.9, 0.18, 0.45, yaw=-yaw_out)
    LIGHT_GLOWS.append((ax, az, 9.5, -1, "lamp"))


for _p in PLAZA_TREES:
    tree(*_p)

# проспект: средний бульвар с деревьями, светильники, пешеходные переходы, светофоры
M["concrete"].box(0, 0.02, 0, 720, 0.3, 4.0, uv_scale=0.25, top=True)
for x in np.arange(-350, 351, 11.5):
    tree(float(x), 0.0, y0=0.32, grate=False)
    for z in (-28.5, 28.5):
        if all(abs(x - cx) > 15 for cx in CROSS_X):
            tree(float(x) + 4, z)
for x in np.arange(-340, 341, 28):
    if all(abs(x - cx) > 14 for cx in CROSS_X):
        lamp(float(x), -29.5, math.pi / 2)
        lamp(float(x) + 14, 29.5, -math.pi / 2)
for cx in CROSS_X:
    for z in np.arange(-300, 301, 30):
        if abs(z) > 34:
            lamp(cx - 14, float(z), 0.0)
            lamp(cx + 14, float(z) + 15, math.pi)

def cut_intervals_pre(lo, hi, cuts, pad):
    segs, cur = [], lo
    for c in sorted(cuts):
        if c - pad > cur:
            segs.append((cur, c - pad))
        cur = c + pad
    if cur < hi:
        segs.append((cur, hi))
    return segs


# ------------------------------------------------------------------ корейские детали тротуаров
for (a, b) in cut_intervals_pre(X0, X1, CROSS_X, CROSS_HALF + 6):
    for zs in (-26.2, 25.9):
        KP.tactile_strip(M, a, zs, b, zs + 0.3, SW)
for cx in CROSS_X:
    for sx in (-1, 1):
        xc = cx + sx * (CROSS_HALF + 3.5)
        for sz in (-1, 1):
            # предупреждающие площадки у перехода и болларды по кромке
            z_in = sz * (BLVD_HALF + 0.3)
            KP.tactile_strip(M, xc - 1.8, z_in, xc + 1.8, z_in + sz * 0.6, SW)
            for k in range(-2, 3):
                KP.bollard(M, xc + k * 1.5 + (2.6 if k == 0 else 0) * 0, SW, sz * (BLVD_HALF + 0.55), rnd) if k != 0 else None
# люки на проезжей части и тротуарах
for x in np.arange(X0 + 10, X1 - 10, 23.0):
    if all(abs(x - cx) > CROSS_HALF + 8 for cx in CROSS_X):
        KP.manhole(M, float(x + rnd.uniform(-4, 4)), 0.045, rnd.choice([-1, 1]) * (2.25 + 3.65 * rnd.randint(0, 4) + 1.8), rnd)
        if rnd.random() < 0.5:
            KP.manhole(M, float(x + rnd.uniform(-6, 6)), SW + 0.012, rnd.choice([-1, 1]) * rnd.uniform(25, 31), rnd, r=0.35)
for cx in CROSS_X:
    for z in np.arange(Z0 + 15, Z1 - 15, 27.0):
        if abs(z) > BLVD_HALF + 8:
            KP.manhole(M, cx + rnd.choice([-1, 1]) * 2.8, 0.046, float(z), rnd)


# разметка
def paint(kind, x0, z0, x1, z1, y=0.04):
    M[kind].flat(x0, z0, x1, z1, y, tile=8.0)


def cut_intervals(lo, hi, cuts, pad):
    segs, cur = [], lo
    for c in sorted(cuts):
        if c - pad > cur:
            segs.append((cur, c - pad))
        cur = c + pad
    if cur < hi:
        segs.append((cur, hi))
    return segs


bl_segs = cut_intervals(X0, X1, CROSS_X, CROSS_HALF + 7)
for (a, b) in bl_segs:
    for zc in (-2.6, 2.6):
        pass
    paint("paint_yellow", a, 2.1, b, 2.25)
    paint("paint_yellow", a, -2.25, b, -2.1)
    paint("paint_blue", a, 20.5, b, 20.75, 0.045)
    paint("paint_blue", a, -20.75, b, -20.5, 0.045)
    for sgn in (1, -1):
        for k in range(1, 5):
            zl = sgn * (2.25 + 3.65 * k) if k < 5 else None
            x = a
            while x < b:
                paint("paint_white", x, min(zl, zl + sgn * 0.15) if sgn > 0 else zl - 0.15, min(x + 3.0, b), (zl + 0.15) if sgn > 0 else zl, 0.05)
                x += 9.0
# пешеходные переходы и стоп-линии на проспекте
for cx in CROSS_X:
    for sx in (-1, 1):
        xc = cx + sx * (CROSS_HALF + 3.5)
        for z in np.arange(-22.5, 23, 1.0):
            if abs(z) > 2.3:
                paint("paint_white", xc - 1.75, z, xc + 1.75, z + 0.5, 0.05)
        xs_ = cx + sx * (CROSS_HALF + 7.5)
        paint("paint_white", xs_ - 0.2, 2.4 if sx == -1 else -22.5, xs_ + 0.2, 22.5 if sx == -1 else -2.4, 0.05)
    # переходы через поперечную улицу
    for sz in (-1, 1):
        zc = sz * (BLVD_HALF + 3.0)
        for x in np.arange(cx - CROSS_HALF + 0.5, cx + CROSS_HALF - 0.4, 1.0):
            paint("paint_white", x, zc - 1.75, x + 0.5, zc + 1.75, 0.05)
    # разметка поперечных улиц
    for (a, b) in cut_intervals(Z0, Z1, [0] + SEC_Z, BLVD_HALF + 8):
        paint("paint_yellow", cx - 0.2, a, cx - 0.05, b)
        paint("paint_yellow", cx + 0.05, a, cx + 0.2, b)
# светофоры
for cx in CROSS_X:
    for sx in (-1, 1):
        for sz in (-1, 1):
            px, pz = cx + sx * (CROSS_HALF + 1.5), sz * (BLVD_HALF + 1.5)
            M["metal_dark"].cylinder(px, SW, pz, 0.1, 6.2, 6)
            M["metal_dark"].box(px, SW + 5.0, pz, 0.45, 1.2, 0.45)
            M["tl_red" if (sx * sz > 0) else "tl_green"].box(px, SW + 5.1, pz, 0.35, 0.3, 0.5)
            M["metal_dark"].box(px + sx * 0.0, SW + 6.2, pz, 0.3, 0.3, 4.0 * sz * 0 + 3.0)
# автобусные остановки
for (x, z) in [(-190, 27.5), (30, -27.5), (190, 27.5), (-30, 27.5), (-250, -27.5), (320, -27.5)]:
    M["metal_dark"].box(x, SW, z, 6.0, 2.6, 1.6, uv_scale=0.25, top=True, sides=True)
    M["led_port"].quad((x - 1.2, SW + 0.5, z + (0.82 if z > 0 else -0.82)), (x + 1.2, SW + 0.5, z + (0.82 if z > 0 else -0.82)),
                       (x + 1.2, SW + 2.4, z + (0.82 if z > 0 else -0.82)), (x - 1.2, SW + 2.4, z + (0.82 if z > 0 else -0.82)),
                       uv=quad_uv(*cell_uv("led_port", rnd.randrange(8))))
    LIGHT_GLOWS.append((x, z + (-3 if z > 0 else 3), 6, 0, "led_port"))
# входы в метро
for (x, z) in [(-60, 30.5), (115, -30.5)]:
    M["glass"].box(x, SW, z, 6.0, 3.2, 3.0, color=(0.7, 0.9, 0.9, 1), uv_scale=0.25)
    M["metal_dark"].box(x, SW + 3.2, z, 6.6, 0.3, 3.6, uv_scale=0.25)
    sgn = 1 if z < 0 else -1
    M["metro"].quad((x - 3, SW + 3.5, z + sgn * 1.9), (x + 3, SW + 3.5, z + sgn * 1.9), (x + 3, SW + 4.9, z + sgn * 1.9), (x - 3, SW + 4.9, z + sgn * 1.9),
                    uv=[(0, 1), (1, 1), (1, 0), (0, 0)])
    M["metro"].quad((x + 3, SW + 3.5, z - sgn * 1.9), (x - 3, SW + 3.5, z - sgn * 1.9), (x - 3, SW + 4.9, z - sgn * 1.9), (x + 3, SW + 4.9, z - sgn * 1.9),
                    uv=[(0, 1), (1, 1), (1, 0), (0, 0)])
    LIGHT_GLOWS.append((x, z + sgn * 5, 8, 0, "metro"))

# ------------------------------------------------------------------ машины
CLEAR = [tuple(map(float, c.split(','))) for c in os.environ.get('CLEAR', '').split(';') if c]


def car(x, z, yaw, taxi=False):
    return  # по просьбе заказчика: машин в сцене нет
    if any(math.hypot(x - cx, z - cz) < 13 for cx, cz in CLEAR):
        return
    c = rnd.choice(CAR_COLORS)
    k = rnd.uniform(0.92, 1.08)
    col = (c[0] * k, c[1] * k, c[2] * k, 1.0)
    L, W = rnd.uniform(4.3, 4.9), 1.82
    cs, sn = math.cos(yaw), math.sin(yaw)

    def T(lx, ly, lz):
        return (x + lx * cs - lz * sn, ly, z + lx * sn + lz * cs)

    M["paint"].box(x, 0.30, z, L, 0.5, W, yaw=yaw, color=col, uv_scale=0.25)
    # скошенный салон: низ y=0.8, верх y=1.42
    lb, lt, wb, wt, off = L * 0.31, L * 0.20, W * 0.47, W * 0.41, -L * 0.05
    yb, yt = 0.8, 1.42
    B = [T(off - lb, yb, -wb), T(off + lb, yb, -wb), T(off + lb, yb, wb), T(off - lb, yb, wb)]
    Tp = [T(off - lt, yt, -wt), T(off + lt, yt, -wt), T(off + lt, yt, wt), T(off - lt, yt, wt)]
    for i in range(4):
        j = (i + 1) % 4
        M["car_glass"].quad(B[i], B[j], Tp[j], Tp[i])
    M["paint"].quad(Tp[0], Tp[1], Tp[2], Tp[3], color=col, normal=np.array([0, 1, 0]))
    for sx in (-1, 1):
        for sz in (-1, 1):
            wx, _, wz = T(sx * L * 0.33, 0, sz * (W / 2 - 0.05))
            M["metal_dark"].box(wx, 0.0, wz, 0.66, 0.6, 0.26, yaw=yaw, uv_scale=1.0)
    for sz in (-1, 1):
        fx, _, fz = T(L / 2 + 0.01, 0, sz * (W / 2 - 0.35))
        M["head"].box(fx, 0.52, fz, 0.1, 0.14, 0.4, yaw=yaw)
        bx, _, bz = T(-L / 2 - 0.01, 0, sz * (W / 2 - 0.35))
        M["tail"].box(bx, 0.55, bz, 0.1, 0.14, 0.46, yaw=yaw)
        LIGHT_GLOWS.append((fx + cs * 3.5, fz + sn * 3.5, 4.5, -2, "head"))
        LIGHT_GLOWS.append((bx - cs * 1.2, bz - sn * 1.2, 2.2, -3, "tail"))


# полосы проспекта
for k in range(5):
    zc = 2.25 + 3.65 * (k + 0.5) if k < 5 else 0
    for (zlane, yaw) in [(zc + 0.1, 0.0), (-zc - 0.1, math.pi)]:
        x = X0 + rnd.uniform(0, 12)
        while x < X1:
            if all(abs(x - cx) > CROSS_HALF + 9 for cx in CROSS_X):
                car(x, zlane, yaw)
            x += rnd.uniform(8, 38)
# автобусные полосы: редкие
for (zlane, yaw) in [(22.2, 0.0), (-22.2, math.pi)]:
    x = X0 + rnd.uniform(10, 60)
    while x < X1:
        if all(abs(x - cx) > CROSS_HALF + 9 for cx in CROSS_X):
            car(x, zlane, yaw)
        x += rnd.uniform(60, 130)
# поперечные улицы
for cx in CROSS_X:
    for (xl, yaw) in [(cx - 2.8, math.pi / 2), (cx + 2.8, -math.pi / 2)]:
        z = Z0 + rnd.uniform(0, 20)
        while z < Z1:
            if abs(z) > BLVD_HALF + 9 and all(abs(z - sz) > SEC_HALF + 7 for sz in SEC_Z):
                car(xl, z, yaw)
            z += rnd.uniform(14, 55)

# ------------------------------------------------------------------ дальний город и хребет
def far_box(cx, cz, w, h, d, col):
    """Дальний дом с фотофасадом (ambientCG Facade012-016): кусок атласа ~ 30 × 30 м на квадрат."""
    sl = rnd.randrange(5)
    x0, x1, z0, z1 = cx - w / 2, cx + w / 2, cz - d / 2, cz + d / 2
    for (a, bb) in [((x0, z0), (x0, z1)), ((x0, z1), (x1, z1)), ((x1, z1), (x1, z0)), ((x1, z0), (x0, z0))]:
        L = math.hypot(bb[0] - a[0], bb[1] - a[1])
        nu, nv = max(1, round(L / 30)), max(1, round(h / 30))
        for i in range(nu):
            for j in range(nv):
                pa = (a[0] + (bb[0] - a[0]) * i / nu, a[1] + (bb[1] - a[1]) * i / nu)
                pb = (a[0] + (bb[0] - a[0]) * (i + 1) / nu, a[1] + (bb[1] - a[1]) * (i + 1) / nu)
                ya, yb = h * j / nv, h * (j + 1) / nv
                u0, u1 = sl / 5 + 0.002, (sl + 1) / 5 - 0.002
                M["farf"].quad((pa[0], ya, pa[1]), (pb[0], ya, pb[1]), (pb[0], yb, pb[1]), (pa[0], yb, pa[1]), uv=[(u0, 1), (u1, 1), (u1, 0), (u0, 0)], color=col)


def far_ring():
    ROOFC=[(0.55,0.55,0.55),(0.45,0.5,0.46),(0.5,0.42,0.38),(0.6,0.6,0.58)]
    for gx in range(-1500, 1501, 66):
        for gz in range(-1200, 1201, 66):
            if abs(gx) < 440 and abs(gz) < 360:
                continue
            r = math.hypot(gx / 1.2, gz)
            w, d = rnd.uniform(36, 54), rnd.uniform(36, 54)
            hmax = 70 if r < 800 else 45
            h = rnd.uniform(10, hmax) * (2.2 if rnd.random() < 0.08 and r < 900 else 1.0)
            t = tint_color((0.72, 0.72, 0.74), 0.08)
            bx, bz = gx + rnd.uniform(-5, 5), gz + rnd.uniform(-5, 5)
            if rnd.random() < 0.65:
                far_box(bx, bz, w, h, d, t)
            else:
                m = M["punched"] if rnd.random() < 0.6 else M["ribbon"]
                m.box(bx, 0, bz, w, h, d, color=tint_color(rnd.choice(FACADE_TINTS), 0.06), uv_scale=1 / 12.0, top=False)
            c = rnd.choice(ROOFC)
            M["far"].box(gx, h, gz, w * 0.98, 0.3, d * 0.98, color=(*c, 1), uv_scale=0.1, sides=False)
    # хребет
    n = 96
    rng = np.random.default_rng(5)
    h = [260 + 160 * (math.sin(i * 0.37) * 0.5 + math.sin(i * 0.91 + 1) * 0.3 + 0.2) + rng.uniform(-20, 20) for i in range(n)]
    for i in range(n):
        a0, a1 = 2 * math.pi * i / n, 2 * math.pi * (i + 1) / n
        rr = 3200
        p0 = (math.cos(a0) * rr * 1.2, 0, math.sin(a0) * rr)
        p1 = (math.cos(a1) * rr * 1.2, 0, math.sin(a1) * rr)
        q0 = (p0[0], h[i], p0[2])
        q1 = (p1[0], h[(i + 1) % n], p1[2])
        col = (0.34, 0.4, 0.42, 1)
        M["far"].quad(p1, p0, q0, q1, color=col)


far_ring()

# ------------------------------------------------------------------ свечения на земле (аддитивные)
def glow_quad(x, z, r, color, y=0.3):
    M["glow"].quad((x - r, y, z - r), (x - r, y, z + r), (x + r, y, z + r), (x + r, y, z - r),
                   uv=[(0, 0), (0, 1), (1, 1), (1, 0)], color=color, normal=np.array([0, 1, 0]))


led_tints = {"led_land": [(0.35, 0.5, 0.9), (0.9, 0.3, 0.8), (0.4, 0.7, 0.3), (0.3, 0.8, 0.9), (0.9, 0.2, 0.4), (0.9, 0.55, 0.6), (0.3, 0.55, 0.95), (0.2, 0.5, 0.9)],
             "led_port": [(0.9, 0.5, 0.1), (0.9, 0.8, 0.3), (0.5, 0.5, 0.7), (0.9, 0.2, 0.6), (0.5, 0.3, 0.9), (0.9, 0.3, 0.1), (0.9, 0.3, 0.2), (0.2, 0.4, 0.7)]}
for (x, z, r, idx, kind) in LIGHT_GLOWS:
    if kind == "lamp":
        glow_quad(x, z, r, (1.0, 0.72, 0.42, 1.0))
    elif kind == "head":
        glow_quad(x, z, r, (1.0, 0.95, 0.85, 0.5))
    elif kind == "tail":
        glow_quad(x, z, r, (1.0, 0.1, 0.05, 0.8))
    elif kind == "alley":
        glow_quad(x, z, r, (1.0, 0.62, 0.34, 0.4))
    elif kind == "shopglow":
        glow_quad(x, z, r, (1.0, 0.85, 0.65, 0.35))
    elif kind == "tent":
        glow_quad(x, z, r, (1.0, 0.5, 0.2, 0.9))
    elif kind == "metro":
        glow_quad(x, z, r, (0.2, 0.9, 0.6, 1.0))
    else:
        c = led_tints[kind][idx % 8]
        glow_quad(x, z, r, (c[0], c[1], c[2], 1.0))

# ------------------------------------------------------------------ запись
for k, mesh in M.items():
    glb.add_mesh(mesh, MI[k])
size = glb.write(OUT / "kandam_district.glb")
for k, mesh in M2.items():
    if mesh.tri_count():
        G2.add_mesh(mesh, MI2[k])
G2.write(OUT / "kandam_interiors.glb")
json.dump(dict(note="Готовые помещения 1-го этажа. Координаты — локальные метры района (сдвиг в карту: +10020, 0, +6560). "
                    "Стримить kandam_interiors.glb при приближении к двери; витрина и стекло — в основном GLB.",
               interiors=INTERIOR_RECORDS), open(OUT / "interiors.json", "w"), ensure_ascii=False, indent=1)
print("interiors", len(INTERIOR_RECORDS), [r_["type"] for r_ in INTERIOR_RECORDS])

tris = sum(m.tri_count() for m in M.values())
stats = dict(glb_bytes=size, triangles=tris, buildings=len(building_records),
             towers=sum(1 for b in building_records if b["kind"] == "tower"),
             mid=sum(1 for b in building_records if b["kind"] == "mid"), low=sum(1 for b in building_records if b["kind"] == "low"),
             max_height=max(b["height"] for b in building_records), materials=len(glb.materials),
             per_material={k: m.tri_count() for k, m in M.items()})
(OUT / "stats.json").write_text(json.dumps(stats, indent=1))
(OUT / "buildings.json").write_text(json.dumps(building_records, indent=1))
brands_used = [dict(id=b["id"], ko=b["ko"], en=b["en"], ru=b["ru"], sector=b["sector"], source=b["source"]) for b in assets.brands()]
(OUT / "brands_used.json").write_text(json.dumps(brands_used, ensure_ascii=False, indent=1))
print(json.dumps({k: v for k, v in stats.items() if k != "per_material"}))
