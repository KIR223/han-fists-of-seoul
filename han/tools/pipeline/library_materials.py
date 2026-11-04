"""Карты пользовательской библиотеки → единые PBR-тройки. Оригиналы не изменяются."""
import argparse
import json
import re
from pathlib import Path

from PIL import Image, ImageOps


def select_maps(files):
    roles = {'base': ('basecolor', 'col', 'albedo', 'diffuse'),
             'normal': ('normalgl', 'normal', 'nrm'),
             'rough': ('roughness', 'rough'), 'gloss': ('gloss', 'glossiness'),
             'metal': ('metallic', 'metalness'), 'ao': ('ambientocclusion', 'ao')}
    result = {}
    for role, names in roles.items():
        matches = []
        for value in files:
            tokens = re.split(r'[_ .-]+', Path(value).stem.casefold())
            if any(token in names for token in tokens):
                if role == 'normal' and any(token in {'dx', 'normaldx'} for token in tokens):
                    continue
                matches.append(value)
        if matches:
            result[role] = sorted(matches, key=lambda x: ('16' in Path(x).stem, len(Path(x).parts), x))[0]
    return result


def prepare(folder, files, output, size=1024):
    if not 16 <= size <= 4096:
        raise ValueError('размер карт должен быть 16..4096')
    maps = select_maps(files)
    if 'base' not in maps:
        raise ValueError('нет карты цвета; комплект масок не является самостоятельным PBR')
    folder, output = Path(folder).resolve(), Path(output)

    def load(role, default, mode='L'):
        if role not in maps:
            return Image.new(mode, (size, size), tuple(default) if isinstance(default, list) else default)
        source = (folder/maps[role]).resolve()
        if not source.is_relative_to(folder):
            raise ValueError('карта вне каталога пакета')
        with Image.open(source) as image:
            return image.convert(mode).resize((size, size), Image.Resampling.LANCZOS)

    base = load('base', 255, 'RGB')
    normal = load('normal', [128, 128, 255], 'RGB')
    rough = load('rough', 200) if 'rough' in maps or 'gloss' not in maps else ImageOps.invert(load('gloss', 0))
    orm = Image.merge('RGB', (load('ao', 255), rough, load('metal', 0)))
    output.mkdir(parents=True, exist_ok=True)
    for name, pixels in [('BC', base), ('N', normal), ('ORM', orm), ('R', rough)]:
        pixels.save(output/(name+'.png'))
    record = {'maps': maps, 'size': size, 'normal_convention': 'OpenGL; NRM/Normal согласно поставке Poliigon',
              'base_color_space': 'sRGB', 'normal_orm_color_space': 'linear', 'gloss_inverted': 'rough' not in maps and 'gloss' in maps}
    (output/'material.json').write_text(json.dumps(record, ensure_ascii=False, indent=2), encoding='utf-8')
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--library', type=Path, default=Path('assets_bin/library/manifest.json'))
    parser.add_argument('--out', type=Path, default=Path('assets_bin/prepared/materials'))
    parser.add_argument('--size', type=int, default=1024)
    args = parser.parse_args()
    records = []
    for package in json.loads(args.library.read_text(encoding='utf-8'))['packages']:
        if not package['original_relative'].startswith('new material and texture/'):
            continue
        item = {'id': package['id'], 'source': package['original_relative'], 'source_sha256': package['sha256'],
                'owner_authorized': bool(package.get('approved_by_owner')), 'authorization': package.get('owner_authorization', ''),
                'license': 'Лицензия поставщика; CC0 не присваивается', 'output': (args.out/package['id']).as_posix()}
        try:
            item.update(prepare(package['content'], [i['file'] for i in package['images']], args.out/package['id'], args.size))
            item['state'] = 'prepared'
        except ValueError as error:
            item.update(state='mask_set', notes=str(error))
        records.append(item)
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out/'index.json').write_text(json.dumps(records, ensure_ascii=False, indent=2), encoding='utf-8')
    print('Материалы:', sum(r['state']=='prepared' for r in records), 'комплекты масок:', sum(r['state']=='mask_set' for r in records))


if __name__ == '__main__':
    main()
