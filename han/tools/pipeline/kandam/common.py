"""Общие утилиты генератора района: тайловый шум, карты нормалей, построитель мешей, запись GLB."""
import io
import json
import math
import struct
from pathlib import Path

import numpy as np
from PIL import Image
from scipy import ndimage

RNG = np.random.default_rng(20261103)


# ---------------------------------------------------------------- шум (бесшовный, через частотный фильтр)
def tile_noise(n, beta=2.0, seed=0, lo=0.0, hi=1.0):
    """Бесшовный шум 1/f^beta размером n x n, нормирован в [lo, hi]."""
    rng = np.random.default_rng(seed)
    f = np.fft.fftfreq(n)
    fx, fy = np.meshgrid(f, f)
    r = np.sqrt(fx * fx + fy * fy)
    r[0, 0] = 1.0
    spec = (rng.normal(size=(n, n)) + 1j * rng.normal(size=(n, n))) / (r ** (beta / 2.0))
    spec[0, 0] = 0
    a = np.real(np.fft.ifft2(spec))
    a = (a - a.min()) / (a.max() - a.min() + 1e-9)
    return lo + a * (hi - lo)


def normal_from_height(h, strength=2.0):
    """Карта нормалей (RGB uint8, OpenGL: +Y вверх) из карты высот [0..1]; бесшовна (wrap)."""
    gx = (np.roll(h, -1, axis=1) - np.roll(h, 1, axis=1)) * 0.5 * strength * h.shape[0] / 64.0
    gy = (np.roll(h, -1, axis=0) - np.roll(h, 1, axis=0)) * 0.5 * strength * h.shape[0] / 64.0
    nx, ny, nz = -gx, gy, np.ones_like(h)  # строка вниз = +v вниз; Y нормали вверх: gy со знаком +
    l = np.sqrt(nx * nx + ny * ny + nz * nz)
    n = np.stack([nx / l, ny / l, nz / l], axis=-1)
    return ((n * 0.5 + 0.5) * 255).astype(np.uint8)


def to_u8(a):
    return (np.clip(a, 0, 1) * 255 + 0.5).astype(np.uint8)


def save_png(path, arr):
    Image.fromarray(arr).save(path, optimize=True)


def png_bytes(arr):
    b = io.BytesIO()
    Image.fromarray(arr).save(b, format="PNG", optimize=True)
    return b.getvalue()


def blur(a, s):
    return ndimage.gaussian_filter(a, s, mode="wrap")


