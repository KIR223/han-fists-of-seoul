import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import helpers
import credits
import han_fetch
import manifest
from helpers import sample_asset

REPO = Path(__file__).resolve().parents[4]


def fake_report(name="PR_TestBarrel", lod0=2400, brand_hits=None, warnings=None):
    return {"name": name, "triangles": {"lod0": lod0, "lod1": lod0 // 2}, "brand_hits": brand_hits or [], "warnings": warnings or [],
            "files": {"lod0": f"{name}.glb", "lod1": f"{name}.lod1.glb", "collision": f"{name}.col.glb"}}


class CheckReportTests(unittest.TestCase):
    def test_within_budget(self):
        self.assertEqual(han_fetch.check_report(sample_asset(), fake_report(warnings=["a"])), ["a"])

    def test_over_budget(self):
        with self.assertRaises(han_fetch.PipelineError):
            han_fetch.check_report(sample_asset(), fake_report(lod0=2501))
        self.assertTrue(han_fetch.check_report(sample_asset(pipeline={"budget_triangles": 3000}), fake_report(lod0=2501)) == [])

    def test_brands_block_until_reviewed(self):
        report = fake_report(brand_hits=[{"name": "Coca_Barrel", "word": "coca"}])
        with self.assertRaises(han_fetch.PipelineError) as ctx:
            han_fetch.check_report(sample_asset(), report)
        self.assertIn("Coca_Barrel", str(ctx.exception))
        han_fetch.check_report(sample_asset(brand_review="логотип закрашен, имя переименовано"), report)


class FindEntryTests(unittest.TestCase):
    def test_prefers_glb_then_gltf_then_fbx_and_shallowest(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "deep" / "er").mkdir(parents=True)
            (root / "deep" / "er" / "b.glb").write_bytes(b"1")
            (root / "a.fbx").write_bytes(b"1")
            self.assertEqual(han_fetch.find_entry(root, "").name, "b.glb")
            (root / "scene.gltf").write_bytes(b"1")
            self.assertEqual(han_fetch.find_entry(root, "").name, "b.glb")  # glb приоритетнее
            (root / "top.glb").write_bytes(b"1")
            self.assertEqual(han_fetch.find_entry(root, "").name, "top.glb")  # из glb — ближайший к корню

    def test_explicit_entry_and_errors(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "x").mkdir()
            (root / "x" / "m.obj").write_bytes(b"1")
            self.assertEqual(han_fetch.find_entry(root, "x/m.obj").name, "m.obj")
            with self.assertRaises(han_fetch.PipelineError):
                han_fetch.find_entry(root, "../outside.glb")
            with self.assertRaises(han_fetch.PipelineError):
                han_fetch.find_entry(root, "x/none.glb")
        with tempfile.TemporaryDirectory() as tmp, self.assertRaises(han_fetch.PipelineError):
            han_fetch.find_entry(Path(tmp), "")


class ProcessWithFakesTests(unittest.TestCase):
    """Оркестрация без настоящего Blender и han_cooker: проверяем порядок шагов, кредиты и ошибки."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.paths = han_fetch.Paths(self.root)
        self.asset = sample_asset(source={"type": "local", "file": "inbox/b.glb"})
        (self.root / "inbox").mkdir()
        (self.root / "inbox" / "b.glb").write_bytes(b"GLB")
        # Поддельный cooker: копирует вход в выход.
        self.cooker_script = self.root / "fake_cooker.py"
        self.cooker_script.write_text("import shutil, sys\nshutil.copy(sys.argv[1], sys.argv[2])\n", encoding="utf-8")
        self.cooker = [sys.executable, str(self.cooker_script)]

    def tearDown(self):
        self.tmp.cleanup()

    def fake_blender(self, report):
        def run(blender, paths, asset, entry, out_dir):
            out_dir.mkdir(parents=True, exist_ok=True)
            self.last_entry = entry
            for name in report["files"].values():
                (out_dir / name).write_bytes(b"glb:" + name.encode())
            return report
        return run

    def test_full_flow_writes_cooked_files_and_credits(self):
        with mock.patch.object(han_fetch, "run_blender", self.fake_blender(fake_report())):
            han_fetch.process_asset(self.asset, self.paths, "blender", self.cooker)
        cooked = sorted(p.name for p in self.paths.cooked.iterdir())
        self.assertEqual(cooked, ["PR_TestBarrel.col.hmesh", "PR_TestBarrel.hmesh", "PR_TestBarrel.lod1.hmesh"])
        self.assertTrue(credits.has_row(self.paths.credits, self.asset))
        self.assertEqual(self.last_entry.name, "b.glb")
        self.assertEqual(len(self.asset["sha256"]), 64)  # загрузка выполнилась автоматически

    def test_over_budget_stops_before_cooking_and_credits(self):
        with mock.patch.object(han_fetch, "run_blender", self.fake_blender(fake_report(lod0=99999))):
            with self.assertRaises(han_fetch.PipelineError):
                han_fetch.process_asset(self.asset, self.paths, "blender", self.cooker)
        self.assertFalse(self.paths.cooked.exists())
        self.assertFalse(credits.has_row(self.paths.credits, self.asset))

    def test_missing_tools_give_clear_errors(self):
        with self.assertRaises(han_fetch.PipelineError) as ctx:
            han_fetch.process_asset(self.asset, self.paths, None, self.cooker)
        self.assertIn("Blender", str(ctx.exception))
        with self.assertRaises(han_fetch.PipelineError) as ctx:
            han_fetch.process_asset(self.asset, self.paths, "blender", None)
        self.assertIn("han_cooker", str(ctx.exception))

    def test_cooker_failure_is_reported(self):
        failing = self.root / "failing.py"
        failing.write_text("import sys\nprint('плохой файл'); sys.exit(3)\n", encoding="utf-8")
        with mock.patch.object(han_fetch, "run_blender", self.fake_blender(fake_report())):
            with self.assertRaises(han_fetch.PipelineError) as ctx:
                han_fetch.process_asset(self.asset, self.paths, "blender", [sys.executable, str(failing)])
        self.assertIn("плохой файл", str(ctx.exception))
        self.assertFalse(credits.has_row(self.paths.credits, self.asset))

    def test_unsupported_kinds_are_refused(self):
        with self.assertRaises(han_fetch.PipelineError):
            han_fetch.process_asset(dict(self.asset, kind="font"), self.paths, "blender", self.cooker)

    def texture_cooker(self):
        script = self.root / "tex_cooker.py"
        script.write_text("import sys\nargs = sys.argv[1:]\nopen(args[-1], 'wb').write(('|'.join(args)).encode())\n", encoding="utf-8")
        return [sys.executable, str(script)]

    def test_texture_is_cooked_to_ktx2_with_linear_flag_for_data_maps(self):
        for name in ("diff.jpg", "nor.jpg"):
            (self.root / "inbox" / name).write_bytes(b"JPG")
        asset = dict(self.asset, id="tx_test", name="TX_test", kind="texture", source={"type": "local", "file": "inbox/diff.jpg"})
        han_fetch.process_asset(asset, self.paths, None, self.texture_cooker())  # Blender для текстур не нужен
        out = self.paths.cooked / "tx_test" / "diff.ktx2"
        self.assertTrue(out.is_file())
        self.assertNotIn("--linear", out.read_text())      # альбедо — sRGB
        self.assertIn("--texture", out.read_text())
        self.assertTrue(credits.has_row(self.paths.credits, asset))
        # Нормали грузим вручную в ту же загрузку: линейный режим
        shutil_copy = self.paths.downloads / "tx_test" / "nor.jpg"
        shutil_copy.write_bytes(b"JPG")
        han_fetch.process_asset(asset, self.paths, None, self.texture_cooker(), write_credits=False)
        self.assertIn("--linear", (self.paths.cooked / "tx_test" / "nor.ktx2").read_text())

    def test_texture_without_albedo_is_an_error(self):
        (self.root / "inbox" / "nor.jpg").write_bytes(b"JPG")
        asset = dict(self.asset, id="tx_bad", name="TX_bad", kind="texture", source={"type": "local", "file": "inbox/nor.jpg"})
        with self.assertRaises(han_fetch.PipelineError) as ctx:
            han_fetch.process_asset(asset, self.paths, None, self.texture_cooker())
        self.assertIn("альбедо", str(ctx.exception))
        self.assertFalse(credits.has_row(self.paths.credits, asset))

    def test_font_is_copied_with_license_text_and_needs_no_tools(self):
        (self.root / "inbox" / "font.ttf").write_bytes(b"TTF")
        asset = dict(self.asset, id="fn_test", name="FN_test", kind="font", source={"type": "local", "file": "inbox/font.ttf"})
        han_fetch.process_asset(asset, self.paths, None, None)  # ни Blender, ни han_cooker шрифту не нужны
        self.assertEqual((self.paths.cooked / "fn_test" / "font.ttf").read_bytes(), b"TTF")
        self.assertTrue(credits.has_row(self.paths.credits, asset))

    def test_font_without_font_file_is_an_error(self):
        (self.root / "inbox" / "readme.txt").write_text("нет шрифта", encoding="utf-8")
        asset = dict(self.asset, id="fn_bad", name="FN_bad", kind="font", source={"type": "local", "file": "inbox/readme.txt"})
        with self.assertRaises(han_fetch.PipelineError):
            han_fetch.process_asset(asset, self.paths, None, None)
        self.assertFalse(credits.has_row(self.paths.credits, asset))

    def test_texture_needs_cooker(self):
        (self.root / "inbox" / "diff.jpg").write_bytes(b"JPG")
        asset = dict(self.asset, id="tx_nc", name="TX_nc", kind="texture", source={"type": "local", "file": "inbox/diff.jpg"})
        with self.assertRaises(han_fetch.PipelineError) as ctx:
            han_fetch.process_asset(asset, self.paths, None, None)
        self.assertIn("han_cooker", str(ctx.exception))

    def test_add_command_validates_and_saves(self):
        argv = ["--root", str(self.root), "add", "--id", "pr_new", "--name", "PR_New", "--zone", "A", "--source", "local", "--file", "inbox/b.glb",
                "--author", "Я", "--license", "CC0", "--used-in", "тест", "--modified", "да — упрощён"]
        self.assertEqual(han_fetch.main(argv), 0)
        data = manifest.load(self.paths.manifest)
        self.assertEqual(data["assets"][0]["id"], "pr_new")
        self.assertEqual(han_fetch.main(argv), 1)  # повтор — отказ
        bad = [*argv[:-8], "--author", "Я", "--license", "CC-BY-NC", "--used-in", "т", "--modified", "м"]
        bad[bad.index("pr_new")] = "pr_bad"
        bad[bad.index("PR_New")] = "PR_Bad"
        self.assertEqual(han_fetch.main(bad), 1)
        self.assertEqual(len(manifest.load(self.paths.manifest)["assets"]), 1)

    def test_verify_reports_missing_credits_and_bad_hash(self):
        data = {"version": 1, "assets": [self.asset]}
        manifest.save(self.paths.manifest, data)
        self.assertEqual(han_fetch.main(["--root", str(self.root), "verify"]), 1)  # нет строки в кредитах
        credits.ensure_row(self.paths.credits, self.asset)
        self.assertEqual(han_fetch.main(["--root", str(self.root), "verify"]), 0)
        folder = self.paths.downloads / self.asset["id"]
        folder.mkdir(parents=True)
        (folder / "x.glb").write_bytes(b"1")
        self.asset["sha256"] = "1" * 64
        manifest.save(self.paths.manifest, data)
        self.assertEqual(han_fetch.main(["--root", str(self.root), "verify"]), 1)  # хэш не совпал


@unittest.skipUnless(han_fetch.find_blender(), "Blender не установлен")
class RealBlenderTests(unittest.TestCase):
    """Настоящий Blender: высокополигональная модель в сантиметрах проходит весь конвейер до GLB."""

    def test_pipeline_meets_budget_and_produces_lods_and_collision(self):
        blender = han_fetch.find_blender()
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            model = root / "highpoly.glb"
            subprocess.run([blender, "--background", "--python", str(Path(__file__).with_name("make_test_model.py")), "--", str(model)],
                           check=True, capture_output=True, timeout=300)
            asset = sample_asset(name="PR_Barrel", id="pr_barrel", pipeline={"scale": 0.01, "budget_triangles": 2500, "max_texture_size": 512},
                                 source={"type": "local", "file": str(model)})
            paths = han_fetch.Paths(REPO)
            out_dir = root / "out"
            report = han_fetch.run_blender(blender, paths, asset, model, out_dir)

            self.assertLessEqual(report["triangles"]["lod0"], 2500)
            self.assertGreater(report["original_triangles"], 10000)
            self.assertLess(report["triangles"]["lod1"], report["triangles"]["lod0"])
            self.assertLess(report["triangles"]["lod3"], report["triangles"]["lod2"])
            self.assertLessEqual(report["collision_vertices"], 32)
            self.assertTrue(report["has_uv"] is True or any("UV" in w for w in report["warnings"]))
            self.assertTrue({"Coca_Barrel_Body", "Samsung_Crate"} <= {h["name"] for h in report["brand_hits"]})
            for name in report["files"].values():
                self.assertTrue((out_dir / name).is_file(), name)
            # Сантиметры стали метрами: без масштаба 0,01 размеры были бы в десятках и сотнях. Высота 2,05 м — бочка 1 м плюс
            # «скрытый» кубик на высоте 2 м (в GLB флаг скрытия не сохраняется, поэтому он остаётся в модели).
            self.assertAlmostEqual(report["size_meters"]["y"], 2.05, delta=0.1)
            self.assertAlmostEqual(report["size_meters"]["x"], 1.9, delta=0.1)

            with self.assertRaises(han_fetch.PipelineError):  # бренды останавливают конвейер до ручной проверки
                han_fetch.check_report(asset, report)


if __name__ == "__main__":
    unittest.main()
