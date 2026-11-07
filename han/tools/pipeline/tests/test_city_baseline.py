import json
import tempfile
from pathlib import Path
import unittest
import helpers  # noqa: F401  (добавляет каталог конвейера в sys.path)
import city_baseline as cb

SCENES = Path(__file__).resolve().parents[4] / "data" / "city" / "baseline_scenes.json"


class CityBaselineTests(unittest.TestCase):
    def test_repo_scenes_are_valid_and_cover_plan_routes(self):
        data = cb.load_scenes(SCENES)
        ids = {s["id"] for s in data["scenes"]}
        for needed in ("mapo_hongdae", "mapo_yeonnam", "mapo_mangwon", "mapo_hapjeong", "kandam_boulevard", "slope_41",
                       "junction_sinchon", "trunk_riverside", "aerial_far_view", "kandam_interior"):
            self.assertIn(needed, ids)
        self.assertTrue({"han_bridge_north_south", "han_bridge_south_north"} <= ids)  # мост проверяется в обе стороны

    def test_bad_scene_files_are_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "s.json"
            for bad in ({"version": 2, "scenes": []},
                        {"version": 1, "scenes": [{"id": "a", "camera": [1, 2, 3]}]},
                        {"version": 1, "scenes": [{"id": "a", "camera": [0, 0, 0, 0, 0]}, {"id": "a", "camera": [0, 0, 0, 0, 0]}]},
                        {"version": 1, "scenes": [{"id": "a", "camera": [0, 0, 0, 0, 0], "hours": [25]}]}):
                path.write_text(json.dumps(bad), encoding="utf-8")
                with self.assertRaises(ValueError):
                    cb.load_scenes(path)

    def test_command_is_deterministic_and_uses_scene_values(self):
        scene = {"id": "x", "camera": [1, 2, 3, 4, 5], "frames": 50}
        cmd = cb.build_command("demo.exe", scene, {"size": [640, 360], "warmup": 10, "traffic": 7}, 21, Path("m.json"), True)
        self.assertEqual(cmd[:2], ["demo.exe", "--camera"])
        self.assertIn("--deterministic", cmd)
        self.assertIn("--no-vsync", cmd)
        self.assertIn("--no-capture", cmd)  # захват мыши поворачивал камеру и менял кадр
        self.assertEqual(cmd[cmd.index("--frames") + 1], "50")
        self.assertEqual(cmd[cmd.index("--size") + 1:cmd.index("--size") + 3], ["640", "360"])
        self.assertEqual(cmd[cmd.index("--traffic") + 1], "7")

    def test_extra_arguments_are_appended_after_scene_arguments(self):
        scene = {"id": "x", "camera": [1, 2, 3, 4, 5]}
        cmd = cb.build_command("demo.exe", scene, {}, 13, Path("m.json"), False, None, ["--osm", "a b.osm", "--no-validation"])
        self.assertEqual(cmd[-3:], ["--osm", "a b.osm", "--no-validation"])
        plain = cb.build_command("demo.exe", scene, {}, 13, Path("m.json"), False)
        self.assertNotIn("--no-validation", plain)

    def test_missing_data_is_reported_relative_to_root(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "have.bin").write_bytes(b"x")
            scene = {"requires": ["have.bin", "gone/file.hmesh"]}
            self.assertEqual(cb.missing_data(scene, root), ["gone/file.hmesh"])

    def test_markdown_marks_missing_data_and_blocked_scenes(self):
        row = {"count": 3, "mean": 8.0, "p50": 8.0, "p95": 9.0, "p99": 9.5, "max": 10.0}
        metrics = {k: row for k in ("wall_ms", "cpu_prep_ms", "gpu_shadow_ms", "gpu_scene_ms", "gpu_total_ms", "draw_calls", "triangles", "cars_simulated", "people_drawn")}
        summary = {"environment": {"date": "d", "git_branch": "b", "git_sha": "0123456789abcdef", "git_dirty_files": 0, "os": "os"}, "present_mode": "FIFO",
                   "runs": [{"scene": "a", "hour": 13, "status": "ok", "data_missing": ["assets_bin/cooked/kandam/kandam_district.hmesh"], "metrics": metrics},
                            {"scene": "b", "hour": 13, "status": "blocked", "reason": "нет данных"}]}
        text = cb.render_markdown(summary)
        self.assertIn("kandam_district.hmesh", text)
        self.assertIn("blocked", text)
        self.assertIn("8.00 / 9.00 / 9.50 / 10.00", text)


if __name__ == "__main__":
    unittest.main()
