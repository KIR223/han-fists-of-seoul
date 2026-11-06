#!/usr/bin/env python3
"""Воспроизводимые замеры города (этап 01 плана): запускает han_city_demo на контрольных сценах и собирает сводку.

    python han/tools/pipeline/city_baseline.py --exe build/windows-release/bin/Release/han_city_demo.exe \\
        [--scenes data/city/baseline_scenes.json] [--out build/metrics/<имя>] [--only id,id] [--no-vsync] [--hours 13,21]

Для каждой сцены и часа запускается демо с --metrics (JSON), результаты сводятся в summary.json и summary.md.
Сцены с отсутствующими данными (поле requires) помечаются и не сравниваются с замерами владельца.
Запускать без фонового Blender/компиляции; питание, драйвер и частота монитора записываются в среду.
"""
from __future__ import annotations

import argparse
import datetime
import json
import platform
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]


def load_scenes(path: Path) -> dict:
    data = json.loads(path.read_text(encoding="utf-8"))
    if data.get("version") != 1:
        raise ValueError("неподдерживаемая версия файла сцен")
    defaults = data.get("defaults", {})
    seen = set()
    for scene in data.get("scenes", []):
        sid = scene.get("id")
        if not sid or sid in seen:
            raise ValueError(f"пустой или повторный id сцены: {sid!r}")
        seen.add(sid)
        camera = scene.get("camera")
        if not (isinstance(camera, list) and len(camera) == 5 and all(isinstance(v, (int, float)) for v in camera)):
            raise ValueError(f"{sid}: camera должна быть списком из 5 чисел")
        for hour in scene.get("hours", defaults.get("hours", [13])):
            if not 0 <= hour < 24:
                raise ValueError(f"{sid}: час вне 0..24")
        fly = scene.get("fly")
        if fly is not None and not (isinstance(fly, list) and len(fly) == 3 and all(isinstance(v, (int, float)) for v in fly) and fly[2] > 0):
            raise ValueError(f"{sid}: fly = [x, z, скорость > 0]")
    return data


def missing_data(scene: dict, root: Path) -> list[str]:
    return [p for p in scene.get("requires", []) if not (root / p).exists()]


def build_command(exe: str, scene: dict, defaults: dict, hour: float, metrics: Path, no_vsync: bool, screenshot: Path | None = None) -> list[str]:
    size = scene.get("size", defaults.get("size", [1280, 720]))
    cmd = [exe, "--camera", *[str(v) for v in scene["camera"]], "--hour", str(hour),
           "--frames", str(scene.get("frames", defaults.get("frames", 600))), "--warmup", str(scene.get("warmup", defaults.get("warmup", 120))),
           "--size", str(size[0]), str(size[1]), "--traffic", str(scene.get("traffic", defaults.get("traffic", 120))),
           "--deterministic", "--no-ui", "--no-capture", "--metrics", str(metrics)]
    if scene.get("fly"):  # [x, z, скорость м/с]: одинаковый пролёт свободной камеры для замеров стриминга
        cmd += ["--fly", *[str(v) for v in scene["fly"]]]
    if no_vsync:
        cmd.append("--no-vsync")
    if screenshot:
        cmd += ["--screenshot", str(screenshot)]  # снимок последнего кадра: доказательство для таблицы дефектов
    return cmd


def run_text(cmd: list[str]) -> str:
    try:
        return subprocess.run(cmd, capture_output=True, text=True, timeout=30, errors="replace").stdout.strip()
    except (OSError, subprocess.SubprocessError):
        return ""


def environment(root: Path) -> dict:
    env = {"date": datetime.datetime.now().isoformat(timespec="seconds"), "os": platform.platform(), "machine": platform.machine(),
           "git_sha": run_text(["git", "-C", str(root), "rev-parse", "HEAD"]), "git_branch": run_text(["git", "-C", str(root), "branch", "--show-current"]),
           "git_dirty_files": len([l for l in run_text(["git", "-C", str(root), "status", "--porcelain"]).splitlines() if l.strip()])}
    smi = run_text(["nvidia-smi", "--query-gpu=name,driver_version,memory.total,memory.used,power.draw,clocks.gr,clocks.max.gr,pstate", "--format=csv,noheader"])
    env["nvidia_smi"] = smi
    if sys.platform == "win32":
        env["power_scheme"] = run_text(["powercfg", "/getactivescheme"])
        env["display"] = run_text(["powershell", "-NoProfile", "-Command",
                                   "Get-CimInstance Win32_VideoController | ForEach-Object { $_.Name + ' ' + $_.CurrentHorizontalResolution + 'x' + $_.CurrentVerticalResolution + '@' + $_.CurrentRefreshRate + 'Hz' }"])
        env["cpu"] = run_text(["powershell", "-NoProfile", "-Command", "(Get-CimInstance Win32_Processor).Name"])
    return env


def summarize_run(metrics: dict) -> dict:
    keys = ("wall_ms", "cpu_prep_ms", "draw_ms", "gpu_shadow_ms", "gpu_scene_ms", "gpu_total_ms", "draw_calls", "shadow_draw_calls",
            "triangles", "shadow_triangles", "cells_loaded", "cell_requests_per_frame", "cars_simulated", "cars_within_150m", "cars_within_400m", "people_drawn", "working_set_mb", "worst_frames")
    return {k: metrics.get(k) for k in keys} | {"meta": metrics.get("meta", {}), "measured_frames": metrics.get("measured_frames")}


def fmt(row: dict | None, field: str = "mean") -> str:
    if not row or not row.get("count"):
        return "—"
    return f"{row[field]:.2f}"


