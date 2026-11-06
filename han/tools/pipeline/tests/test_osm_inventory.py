import tempfile
from pathlib import Path
import unittest
import helpers  # noqa: F401
import osm_inventory as inv

OSM = """<?xml version="1.0"?>
<osm version="0.6">
 <node id="1" lat="37.557" lon="126.914"/>
 <node id="2" lat="37.5571" lon="126.9141"/>
 <node id="3" lat="37.5572" lon="126.9142"/>
 <node id="4" lat="37.5573" lon="126.9143"/>
 <way id="10"><nd ref="1"/><nd ref="2"/><nd ref="3"/><nd ref="1"/><tag k="building" v="yes"/><tag k="height" v="120"/></way>
 <way id="11"><nd ref="1"/><nd ref="2"/><nd ref="3"/><nd ref="1"/><tag k="building" v="yes"/><tag k="building:levels" v="25"/></way>
 <way id="12"><nd ref="1"/><nd ref="2"/><nd ref="3"/><nd ref="1"/><tag k="building" v="yes"/></way>
 <way id="13"><nd ref="1"/><nd ref="4"/><tag k="highway" v="primary"/><tag k="bridge" v="yes"/></way>
 <way id="14"><nd ref="1"/><nd ref="2"/><nd ref="3"/><nd ref="1"/><tag k="leisure" v="park"/></way>
 <way id="15"><nd ref="1"/><nd ref="2"/><nd ref="3"/><nd ref="1"/><tag k="natural" v="water"/></way>
 <way id="16"><nd ref="1"/><nd ref="9"/><tag k="highway" v="residential"/></way>
 <relation id="20"><member type="way" ref="15" role="outer"/><tag k="type" v="multipolygon"/><tag k="natural" v="water"/></relation>
</osm>
"""


class OsmInventoryTests(unittest.TestCase):
    def test_projection_matches_engine_conventions(self):
        p = inv.Projection(37.557, 126.914)
        self.assertEqual(p(37.557, 126.914), (0.0, -0.0))
        x, z = p(37.558, 126.915)
        self.assertGreater(x, 0)  # на восток
        self.assertLess(z, 0)     # на север = отрицательный z

    def test_counts_classes_heights_and_relations(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "t.osm"
            path.write_text(OSM, encoding="utf-8")
            r = inv.inventory(path, inv.Projection(37.557, 126.914), 256.0, (-300, -300, 300, 300))
        self.assertEqual(r["totals"]["building"], 3)
        self.assertEqual(r["totals"]["road"], 2)
        self.assertEqual(r["totals"]["park"], 1)
        self.assertEqual(r["totals"]["water"], 1)
        self.assertEqual(r["totals"]["relation"], 1)  # мультиполигоны не разбираются: вода/парки из relations отсутствуют
        self.assertEqual(r["buildings_height_info"], {"with_height": 1, "with_levels": 1, "none": 1})
        self.assertEqual(r["tall_buildings"]["count"], 2)  # 120 м и 25 этажей
        self.assertEqual(r["tall_buildings"]["top"][0]["est_height_m"], 120.0)
        self.assertEqual(r["ways_with_missing_nodes"], 1)  # way 16 ссылается на узел 9 вне файла

    def test_numbers_with_units_and_garbage(self):
        self.assertEqual(inv.parse_number("12.5 m"), 12.5)
        self.assertEqual(inv.parse_number("12,5"), 12.5)
        self.assertIsNone(inv.parse_number("высокий"))
        self.assertIsNone(inv.parse_number("-3"))
        self.assertIsNone(inv.parse_number(None))


if __name__ == "__main__":
    unittest.main()