# ---------------------------------------------------------------- построитель мешей
class Mesh:
    """Один материал = один Mesh. Позиции в метрах, Y вверх."""

    def __init__(self, name):
        self.name = name
        self.pos, self.nrm, self.uv, self.col = [], [], [], []
        self.idx = []

    def n(self):
        return len(self.pos)

    def tri_count(self):
        return len(self.idx) // 3

    def quad(self, p0, p1, p2, p3, uv=None, color=(1, 1, 1, 1), normal=None):
        """Четырёхугольник p0..p3 против часовой стрелки (если смотреть со стороны нормали)."""
        p = [np.asarray(q, dtype=np.float64) for q in (p0, p1, p2, p3)]
        if normal is None:
            nn = np.cross(p[1] - p[0], p[3] - p[0])
            ln = np.linalg.norm(nn)
            normal = nn / ln if ln > 1e-12 else np.array([0, 1, 0])
        if uv is None:
            uv = [(0, 1), (1, 1), (1, 0), (0, 0)]
        b = self.n()
        for q, t in zip(p, uv):
            self.pos.append(q)
            self.nrm.append(normal)
            self.uv.append(t)
            self.col.append(color)
        self.idx += [b, b + 1, b + 2, b, b + 2, b + 3]

    def tri(self, p0, p1, p2, uv=None, color=(1, 1, 1, 1), normal=None):
        p = [np.asarray(q, dtype=np.float64) for q in (p0, p1, p2)]
        if normal is None:
            nn = np.cross(p[1] - p[0], p[2] - p[0])
            ln = np.linalg.norm(nn)
            normal = nn / ln if ln > 1e-12 else np.array([0, 1, 0])
        if uv is None:
            uv = [(0, 1), (1, 1), (0, 0)]
        b = self.n()
        for q, t in zip(p, uv):
            self.pos.append(q)
            self.nrm.append(normal)
            self.uv.append(t)
            self.col.append(color)
        self.idx += [b, b + 1, b + 2]

    def wall(self, a, b, y0, y1, tile_w, tile_h, uoff=0.0, voff=0.0, color=(1, 1, 1, 1), flip=False, ybase=0.0):
        """Вертикальная стена от точки a=(x,z) до b=(x,z), UV в метрах / размер тайла. Нормаль — вправо от направления a->b (наружу при обходе против часовой на карте x,z... см. оболочку здания)."""
        ax, az = a
        bx, bz = b
        length = math.hypot(bx - ax, bz - az)
        if length < 1e-6:
            return
        p0 = (ax, y0, az)
        p1 = (bx, y0, bz)
        p2 = (bx, y1, bz)
        p3 = (ax, y1, az)
        u1 = uoff + length / tile_w
        v0 = voff + (y0 - ybase) / tile_h
        v1 = voff + (y1 - ybase) / tile_h
        uv = [(uoff, 1 - v0), (u1, 1 - v0), (u1, 1 - v1), (uoff, 1 - v1)]
        if flip:
            self.quad(p1, p0, p3, p2, uv=[uv[1], uv[0], uv[3], uv[2]], color=color)
        else:
            self.quad(p0, p1, p2, p3, uv=uv, color=color)

    def box(self, cx, y0, cz, w, h, d, yaw=0.0, color=(1, 1, 1, 1), uv_scale=1.0, top=True, bottom=False, sides=True):
        """Параллелепипед с центром основания (cx, y0, cz), размеры w (x) × h (y) × d (z), поворот yaw вокруг Y."""
        c, s = math.cos(yaw), math.sin(yaw)

        def tp(lx, lz):
            return (cx + lx * c - lz * s, cz + lx * s + lz * c)

        hw, hd = w / 2, d / 2
        corners = [tp(-hw, -hd), tp(hw, -hd), tp(hw, hd), tp(-hw, hd)]
        y1 = y0 + h
        if sides:
            # порядок обхода: чтобы нормаль смотрела наружу, идём по часовой стрелке при взгляде сверху (x вправо, z вниз)
            for i in range(4):
                a = corners[i]
                b = corners[(i + 1) % 4]
                length = math.hypot(b[0] - a[0], b[1] - a[1])
                self.quad((b[0], y0, b[1]), (a[0], y0, a[1]), (a[0], y1, a[1]), (b[0], y1, b[1]),
                          uv=[(length * uv_scale, h * uv_scale), (0, h * uv_scale), (0, 0), (length * uv_scale, 0)], color=color)
        if top:
            self.quad((corners[0][0], y1, corners[0][1]), (corners[3][0], y1, corners[3][1]),
                      (corners[2][0], y1, corners[2][1]), (corners[1][0], y1, corners[1][1]),
                      uv=[(0, 0), (0, d * uv_scale), (w * uv_scale, d * uv_scale), (w * uv_scale, 0)], color=color, normal=np.array([0, 1, 0]))
        if bottom:
            self.quad((corners[0][0], y0, corners[0][1]), (corners[1][0], y0, corners[1][1]),
                      (corners[2][0], y0, corners[2][1]), (corners[3][0], y0, corners[3][1]),
                      uv=[(0, 0), (w * uv_scale, 0), (w * uv_scale, d * uv_scale), (0, d * uv_scale)], color=color, normal=np.array([0, -1, 0]))

    def flat(self, x0, z0, x1, z1, y, tile=4.0, color=(1, 1, 1, 1), uoff=0.0, voff=0.0):
        """Горизонтальный прямоугольник, нормаль вверх, UV в метрах / tile."""
        w = (x1 - x0) / tile
        d = (z1 - z0) / tile
        self.quad((x0, y, z0), (x0, y, z1), (x1, y, z1), (x1, y, z0),
                  uv=[(uoff, voff), (uoff, voff + d), (uoff + w, voff + d), (uoff + w, voff)], color=color, normal=np.array([0, 1, 0]))

    def cylinder(self, cx, y0, cz, r, h, sides=8, color=(1, 1, 1, 1), r_top=None, cap=True):
        r_top = r if r_top is None else r_top
        for i in range(sides):
            a0 = 2 * math.pi * i / sides
            a1 = 2 * math.pi * (i + 1) / sides
            p0 = (cx + r * math.cos(a0), y0, cz + r * math.sin(a0))
            p1 = (cx + r * math.cos(a1), y0, cz + r * math.sin(a1))
            p2 = (cx + r_top * math.cos(a1), y0 + h, cz + r_top * math.sin(a1))
            p3 = (cx + r_top * math.cos(a0), y0 + h, cz + r_top * math.sin(a0))
            self.quad(p1, p0, p3, p2, uv=[(i / sides, 1), ((i + 1) / sides, 1), ((i + 1) / sides, 0), (i / sides, 0)], color=color)
        if cap and r_top > 0:
            for i in range(sides):
                a0 = 2 * math.pi * i / sides
                a1 = 2 * math.pi * (i + 1) / sides
                self.tri((cx, y0 + h, cz), (cx + r_top * math.cos(a1), y0 + h, cz + r_top * math.sin(a1)),
                         (cx + r_top * math.cos(a0), y0 + h, cz + r_top * math.sin(a0)), color=color, normal=np.array([0, 1, 0]))

    def blob(self, cx, cy, cz, rx, ry, rz, seed=0, color_fn=None, subdiv=1):
        """Низкополигональная «крона»: икосфера с шумом по радиусу и цветом вершин."""
        rng = np.random.default_rng(seed)
        t = (1 + 5 ** 0.5) / 2
        v = [(-1, t, 0), (1, t, 0), (-1, -t, 0), (1, -t, 0), (0, -1, t), (0, 1, t), (0, -1, -t), (0, 1, -t),
             (t, 0, -1), (t, 0, 1), (-t, 0, -1), (-t, 0, 1)]
        f = [(0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11), (1, 5, 9), (5, 11, 4), (11, 10, 2), (10, 7, 6), (7, 1, 8),
             (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8), (3, 8, 9), (4, 9, 5), (2, 4, 11), (6, 2, 10), (8, 6, 7), (9, 8, 1)]
        v = [np.array(p, dtype=float) / np.linalg.norm(p) for p in v]
        for _ in range(subdiv):
            cache = {}
            nf = []

            def mid(a, b):
                k = (min(a, b), max(a, b))
                if k not in cache:
                    m = (v[a] + v[b]) / 2
                    v.append(m / np.linalg.norm(m))
                    cache[k] = len(v) - 1
                return cache[k]

            for a, b, c in f:
                ab, bc, ca = mid(a, b), mid(b, c), mid(c, a)
                nf += [(a, ab, ca), (b, bc, ab), (c, ca, bc), (ab, bc, ca)]
            f = nf
        rad = 0.85 + 0.3 * rng.random(len(v))
        pts = [np.array([cx + p[0] * rx * r, cy + p[1] * ry * r, cz + p[2] * rz * r]) for p, r in zip(v, rad)]
        for a, b, c in f:
            col = color_fn((pts[a] + pts[b] + pts[c]) / 3 - np.array([cx, cy, cz]), rng) if color_fn else (1, 1, 1, 1)
            self.tri(pts[a], pts[b], pts[c], uv=[(0, 0), (1, 0), (0, 1)], color=col)

    def bounds(self):
        a = np.array(self.pos)
        return a.min(axis=0), a.max(axis=0)


