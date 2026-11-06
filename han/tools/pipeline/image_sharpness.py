#!/usr/bin/env python3
"""Численная оценка резкости кадра относительно эталона со сверхсэмплингом (этап 02 плана города).

    python han/tools/pipeline/image_sharpness.py эталон.png кадр.png [--reference-downsample 2] [--crop x y w h] [--json]

Эталон — тот же кадр, снятый в k раз большем разрешении (обычно 2560×1440 для 1280×720); он усредняется блоками k×k до размера кадра.
Метрики по яркости (sRGB-значения, 0..255):
  gradient_ratio — энергия градиента кадра / энергия градиента эталона (1 — резкость как у эталона; <1 — размытие; >1 — ступеньки/перешарп);
  psnr           — пиковое отношение сигнал/шум относительно эталона, дБ (больше — ближе к эталону).
Метрика не заменяет просмотр кадра: сглаживание ступенек уменьшает gradient_ratio, но приближает кадр к эталону (растёт PSNR).
"""
from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path

import numpy as np
from PIL import Image


def load_luma(path: Path) -> np.ndarray:
    rgb = np.asarray(Image.open(path).convert("RGB"), dtype=np.float64)
    return rgb @ np.array([0.2126, 0.7152, 0.0722])


def box_downsample(image: np.ndarray, factor: int) -> np.ndarray:
    if factor < 1:
        raise ValueError("коэффициент уменьшения должен быть ≥ 1")
    if factor == 1:
        return image
    h, w = (image.shape[0] // factor) * factor, (image.shape[1] // factor) * factor
    return image[:h, :w].reshape(h // factor, factor, w // factor, factor).mean(axis=(1, 3))


def crop(image: np.ndarray, box: tuple[int, int, int, int] | None) -> np.ndarray:
    if box is None:
        return image
    x, y, w, h = box
    if w <= 2 or h <= 2 or x < 0 or y < 0 or x + w > image.shape[1] or y + h > image.shape[0]:
        raise ValueError("область вне изображения")
    return image[y:y + h, x:x + w]


def gradient_energy(image: np.ndarray) -> float:
    gx = np.diff(image, axis=1)[:-1, :]
    gy = np.diff(image, axis=0)[:, :-1]
    return float(np.mean(gx * gx + gy * gy))


def psnr(reference: np.ndarray, candidate: np.ndarray) -> float:
    mse = float(np.mean((reference - candidate) ** 2))
    return math.inf if mse == 0 else 10.0 * math.log10(255.0 * 255.0 / mse)


def compare(reference: np.ndarray, candidate: np.ndarray, box: tuple[int, int, int, int] | None = None) -> dict:
    if reference.shape != candidate.shape:
        raise ValueError(f"размеры не совпадают после уменьшения эталона: {reference.shape} и {candidate.shape}")
    ref, cand = crop(reference, box), crop(candidate, box)
    ref_energy, cand_energy = gradient_energy(ref), gradient_energy(cand)
    return {"gradient_energy_reference": ref_energy, "gradient_energy_candidate": cand_energy,
            "gradient_ratio": cand_energy / ref_energy if ref_energy > 0 else math.nan, "psnr": psnr(ref, cand)}


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("reference", type=Path)
    ap.add_argument("candidate", type=Path)
    ap.add_argument("--reference-downsample", type=int, default=1)
    ap.add_argument("--crop", type=int, nargs=4, metavar=("X", "Y", "W", "H"))
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args(argv)
    result = compare(box_downsample(load_luma(args.reference), args.reference_downsample), load_luma(args.candidate),
                     tuple(args.crop) if args.crop else None)
    if args.json:
        print(json.dumps(result))
    else:
        print(f"gradient_ratio {result['gradient_ratio']:.4f}  psnr {result['psnr']:.2f} dB")
    return 0


if __name__ == "__main__":
    sys.exit(main())
