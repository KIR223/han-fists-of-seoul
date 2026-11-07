"""Этап 07: реестр материалов города находит основную папку, свободный аналог и плоский fallback."""
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
SCRIPT = ROOT / "han/tools/pipeline/material_inventory.py"
KTX2_ID = b"\xabKTX 20\xbb\r\n\x1a\n"


def write_ktx2(path: Path, vk_format: int, levels: int = 5):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(KTX2_ID + struct.pack("<9I", vk_format, 1, 256, 256, 0, 0, 1, levels, 0) + b"\0" * 32)


class MaterialInventoryTest(unittest.TestCase):
    def run_inventory(self, header: str, cooked: Path):
        h = cooked.parent / "slots.h"
        h.write_text(header, encoding="utf-8")
        out = cooked.parent / "out.json"
        subprocess.run([sys.executable, "-I", str(SCRIPT), "--cooked", str(cooked), "--header", str(h), "--json", str(out)], check=True, capture_output=True)
        import json
        return {r["material"]: r for r in json.loads(out.read_text(encoding="utf-8"))}

    def test_primary_alt_fallback_and_color_space(self):
        header = ('{world::GeoMaterial::Road, "tx_main", {0.2f, 0.2f, 0.2f}, 0.9f, "tx_free"},\n'
                  '{world::GeoMaterial::Park, "tx_lib_missing", {0.2f, 0.2f, 0.2f}, 0.9f, "tx_free"},\n'
                  '{world::GeoMaterial::Roof, "tx_none", {0.2f, 0.2f, 0.2f}, 0.9f},\n'
                  '{world::GeoMaterial::Storefront, nullptr, {0.2f, 0.2f, 0.2f}, 0.5f},\n')
        with tempfile.TemporaryDirectory() as tmp:
            cooked = Path(tmp) / "cooked"
            write_ktx2(cooked / "tx_main/diff.ktx2", 43)
            write_ktx2(cooked / "tx_main/nor.ktx2", 43)  # нормаль в sRGB — ошибка
            write_ktx2(cooked / "tx_free/diff.ktx2", 43)
            write_ktx2(cooked / "tx_free/nor.ktx2", 37)
            write_ktx2(cooked / "tx_free/rough.ktx2", 37)
            rows = self.run_inventory(header, cooked)
        self.assertEqual(rows["Road"]["used"], "tx_main")
        self.assertIn("нет карт: rough", rows["Road"]["status"])
        self.assertIn("nor в sRGB", rows["Road"]["status"])
        self.assertEqual(rows["Park"]["used"], "tx_free")
        self.assertEqual(rows["Park"]["status"], "свободный аналог")
        self.assertTrue(rows["Roof"]["status"].startswith("fallback"))
        self.assertIn("процедурный", rows["Storefront"]["status"])


if __name__ == "__main__":
    unittest.main()
