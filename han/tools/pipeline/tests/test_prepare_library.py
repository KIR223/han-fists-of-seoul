import unittest
import helpers
from prepare_library_models import choose_model, variant_models


class LibrarySelectionTests(unittest.TestCase):
    def test_chooses_portable_model_over_duplicate_formats_and_native_blend_over_fbx(self):
        self.assertEqual(choose_model([{'file':'x/scene.glb'},{'file':'scene.fbx'},{'file':'scene.blend'}]),'x/scene.glb')
        self.assertEqual(choose_model([{'file':'scene.fbx'},{'file':'scene.blend'}]),'scene.blend')
        self.assertEqual(choose_model([{'file':'scene.3ds'}]),'scene.3ds')
        self.assertEqual(choose_model([{'file':'scene.c4d'}]),None)

    def test_keeps_distinct_bodies_and_hair_in_portable_formats(self):
        values=[{'file':'male.gltf'},{'file':'female.gltf'},{'file':'hair.glb'},{'file':'male.fbx'}]
        self.assertEqual([m['file'] for m in variant_models(values)],['male.gltf','female.gltf','hair.glb'])
