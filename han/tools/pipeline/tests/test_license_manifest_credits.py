import json
import tempfile
import unittest
from pathlib import Path

import helpers  # noqa: F401  (настраивает sys.path)
import credits
import license_rules
import manifest
from helpers import sample_asset


class LicenseTests(unittest.TestCase):
    def test_shared_vectors(self):
        cases = Path(__file__).with_name("license_cases.tsv").read_text(encoding="utf-8").splitlines()
        checked = 0
        for line in cases:
            if not line or line.startswith("#"):
                continue
            expected, text = line.split("\t", 1)
            with self.subTest(text=text):
                self.assertEqual(license_rules.classify(text), expected)
            checked += 1
        self.assertGreaterEqual(checked, 25)

    def test_empty_is_forbidden(self):
        self.assertEqual(license_rules.classify(""), license_rules.FORBIDDEN)
        self.assertEqual(license_rules.classify("   "), license_rules.FORBIDDEN)

    def test_sketchfab_slugs(self):
        self.assertEqual(license_rules.classify(license_rules.from_sketchfab_slug("cc0")), license_rules.ALLOWED)
        self.assertEqual(license_rules.classify(license_rules.from_sketchfab_slug("by")), license_rules.ALLOWED)
        self.assertEqual(license_rules.classify(license_rules.from_sketchfab_slug("by-sa")), license_rules.NEEDS_OWNER)
        for slug in ("by-nc", "by-nd", "by-nc-sa", "by-nc-nd", "ed", "st", "что-то-новое", ""):
            with self.subTest(slug=slug):
                self.assertEqual(license_rules.classify(license_rules.from_sketchfab_slug(slug)), license_rules.FORBIDDEN)


class ManifestTests(unittest.TestCase):
    def valid(self, **overrides):
        return {"version": 1, "assets": [sample_asset(**overrides)]}

    def test_valid_manifest(self):
        self.assertEqual(manifest.validate(self.valid()), [])

    def test_empty_manifest_is_valid(self):
        self.assertEqual(manifest.validate(manifest.empty()), [])

    def test_rejects_forbidden_license(self):
        errors = manifest.validate(self.valid(license="CC-BY-NC 4.0"))
        self.assertTrue(any("не разрешена" in e for e in errors), errors)

    def test_needs_owner_approval(self):
        self.assertTrue(manifest.validate(self.valid(license="CC-BY-SA 4.0")))
        self.assertEqual(manifest.validate(self.valid(license="CC-BY-SA 4.0", approved_by_owner=True)), [])

    def test_bad_id_name_and_fields(self):
        for overrides in ({"id": "Bad Id"}, {"name": "barrel"}, {"kind": "video"}, {"zone": "C"}, {"author": ""}, {"used_in": " "},
                          {"modified": ""}, {"retrieved": "вчера"}, {"sha256": "abc"}):
            with self.subTest(overrides=overrides):
                self.assertTrue(manifest.validate(self.valid(**overrides)))

    def test_source_requirements(self):
        self.assertTrue(manifest.validate(self.valid(source={"type": "direct", "page": "https://a.b/c"})))  # нет url
        self.assertTrue(manifest.validate(self.valid(source={"type": "direct", "page": "http://a.b/c", "url": "https://a.b/f.glb"})))
        self.assertTrue(manifest.validate(self.valid(source={"type": "sketchfab", "page": "https://sketchfab.com/x"})))  # нет asset_id
        self.assertEqual(manifest.validate(self.valid(source={"type": "local", "file": "assets_src/inbox/a.glb"})), [])

    def test_duplicates(self):
        data = {"version": 1, "assets": [sample_asset(), sample_asset()]}
        errors = manifest.validate(data)
        self.assertTrue(any("повторяющийся id" in e for e in errors))
        self.assertTrue(any("повторяющееся имя" in e for e in errors))

    def test_pipeline_budgets(self):
        self.assertEqual(manifest.pipeline_of(sample_asset(name="PR_Chair"))["budget_triangles"], 2500)
        self.assertEqual(manifest.pipeline_of(sample_asset(name="VH_Taxi"))["budget_triangles"], 60000)
        self.assertEqual(manifest.pipeline_of(sample_asset(name="PR_Chair", pipeline={"budget_triangles": 500}))["budget_triangles"], 500)
        self.assertTrue(manifest.validate(self.valid(pipeline={"budget_triangles": -5})))
        self.assertTrue(manifest.validate(self.valid(pipeline={"lods": [0.5, 1.5]})))

    def test_roundtrip_and_hash(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "m" / "manifest.json"
            data = self.valid()
            manifest.save(path, data)
            self.assertEqual(manifest.load(path), data)
            self.assertTrue(path.read_text(encoding="utf-8").endswith("\n"))
            self.assertIn("Автор Тестов", path.read_text(encoding="utf-8"))  # кириллица не экранируется
            blob = Path(tmp) / "f.bin"
            blob.write_bytes(b"abc")
            self.assertEqual(manifest.sha256_file(blob), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")

    def test_real_manifest_is_valid(self):
        real = Path(__file__).resolve().parents[4] / "assets_src" / "manifest.json"
        if real.exists():
            self.assertEqual(manifest.validate(json.loads(real.read_text(encoding="utf-8"))), [])


class CreditsTests(unittest.TestCase):
    def test_add_update_and_idempotence(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            asset = sample_asset()
            self.assertFalse(credits.has_row(directory, asset))
            self.assertTrue(credits.ensure_row(directory, asset))
            self.assertTrue(credits.has_row(directory, asset))
            self.assertFalse(credits.ensure_row(directory, asset))  # повтор ничего не меняет

            def rows(text):  # имя ассета встречается в строке дважды (имя и путь результата), поэтому считаем строки
                return [line for line in text.splitlines() if line.startswith("| PR_TestBarrel (")]

            text = (directory / "A.md").read_text(encoding="utf-8")
            self.assertIn("| Ассет | Автор |", text)
            self.assertEqual(len(rows(text)), 1)

            asset["modified"] = "да — переделан заново"
            self.assertTrue(credits.ensure_row(directory, asset))
            text = (directory / "A.md").read_text(encoding="utf-8")
            self.assertEqual(len(rows(text)), 1)
            self.assertIn("переделан заново", text)

    def test_other_rows_untouched_and_zone_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            (directory / "B.md").write_text("# Ассеты роли B\n\n| Ассет | Автор | Источник (URL) | Лицензия | Где используется | Изменён? |\n|---|---|---|---|---|---|\n| чужая строка | x | y | CC0 | z | нет |\n", encoding="utf-8")
            credits.ensure_row(directory, sample_asset(zone="B"))
            text = (directory / "B.md").read_text(encoding="utf-8")
            self.assertIn("| чужая строка |", text)
            self.assertIn("PR_TestBarrel", text)
            self.assertFalse((directory / "A.md").exists())

    def test_cells_are_sanitized(self):
        row = credits.row_for(sample_asset(author="А | Б\nВ"))
        self.assertEqual(row.count("|"), 7)
        self.assertNotIn("\n", row)

    def test_row_passes_ci_rule(self):
        # Правило CI: в первой колонке путь файла/папки, в колонке лицензии — разрешённая и без NC/ND.
        row = credits.row_for(sample_asset())
        cells = [c.strip() for c in row.strip("|").split("|")]
        self.assertIn("assets_bin/cooked/PR_TestBarrel.hmesh", cells[0])
        self.assertRegex(cells[3], r"(?i)cc0|cc[- ]by")


if __name__ == "__main__":
    unittest.main()
