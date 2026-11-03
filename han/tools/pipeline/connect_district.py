"""Соединение поставленного квартала с реальной картой: обрезка старых дорог по рамке и общий периметр.

Новые дороги внутри квартала используются трафиком, их 3D-геометрия уже есть в GLB.
Исходный OSM не меняется. Выходной файл — производные данные ODbL, хранится вне Git.
"""
import argparse
import json
import math
import os
from pathlib import Path
import xml.etree.ElementTree as ET


def outside_segments(points, rect):
    """Части ломаной снаружи прямоугольника; сохраняет пограничные точки для соединения графов."""
    result, current = [], []
    for a, b in zip(points, points[1:]):
        dx, dz = b[0] - a[0], b[1] - a[1]
        lo, hi = 0., 1.
        intersects = True
        for p, q in ((-dx, a[0] - rect[0]), (dx, rect[2] - a[0]), (-dz, a[1] - rect[1]), (dz, rect[3] - a[1])):
            if abs(p) < 1e-12:
                if q < 0:
                    intersects = False; break
            elif p < 0:
                lo = max(lo, q / p)
            else:
                hi = min(hi, q / p)
        intersects = intersects and lo < hi
        pieces = [(a, b)] if not intersects else []
        if intersects and lo > 0:
            pieces.append((a, (a[0] + dx * lo, a[1] + dz * lo)))
        if intersects and hi < 1:
            pieces.append(((a[0] + dx * hi, a[1] + dz * hi), b))
        for start, end in pieces:
            if current and math.dist(current[-1], start) > 1e-6:
                if len(current) > 1: result.append(current)
                current = []
            if not current: current.append(start)
            if math.dist(current[-1], end) > 1e-6: current.append(end)
        if intersects and hi == 1:
            if len(current) > 1: result.append(current)
            current = []
    if len(current) > 1: result.append(current)
    return result


def geographic_projection(latitude, longitude):
    phi = math.radians(latitude)
    lat_scale = 111132.92 - 559.82 * math.cos(2 * phi) + 1.175 * math.cos(4 * phi)
    lon_scale = 111412.84 * math.cos(phi) - 93.5 * math.cos(3 * phi)
    return (lambda lat, lon: ((lon - longitude) * lon_scale, (latitude - lat) * lat_scale),
            lambda x, z: (latitude - z / lat_scale, longitude + x / lon_scale))


