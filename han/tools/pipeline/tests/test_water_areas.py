import unittest
import helpers
from extract_water_areas import triangulate_rings
from prepare_water_surface import clip_triangle,prepare
from pathlib import Path
import tempfile
from prepare_district import read_glb


class WaterAreaTests(unittest.TestCase):
    def test_clipped_surface_stays_inside_extent_and_faces_up(self):
        self.assertEqual(clip_triangle([(10,10),(12,10),(10,12)],(0,0,1,1)),[])
        with tempfile.TemporaryDirectory() as temporary:
            profile={'output':str(Path(temporary)/'river.glb'),'level':4,'color':[.05,.1,.1],'roughness':.14}
            areas={'bbox':[37,126,38,127],'polygons':[{'triangles':[[(125,37),(128,37),(126.5,39)]]}]}
            result=prepare(areas,profile,(37.5,126.5))
            self.assertGreater(result['triangles'],0)
            document,binary=read_glb(Path(profile['output']))
            from prepare_district import accessor_values
            primitive=document['meshes'][0]['primitives'][0]
            points=accessor_values(document,binary,primitive['attributes']['POSITION'])
            for a,b,c in zip(points[::3],points[1::3],points[2::3]):
                self.assertGreater((b[2]-a[2])*(c[0]-a[0])-(b[0]-a[0])*(c[2]-a[2]),0)
                self.assertEqual(a[1],4)

    def test_island_is_not_filled_and_water_area_is_conserved(self):
        try:
            import mapbox_earcut
            import numpy
        except ImportError:
            self.skipTest('Для подготовки географии нужны numpy и mapbox-earcut')
        triangles=triangulate_rings([[(0,0),(10,0),(10,10),(0,10),(0,0)],[(3,3),(3,7),(7,7),(7,3),(3,3)]])
        total=0
        for a,b,c in triangles:
            total+=abs((b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]))/2
            center=((a[0]+b[0]+c[0])/3,(a[1]+b[1]+c[1])/3)
            self.assertFalse(3<center[0]<7 and 3<center[1]<7)
        self.assertAlmostEqual(total,84)
        with self.assertRaises(ValueError): triangulate_rings([[(0,0),(0,0)]])
