import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import helpers
from kandam.font_paths import font_path


class KandamFontTests(unittest.TestCase):
    def test_local_font_and_linux_collection(self):
        with tempfile.TemporaryDirectory() as temporary, patch.dict(os.environ, {'KANDAM_FONT': ''}):
            root = Path(temporary)
            local = root / 'assets_bin/cooked/fn_noto_sans_kr/NotoSansKR.ttf'
            local.parent.mkdir(parents=True)
            local.touch()
            self.assertEqual(font_path(root=root, system=root), (local, 0))
            local.unlink()
            for bold, name in [(True, 'Bold'), (False, 'Medium')]:
                collection = root / f'NotoSansCJK-{name}.ttc'
                collection.touch()
                self.assertEqual(font_path(bold, root=root, system=root), (collection, 1))

    def test_override_and_missing_font_fail_explicitly(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            custom = root / 'font.ttf'
            with patch.dict(os.environ, {'KANDAM_FONT': str(custom)}):
                with self.assertRaises(FileNotFoundError):
                    font_path(root=root, system=root)
                custom.touch()
                self.assertEqual(font_path(root=root, system=root), (custom, 0))
            with patch.dict(os.environ, {'KANDAM_FONT': ''}):
                with self.assertRaises(FileNotFoundError):
                    font_path(root=root, system=root)
