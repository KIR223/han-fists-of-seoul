import json
import tempfile
import unittest
import zipfile
from pathlib import Path
import helpers
import inspect_assets


class InspectAssetsTests(unittest.TestCase):
    def test_nested_archives_and_duplicates_are_accounted_for(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); source = root / 'in'; source.mkdir()
            nested = root / 'nested.zip'
            with zipfile.ZipFile(nested, 'w') as archive:
                archive.writestr('model.obj', 'v 0 0 0\n')
                archive.writestr('LICENSE.txt', 'CC0')
            with zipfile.ZipFile(source / 'one.zip', 'w') as archive:
                archive.write(nested, 'source/nested.zip')
            (source / 'duplicate.zip').write_bytes((source / 'one.zip').read_bytes())
            result = inspect_assets.scan(source, root / 'out', root / 'report.json')
            self.assertEqual(len(result), 2)
            primary = next(record for record in result if 'duplicate_of' not in record)
            self.assertEqual(len(primary['models']), 1)
            self.assertEqual(primary['archives_opened'], 2)
            self.assertEqual(primary['legal'][0]['text'], 'CC0')
            self.assertEqual(len(json.loads((root / 'report.json').read_text())), 2)

    def test_archive_cannot_write_outside_destination(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            with zipfile.ZipFile(root / 'bad.zip', 'w') as archive:
                archive.writestr('../outside.obj', 'bad')
            with self.assertRaises(Exception):
                inspect_assets.extract(root / 'bad.zip', root / 'out')
            self.assertFalse((root / 'outside.obj').exists())

    def test_output_cannot_reenter_source(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            with self.assertRaises(ValueError):
                inspect_assets.scan(root, root / 'out', root / 'report.json')
