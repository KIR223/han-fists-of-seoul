from pathlib import Path
import tempfile
import unittest
import helpers
from district_shadow_proxy import build
from prepare_district import read_glb,write_glb


class DistrictShadowProxyTests(unittest.TestCase):
    def test_drops_small_decoration_and_preserves_leaf_alpha(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary)
            document={'buffers':[{}],'bufferViews':[],'accessors':[{'count':12},{'count':6},{'count':9}],
                      'materials':[{'name':'wall','alphaMode':'OPAQUE'},{'name':'leaves','alphaMode':'MASK','alphaCutoff':.65,
                                    'pbrMetallicRoughness':{'baseColorTexture':{'index':0}}},{'name':'wire'}],
                      'meshes':[{'primitives':[{'material':i,'indices':i,'attributes':{'POSITION':0}} for i in range(3)]}]}
            write_glb(root/'source.glb',document,bytearray(b'geometry'))
            stats=build(root/'source.glb',root/'shadow.glb',{'wall','leaves'})
            result,_=read_glb(root/'shadow.glb')
            self.assertEqual(stats,{'before':9,'after':6,'masked':2})
            self.assertEqual(len(result['meshes'][0]['primitives']),2)
            self.assertEqual(result['materials'][1]['alphaMode'],'MASK')
            self.assertEqual(result['materials'][1]['alphaCutoff'],.65)
            self.assertEqual(result['materials'][1]['pbrMetallicRoughness'],document['materials'][1]['pbrMetallicRoughness'])
