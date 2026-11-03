"""Выборка геометрии из регионального PBF Geofabrik без зависимости от публичного Overpass.

Требуется osmium (pyosmium). Исходники и полученный OSM — только в игнорируемом assets_src/osm/.
python han/tools/pipeline/han_osm_extract.py <south-korea.osm.pbf> <seoul.osm> --config data/city/seoul_extent.json
"""
import argparse
import hashlib
import json
from pathlib import Path
import os

KEYS = {'highway', 'building', 'height', 'building:levels', 'min_height', 'building:min_level', 'layer', 'bridge',
        'tunnel', 'junction', 'oneway', 'maxspeed', 'width', 'lanes', 'leisure', 'landuse', 'natural', 'waterway',
        'water', 'amenity', 'type', 'area', 'surface', 'access', 'foot', 'motor_vehicle', 'service'}


def overlaps(points, bbox):
    if not points:
        return False
    south, west, north, east = bbox
    latitudes, longitudes = zip(*points)
    return min(latitudes) <= north and max(latitudes) >= south and min(longitudes) <= east and max(longitudes) >= west


def geometry_tags(tags):
    return {key: value for key, value in tags if key in KEYS}


def extract(source, destination, bbox):
    import osmium
    if not source.is_file() or source.resolve() == destination.resolve():
        raise ValueError('input PBF is missing or output would replace the source')
    if len(bbox) != 4 or not (-90 <= bbox[0] < bbox[2] <= 90 and -180 <= bbox[1] < bbox[3] <= 180):
        raise ValueError('invalid geographic bounds')
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_name(destination.stem + '.extracting.osm')
    stats = {'ways': 0, 'highways': 0, 'buildings': 0, 'water': 0, 'invalid_ways': 0}
    selected = osmium.FileProcessor(source).with_locations().with_filter(osmium.filter.EntityFilter(osmium.osm.WAY))
    selected = selected.with_filter(osmium.filter.KeyFilter('highway', 'building', 'natural', 'waterway', 'leisure', 'landuse'))
    # BackReferenceWriter сам добавляет все узлы выбранных линий, в том числе за рамкой: на стыках нет оборванных дорог.
    with osmium.BackReferenceWriter(temporary, source, overwrite=True) as writer:
        for way in selected:
            points = [(node.lat, node.lon) for node in way.nodes if node.location.valid()]
            if not overlaps(points, bbox):
                continue
            if len(points) != len(way.nodes):
                stats['invalid_ways'] += 1
                continue
            tags = geometry_tags((tag.k, tag.v) for tag in way.tags)
            writer.add_way(way.replace(tags=tags))
            stats['ways'] += 1
            stats['highways'] += 'highway' in tags
            stats['buildings'] += 'building' in tags
            stats['water'] += tags.get('natural') == 'water' or tags.get('waterway') == 'riverbank'
            if stats['ways'] % 10000 == 0:
                print(stats, flush=True)
    os.replace(temporary, destination)
    with source.open('rb') as source_file:
        digest = hashlib.file_digest(source_file, 'sha256').hexdigest()
    note = {'source': str(source), 'sha256': digest, 'bbox': bbox, 'stats': stats,
            'attribution': 'OpenStreetMap contributors; Geofabrik GmbH', 'license': 'ODbL-1.0',
            'source_url': 'https://download.geofabrik.de/asia/south-korea.html',
            'license_url': 'https://www.openstreetmap.org/copyright',
            'limitations': 'Отношения-мультиполигоны ещё не преобразуются; исходные названия и бренды удалены.'}
    destination.with_suffix('.source.json').write_text(json.dumps(note, ensure_ascii=False, indent=2), encoding='utf-8')
    print(stats, flush=True)
    return stats


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('destination', type=Path)
    parser.add_argument('--config', type=Path, required=True)
    args = parser.parse_args()
    profile = json.loads(args.config.read_text(encoding='utf-8'))
    extract(args.source, args.destination, profile['bbox'])
