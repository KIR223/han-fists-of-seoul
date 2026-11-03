from pathlib import Path
import tempfile
import unittest
import xml.etree.ElementTree as ET
import helpers
import han_osm_grid as grid


class OsmGridTests(unittest.TestCase):
    def test_tiles_cover_extent_without_gaps(self):
        result = grid.tiles((37.5, 126.9, 37.54, 126.94), .025)
        self.assertEqual(len(result), 4)
        self.assertEqual(result[0][2], result[2][0])
        self.assertEqual(result[0][3], result[1][1])
        self.assertEqual(result[-1][2:], (37.54, 126.94))
        for size in (0, -.1, .1, float('nan')):
            with self.assertRaises(ValueError):
                grid.tiles((37.5, 126.9, 37.54, 126.94), size)

    def test_merge_deduplicates_types_and_keeps_latest_version(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            first, second, output = root / 'a.osm', root / 'b.osm', root / 'out.osm'
            first.write_text('<osm><node id="1" version="1" lat="0" lon="0"/><node id="2" lat="0" lon="1"/><way id="1"><nd ref="1"/><nd ref="2"/></way></osm>')
            second.write_text('<osm><node id="1" version="2" lat="1" lon="0"/><node id="2" lat="0" lon="1"/><way id="1"><nd ref="1"/><nd ref="2"/></way></osm>')
            self.assertEqual(grid.merge([second, first], output), {'node': 2, 'way': 1, 'relation': 0})
            self.assertEqual(ET.parse(output).getroot().find('node').get('version'), '2')

    def test_missing_node_or_incomplete_download_does_not_replace_output(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); source, output = root / 'in.osm', root / 'out.osm'
            output.write_text('original')
            for text in ('<osm><way id="1"><nd ref="8"/></way></osm>', '<osm><remark>timeout</remark></osm>'):
                source.write_text(text)
                with self.assertRaises(ValueError):
                    grid.merge([source], output)
                self.assertEqual(output.read_text(), 'original')
