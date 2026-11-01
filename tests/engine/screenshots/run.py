#!/usr/bin/env python3
"""Скриншот-тест сцены (E4, критерий готовности): запускает han_scene_demo в детерминированном режиме, снимает кадры и сравнивает
с эталонами в tests/engine/screenshots/golden.

    python tests/engine/screenshots/run.py --exe build/windows-release/bin/Release/han_scene_demo.exe
    python tests/engine/screenshots/run.py --exe ... --update          # перезаписать эталоны текущими кадрами (после осознанных изменений)
    python tests/engine/screenshots/run.py --exe ... --scene sunset    # одна сцена

Требует видеокарту и окно: на CI без GPU не запускается, запускать у себя перед PR, который трогает рендеринг.
Эталоны сняты на NVIDIA GeForce RTX 2050; на другой видеокарте возможны небольшие отличия (допуски ниже учитывают это).
Код выхода: 0 — все кадры совпали, 1 — есть отличия, 2 — демо не запустилось или не сохранило кадр.
"""
from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import png_tools  # noqa: E402

HERE = Path(__file__).resolve().parent
GOLDEN = HERE / "golden"
FRAMES = "90"            # столько кадров прогоняется до снимка: TAA успевает накопить историю
SIZE = ("640", "360")

# Допуски: кадры детерминированы на одной видеокарте (побайтово), но разные видеокарты округляют по-разному.
MAX_MEAN_ABS = 1.0       # средний модуль отличия по каналам, из 255
MAX_BAD_FRACTION = 0.004  # доля пикселей с отличием больше 24 из 255

# Сцены: имя → аргументы демо. Названия отражают проверяемое: свет, тени, bloom, каскады.
SCENES: dict[str, list[str]] = {
    "noon": ["--sun", "58", "200", "5800"],
    "morning_shadows": ["--camera", "1", "2", "5.5", "-4", "-9"],
    "sunset": ["--sun", "4", "255", "2800", "--exposure", "1.2"],
    "blue_hour": ["--sun", "-5", "255", "4000", "--exposure", "3"],
    "sun_bloom": ["--camera", "0", "3", "11", "60", "9", "--sun", "14", "60", "3600"],
    "cascades_top": ["--camera", "0", "40", "30", "0", "-40", "--cascades"],
    "night": ["--sun", "-30", "0", "4000", "--exposure", "14"],
}


def capture(exe: Path, name: str, out: Path) -> None:
    command = [str(exe), "--frames", FRAMES, "--deterministic", "--no-ui", "--no-capture", "--size", *SIZE, "--screenshot", str(out), *SCENES[name]]
    out.parent.mkdir(parents=True, exist_ok=True)
    if out.exists():
        out.unlink()
    result = subprocess.run(command, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=180)
    if result.returncode not in (0,) or not out.exists():
        raise RuntimeError(f"{name}: демо завершилось с кодом {result.returncode}, кадр {'есть' if out.exists() else 'не сохранён'}\n"
                           f"{(result.stdout + result.stderr).strip()[-600:]}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Скриншот-тест сцены")
    parser.add_argument("--exe", required=True, type=Path, help="путь к han_scene_demo.exe")
    parser.add_argument("--update", action="store_true", help="перезаписать эталоны текущими кадрами")
    parser.add_argument("--scene", choices=sorted(SCENES), help="только одна сцена")
    parser.add_argument("--out", type=Path, default=HERE.parents[2] / "build" / "screenshots", help="куда складывать кадры и картинки различий")
    args = parser.parse_args(argv)

    if not args.exe.is_file():
        print(f"нет файла {args.exe}: соберите проект (cmake --preset … и --build)", file=sys.stderr)
        return 2
    names = [args.scene] if args.scene else list(SCENES)
    failed, errors = [], 0
    for name in names:
        actual_path = args.out / f"{name}.actual.png"
        try:
            capture(args.exe, name, actual_path)
        except (RuntimeError, subprocess.TimeoutExpired) as e:
            print(f"ОШИБКА {e}", file=sys.stderr)
            errors += 1
            continue
        actual = png_tools.read_png(actual_path)
        golden_path = GOLDEN / f"{name}.png"
        if args.update:
            png_tools.write_png(golden_path, actual)
            print(f"{name}: эталон обновлён ({golden_path.stat().st_size // 1024} КБ)")
            continue
        if not golden_path.is_file():
            print(f"{name}: НЕТ эталона {golden_path.name} — запустите с --update и закоммитьте его", file=sys.stderr)
            failed.append(name)
            continue
        expected = png_tools.read_png(golden_path)
        try:
            diff = png_tools.compare(actual, expected)
        except ValueError as e:
            print(f"{name}: ОТЛИЧАЕТСЯ — {e}", file=sys.stderr)
            failed.append(name)
            continue
        ok = diff.mean_abs <= MAX_MEAN_ABS and diff.bad_fraction <= MAX_BAD_FRACTION
        print(f"{name}: {'совпало' if ok else 'ОТЛИЧАЕТСЯ'} (среднее {diff.mean_abs:.3f}/255, плохих пикселей {diff.bad_fraction * 100:.3f}%, макс. {diff.max_abs})")
        if not ok:
            failed.append(name)
            png_tools.write_png(args.out / f"{name}.diff.png", png_tools.difference_image(actual, expected))
            print(f"   кадр: {actual_path}\n   различия: {args.out / (name + '.diff.png')}", file=sys.stderr)
    if errors:
        return 2
    if failed:
        print(f"Не совпали: {', '.join(failed)}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
