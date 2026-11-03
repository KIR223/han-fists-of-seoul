import json
from pathlib import Path
import tempfile
import unittest
import xml.etree.ElementTree as ET
import helpers
import connect_district as connect


class DistrictConnectionTests(unittest.TestCase):
    def test_crossing_polyline_is_split_without_cutting_outside_junctions(self):
        rect = (-1, -1, 1, 1)
        self.assertEqual(connect.outside_segments([(-3, 0), (3, 0)], rect), [[(-3, 0), (-1., 0.)], [(1., 0.), (3, 0)]])
        self.assertEqual(connect.outside_segments([(0, 0), (0, .5)], rect), [])
        self.assertEqual(connect.outside_segments([(-3, 2), (3, 2)], rect), [[(-3, 2), (3, 2)]])
        self.assertEqual(connect.outside_segments([(-3, 0), (0, 0), (3, 0)], rect), [[(-3, 0), (-1., 0.)], [(1., 0.), (3, 0)]])

    def test_overlay_has_complete_references_and_shared_boundary_graph(self):
        repo = Path(__file__).resolve().parents[4]
        profile = json.loads((repo / 'data/city/kandam_layout.json').read_text(encoding='utf-8'))
        style = json.loads((repo / 'data/city/streetscape.json').read_text(encoding='utf-8'))
        local, geo = connect.geographic_projection(style['origin_lat'], style['origin_lon'])
        ox, oz = profile['origin']
        root = ET.Element('osm')
        for identifier, point in enumerate([(ox - 500, oz), (ox, oz), (ox + 500, oz)], 1):
            lat, lon = geo(*point)
            ET.SubElement(root, 'node', id=str(identifier), lat=str(lat), lon=str(lon))
        way = ET.SubElement(root, 'way', id='10')
        for identifier in (1, 2, 3): ET.SubElement(way, 'nd', ref=str(identifier))
        ET.SubElement(way, 'tag', k='highway', v='primary')
        with tempfile.TemporaryDirectory() as temporary:
            source, destination = Path(temporary) / 'in.osm', Path(temporary) / 'out.osm'
            ET.ElementTree(root).write(source)
            stats = connect.connect(source, destination, profile, style)
            self.assertEqual(stats['clipped_roads'], 1)
            data = ET.parse(destination).getroot()
            nodes = {node.get('id') for node in data.findall('node')}
            uses = {}
            for way in data.findall('way'):
                for node in way.findall('nd'):
                    self.assertIn(node.get('ref'), nodes)
                    uses[node.get('ref')] = uses.get(node.get('ref'), 0) + 1
            self.assertTrue(any(count >= 3 for count in uses.values()))
            self.assertTrue(any(any(tag.get('k') == 'han:visual' for tag in way.findall('tag')) for way in data.findall('way')))
            self.assertEqual(source.read_bytes(), ET.tostring(root))
