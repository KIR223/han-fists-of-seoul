import json
from pathlib import Path
import struct
import tempfile
import unittest
import helpers
import prepare_district as district


class PrepareDistrictTests(unittest.TestCase):
    def test_tower_roofs_inside_podium_do_not_duplicate_obstacles(self):
        rectangle = lambda x0, x1, y: [(x0, y, 0), (x1, y, 0), (x1, y, 10), (x0, y, 10)]
        self.assertEqual(district.roof_footprints(rectangle(0, 10, 5) + rectangle(1, 9, 30) + rectangle(20, 30, 8)),
                         [[0, 0, 10, 10], [20, 0, 30, 10]])
        with self.assertRaises(ValueError):
            district.roof_footprints([(0, 0, 0)])

    def test_filter_keeps_static_geometry_and_maps_but_removes_preview_ring(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            points = [(0., 0., 0.), (1., 0., 0.), (0., 0., 1.), (100., 0., 100.), (101., 0., 100.), (100., 0., 101.)]
            binary = bytearray(b''.join(struct.pack('<3f', *p) for p in points) + struct.pack('<6I', 0, 1, 2, 3, 4, 5))
            data = {'asset': {'version': '2.0'}, 'nodes': [{'mesh': 0}], 'buffers': [{'byteLength': len(binary)}],
                    'bufferViews': [{'buffer': 0, 'byteOffset': 0, 'byteLength': 72}, {'buffer': 0, 'byteOffset': 72, 'byteLength': 24}],
                    'accessors': [{'bufferView': 0, 'componentType': 5126, 'type': 'VEC3', 'count': 6},
                                  {'bufferView': 1, 'componentType': 5125, 'type': 'SCALAR', 'count': 6}],
                    'materials': [{'name': 'concrete', 'normalTexture': {'index': 0}}, {'name': 'ring', 'extras': {'kind': 'distant'}}],
                    'meshes': [{'primitives': [{'attributes': {'POSITION': 0}, 'indices': 1, 'material': 0},
                                               {'attributes': {'POSITION': 0}, 'indices': 1, 'material': 1}]}]}
            district.write_glb(root / 'in.glb', data, binary)
            result = district.prepare(root / 'in.glb', root / 'out.glb', 5, 5, root / 'lights.json', root / 'collision.glb')
            self.assertEqual(result['triangles_before'], 4)
            self.assertEqual(result['triangles_after'], 1)
            prepared, content = district.read_glb(root / 'out.glb')
            primitives = prepared['meshes'][0]['primitives']
            self.assertEqual(len(primitives), 1)
            self.assertEqual(prepared['materials'][0]['normalTexture'], {'index': 0})
            self.assertEqual(district.accessor_values(prepared, content, primitives[0]['indices']), [(0,), (1,), (2,)])
            collision, _ = district.read_glb(root / 'collision.glb')
            self.assertNotIn('materials', collision)
            self.assertNotIn('material', collision['meshes'][0]['primitives'][0])
            self.assertEqual(json.loads((root / 'lights.json').read_text())['lights'], [])

    def test_truncated_glb_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'bad.glb'
            path.write_bytes(struct.pack('<3I', 0x46546C67, 2, 1000))
            with self.assertRaises(ValueError):
                district.read_glb(path)
