"""Загрузка исходной географии Сеула частями с возобновлением и объединением по OSM ID.

python han/tools/pipeline/han_osm_grid.py --config data/city/seoul_extent.json --out assets_src/osm/seoul.osm
Данные ODbL остаются вне Git. Запросы последовательные, скачанные плитки повторно не запрашиваются.
"""
import argparse
import json
import math
import os
from pathlib import Path
import time
import xml.etree.ElementTree as ET
import han_osm_fetch as osm


def tiles(bbox, step):
    osm.validate_bbox(bbox, allow_large=True)
    if not math.isfinite(step) or not 0 < step <= osm.MAX_SPAN:
        raise ValueError('tile size is outside Overpass limits')
    south, west, north, east = bbox
    rows, columns = math.ceil((north - south) / step), math.ceil((east - west) / step)
    if rows * columns > 200:
        raise ValueError('too many tiles for a city download')
    return [(round(south + row * step, 8), round(west + column * step, 8),
             round(min(north, south + (row + 1) * step), 8), round(min(east, west + (column + 1) * step), 8))
            for row in range(rows) for column in range(columns)]


def merge(paths, destination):
    elements = {}
    for path in paths:
        tree = ET.parse(path)
        root = tree.getroot()
        if root.tag != 'osm' or root.find('remark') is not None:
            raise ValueError('invalid/incomplete OSM document: ' + str(path))
        for element in root:
            if element.tag in {'node', 'way', 'relation'}:
                key = element.tag, int(element.attrib['id'])
                previous = elements.get(key)
                if previous is None or int(element.get('version', '0')) >= int(previous.get('version', '0')):
                    elements[key] = element
    known = {key[1] for key in elements if key[0] == 'node'}
    for (kind, _), element in elements.items():
        if kind == 'way' and any(int(node.attrib['ref']) not in known for node in element.findall('nd')):
            raise ValueError('way has a missing node: ' + element.attrib['id'])
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_suffix(destination.suffix + '.part')
    with temporary.open('wb') as output:
        output.write(b'<?xml version="1.0" encoding="UTF-8"?>\n<osm version="0.6" generator="HAN grid merger">\n')
        for key in sorted(elements, key=lambda k: ({'node': 0, 'way': 1, 'relation': 2}[k[0]], k[1])):
            output.write(ET.tostring(elements[key], encoding='utf-8'))
            output.write(b'\n')
        output.write(b'</osm>\n')
    os.replace(temporary, destination)
    return {kind: sum(key[0] == kind for key in elements) for kind in ('node', 'way', 'relation')}


def run(config, destination, server):
    profile = json.loads(config.read_text(encoding='utf-8'))
    if profile.get('version') != 1:
        raise ValueError('unsupported city extent profile')
    boxes = tiles(profile['bbox'], profile['tile_degrees'])
    pause = profile['pause_seconds']
    if not isinstance(pause, (int, float)) or not math.isfinite(pause) or not 1 <= pause <= 60:
        raise ValueError('download pause must be 1..60 seconds')
    directory = destination.parent / (destination.stem + '_tiles')
    directory.mkdir(parents=True, exist_ok=True)
    files = []
    for index, bbox in enumerate(boxes):
        path = directory / f'{index:03d}.osm'
        stamp = path.with_suffix('.bbox.json')
        cached = path.exists() and stamp.exists() and json.loads(stamp.read_text()) == list(bbox)
        if not cached:
            for attempt in range(3):
                try:
                    data = osm.download(server, osm.build_query(bbox))
                    osm.check_osm(data)
                    osm.write_result(data, path, bbox, server)
                    stamp.write_text(json.dumps(bbox), encoding='utf-8')
                    break
                except osm.FetchError as error:
                    print(f'tile {index + 1}/{len(boxes)} attempt {attempt + 1}: {error}', flush=True)
                    if attempt == 2:
                        raise
                    time.sleep(30 * (attempt + 1))
            time.sleep(pause)
        files.append(path)
        print(f'tile {index + 1}/{len(boxes)}: {path.stat().st_size} bytes' + (' cached' if cached else ''), flush=True)
    stats = merge(files, destination)
    destination.with_name(destination.name + '.license.txt').write_text(
        'OpenStreetMap contributors, ODbL 1.0: https://www.openstreetmap.org/copyright\n'
        f'Source: {server}\nProfile: {config}\nTile provenance: {directory}\n', encoding='utf-8')
    print(stats, flush=True)
    return stats


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--server', default=osm.DEFAULT_SERVER)
    args = parser.parse_args()
    run(args.config, args.out, args.server)
