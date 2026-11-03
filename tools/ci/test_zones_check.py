import os
from pathlib import Path
import subprocess
import sys
import unittest


class ZonesCheckTests(unittest.TestCase):
    def test_failed_comparison_does_not_pass(self):
        script = Path(__file__).resolve().parents[1] / "han_zones.py"
        result = subprocess.run([sys.executable, str(script), "check", "--base", "audit/nonexistent-revision", "--head", "HEAD", "--branch", "a/test"], capture_output=True)
        self.assertEqual(result.returncode, 2)

    def test_success_output_works_in_windows_cp1251(self):
        script = Path(__file__).resolve().parents[1] / "han_zones.py"
        result = subprocess.run([sys.executable, str(script), "check", "--base", "HEAD", "--head", "HEAD", "--branch", "a/test"],
                                capture_output=True, env=dict(os.environ, PYTHONIOENCODING="cp1251"))
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
