#!/usr/bin/env python3
"""han_fetch — конвейер ассетов из интернета (этап E20, часть 76 дизайна).

Одна команда от ссылки до игрового ассета и строки в docs/credits:

    python han/tools/pipeline/han_fetch.py add --id pr_barrel --name PR_Barrel --zone A --kind model \\
        --source polyhaven --asset-id barrel_01 --page https://polyhaven.com/a/barrel_01 \\
        --author "Автор" --license CC0 --used-in "пропсы улиц" --modified "упрощён, LOD, коллизия" --process

Команды: add (занести в манифест), fetch (скачать и проверить хэш), process (Blender → han_cooker → кредиты),
verify (проверить манифест и кредиты), all (весь манифест с нуля), list.
Скачанное лежит в assets_bin/downloads/<id>, переделанное — assets_bin/processed/<id>, готовое — assets_bin/cooked (не в Git).
"""
from __future__ import annotations

import argparse
import datetime
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any
from urllib.parse import urlparse

sys.path.insert(0, str(Path(__file__).resolve().parent))

import credits as credits_mod  # noqa: E402
import fetcher  # noqa: E402
import license_rules  # noqa: E402
import manifest as manifest_mod  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parents[3]
MODEL_EXTENSIONS = (".glb", ".gltf", ".fbx", ".obj")


class PipelineError(Exception):
    pass


# ----- Пути ---------------------------------------------------------------------------------------------------------------

class Paths:
    def __init__(self, root: Path):
        self.root = root
        self.manifest = root / "assets_src" / "manifest.json"
        self.downloads = root / "assets_bin" / "downloads"
        self.processed = root / "assets_bin" / "processed"
        self.cooked = root / "assets_bin" / "cooked"
        self.credits = root / "docs" / "credits"
        self.blender_script = root / "han" / "tools" / "blender" / "han_pipeline.py"
        self.brand_blocklist = root / "han" / "tools" / "pipeline" / "real_brands.txt"


# ----- Поиск внешних инструментов -----------------------------------------------------------------------------------------

