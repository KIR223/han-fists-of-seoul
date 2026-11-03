"""Подготовка пользовательского GLB-района к общей карте: убрать окружение превью, сохранить PBR.

Никаких скриптов из пакета не исполняет. Работает с одиночным статическим мешем GLB 2.0.
"""
import argparse
from array import array
import copy
import json
from pathlib import Path
import struct


def read_glb(path):
    data = path.read_bytes()
    magic, version, size = struct.unpack_from('<III', data)
    if magic != 0x46546C67 or version != 2 or size != len(data):
        raise ValueError('invalid GLB header')
    document, binary = None, None
    offset = 12
    while offset < size:
        length, kind = struct.unpack_from('<II', data, offset)
        chunk = data[offset + 8:offset + 8 + length]
        if len(chunk) != length:
            raise ValueError('truncated GLB chunk')
        if kind == 0x4E4F534A:
            document = json.loads(chunk)
        elif kind == 0x004E4942:
            binary = bytearray(chunk)
        offset += 8 + length
    if document is None or binary is None:
        raise ValueError('GLB needs JSON and BIN chunks')
    return document, binary


def write_glb(path, document, binary):
    document['buffers'][0]['byteLength'] = len(binary)
    text = json.dumps(document, ensure_ascii=False, separators=(',', ':')).encode('utf-8')
    text += b' ' * (-len(text) % 4)
    binary += b'\0' * (-len(binary) % 4)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('wb') as output:
        output.write(struct.pack('<5I', 0x46546C67, 2, 28 + len(text) + len(binary), len(text), 0x4E4F534A))
        output.write(text)
        output.write(struct.pack('<2I', len(binary), 0x004E4942))
        output.write(binary)


def accessor_values(document, binary, index):
    accessor = document['accessors'][index]
    view = document['bufferViews'][accessor['bufferView']]
    channels = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4}[accessor['type']]
    code = {5121: 'B', 5123: 'H', 5125: 'I', 5126: 'f'}[accessor['componentType']]
    element = struct.Struct('<' + code * channels)
    stride = view.get('byteStride', element.size)
    offset = view.get('byteOffset', 0) + accessor.get('byteOffset', 0)
    if view.get('buffer', 0) != 0 or 'sparse' in accessor:
        raise ValueError('external/sparse accessor is unsupported')
    return [element.unpack_from(binary, offset + i * stride) for i in range(accessor['count'])]


def roof_footprints(positions):
    """Крыши исходного района — отдельные горизонтальные четырёхугольники; башенные надстройки исключаются."""
    rectangles = []
    for offset in range(0, len(positions), 4):
        quad = positions[offset:offset + 4]
        if len(quad) != 4 or max(p[1] for p in quad) - min(p[1] for p in quad) > .001:
            raise ValueError('roof is not a flat quad')
        rectangle = [min(p[0] for p in quad), min(p[2] for p in quad), max(p[0] for p in quad), max(p[2] for p in quad)]
        if rectangle[2] > rectangle[0] and rectangle[3] > rectangle[1] and rectangle not in rectangles:
            rectangles.append(rectangle)
    return [a for a in rectangles if not any(a != b and b[0] <= a[0] and b[1] <= a[1] and b[2] >= a[2] and b[3] >= a[3] for b in rectangles)]


def prepare(source, destination, half_width, half_depth, lights_path, collision_path=None, interiors_path=None):
    document, binary = read_glb(source)
    if len(document['meshes']) != 1 or len(document['nodes']) != 1 or any(k in document['nodes'][0] for k in ('matrix', 'translation', 'rotation', 'scale')):
        raise ValueError('expected one mesh with identity transform')
    kept, lights, seen_lights, footprints = [], [], set(), []
    before = after = 0
    for primitive in document['meshes'][0]['primitives']:
        material = document['materials'][primitive['material']]
        indices = [i[0] for i in accessor_values(document, binary, primitive['indices'])]
        before += len(indices) // 3
        if material.get('extras', {}).get('kind') in {'distant', 'glow'} or material.get('name') == 'far_facade':
            continue
        positions = accessor_values(document, binary, primitive['attributes']['POSITION'])
        if material.get('name') == 'roof':
            footprints = roof_footprints(positions)
        selected = array('I')
        for offset in range(0, len(indices), 3):
            triangle = indices[offset:offset + 3]
            x = sum(positions[i][0] for i in triangle) / 3
            z = sum(positions[i][2] for i in triangle) / 3
            if abs(x) <= half_width and abs(z) <= half_depth:
                selected.extend(triangle)
        if not selected:
            continue
        if material.get('name') == 'lamp_head':
            # Небольшие плафоны исходного района стоят на целых метрах; объединяем грани одного плафона.
            for x, y, z in positions:
                key = (round(x), round(z))
                if key not in seen_lights and abs(x) <= half_width and abs(z) <= half_depth:
                    seen_lights.add(key)
                    lights.append([key[0], round(y - .2, 3), key[1]])
        binary += b'\0' * (-len(binary) % 4)
        view_index = len(document['bufferViews'])
        document['bufferViews'].append({'buffer': 0, 'byteOffset': len(binary), 'byteLength': len(selected) * 4})
        binary.extend(selected.tobytes())
        accessor_index = len(document['accessors'])
        document['accessors'].append({'bufferView': view_index, 'componentType': 5125, 'count': len(selected), 'type': 'SCALAR'})
        prepared = copy.deepcopy(primitive)
        prepared['indices'] = accessor_index
        kept.append(prepared)
        after += len(selected) // 3
    document['meshes'][0]['primitives'] = kept
    if collision_path is not None:
        collision = copy.deepcopy(document)
        solid = {'asphalt', 'sidewalk', 'plaza', 'concrete', 'roof', 'facade_glass', 'facade_ribbon', 'facade_punched',
                 'facade_brick', 'facade_shop', 'wall_brick', 'wall_tile', 'wall_plaster', 'slab', 'window_glass', 'storefront_glass'}
        primitives = [copy.deepcopy(p) for p in kept if document['materials'][p['material']]['name'] in solid]
        for primitive in primitives:
            primitive.pop('material')
            primitive['attributes'] = {'POSITION': primitive['attributes']['POSITION']}
        collision['meshes'][0]['primitives'] = primitives
        for key in ('materials', 'images', 'textures', 'samplers', 'extensionsUsed', 'extensionsRequired'):
            collision.pop(key, None)
        write_glb(collision_path, collision, bytearray(binary))
    write_glb(destination, document, binary)
    lights_path.parent.mkdir(parents=True, exist_ok=True)
    indoor = []
    if interiors_path is not None:
        for room in json.loads(interiors_path.read_text(encoding='utf-8'))['interiors']:
            polygon = room['footprint']
            indoor.append([sum(p[0] for p in polygon) / len(polygon), room['floor_y'] + room['height'] - .35,
                           sum(p[1] for p in polygon) / len(polygon)])
    lights_path.write_text(json.dumps({'version': 1, 'lights': lights, 'interior_lights': indoor, 'footprints': footprints}, indent=2), encoding='utf-8')
    return {'triangles_before': before, 'triangles_after': after, 'materials': len(kept), 'lights': len(lights), 'footprints': len(footprints)}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('destination', type=Path)
    parser.add_argument('--half-width', type=float, required=True)
    parser.add_argument('--half-depth', type=float, required=True)
    parser.add_argument('--lights', type=Path, required=True)
    parser.add_argument('--collision', type=Path)
    parser.add_argument('--interiors-meta', type=Path)
    args = parser.parse_args()
    print(prepare(args.source, args.destination, args.half_width, args.half_depth, args.lights, args.collision, args.interiors_meta))
