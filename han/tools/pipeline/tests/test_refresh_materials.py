from io import BytesIO
import json
from pathlib import Path
import tempfile
import unittest

import helpers
from PIL import Image
from prepare_district import read_glb,write_glb
from refresh_district_materials import refresh,image_from_texture


class MaterialRefreshTests(unittest.TestCase):
    def test_replacement_keeps_geometry_emission_and_facade_windows(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp); binary=bytearray(b'geometry')
            document={'buffers':[{}],'bufferViews':[],'images':[],'textures':[],
                      'meshes':[{'primitives':[{'attributes':{'POSITION':0},'material':0}]}]}
            for color in [(20,30,40),(128,128,255),(255,255,0)]:
                pixels=Image.new('RGB',(2,1),color)
                if color==(255,255,0): pixels.putpixel((1,0),(255,12,0))
                data=BytesIO(); pixels.save(data,format='PNG'); index=len(document['images'])
                document['bufferViews'].append({'buffer':0,'byteOffset':len(binary),'byteLength':len(data.getvalue())})
                binary.extend(data.getvalue()); document['images'].append({'bufferView':index,'mimeType':'image/png'})
                document['textures'].append({'source':index})
            document['materials']=[{'name':'facade','normalTexture':{'index':1},'emissiveFactor':[1,.5,.1],
                                    'pbrMetallicRoughness':{'baseColorTexture':{'index':0},'metallicRoughnessTexture':{'index':2}}}]
            write_glb(root/'in.glb',document,binary)
            for name,color in [('BC',(200,100,50)),('N',(128,128,255)),('ORM',(255,230,0))]:
                Image.new('RGB',(2,1),color).save(root/(name+'.png'))
            refresh(root/'in.glb',root/'out.glb',{'facade':{'source':'brick','preserve_facade_details':True}},
                    [{'source':'brick.zip','state':'prepared','output':str(root)}])
            result,data=read_glb(root/'out.glb')
            self.assertEqual(result['meshes'],document['meshes'])
            self.assertEqual(data[:8],b'geometry')
            self.assertEqual(result['materials'][0]['emissiveFactor'],[1,.5,.1])
            pixels=image_from_texture(result,data,result['materials'][0]['pbrMetallicRoughness']['baseColorTexture']['index'])
            self.assertEqual(pixels.getpixel((0,0)),(200,100,50))
            self.assertEqual(pixels.getpixel((1,0)),(20,30,40))
