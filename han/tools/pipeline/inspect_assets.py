"""Полная опись локальных ассетов и безопасная распаковка, без запуска кода из архивов.

python han/tools/pipeline/inspect_assets.py <папка> --out assets_bin/review/new --report build/new_assets_review.json
"""
from __future__ import annotations
import argparse
import hashlib
import json
import re
import subprocess
import zipfile
from pathlib import Path

import fetcher

ARCHIVES = {'.zip', '.rar', '.7z', '.unitypackage'}
MODELS = {'.glb', '.gltf', '.fbx', '.obj', '.blend', '.3ds', '.dae', '.ply', '.stl'}
IMAGES = {'.png', '.jpg', '.jpeg', '.tga', '.exr', '.hdr', '.bmp', '.tif', '.tiff', '.dds'}


def digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda: source.read(8 * 1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def extract(archive: Path, target: Path) -> None:
    if archive.suffix.lower() == '.zip':
        fetcher.extract_zip(archive, target)
        return
    listing = subprocess.run(['tar', '-tf', str(archive)], capture_output=True, text=True, encoding='utf-8', errors='replace', check=True)
    names = listing.stdout.splitlines()
    if len(names) > fetcher.MAX_ARCHIVE_FILES:
        raise ValueError('слишком много файлов в архиве')
    root = target.resolve()
    for name in names:
        relative = fetcher.safe_member_path(name)
        if not (target / Path(*relative.parts)).resolve().is_relative_to(root):
            raise ValueError('выход за каталог распаковки')
    details = subprocess.run(['tar', '-tvf', str(archive)], capture_output=True, text=True, encoding='utf-8', errors='replace', check=True)
    if any(line.lstrip().startswith(('l', 'h')) or ' link to ' in line or ' -> ' in line for line in details.stdout.splitlines()):
        raise ValueError('ссылки внутри архива не допускаются')
    target.mkdir(parents=True, exist_ok=True)
    subprocess.run(['tar', '-xf', str(archive), '-C', str(target)], check=True, capture_output=True)


def inspect_folder(folder: Path) -> dict:
    result = {'models': [], 'images': [], 'legal': [], 'formats': {}}
    for file in sorted(folder.rglob('*')):
        if not file.is_file() or '__MACOSX' in file.parts:
            continue
        relative = file.relative_to(folder).as_posix()
        suffix = file.suffix.lower()
        result['formats'][suffix] = result['formats'].get(suffix, 0) + 1
        if suffix in MODELS:
            result['models'].append({'file': relative, 'bytes': file.stat().st_size})
        if suffix in IMAGES:
            image = {'file': relative, 'bytes': file.stat().st_size}
            if suffix not in {'.exr', '.hdr'}:
                try:
                    from PIL import Image
                    with Image.open(file) as pixels:
                        image.update(width=pixels.width, height=pixels.height, mode=pixels.mode)
                        pixels.verify()
                except Exception as error:
                    image['error'] = str(error)
            result['images'].append(image)
        if suffix in {'.md', '.txt'} and any(word in file.name.lower() for word in ('license', 'licence', 'readme', 'credit')) and file.stat().st_size < 200000:
            result['legal'].append({'file': relative, 'text': file.read_text(encoding='utf-8', errors='replace')})
    return result


def scan(source: Path, out: Path, report: Path) -> list[dict]:
    source, out = source.resolve(), out.resolve()
    if source == out or out.is_relative_to(source):
        raise ValueError('распакованные файлы должны лежать вне исходной папки')
    records, seen = [], {}
    for index, file in enumerate(sorted(p for p in source.rglob('*') if p.is_file())):
        sha = digest(file)
        key = re.sub(r'[^a-z0-9_-]+', '_', file.stem.lower()).strip('_')[:70] + '_' + sha[:10]
        record = {'source': str(file), 'relative': file.relative_to(source).as_posix(), 'sha256': sha, 'bytes': file.stat().st_size, 'id': key}
        if sha in seen:
            record['duplicate_of'] = seen[sha]
        else:
            seen[sha] = key
            target = out / key
            record['folder'] = str(target)
            marker = target / '.han-inspection.json'
            if marker.exists():
                record.update(json.loads(marker.read_text(encoding='utf-8')))
            else:
                target.mkdir(parents=True, exist_ok=True)
                try:
                    if file.suffix.lower() in ARCHIVES:
                        extract(file, target)
                    else:
                        import shutil
                        shutil.copy2(file, target / file.name)
                    pending = [(p, 0) for p in sorted(target.rglob('*')) if p.suffix.lower() in ARCHIVES and p.is_file()]
                    completed = set()
                    while pending:
                        archive, depth = pending.pop(0)
                        if archive in completed:
                            continue
                        if depth >= 5:
                            raise ValueError('вложенность архивов больше пяти')
                        completed.add(archive)
                        nested = archive.with_name(archive.name + '.unpacked')
                        extract(archive, nested)
                        pending += [(p, depth + 1) for p in sorted(nested.rglob('*')) if p.suffix.lower() in ARCHIVES and p.is_file()]
                    record.update(inspect_folder(target))
                    record['archives_opened'] = len(completed) + (file.suffix.lower() in ARCHIVES)
                except Exception as error:
                    record['error'] = str(error)
                # Ошибки не кэшируем: повторный запуск должен позволять исправить недостающий декодер.
                if 'error' not in record:
                    marker.write_text(json.dumps({k: v for k, v in record.items() if k not in {'source', 'relative', 'sha256', 'bytes', 'id', 'folder'}}, ensure_ascii=False, indent=2), encoding='utf-8')
        records.append(record)
        report.parent.mkdir(parents=True, exist_ok=True)
        report.write_text(json.dumps(records, ensure_ascii=False, indent=2), encoding='utf-8')
        print(f'{index + 1}: {record["relative"]}: {len(record.get("models", []))} моделей, {len(record.get("images", []))} картинок' + (' ERROR ' + record['error'] if 'error' in record else ''), flush=True)
    return records


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    records = scan(args.source, args.out, args.report)
    raise SystemExit(1 if any('error' in item for item in records) else 0)