def connect(source, destination, profile, style):
    if source.resolve() == destination.resolve():
        raise ValueError('output must differ from source')
    local, geo = geographic_projection(style['origin_lat'], style['origin_lon'])
    ox, oz = profile['origin']; px, pz = profile['perimeter_half_size']; hx, hz = profile['half_size']
    if not (0 < hx < px < 5000 and 0 < hz < pz < 5000):
        raise ValueError('invalid district/perimeter bounds')
    rect = ox - px, oz - pz, ox + px, oz + pz
    clearance = profile['clearance']
    nodes, coordinate_ids, new_nodes, ways, boundary = {}, {}, {}, [], set()
    next_node, next_way = -5000000000, -5000000000
    stats = {'removed_buildings': 0, 'clipped_roads': 0, 'new_roads': 0, 'boundary_connections': 0}

    def node_for(point):
        nonlocal next_node
        key = round(point[0], 3), round(point[1], 3)
        if key not in coordinate_ids:
            next_node -= 1
            coordinate_ids[key] = next_node
            new_nodes[next_node] = geo(*point)
        return coordinate_ids[key]

    def on_boundary(point):
        x, z = point
        return rect[0] - .01 <= x <= rect[2] + .01 and rect[1] - .01 <= z <= rect[3] + .01 and min(abs(x - rect[0]), abs(x - rect[2]), abs(z - rect[1]), abs(z - rect[3])) < .01

    def way_for(points, tags, identifier=None):
        nonlocal next_way
        if identifier is None:
            next_way -= 1; identifier = next_way; stats['new_roads'] += 1
        way = ET.Element('way', id=str(identifier))
        for point in points: ET.SubElement(way, 'nd', ref=str(node_for(point)))
        for key, value in tags.items(): ET.SubElement(way, 'tag', k=key, v=str(value))
        ways.append(ET.tostring(way, encoding='utf-8'))

    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_suffix('.connecting.osm')
    with temporary.open('wb') as output:
        output.write(b'<?xml version="1.0" encoding="UTF-8"?>\n<osm version="0.6" generator="HAN district connector">\n')
        context = ET.iterparse(source, events=('start', 'end'))
        _, root = next(context)
        for event, element in context:
            if event != 'end' or element.tag not in {'node', 'way'}: continue
            if element.tag == 'node':
                point = local(float(element.get('lat')), float(element.get('lon')))
                identifier = int(element.get('id'))
                nodes[identifier] = point
                # Достаточно индексировать координаты возле района; остальная карта не нуждается в новых узлах.
                if abs(point[0] - ox) <= px + clearance and abs(point[1] - oz) <= pz + clearance:
                    coordinate_ids[round(point[0], 3), round(point[1], 3)] = identifier
                output.write(ET.tostring(element, encoding='utf-8') + b'\n')
            else:
                tags = {tag.get('k'): tag.get('v') for tag in element.findall('tag')}
                references = [int(nd.get('ref')) for nd in element.findall('nd')]
                if any(ref not in nodes for ref in references): raise ValueError('missing OSM node')
                points = [nodes[ref] for ref in references]
                if not points:
                    element.clear(); root.clear(); continue
                touches = min(p[0] for p in points) <= rect[2] + clearance and max(p[0] for p in points) >= rect[0] - clearance and min(p[1] for p in points) <= rect[3] + clearance and max(p[1] for p in points) >= rect[1] - clearance
                if not touches:
                    ways.append(ET.tostring(element, encoding='utf-8'))
                elif 'highway' in tags:
                    pieces = outside_segments(points, rect)
                    if len(pieces) == 1 and len(pieces[0]) == len(points) and all(math.dist(a, b) < .0001 for a, b in zip(pieces[0], points)):
                        ways.append(ET.tostring(element, encoding='utf-8'))
                    else:
                        stats['clipped_roads'] += 1
                        for index, piece in enumerate(pieces):
                            for point in piece:
                                if on_boundary(point): boundary.add((round(point[0], 3), round(point[1], 3)))
                                # Сохраняем исходные OSM ID на внешних концах линий.
                                for ref, original in zip(references, points):
                                    if math.dist(point, original) < .0001:
                                        coordinate_ids[round(point[0], 3), round(point[1], 3)] = ref; break
                            way_for(piece, tags, int(element.get('id')) if index == 0 else None)
                else:
                    stats['removed_buildings'] += 'building' in tags
            element.clear(); root.clear()

        # Внутренние пересечения имеют общие ID. Продолжения за край GLB достраиваются обычным городским конвейером.
        cross_x, cross_z = profile['cross_x'], profile['cross_z']
        def road(points, tags, external=False):
            if external: tags = dict(tags, **{'han:visual': 'external'})
            way_for([(ox + x, oz + z) for x, z in points], tags)

        for z in cross_z:
            boulevard = z == 0
            tags = {'highway': 'primary' if boulevard else 'secondary', 'width': profile['boulevard_width' if boulevard else 'secondary_width'],
                    'lanes': profile['boulevard_lanes' if boulevard else 'secondary_lanes']}
            if boulevard: tags['han:median_half_width'] = profile['boulevard_median_half_width']
            road([(x, z) for x in [-hx] + cross_x + [hx]], tags, True)
            road([(-px, z), (-hx, z)], tags); road([(hx, z), (px, z)], tags)
            boundary.update({(ox - px, oz + z), (ox + px, oz + z)})
        for x in cross_x:
            tags = {'highway': 'secondary', 'width': profile['cross_width'], 'lanes': profile['cross_lanes']}
            road([(x, z) for z in [-hz] + cross_z + [hz]], tags, True)
            road([(x, -pz), (x, -hz)], tags); road([(x, hz), (x, pz)], tags)
            boundary.update({(ox + x, oz - pz), (ox + x, oz + pz)})
        boundary.update({(rect[0], rect[1]), (rect[2], rect[1]), (rect[2], rect[3]), (rect[0], rect[3])})
        ring = sorted((p for p in boundary if abs(p[1] - rect[1]) < .01), key=lambda p: p[0])
        ring += sorted((p for p in boundary if abs(p[0] - rect[2]) < .01 and p not in ring), key=lambda p: p[1])
        ring += sorted((p for p in boundary if abs(p[1] - rect[3]) < .01 and p not in ring), key=lambda p: -p[0])
        ring += sorted((p for p in boundary if abs(p[0] - rect[0]) < .01 and p not in ring), key=lambda p: -p[1])
        way_for(ring + [ring[0]], {'highway': 'residential', 'width': profile['perimeter_width'], 'lanes': profile['perimeter_lanes']})
        stats['boundary_connections'] = len(boundary)
        for identifier, (lat, lon) in new_nodes.items():
            output.write(ET.tostring(ET.Element('node', id=str(identifier), lat=f'{lat:.10f}', lon=f'{lon:.10f}')) + b'\n')
        for way in ways: output.write(way + b'\n')
        output.write(b'</osm>\n')
    os.replace(temporary, destination)
    destination.with_suffix('.connection.json').write_text(json.dumps({'source': str(source), 'profile': profile, 'stats': stats,
        'license': 'ODbL-1.0', 'attribution': 'OpenStreetMap contributors; district layout: HAN project'}, ensure_ascii=False, indent=2), encoding='utf-8')
    print(stats, flush=True)
    return stats


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path); parser.add_argument('destination', type=Path)
    parser.add_argument('--profile', type=Path, required=True); parser.add_argument('--style', type=Path, default=Path('data/city/streetscape.json'))
    args = parser.parse_args()
    connect(args.source, args.destination, json.loads(args.profile.read_text(encoding='utf-8')), json.loads(args.style.read_text(encoding='utf-8')))
