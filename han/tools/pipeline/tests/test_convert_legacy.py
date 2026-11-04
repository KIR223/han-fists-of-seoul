from array import array
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import helpers
from convert_legacy_model import convert,image_lookup


class LegacyConversionTests(unittest.TestCase):
    def test_fbx_6100_null_material_slots_do_not_get_dereferenced(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary)
            (root/'old.fbx').write_bytes(b'Kaydara FBX Binary  \x00\x1a\x00'+(6100).to_bytes(4,'little'))
            xyz=lambda x,y,z: SimpleNamespace(x=x,y=y,z=z)
            columns=SimpleNamespace(c0=(1,0,0),c1=(0,1,0),c2=(0,0,1),c3=(0,0,0))
            class Instance:
                geometry_to_world=columns
                @property
                def materials(self): raise AssertionError('Пустой нативный слот нельзя читать')
            uv=SimpleNamespace(exists=True,values=[xyz(0,0,0),xyz(1,0,0),xyz(0,1,0)],indices=[0,1,2])
            mesh=SimpleNamespace(name='Old',vertices=[xyz(0,0,0),xyz(1,0,0),xyz(0,1,0)],
                                 faces=[SimpleNamespace(index_begin=0,num_indices=3)],vertex_indices=[0,1,2],
                                 face_material=[0],vertex_uv=uv,instances=[Instance()])
            fake=SimpleNamespace(load_file=lambda _:SimpleNamespace(meshes=[mesh]))
            with patch.dict('sys.modules',{'ufbx':fake}): convert(root/'old.fbx',root/'out.obj',[])
            self.assertIn('f 1/1 2/2 3/3',(root/'out.obj').read_text())
            self.assertIn('neutral_replacement_required',(root/'out.conversion.json').read_text())

    def test_preserves_uv_indices_and_resolves_relative_texture_basename(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary); (root/'source.dae').write_text('fixture')
            (root/'wall.png').write_bytes(b'test texture')
            mesh=SimpleNamespace(name='Wall',material_index=0,num_vertices=3,
                                 vertices=array('f',[0,0,0,1,0,0,0,1,0]),indices=array('I',[0,1,2]),
                                 texcoords=[array('f',[0,0,0,1,0,0,0,1,0])])
            scene=SimpleNamespace(meshes=[mesh],materials=[{'COLOR_DIFFUSE':[1,1,1],'TEXTURES':{1:['old\\wall.png']}}])
            fake=SimpleNamespace(Process_Triangulate=1,Process_PreTransformVertices=2,TextureType_DIFFUSE=1,
                                 import_file=lambda *args:scene)
            self.assertIn('wall.png',image_lookup([root]))
            with patch.dict('sys.modules',{'assimp_py':fake}):
                convert(root/'source.dae',root/'output/model.obj',[root])
            result=(root/'output/model.obj').read_text()
            self.assertIn('vt 1.0 0.0',result)
            self.assertIn('f 1/1 2/2 3/3',result)
            self.assertIn('map_Kd texture_', (root/'output/model.mtl').read_text())
