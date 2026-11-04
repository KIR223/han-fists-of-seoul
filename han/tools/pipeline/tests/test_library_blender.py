"""Настоящий Blender: безопасный BLEND, бюджет после модификаторов, сохранение skin/actions."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

import helpers
import han_fetch


@unittest.skipUnless(han_fetch.find_blender(), 'Blender не установлен')
class LibraryBlenderTests(unittest.TestCase):
    def test_blend_modifiers_and_animation_are_prepared_without_executing_embedded_python(self):
        root=Path(__file__).resolve().parents[4]
        with tempfile.TemporaryDirectory() as temporary:
            folder=Path(temporary); marker=folder/'unexpected.txt'
            fixture=folder/'fixtures.py'
            fixture.write_text('''import bpy
from pathlib import Path
out=Path(__file__).parent
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.mesh.primitive_cube_add()
obj=bpy.context.object
modifier=obj.modifiers.new('Subdivision','SUBSURF'); modifier.levels=1
text=bpy.data.texts.new('embedded.py'); text.use_module=True
text.write("from pathlib import Path; Path("+repr(str(out/'unexpected.txt'))+").write_text('executed')")
bpy.ops.wm.save_as_mainfile(filepath=str(out/'static.blend'))
obj.modifiers.clear()
bpy.ops.object.armature_add()
rig=bpy.context.object; rig.name='AdultRig'
group=obj.vertex_groups.new(name='Bone'); group.add(list(range(len(obj.data.vertices))),1.0,'REPLACE')
modifier=obj.modifiers.new('Skin','ARMATURE'); modifier.object=rig
bone=rig.pose.bones[0]; bone.location.x=0; bone.keyframe_insert(data_path='location',frame=1)
bone.location.x=0.2; bone.keyframe_insert(data_path='location',frame=15)
bpy.ops.wm.save_as_mainfile(filepath=str(out/'animated.blend'))
''',encoding='utf-8')
            blender=han_fetch.find_blender()
            def run(script,extra=()):
                result=subprocess.run([blender,'--background','--factory-startup','--disable-autoexec','--python-exit-code','1','--python',str(script),*extra],
                                      stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf-8',errors='replace',timeout=120)
                self.assertEqual(result.returncode,0,result.stdout[-4000:])
            run(fixture)
            reports={}
            for kind in ('static','animated'):
                output=folder/kind; output.mkdir()
                parameters={'input':str(folder/(kind+'.blend')),'output_dir':str(output),'name':'PR_Test',
                            'report':str(output/'report.json'),'pipeline':{'profile':'library_auto','budget_triangles':12,'lods':[.5],
                                                                           'collision_shape':'box','max_texture_size':16}}
                config=folder/(kind+'.json'); config.write_text(json.dumps(parameters),encoding='utf-8')
                run(root/'han/tools/blender/han_pipeline.py',('--',str(config)))
                reports[kind]=json.loads((output/'report.json').read_text(encoding='utf-8'))
            self.assertFalse(marker.exists())
            self.assertGreater(reports['static']['original_triangles'],12)
            self.assertLessEqual(reports['static']['triangles']['lod0'],12)
            self.assertEqual(reports['static']['collision_vertices'],8)
            self.assertEqual(reports['animated']['type'],'skinned_source')
            self.assertEqual(reports['animated']['bones'],1)
            self.assertGreaterEqual(len(reports['animated']['actions']),1)
