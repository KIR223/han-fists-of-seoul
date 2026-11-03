import unittest
import helpers
import han_osm_extract as extract


class OsmExtractTests(unittest.TestCase):
    def test_long_road_crossing_extent_is_kept(self):
        bbox = (37, 126, 38, 127)
        self.assertTrue(extract.overlaps([(37.5, 125), (37.5, 128)], bbox))
        self.assertTrue(extract.overlaps([(37.5, 126.5)], bbox))
        self.assertFalse(extract.overlaps([(36, 126.5), (36.5, 126.6)], bbox))
        self.assertFalse(extract.overlaps([], bbox))

    def test_names_and_brands_do_not_enter_game_geometry(self):
        tags = [('highway', 'primary'), ('brand', 'Real brand'), ('name', 'Real shop'), ('bridge', 'yes'), ('layer', '1')]
        self.assertEqual(extract.geometry_tags(tags), {'highway': 'primary', 'bridge': 'yes', 'layer': '1'})
