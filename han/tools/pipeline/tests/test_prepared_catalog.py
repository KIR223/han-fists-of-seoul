import tempfile
from pathlib import Path
import unittest
import helpers
from prepared_catalog import combine,inspect_result


class PreparedCatalogTests(unittest.TestCase):
    def test_failed_retry_does_not_hide_success_but_corrected_success_wins(self):
        first={'id':'wall','state':'prepared','output':'first'}
        failed={'id':'wall','state':'needs_review'}
        corrected={'id':'wall','state':'prepared','output':'corrected'}
        self.assertEqual(combine([[first],[failed]]),[first])
        self.assertEqual(combine([[failed],[first],[corrected]]),[corrected])

    def test_missing_files_and_oversized_model_are_not_runtime_ready(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary)
            record={'id':'city','state':'prepared','output':'out','report':{'files':{'lod0':'city.glb'},'size_meters':{'x':10000,'y':10,'z':10}}}
            result=inspect_result(record,root)
            self.assertFalse(result['checks']['technical_outputs_present'])
            self.assertFalse(result['checks']['runtime_ready'])
            self.assertIn('verify_physical_scale_or_preview_geometry',result['checks']['issues'])
