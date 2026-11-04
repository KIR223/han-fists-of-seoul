import tempfile
import unittest
from pathlib import Path

import helpers
from PIL import Image
import library_materials as materials


class LibraryMaterialTests(unittest.TestCase):
    def test_gloss_is_inverted_and_data_channels_preserved(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for name, color in [('wall_COL_4K.png', (60, 90, 120)), ('wall_GLOSS_4K.png', (32, 32, 32)),
                                ('wall_AO_4K.png', (120, 120, 120)), ('wall_METALNESS_4K.png', (18, 18, 18))]:
                Image.new('RGB', (16,16), color).save(root/name)
            result = materials.prepare(root, [p.name for p in root.glob('*.png')], root/'out', 16)
            self.assertTrue(result['gloss_inverted'])
            with Image.open(root/'out/ORM.png') as image: self.assertEqual(image.getpixel((0,0)), (120,223,18))
            with Image.open(root/'out/N.png') as image: self.assertEqual(image.getpixel((0,0)), (128,128,255))

    def test_normal_roughness_preference_and_mask_only_rejection(self):
        maps = materials.select_maps(['wall_NormalDX.png','wall_NormalGL.png','wall_Roughness.jpg','wall_GLOSS.jpg','wall_BaseColor.jpg','wall_Preview1.png'])
        self.assertEqual(maps['normal'], 'wall_NormalGL.png')
        self.assertEqual(maps['rough'], 'wall_Roughness.jpg')
        with self.assertRaises(ValueError): materials.prepare('.', ['Smudges_ROUGHNESS.png'], 'unused')
        with self.assertRaises(ValueError): materials.prepare('.', ['../outside_COL.png'], 'unused', 16)