def find_blender() -> str | None:
    env = os.environ.get("HAN_BLENDER")
    if env and Path(env).is_file():
        return env
    found = shutil.which("blender")
    if found:
        return found
    for base in (os.environ.get("ProgramFiles", r"C:\Program Files"), os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")):
        folder = Path(base) / "Blender Foundation"
        if folder.is_dir():
            candidates = sorted(p / "blender.exe" for p in folder.iterdir() if (p / "blender.exe").is_file())
            if candidates:
                return str(candidates[-1])
    return None


def find_cooker(root: Path) -> list[str] | None:
    env = os.environ.get("HAN_COOKER")
    if env:
        return [env]
    for pattern in ("build/*/bin/*/han_cooker.exe", "build/*/bin/han_cooker", "build/*/bin/*/han_cooker"):
        matches = sorted(root.glob(pattern))
        if matches:
            return [str(matches[-1])]
    return None


# ----- Манифест -----------------------------------------------------------------------------------------------------------

def load_manifest(paths: Paths) -> dict[str, Any]:
    data = manifest_mod.load(paths.manifest)
    errors = manifest_mod.validate(data)
    if errors:
        raise PipelineError("манифест не прошёл проверку:\n  " + "\n  ".join(errors))
    return data


def gate_license(asset: dict[str, Any]) -> None:
    verdict = manifest_mod.license_verdict(asset)
    if verdict == license_rules.FORBIDDEN:
        raise PipelineError(f"{asset['id']}: лицензия «{asset['license']}» не разрешена")
    if verdict == license_rules.NEEDS_OWNER and not asset.get("approved_by_owner"):
        raise PipelineError(f"{asset['id']}: лицензия «{asset['license']}» требует одобрения владельца (метка legal)")


# ----- Загрузка -----------------------------------------------------------------------------------------------------------

def fetch_asset(asset: dict[str, Any], paths: Paths, update_hash: bool = False, force: bool = False) -> bool:
    """Скачивает ассет в assets_bin/downloads/<id>. Возвращает True, если в манифесте изменился sha256."""
    gate_license(asset)
    dest = paths.downloads / asset["id"]
    expected = asset.get("sha256", "")
    if dest.is_dir() and expected and not force and fetcher.tree_hash(dest) == expected:
        print(f"{asset['id']}: уже скачан, хэш совпадает")
        return False

    if dest.exists():
        shutil.rmtree(dest)
    dest.mkdir(parents=True)
    source = asset["source"]
    kind = source["type"]
    try:
        if kind == "direct":
            name = source.get("filename") or Path(urlparse(source["url"]).path).name or "download.bin"
            fetcher.safe_member_path(name)
            target = dest / name
            fetcher.download(source["url"], target)
            if name.lower().endswith(".zip"):
                fetcher.extract_zip(target, dest)
                target.unlink()
        elif kind == "polyhaven":
            resolver = fetcher.resolve_polyhaven_texture if asset.get("kind") == "texture" else fetcher.resolve_polyhaven
            resolved = resolver(source["asset_id"], source.get("resolution", "1k"),
                                os.environ.get("HAN_POLYHAVEN_API", fetcher.POLYHAVEN_API))
            _check_meta(asset, resolved.meta)
            for url, rel in resolved.files:
                target = dest / Path(*fetcher.safe_member_path(rel).parts)
                fetcher.download(url, target)
        elif kind == "sketchfab":
            resolved = fetcher.resolve_sketchfab(source["asset_id"], os.environ.get("SKETCHFAB_TOKEN", ""),
                                                 os.environ.get("HAN_SKETCHFAB_API", fetcher.SKETCHFAB_API))
            _check_meta(asset, resolved.meta)
            for url, rel in resolved.files:
                target = dest / rel
                fetcher.download(url, target)  # подписанная ссылка: токен Sketchfab на другой хост не отправляется
                if rel.lower().endswith(".zip"):
                    fetcher.extract_zip(target, dest)
                    target.unlink()
        elif kind == "local":
            src = Path(source["file"])
            if not src.is_absolute():
                src = paths.root / src
            if not src.is_file():
                raise PipelineError(f"{asset['id']}: локальный файл не найден: {src}")
            shutil.copy2(src, dest / src.name)
            if src.suffix.lower() == ".zip":
                fetcher.extract_zip(dest / src.name, dest)
                (dest / src.name).unlink()
    except (fetcher.FetchError, OSError) as e:
        shutil.rmtree(dest, ignore_errors=True)
        raise PipelineError(f"{asset['id']}: {e}") from e

    digest = fetcher.tree_hash(dest)
    if expected and digest != expected and not update_hash:
        shutil.rmtree(dest, ignore_errors=True)
        raise PipelineError(f"{asset['id']}: хэш не совпал (манифест {expected[:12]}…, скачано {digest[:12]}…). "
                            "Источник изменился? Проверьте лицензию и запустите с --update-hash")
    changed = digest != expected
    asset["sha256"] = digest
    print(f"{asset['id']}: скачан, sha256 {digest[:16]}…")
    return changed


def _check_meta(asset: dict[str, Any], meta: dict[str, str]) -> None:
    """Сверяет то, что сообщил источник, с манифестом: лицензия источника не должна быть хуже заявленной."""
    reported = meta.get("license", "")
    if reported and license_rules.classify(reported) == license_rules.FORBIDDEN:
        raise PipelineError(f"{asset['id']}: источник сообщает лицензию «{reported}» — она не разрешена")
    if reported and license_rules.classify(reported) != manifest_mod.license_verdict(asset):
        print(f"ВНИМАНИЕ {asset['id']}: источник сообщает лицензию «{reported}», в манифесте «{asset['license']}» — сверьте со страницей")


# ----- Обработка ----------------------------------------------------------------------------------------------------------

def find_entry(download_dir: Path, wanted: str) -> Path:
    if wanted:
        try:
            entry = download_dir / Path(*fetcher.safe_member_path(wanted).parts)
        except fetcher.FetchError as e:
            raise PipelineError(f"неверный путь главного файла: {wanted}") from e
        if not entry.is_file():
            raise PipelineError(f"главный файл не найден: {wanted}")
        return entry
    for extension in MODEL_EXTENSIONS:
        matches = sorted((p for p in download_dir.rglob("*") if p.suffix.lower() == extension), key=lambda p: (len(p.parts), str(p)))
        if matches:
            return matches[0]
    raise PipelineError(f"в {download_dir} нет модели ({', '.join(MODEL_EXTENSIONS)})")


def run_blender(blender: str, paths: Paths, asset: dict[str, Any], entry: Path, out_dir: Path) -> dict[str, Any]:
    out_dir.mkdir(parents=True, exist_ok=True)
    params = {
        "input": str(entry),
        "output_dir": str(out_dir),
        "name": asset["name"],
        "report": str(out_dir / "report.json"),
        "pipeline": manifest_mod.pipeline_of(asset),
        "brand_blocklist": str(paths.brand_blocklist),
    }
    params_file = out_dir / "params.json"
    params_file.write_text(json.dumps(params, ensure_ascii=False, indent=2), encoding="utf-8")
    command = [blender, "--background", "--python", str(paths.blender_script), "--", str(params_file)]
    result = subprocess.run(command, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=3600)
    if result.returncode != 0:
        tail = "\n".join((result.stdout + result.stderr).splitlines()[-15:])
        raise PipelineError(f"{asset['id']}: Blender завершился с кодом {result.returncode}:\n{tail}")
    return json.loads((out_dir / "report.json").read_text(encoding="utf-8"))


def cook_all(cooker: list[str], processed: Path, report: dict[str, Any], cooked: Path) -> list[Path]:
    cooked.mkdir(parents=True, exist_ok=True)
    outputs = []
    for key, glb_name in report["files"].items():
        # LOD0 → <Имя>.hmesh, LODn → <Имя>.lodN.hmesh, коллизия → <Имя>.col.hmesh
        out = cooked / (Path(glb_name).stem + ".hmesh")
        result = subprocess.run([*cooker, str(processed / glb_name), str(out)], capture_output=True, text=True, encoding="utf-8", errors="replace")
        if result.returncode != 0:
            raise PipelineError(f"han_cooker не смог сварить {glb_name}:\n{(result.stdout + result.stderr).strip()}")
        outputs.append(out)
    return outputs


def check_report(asset: dict[str, Any], report: dict[str, Any]) -> list[str]:
    """Шаг 9 «Проверка»: бюджет, бренды. Возвращает предупреждения; фатальное бросает PipelineError."""
    budget = manifest_mod.pipeline_of(asset)["budget_triangles"]
    lod0 = report["triangles"]["lod0"]
    if lod0 > budget:
        raise PipelineError(f"{asset['id']}: {lod0} треугольников при бюджете {budget}")
    hits = report.get("brand_hits", [])
    if hits and not asset.get("brand_review"):
        listing = ", ".join(f"{h['name']} (~{h['word']})" for h in hits)
        raise PipelineError(f"{asset['id']}: возможные реальные бренды в именах: {listing}. "
                            "Проверьте модель и текстуры глазами, замените или уберите, затем впишите в манифест brand_review "
                            "с описанием, что сделано (часть 76.4, шаг 6)")
    return list(report.get("warnings", []))


def process_texture(asset: dict[str, Any], paths: Paths, cooker: list[str] | None, write_credits: bool) -> dict[str, Any]:
    """Текстура: JPG/PNG из загрузки → .ktx2 с мипами в assets_bin/cooked/<id>/ (альбедо — sRGB, остальное — линейное)."""
    if not cooker:
        raise PipelineError("han_cooker не найден: соберите проект (cmake --preset …) или задайте путь в переменной HAN_COOKER")
    download_dir = paths.downloads / asset["id"]
    if not download_dir.is_dir():
        fetch_asset(asset, paths)
    out_dir = paths.cooked / asset["id"]
    out_dir.mkdir(parents=True, exist_ok=True)
    produced = []
    for image in sorted(download_dir.iterdir()):
        if image.suffix.lower() not in (".jpg", ".jpeg", ".png", ".tga", ".bmp"):
            continue
        target = out_dir / (image.stem + ".ktx2")
        command = cooker + ["--texture"] + ([] if image.stem.startswith("diff") else ["--linear"]) + [str(image), str(target)]
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode != 0:
            raise PipelineError(f"{asset['id']}: han_cooker не смог обработать {image.name}: {result.stderr.strip()}")
        produced.append(target.name)
    if "diff.ktx2" not in produced:
        raise PipelineError(f"{asset['id']}: в загрузке нет альбедо (diff.*)")
    if write_credits and credits_mod.ensure_row(paths.credits, asset):
        print(f"{asset['id']}: строка добавлена в docs/credits/{asset['zone']}.md")
    print(f"{asset['id']}: готово — {', '.join(produced)}")
    return {"textures": produced, "triangles": {"lod0": 0}}


def process_asset(asset: dict[str, Any], paths: Paths, blender: str | None, cooker: list[str] | None, write_credits: bool = True,
                  cook: bool = True) -> dict[str, Any]:
    gate_license(asset)
    if asset.get("kind") == "texture":
        return process_texture(asset, paths, cooker, write_credits)
    if asset.get("kind") != "model":
        raise PipelineError(f"{asset['id']}: обработка типа «{asset.get('kind')}» пока не реализована (только model)")
    if not blender:
        raise PipelineError("Blender не найден: установите его или задайте путь в переменной HAN_BLENDER")
    if cook and not cooker:
        raise PipelineError("han_cooker не найден: соберите проект (cmake --preset …) или задайте путь в переменной HAN_COOKER")

    download_dir = paths.downloads / asset["id"]
    if not download_dir.is_dir():
        fetch_asset(asset, paths)
    entry = find_entry(download_dir, manifest_mod.pipeline_of(asset)["entry"])

    out_dir = paths.processed / asset["id"]
    if out_dir.exists():
        shutil.rmtree(out_dir)
    report = run_blender(blender, paths, asset, entry, out_dir)
    warnings = check_report(asset, report)
    outputs = cook_all(cooker, out_dir, report, paths.cooked) if cook else []
    if write_credits and credits_mod.ensure_row(paths.credits, asset):
        print(f"{asset['id']}: строка добавлена в docs/credits/{asset['zone']}.md")
    for warning in warnings:
        print(f"  предупреждение: {warning}")
    produced = ", ".join(p.name for p in outputs) if cook else f"без варки; GLB в {out_dir.relative_to(paths.root)}"
    print(f"{asset['id']}: готово — {produced}; треугольников LOD0: {report['triangles']['lod0']}")
    return report


# ----- Команды ------------------------------------------------------------------------------------------------------------

def cmd_add(args: argparse.Namespace, paths: Paths) -> int:
    data = manifest_mod.load(paths.manifest)
    source: dict[str, Any] = {"type": args.source}
    if args.page:
        source["page"] = args.page
    if args.url:
        source["url"] = args.url
    if args.asset_id:
        source["asset_id"] = args.asset_id
    if args.file:
        source["file"] = args.file
    asset: dict[str, Any] = {
        "id": args.id, "name": args.name, "kind": args.kind, "zone": args.zone, "source": source,
        "author": args.author, "license": args.license, "retrieved": args.retrieved or datetime.date.today().isoformat(),
        "sha256": "", "used_in": args.used_in, "modified": args.modified,
    }
    if args.license_url:
        asset["license_url"] = args.license_url
    if args.budget:
        asset["pipeline"] = {"budget_triangles": args.budget}
    if manifest_mod.find(data, args.id):
        raise PipelineError(f"ассет {args.id} уже есть в манифесте")
    data["assets"].append(asset)
    errors = manifest_mod.validate(data)
    if errors:
        raise PipelineError("ассет не принят:\n  " + "\n  ".join(errors))
    manifest_mod.save(paths.manifest, data)
    print(f"{args.id}: добавлен в {paths.manifest.relative_to(paths.root)}")
    if args.process:
        return cmd_process(argparse.Namespace(ids=[args.id], all=False, blender=None, cooker=None, no_cook=args.no_cook), paths)
    return 0


def _select(data: dict[str, Any], args: argparse.Namespace) -> list[dict[str, Any]]:
    if getattr(args, "all", False):
        return list(data["assets"])
    selected = []
    for asset_id in args.ids:
        asset = manifest_mod.find(data, asset_id)
        if not asset:
            raise PipelineError(f"в манифесте нет ассета {asset_id}")
        selected.append(asset)
    if not selected:
        raise PipelineError("укажите id ассета или --all")
    return selected


def cmd_fetch(args: argparse.Namespace, paths: Paths) -> int:
    data = load_manifest(paths)
    changed = False
    for asset in _select(data, args):
        changed |= fetch_asset(asset, paths, update_hash=args.update_hash, force=args.force)
    if changed:
        manifest_mod.save(paths.manifest, data)
    return 0


def cmd_process(args: argparse.Namespace, paths: Paths) -> int:
    data = load_manifest(paths)
    blender = args.blender or find_blender()
    cooker = [args.cooker] if args.cooker else find_cooker(paths.root)
    failures = 0
    for asset in _select(data, args):
        try:
            process_asset(asset, paths, blender, cooker, cook=not args.no_cook)
        except PipelineError as e:
            failures += 1
            print(f"ОШИБКА {e}", file=sys.stderr)
    manifest_mod.save(paths.manifest, data)  # мог измениться sha256 после автоматической загрузки
    return 1 if failures else 0


def cmd_all(args: argparse.Namespace, paths: Paths) -> int:
    args.all = True
    args.ids = []
    return cmd_process(args, paths)


def cmd_verify(args: argparse.Namespace, paths: Paths) -> int:
    data = manifest_mod.load(paths.manifest)
    problems = manifest_mod.validate(data)
    for asset in data.get("assets", []):
        if not credits_mod.has_row(paths.credits, asset):
            problems.append(f"{asset.get('id')}: нет строки в docs/credits/{asset.get('zone')}.md (han_fetch.py process добавит)")
        download_dir = paths.downloads / str(asset.get("id", ""))
        if download_dir.is_dir() and asset.get("sha256") and fetcher.tree_hash(download_dir) != asset["sha256"]:
            problems.append(f"{asset['id']}: скачанные файлы не совпадают с хэшем манифеста")
    if problems:
        print("Проблемы:\n  " + "\n  ".join(problems), file=sys.stderr)
        return 1
    print(f"Манифест в порядке: ассетов {len(data.get('assets', []))}")
    return 0


def cmd_list(args: argparse.Namespace, paths: Paths) -> int:
    data = manifest_mod.load(paths.manifest)
    for asset in data.get("assets", []):
        downloaded = "скачан" if (paths.downloads / asset["id"]).is_dir() else "—"
        print(f"{asset['id']:28} {asset['name']:24} {asset['license']:12} {manifest_mod.license_verdict(asset):12} {downloaded}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Конвейер ассетов из интернета (E20)")
    parser.add_argument("--root", type=Path, default=REPO_ROOT, help="корень репозитория")
    sub = parser.add_subparsers(dest="command", required=True)

    add = sub.add_parser("add", help="занести ассет в манифест")
    add.add_argument("--id", required=True)
    add.add_argument("--name", required=True, help="игровое имя по каталогу, например PR_TrashBin")
    add.add_argument("--zone", required=True, choices=manifest_mod.ZONES)
    add.add_argument("--kind", default="model", choices=manifest_mod.KINDS)
    add.add_argument("--source", required=True, choices=manifest_mod.SOURCE_TYPES)
    add.add_argument("--page", help="страница ассета (автор и лицензия проверяются людьми)")
    add.add_argument("--url", help="прямая ссылка (для direct)")
    add.add_argument("--asset-id", help="идентификатор у источника (polyhaven, sketchfab)")
    add.add_argument("--file", help="локальный файл (для local)")
    add.add_argument("--author", required=True)
    add.add_argument("--license", required=True)
    add.add_argument("--license-url")
    add.add_argument("--used-in", required=True, help="где используется (в credits)")
    add.add_argument("--modified", required=True, help="что изменено (в credits)")
    add.add_argument("--retrieved", help="дата ГГГГ-ММ-ДД, по умолчанию сегодня")
    add.add_argument("--budget", type=int, help="бюджет треугольников LOD0 (по умолчанию по префиксу имени)")
    add.add_argument("--process", action="store_true", help="сразу скачать, переделать и сварить")
    add.add_argument("--no-cook", action="store_true", help="с --process: без варки han_cooker (только Blender)")
    add.set_defaults(func=cmd_add)

    for name, func, helptext in (("fetch", cmd_fetch, "скачать и проверить хэш"),
                                 ("process", cmd_process, "переделать в Blender, сварить, записать кредиты")):
        p = sub.add_parser(name, help=helptext)
        p.add_argument("ids", nargs="*")
        p.add_argument("--all", action="store_true")
        if name == "fetch":
            p.add_argument("--update-hash", action="store_true", help="принять новый хэш (источник изменился; сверьте лицензию!)")
            p.add_argument("--force", action="store_true", help="скачать заново")
        else:
            p.add_argument("--blender")
            p.add_argument("--cooker")
            p.add_argument("--no-cook", action="store_true", help="без варки han_cooker (только Blender)")
        p.set_defaults(func=func)

    everything = sub.add_parser("all", help="весь манифест с нуля: скачать, переделать, сварить")
    everything.add_argument("--blender")
    everything.add_argument("--cooker")
    everything.add_argument("--no-cook", action="store_true", help="без варки han_cooker (только Blender)")
    everything.set_defaults(func=cmd_all)
    sub.add_parser("verify", help="проверить манифест, хэши и кредиты").set_defaults(func=cmd_verify)
    sub.add_parser("list", help="список ассетов").set_defaults(func=cmd_list)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        return args.func(args, Paths(args.root.resolve()))
    except (PipelineError, fetcher.FetchError) as e:
        print(f"ОШИБКА {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