def render_markdown(summary: dict) -> str:
    env = summary["environment"]
    lines = ["# Замеры города", "", f"Дата: {env['date']}, ветка `{env['git_branch']}`, SHA `{env['git_sha'][:10]}`, изменённых файлов: {env['git_dirty_files']}",
             f"ОС: {env['os']}", f"GPU: {env.get('nvidia_smi', '')}", f"Дисплей: {env.get('display', '')}", f"Питание: {env.get('power_scheme', '')}",
             f"Режим презентации: {summary['present_mode']}", "",
             "| Сцена | Час | Кадр, мс (ср / p95 / p99 / max) | CPU-подготовка | Тени GPU | Сцена GPU | Всего GPU | Вызовов | Треуг., тыс. | Машин всего / ≤150 м / ≤400 м, людей | Данные |",
             "|---|---|---|---|---|---|---|---|---|---|---|"]
    for r in summary["runs"]:
        if r["status"] != "ok":
            lines.append(f"| {r['scene']} | {r['hour']} | — | — | — | — | — | — | — | — | {r['status']}: {r.get('reason', '')} |")
            continue
        m = r["metrics"]
        w = m["wall_ms"]
        note = "нет: " + ", ".join(Path(p).name for p in r["data_missing"]) if r["data_missing"] else "полные"
        lines.append(f"| {r['scene']} | {r['hour']} | {fmt(w)} / {fmt(w, 'p95')} / {fmt(w, 'p99')} / {fmt(w, 'max')} | {fmt(m['cpu_prep_ms'])} | {fmt(m['gpu_shadow_ms'])} | "
                     f"{fmt(m['gpu_scene_ms'])} | {fmt(m['gpu_total_ms'])} | {fmt(m['draw_calls'], 'mean').split('.')[0]} | "
                     f"{float(m['triangles']['mean']) / 1000:.0f} | {fmt(m['cars_simulated']).split('.')[0]} / {fmt(m.get('cars_within_150m')).split('.')[0]} / {fmt(m.get('cars_within_400m')).split('.')[0]}, {fmt(m['people_drawn']).split('.')[0]} | {note} |")
    lines += ["", "Кадр = CPU-подготовка + вызов drawFrame (ожидание GPU). GPU-значения приходят с запаздыванием на кадры в полёте; «Всего GPU» при FIFO может включать ожидание swapchain.", ""]
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", required=True)
    ap.add_argument("--scenes", type=Path, default=REPO_ROOT / "data" / "city" / "baseline_scenes.json")
    ap.add_argument("--out", type=Path)
    ap.add_argument("--only", default="")
    ap.add_argument("--hours", default="")
    ap.add_argument("--no-vsync", action="store_true")
    ap.add_argument("--shots", action="store_true", help="сохранять PNG последнего кадра каждой сцены рядом с метриками")
    ap.add_argument("--root", type=Path, default=REPO_ROOT, help="корень репозитория (демо запускается оттуда)")
    args = ap.parse_args(argv)

    args.exe = str(Path(args.exe).resolve())  # демо запускается из корня репозитория, относительный путь потерялся бы
    args.root = args.root.resolve()
    data = load_scenes(args.scenes)
    defaults = data.get("defaults", {})
    only = {s for s in args.only.split(",") if s}
    hours_override = [float(h) for h in args.hours.split(",") if h]
    out = (args.out or args.root / "build" / "metrics" / datetime.datetime.now().strftime("%Y%m%d-%H%M%S")).resolve()
    out.mkdir(parents=True, exist_ok=True)
    runs = []
    present_mode = ""
    for scene in data["scenes"]:
        if only and scene["id"] not in only:
            continue
        gaps = missing_data(scene, args.root)
        for hour in hours_override or scene.get("hours", defaults.get("hours", [13])):
            run = {"scene": scene["id"], "title": scene.get("title", ""), "hour": hour, "camera": scene["camera"], "data_missing": gaps}
            if gaps and scene.get("blocked_without_data"):
                run |= {"status": "blocked", "reason": "нет данных: " + ", ".join(gaps)}
                runs.append(run)
                continue
            metrics_path = out / f"{scene['id']}-h{int(hour)}.json"
            shot = out / f"{scene['id']}-h{int(hour)}.png" if args.shots else None
            cmd = build_command(args.exe, scene, defaults, hour, metrics_path, args.no_vsync, shot)
            print("запуск:", scene["id"], "час", hour, flush=True)
            proc = subprocess.run(cmd, cwd=args.root, capture_output=True, text=True, errors="replace")
            (out / f"{scene['id']}-h{int(hour)}.log").write_text(proc.stdout + proc.stderr, encoding="utf-8")
            if proc.returncode != 0 or not metrics_path.exists():
                run |= {"status": "failed", "reason": f"код возврата {proc.returncode}"}
            else:
                metrics = json.loads(metrics_path.read_text(encoding="utf-8"))
                present_mode = metrics.get("meta", {}).get("present_mode", present_mode)
                run |= {"status": "ok", "metrics": summarize_run(metrics)}
            runs.append(run)
    summary = {"environment": environment(args.root), "present_mode": present_mode, "scenes_file": str(args.scenes), "runs": runs}
    (out / "summary.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8")
    (out / "summary.md").write_text(render_markdown(summary), encoding="utf-8")
    print("сводка:", out / "summary.md")
    return 0 if all(r["status"] in ("ok", "blocked") for r in runs) else 1


if __name__ == "__main__":
    sys.exit(main())
