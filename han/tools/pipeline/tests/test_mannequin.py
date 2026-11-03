"""Проверяем сохранность реального skin/ключей и отказ при неподходящем rig."""
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import helpers  # добавляет каталог pipeline в sys.path
import mannequin
import fetcher


def fixture():
    names = ["pelvis", "Head", "hand_l", "hand_r", "thumb_01_l", "index_03_r", "foot_l", "foot_r"]
    return {"asset": {"version": "2.0"}, "scenes": [{"nodes": [0]}],
            "nodes": [{"name": "Armature"}] + [{"name": n} for n in names],
            "skins": [{"joints": list(range(1, len(names) + 1))}],
            "meshes": [{"name": "Mannequin", "primitives": [{"attributes": {"POSITION": 0, "NORMAL": 0, "JOINTS_0": 0, "WEIGHTS_0": 0}, "indices": 1}]}],
            "accessors": [{"min": [-1, 0.01, -0.2], "max": [1, 1.84, 0.2]}, {"count": 6}],
            "materials": [{"name": "original"}],
            "animations": [{"name": name, "channels": [{"target": {"node": 2, "path": "rotation"}}]} for name in mannequin.REQUIRED_CLIPS]}


class MannequinTests(unittest.TestCase):
    def test_skin_keys_and_binary_remain_identical(self):
        doc = fixture()
        with tempfile.TemporaryDirectory() as temp:
            source, target = Path(temp) / "input.glb", Path(temp) / "output.glb"
            binary = [(0x004E4942, b"abcd" * 4)]
            mannequin.write_glb(source, doc, binary)
            report = mannequin.prepare(source, target)
            out, chunks = mannequin.read_glb(target)
            self.assertEqual(chunks, binary)
            for field in ("skins", "animations", "accessors", "meshes"):
                self.assertEqual(out[field], doc[field])
            parent = out["nodes"][-1]
            self.assertAlmostEqual(1.84 * parent["scale"][1] + parent["translation"][1], 1.8)
            self.assertAlmostEqual(0.01 * parent["scale"][1] + parent["translation"][1], 0)
            self.assertEqual(parent["children"], [0])
            self.assertEqual(report["triangles"], 2)

    def test_missing_fingers_weights_clips_and_changed_scale_are_rejected(self):
        changes = [lambda d: d["nodes"][5].update(name="missing"),
                   lambda d: d["meshes"][0]["primitives"][0]["attributes"].pop("WEIGHTS_0"),
                   lambda d: d["animations"].pop(),
                   lambda d: d["nodes"][0].update(scale=[100, 100, 100])]
        with tempfile.TemporaryDirectory() as temp:
            for change in changes:
                doc = fixture()
                change(doc)
                source = Path(temp) / "input.glb"
                mannequin.write_glb(source, doc, [])
                with self.assertRaises(ValueError):
                    mannequin.prepare(source, Path(temp) / "output.glb")

    def test_truncated_glb_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / "bad.glb"
            source.write_bytes(b"glTF")
            with self.assertRaises(ValueError):
                mannequin.read_glb(source)

    def test_paid_itch_file_not_requested(self):
        class Response:
            def __init__(self, data): self.data = data
            def __enter__(self): return self
            def __exit__(self, *args): pass
            def read(self, size): return self.data
        opener = mock.Mock()
        opener.open.side_effect = [Response(b'<meta name="csrf_token" value="anonymous">'),
                                  Response(b'{"url":"https://quaternius.itch.io/free"}'), Response(b'<div>No uploads</div>')]
        with mock.patch.object(fetcher.urllib.request, "build_opener", return_value=opener):
            with self.assertRaises(fetcher.FetchError):
                fetcher.resolve_itch("https://quaternius.itch.io/universal-animation-library", "123")
        self.assertEqual(opener.open.call_count, 3)
