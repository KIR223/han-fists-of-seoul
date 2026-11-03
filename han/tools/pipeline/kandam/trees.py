"""Уличные деревья Сеула: гинкго (узкая пирамидальная крона, жёлтая осенью) и дзелква (вазообразная).
Ствол и ветки — трубки с корой (UV в метрах), листва — карточки-кластеры на концах веток, поэтому крона
физически «сидит» на ветках, а не висит отдельно."""
import math

import numpy as np


def _frame(d):
    d = d / np.linalg.norm(d)
    a = np.array([0.0, 1.0, 0.0]) if abs(d[1]) < 0.9 else np.array([1.0, 0.0, 0.0])
    u = np.cross(d, a); u /= np.linalg.norm(u)
    v = np.cross(d, u)
    return d, u, v


def tube(mesh, p0, p1, r0, r1, sides=7, v0=0.0, color=(1, 1, 1, 1), twist=0.0):
    """Конусная трубка p0->p1. Возвращает v-координату конца (кора тянется непрерывно)."""
    p0 = np.asarray(p0, float); p1 = np.asarray(p1, float)
    L = float(np.linalg.norm(p1 - p0))
    if L < 1e-4:
        return v0
    d, u, v = _frame(p1 - p0)
    circ = 2 * math.pi * max(r0, 0.02)
    us = max(1.0, round(circ / 0.6))  # кора ~0,6 м на тайл по окружности
    v1 = v0 + L / 0.9
    for i in range(sides):
        a0 = 2 * math.pi * i / sides + twist
        a1 = 2 * math.pi * (i + 1) / sides + twist
        c0, s0, c1, s1 = math.cos(a0), math.sin(a0), math.cos(a1), math.sin(a1)
        q0 = p0 + (u * c0 + v * s0) * r0
        q1 = p0 + (u * c1 + v * s1) * r0
        q2 = p1 + (u * c1 + v * s1) * r1
        q3 = p1 + (u * c0 + v * s0) * r1
        uu0, uu1 = us * i / sides, us * (i + 1) / sides
        mesh.quad(q1, q0, q3, q2, uv=[(uu1, v0), (uu0, v0), (uu0, v1), (uu1, v1)], color=color)
    return v1


def _rot(vec, axis, ang):
    axis = axis / np.linalg.norm(axis)
    return vec * math.cos(ang) + np.cross(axis, vec) * math.sin(ang) + axis * np.dot(axis, vec) * (1 - math.cos(ang))


def leaf_card(mesh, c, normal_hint, size, u0, u1, rnd, tint=1.0):
    """Карточка листвы с центром c, слегка повёрнутая наружу от ствола."""
    n = np.asarray(normal_hint, float)
    n = n / (np.linalg.norm(n) + 1e-6)
    n = n + np.array([rnd.uniform(-0.6, 0.6), rnd.uniform(-0.2, 0.7), rnd.uniform(-0.6, 0.6)])
    d, a, b = _frame(n)
    ang = rnd.uniform(0, 2 * math.pi)
    ax = a * math.cos(ang) + b * math.sin(ang)
    ay = np.cross(d, ax)
    hx, hy = ax * size * 0.5, ay * size * 0.5
    c = np.asarray(c, float)
    k = tint * rnd.uniform(0.85, 1.08)
    col = (min(k, 1.0), min(k, 1.0), min(k * 0.98, 1.0), 1.0)
    mesh.quad(c - hx - hy, c + hx - hy, c + hx + hy, c - hx + hy, uv=[(u0, 1.0), (u1, 1.0), (u1, 0.0), (u0, 0.0)], color=col)


