import json
import contextlib
import io
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
import runpy
from unittest.mock import patch

from credits_policy import license_allowed, owner_approved


class CreditsPolicyTests(unittest.TestCase):
    def test_forbidden_and_unknown_spellings(self):
        for text in ("CC-BY-NC4.0", "CC-BY-ND4.0", "CC BY NC 4.0", "Limited license", "MITunknown", "", "free download", "own work NC"):
            with self.subTest(text=text):
                self.assertFalse(license_allowed(text))
                self.assertFalse(license_allowed(text, approved_by_owner=True))

    def test_allowed_licenses(self):
        for text in ("CC0", "CC-BY 4.0", "CC BY 3.0", "CC Attribution", "MIT", "BSD-3-Clause", "Apache 2.0", "SIL OFL 1.1", "zlib", "Public Domain", "свой", "Unlicense"):
            with self.subTest(text=text):
                self.assertTrue(license_allowed(text))

    def test_owner_decision_does_not_allow_forbidden_license(self):
        self.assertFalse(license_allowed("CC-BY-SA 4.0"))
        self.assertTrue(license_allowed("CC-BY-SA 4.0", True))
        self.assertFalse(license_allowed("CC-BY-NC 4.0", True))

    def test_owner_approval_requires_same_name_license_and_boolean(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            row = ["Lamp (assets_src/lamp/)", "Author", "https://example.test", "CC-BY-SA 4.0"]
            self.assertFalse(owner_approved(row, root))
            manifest = root / "assets_src" / "manifest.json"
            manifest.parent.mkdir()
            asset = {"name": "Lamp", "license": "CC-BY-SA 4.0", "approved_by_owner": True}
            manifest.write_text(json.dumps({"assets": [asset]}), encoding="utf-8")
            self.assertTrue(owner_approved(row, root))
            for field, value in (("name", "AnotherLamp"), ("license", "CC0"), ("approved_by_owner", "true")):
                changed = dict(asset, **{field: value})
                manifest.write_text(json.dumps({"assets": [changed]}), encoding="utf-8")
                self.assertFalse(owner_approved(row, root))
            manifest.write_text("broken", encoding="utf-8")
            self.assertFalse(owner_approved(row, root))

    def test_git_error_is_not_success(self):
        script = Path(__file__).with_name("credits_check.py")
        result = subprocess.run([os.sys.executable, str(script), "--base", "audit/nonexistent-revision"], capture_output=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn("Не удалось сравнить".encode("utf-8"), result.stderr)

    def test_cli_checks_replaced_assets_and_their_credit(self):
        script = Path(__file__).with_name("credits_check.py")
        with tempfile.TemporaryDirectory() as tmp:
            credits = Path(tmp) / "A.md"
            for text, expected in (("CC0", 0), ("CC-BY-NC4.0", 1), ("Limited license", 1)):
                credits.write_text(f"| assets_src/фонарь.png | Author | URL | {text} | Place | No |\n", encoding="utf-8")
                result = subprocess.CompletedProcess([], 0, "assets_src/фонарь.png\0", "")
                with patch("sys.argv", [str(script)]), patch("subprocess.run", return_value=result) as run, patch("glob.glob", return_value=[str(credits)]):
                    code = 0
                    with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                        try:
                            runpy.run_path(str(script), run_name="__main__")
                        except SystemExit as caught:
                            code = caught.code
                    self.assertEqual(code, expected)
                    self.assertIn("--diff-filter=AM", run.call_args.args[0])


if __name__ == "__main__":
    unittest.main()
