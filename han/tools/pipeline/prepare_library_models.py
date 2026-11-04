"""Подготовка уникальных комплектов библиотеки: GLB, LOD, коллизии; skin сохраняется отдельно."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

import han_fetch


def choose_model(models):
    order = {'.glb': 0, '.gltf': 1, '.blend': 2, '.fbx': 3, '.obj': 4, '.ply':5, '.stl':6, '.3ds':7, '.dae':8}
    valid = [m['file'] for m in models if Path(m['file']).suffix.lower() in order]
    return min(valid, key=lambda value: (order[Path(value).suffix.lower()], len(Path(value).parts), len(value), value)) if valid else None


def variant_models(models):
    portable = [m for m in models if Path(m['file']).suffix.lower() in {'.glb','.gltf'}]
    if portable:
        return portable
    for suffix in ('.blend','.fbx','.obj','.ply','.stl','.3ds','.dae'):
        values = [m for m in models if Path(m['file']).suffix.lower()==suffix]
        if values: return values
    return []


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--library', type=Path, default=Path('assets_bin/library/manifest.json'))
    parser.add_argument('--out', type=Path, default=Path('assets_bin/prepared/models'))
    parser.add_argument('--profile', type=Path, default=Path('data/city/library_preparation.json'))
    parser.add_argument('--dependencies', type=Path, help='полный каталог для поиска текстур соседних комплектов')
    parser.add_argument('--only-prefix', default='')
    parser.add_argument('--retry-failed', action='store_true', help='повторить только ещё не подготовленные комплекты')
    parser.add_argument('--variants-only', action='store_true', help='дополнительные модели комплекта, без дублей других форматов')
    args = parser.parse_args()
    root = han_fetch.REPO_ROOT
    config = json.loads(args.profile.read_text(encoding='utf-8'))
    blender = han_fetch.find_blender()
    if not blender:
        raise RuntimeError('Blender не найден')
    results = []
    packages = json.loads(args.library.read_text(encoding='utf-8'))['packages']
    if args.variants_only:
        variants = []
        for package in packages:
            primary = choose_model(package['models'])
            for model in variant_models(package['models']):
                if model['file']==primary: continue
                variant = dict(package)
                variant['id'] = package['id'][:48]+'_v_'+hashlib.sha256(model['file'].encode()).hexdigest()[:10]
                variant['models'] = [model]
                variant['original_relative'] += ' :: '+model['file']
                variants.append(variant)
        packages = variants
    for package in packages:
        if not package['models'] or not package['original_relative'].startswith(args.only_prefix):
            continue
        filename = choose_model(package['models'])
        record = {'id': package['id'], 'original_relative': package['original_relative'], 'source_sha256': package['sha256'],
                  'owner_authorized': bool(package.get('approved_by_owner') or package['status']=='approved'), 'source_file': filename}
        output = args.out/package['id']
        output.mkdir(parents=True, exist_ok=True)
        previous = output/'prepared.json'
        if args.retry_failed and previous.exists():
            cached = json.loads(previous.read_text(encoding='utf-8'))
            if cached.get('state') == 'prepared' and cached.get('source_sha256') == package['sha256']:
                results.append(cached)
                (args.out/'index.json').write_text(json.dumps(results, ensure_ascii=False, indent=2), encoding='utf-8')
                continue
        if not filename:
            record.update(state='needs_conversion', notes='Нативный формат: использовать проверенный альтернативный экспорт.')
        else:
            name = 'AS_' + package['id']
            cfg = dict(profile='library_auto', scale=1, pivot='base_center', max_materials=config['max_materials'],
                       max_texture_size=config['max_texture_size'], lods=config['lods'], collision=True)
            label = package['original_relative'].lower()
            cfg['budget_triangles'] = config['plant_triangles'] if '/plant' in label else config['building_triangles'] if package['category']=='city' else config['prop_triangles']
            if any(word in label for word in ('/city ', 'kandam_district', 'modular-kit')):
                cfg.update(budget_triangles=config['scene_triangles'], collision=False, pivot='keep')
            cfg.update(config['overrides'].get(Path(package['original_relative']).name, {}))
            cfg.update(config.get('path_overrides', {}).get(package['original_relative'], {}))
            params = {'input': str((root/package['content']/filename).resolve()), 'output_dir': str(output.resolve()),
                      'name': name, 'report': str((output/'report.json').resolve()), 'pipeline': cfg,
                      'brand_blocklist': str(root/'han/tools/pipeline/real_brands.txt')}
            source=Path(params['input'])
            with source.open('rb') as stream: header=stream.read(32)
            old_fbx = source.suffix.lower()=='.fbx' and (not header.startswith(b'Kaydara FBX Binary') or (len(header)>=27 and int.from_bytes(header[23:27],'little')<7100))
            if source.suffix.lower() in {'.3ds','.dae'} or old_fbx:
                converted=output/'legacy/model.obj'
                converted.parent.mkdir(parents=True,exist_ok=True)
                try:
                    conversion=subprocess.run([sys.executable,str(root/'han/tools/pipeline/convert_legacy_model.py'),str(source),str(converted),
                                           '--textures-root',str((root/package['content']).resolve()),'--dependencies',str((args.dependencies or args.library).resolve())],
                                          stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf-8',errors='replace',timeout=config['timeout_seconds'])
                except subprocess.TimeoutExpired:
                    record.update(state='needs_review',notes='Декодер старого формата превысил лимит времени; исходник сохранён.')
                    results.append(record)
                    (args.out/'index.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8')
                    print(len(results),record['state'],package['original_relative'],flush=True)
                    continue
                (output/'legacy/conversion.log').write_text(conversion.stdout,encoding='utf-8')
                if conversion.returncode!=0:
                    record.update(state='needs_review',notes=conversion.stdout[-1000:])
                    results.append(record)
                    (args.out/'index.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8')
                    print(len(results),record['state'],package['original_relative'],flush=True)
                    continue
                params['input']=str(converted.resolve())
            fingerprint = hashlib.sha256((json.dumps(params, sort_keys=True)+package['sha256']+(root/'han/tools/blender/han_pipeline.py').read_text(encoding='utf-8')).encode()).hexdigest()
            marker = output/'prepared.json'
            if marker.exists() and json.loads(marker.read_text(encoding='utf-8')).get('state')=='prepared' and json.loads(marker.read_text(encoding='utf-8')).get('fingerprint')==fingerprint:
                record = json.loads(marker.read_text(encoding='utf-8'))
            else:
                (output/'params.json').write_text(json.dumps(params, ensure_ascii=False, indent=2), encoding='utf-8')
                with (output/'blender.log').open('w', encoding='utf-8') as log:
                    try:
                        process = subprocess.run([blender,'--background','--factory-startup','--disable-autoexec','--python-exit-code','1',
                                                  '--python',str(root/'han/tools/blender/han_pipeline.py'),'--',str((output/'params.json').resolve())],
                                                 stdout=log, stderr=subprocess.STDOUT, timeout=config['timeout_seconds'])
                        record['state'] = 'prepared' if process.returncode==0 and (output/'report.json').exists() else 'needs_review'
                    except subprocess.TimeoutExpired:
                        record.update(state='needs_review', notes='Превышен лимит времени; оригинал сохранён.')
                if record['state']=='prepared':
                    result = json.loads((output/'report.json').read_text(encoding='utf-8'))
                    conversion_note = output/'legacy/model.conversion.json'
                    if conversion_note.exists():
                        result['conversion'] = json.loads(conversion_note.read_text(encoding='utf-8'))
                        if result['conversion'].get('material_status') == 'neutral_replacement_required':
                            result.setdefault('warnings', []).append('FBX 6.x: геометрия/UV сохранены, исходные пустые слоты материалов заменены нейтральным; необходима художественная подготовка.')
                        (output/'report.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
                    for value in result['files'].values():
                        if not (output/value).is_file(): raise RuntimeError('Отсутствует результат '+value)
                    record['report'] = result
                record.update(fingerprint=fingerprint, output=output.as_posix())
                marker.write_text(json.dumps(record, ensure_ascii=False, indent=2), encoding='utf-8')
        results.append(record)
        (args.out/'index.json').write_text(json.dumps(results, ensure_ascii=False, indent=2), encoding='utf-8')
        print(len(results),record['state'],package['original_relative'],flush=True)
    return 0 if all(r['state']=='prepared' for r in results) else 1


if __name__ == '__main__':
    sys.exit(main())