def street_tree(M, x, y0, z, rnd, species="ginkgo", yellow=True, scale=1.0, bark="trunk", leaf="leaf"):
    """M[bark] — меш коры, M[leaf] — меш карточек листвы. Атлас листвы: 4 ячейки по U
    (0: гинкго жёлтый, 1: гинкго зелёный, 2: дзелква осенняя, 3: дзелква зелёная)."""
    cell = {("ginkgo", True): 0, ("ginkgo", False): 1, ("zelkova", True): 2, ("zelkova", False): 3}[(species, yellow)]
    u0, u1 = cell / 4 + 0.002, (cell + 1) / 4 - 0.002
    s = scale * rnd.uniform(0.88, 1.12)
    up = np.array([0.0, 1.0, 0.0])
    lean = np.array([rnd.uniform(-0.05, 0.05), 1.0, rnd.uniform(-0.05, 0.05)])
    base = np.array([x, y0, z], float)
    tips = []
    if species == "ginkgo":
        H = 8.5 * s; r_base = 0.2 * s
        nseg = 6
        pts = [base]
        for i in range(1, nseg + 1):
            t = i / nseg
            p = base + lean / np.linalg.norm(lean) * H * t + np.array([rnd.uniform(-0.12, 0.12), 0, rnd.uniform(-0.12, 0.12)]) * t
            pts.append(p)
        vv = 0.0
        for i in range(nseg):
            t0, t1 = i / nseg, (i + 1) / nseg
            vv = tube(M[bark], pts[i], pts[i + 1], r_base * (1 - 0.85 * t0) + 0.015, r_base * (1 - 0.85 * t1) + 0.015, 8, vv)
        # прикорневое утолщение
        tube(M[bark], base - np.array([0, 0.05, 0]), base + np.array([0, 0.5, 0]), r_base * 1.45, r_base * 1.02, 8, 0.0)
        # ярусы веток: снизу длиннее (пирамида), ветки идут вверх под 35-55°
        h_first = 2.6 * s
        n_br = 18
        for k in range(n_br):
            t = (h_first + (H * 0.95 - h_first) * k / (n_br - 1)) / H
            p = base + (pts[-1] - base) * t
            yaw = k * 2.39996 + rnd.uniform(-0.3, 0.3)
            out = np.array([math.cos(yaw), 0, math.sin(yaw)])
            elev = math.radians(rnd.uniform(35, 58))
            dirv = out * math.cos(elev) + up * math.sin(elev)
            L = (3.4 - 2.6 * t) * s * rnd.uniform(0.8, 1.15)
            rr = r_base * (0.42 - 0.3 * t) + 0.02
            mid = p + dirv * L * 0.55 + np.array([0, 0.15, 0])
            end = mid + (dirv * 0.6 + up * 0.4) * L * 0.45
            v1 = tube(M[bark], p, mid, rr, rr * 0.6, 5)
            tube(M[bark], mid, end, rr * 0.6, 0.012, 4, v1)
            tips.append((end, out, L))
            # веточки
            for j in range(2):
                a = mid + (end - mid) * rnd.uniform(0.0, 0.6)
                tw = _rot(dirv, up, rnd.uniform(-1.0, 1.0)) + up * 0.3
                tw /= np.linalg.norm(tw)
                e2 = a + tw * L * rnd.uniform(0.3, 0.45)
                tube(M[bark], a, e2, rr * 0.35, 0.008, 4)
                tips.append((e2, out, L * 0.7))
        top = pts[-1]
        tips.append((top + up * 0.4, up, 1.0))
        # листва: кластеры вокруг каждой ветки/веточки (гинкго — плотные короткие побеги вдоль веток)
        for (e, out, L) in tips:
            nc = 3 + int(L * 1.2)
            for _ in range(nc):
                c = e + np.array([rnd.uniform(-0.5, 0.5), rnd.uniform(-0.45, 0.35), rnd.uniform(-0.5, 0.5)]) * (0.6 + 0.25 * L)
                leaf_card(M[leaf], c, out, rnd.uniform(0.9, 1.35) * s, u0, u1, rnd)
        # заполнение у ствола (тень внутри кроны чуть темнее)
        for _ in range(16):
            t = rnd.uniform(0.35, 0.95)
            p = base + (pts[-1] - base) * t
            yaw = rnd.uniform(0, 2 * math.pi)
            rr = (3.0 - 2.5 * t) * s * rnd.uniform(0.3, 0.8)
            out = np.array([math.cos(yaw), 0.2, math.sin(yaw)])
            leaf_card(M[leaf], p + out * rr, out, rnd.uniform(1.0, 1.4) * s, u0, u1, rnd, tint=0.78)
    else:  # дзелква: короткий ствол, 4-5 скелетных ветвей веером вверх, широкий «зонт»
        H0 = 2.4 * s; r_base = 0.24 * s
        top0 = base + lean / np.linalg.norm(lean) * H0
        tube(M[bark], base - np.array([0, 0.05, 0]), base + np.array([0, 0.45, 0]), r_base * 1.4, r_base, 8, 0.0)
        tube(M[bark], base, top0, r_base, r_base * 0.82, 8)
        nb = rnd.randint(4, 6)
        for k in range(nb):
            yaw = 2 * math.pi * k / nb + rnd.uniform(-0.35, 0.35)
            out = np.array([math.cos(yaw), 0, math.sin(yaw)])
            elev = math.radians(rnd.uniform(52, 68))
            d1 = out * math.cos(elev) + up * math.sin(elev)
            L1 = rnd.uniform(3.0, 4.0) * s
            p1 = top0 + d1 * L1
            v1 = tube(M[bark], top0, p1, r_base * 0.5, r_base * 0.3, 6)
            for j in range(3):
                yy = _rot(d1, up, rnd.uniform(-0.7, 0.7))
                el2 = math.radians(rnd.uniform(25, 50))
                o2 = yy - up * np.dot(yy, up); o2 /= np.linalg.norm(o2) + 1e-6
                d2 = o2 * math.cos(el2) + up * math.sin(el2)
                L2 = rnd.uniform(1.8, 2.8) * s
                p2 = p1 + d2 * L2
                tube(M[bark], p1, p2, r_base * 0.28, 0.02, 5, v1)
                tips.append((p2, o2, L2))
                for q in range(2):
                    a = p1 + (p2 - p1) * rnd.uniform(0.3, 0.8)
                    d3 = _rot(d2, up, rnd.uniform(-1.2, 1.2)); d3 = d3 + up * 0.2
                    p3 = a + d3 / np.linalg.norm(d3) * rnd.uniform(0.9, 1.4) * s
                    tube(M[bark], a, p3, 0.03, 0.008, 4)
                    tips.append((p3, o2, 1.2))
        for (e, out, L) in tips:
            for _ in range(5):
                c = e + np.array([rnd.uniform(-0.8, 0.8), rnd.uniform(-0.6, 0.3), rnd.uniform(-0.8, 0.8)])
                leaf_card(M[leaf], c, out + np.array([0, 0.5, 0]), rnd.uniform(1.1, 1.6) * s, u0, u1, rnd)
    return tips