# ---------------------------------------------------------------- запись GLB
class GLB:
    def __init__(self):
        self.images = []      # bytes PNG/JPEG
        self.mimes = []
        self.top_extras = {}
        self.img_names = []
        self.textures = []    # индексы изображений
        self.materials = []
        self.meshes = []      # (Mesh, material_index)
        self.mat_names = {}

    def add_image(self, name, arr, fmt="png"):
        if fmt == "jpeg":
            b = io.BytesIO()
            Image.fromarray(arr).save(b, format="JPEG", quality=95, subsampling=0)
            self.images.append(b.getvalue())
            self.mimes.append("image/jpeg")
        else:
            self.images.append(png_bytes(arr))
            self.mimes.append("image/png")
        self.img_names.append(name)
        self.textures.append(len(self.images) - 1)
        return len(self.textures) - 1

    def add_material(self, name, base=None, normal=None, orm=None, emissive=None, base_factor=(1, 1, 1, 1),
                     metallic=0.0, roughness=1.0, emissive_factor=(0, 0, 0), emissive_strength=1.0, double_sided=False,
                     alpha_mode="OPAQUE", extras=None):
        m = {"name": name, "pbrMetallicRoughness": {"baseColorFactor": list(base_factor), "metallicFactor": metallic, "roughnessFactor": roughness}}
        if base is not None:
            m["pbrMetallicRoughness"]["baseColorTexture"] = {"index": base}
        if orm is not None:
            m["pbrMetallicRoughness"]["metallicRoughnessTexture"] = {"index": orm}
        if normal is not None:
            m["normalTexture"] = {"index": normal}
        if emissive is not None:
            m["emissiveTexture"] = {"index": emissive}
        if emissive is not None or any(emissive_factor):
            m["emissiveFactor"] = list(emissive_factor if any(emissive_factor) else (1, 1, 1))
            if emissive_strength != 1.0:
                m["extensions"] = {"KHR_materials_emissive_strength": {"emissiveStrength": emissive_strength}}
        if double_sided:
            m["doubleSided"] = True
        if alpha_mode != "OPAQUE":
            m["alphaMode"] = alpha_mode
        m["extras"] = extras or {}
        self.materials.append(m)
        self.mat_names[name] = len(self.materials) - 1
        return len(self.materials) - 1

    def add_mesh(self, mesh, material):
        if mesh.n() > 0:
            self.meshes.append((mesh, material))

    def write(self, path):
        buf = bytearray()
        views, accs, prims, images = [], [], [], []

        def add_view(data, target=None):
            while len(buf) % 4:
                buf.append(0)
            off = len(buf)
            buf.extend(data)
            v = {"buffer": 0, "byteOffset": off, "byteLength": len(data)}
            if target:
                v["target"] = target
            views.append(v)
            return len(views) - 1

        def add_acc(view, ctype, count, atype, mn=None, mx=None, normalized=False):
            a = {"bufferView": view, "componentType": ctype, "count": count, "type": atype}
            if mn is not None:
                a["min"], a["max"] = mn, mx
            if normalized:
                a["normalized"] = True
            accs.append(a)
            return len(accs) - 1

        for png, mime in zip(self.images, self.mimes):
            images.append({"bufferView": add_view(png), "mimeType": mime})
        for mesh, mat in self.meshes:
            pos = np.array(mesh.pos, dtype=np.float32)
            nrm = np.array(mesh.nrm, dtype=np.float32)
            uv = np.array(mesh.uv, dtype=np.float32)
            col = (np.clip(np.array(mesh.col, dtype=np.float32), 0, 1) * 255 + 0.5).astype(np.uint8)
            idx = np.array(mesh.idx, dtype=np.uint32)
            attrs = {
                "POSITION": add_acc(add_view(pos.tobytes(), 34962), 5126, len(pos), "VEC3", pos.min(axis=0).tolist(), pos.max(axis=0).tolist()),
                "NORMAL": add_acc(add_view(nrm.tobytes(), 34962), 5126, len(nrm), "VEC3"),
                "TEXCOORD_0": add_acc(add_view(uv.tobytes(), 34962), 5126, len(uv), "VEC2"),
                "COLOR_0": add_acc(add_view(col.tobytes(), 34962), 5121, len(col), "VEC4", normalized=True),
            }
            prims.append({"attributes": attrs, "indices": add_acc(add_view(idx.tobytes(), 34963), 5125, len(idx), "SCALAR"), "material": mat, "mode": 4})
        gltf = {
            "asset": {"version": "2.0", "generator": "kandam_district_gen 1.0 (procedural, own work)"},
            "extensionsUsed": ["KHR_materials_emissive_strength"],
            "scene": 0, "scenes": [{"nodes": [0]}],
            "nodes": [{"name": "Kandam_District", "mesh": 0}],
            "meshes": [{"name": "Kandam_District", "primitives": prims}],
            "materials": self.materials,
            "samplers": [{"magFilter": 9729, "minFilter": 9987, "wrapS": 10497, "wrapT": 10497}],
            "textures": [{"source": i, "sampler": 0, "name": self.img_names[i]} for i in self.textures],
            "images": images, "accessors": accs, "bufferViews": views,
            "buffers": [{"byteLength": len(buf)}],
            "extras": self.top_extras,
        }
        js = json.dumps(gltf, separators=(",", ":")).encode("utf-8")
        while len(js) % 4:
            js += b" "
        while len(buf) % 4:
            buf.append(0)
        total = 12 + 8 + len(js) + 8 + len(buf)
        with open(path, "wb") as f:
            f.write(struct.pack("<III", 0x46546C67, 2, total))
            f.write(struct.pack("<II", len(js), 0x4E4F534A))
            f.write(js)
            f.write(struct.pack("<II", len(buf), 0x004E4942))
            f.write(buf)
        return total
