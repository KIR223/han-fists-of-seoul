"""Тесты инструментов скриншот-теста (без видеокарты): PNG, сравнение, картинка различий, список сцен."""
import tempfile
import unittest
from pathlib import Path

import png_tools
import run
from png_tools import Image


def solid(width, height, color, channels=3):
    return Image(width, height, channels, [bytearray(color * width) for _ in range(height)])


class PngTests(unittest.TestCase):
    def test_roundtrip_rgb_and_rgba(self):
        with tempfile.TemporaryDirectory() as tmp:
            for channels, color in ((3, bytes([10, 200, 30])), (4, bytes([10, 200, 30, 255]))):
                image = solid(17, 9, color, channels)
                image.rows[4][3] = 99  # одиночное отличие внутри картинки
                path = Path(tmp) / f"x{channels}.png"
                png_tools.write_png(path, image)
                back = png_tools.read_png(path)
                self.assertEqual((back.width, back.height, back.channels), (17, 9, channels))
                self.assertEqual([bytes(r) for r in back.rows], [bytes(r) for r in image.rows])

    def test_reads_all_filter_types(self):
        # Строки закодированы вручную со всеми пятью фильтрами PNG и должны декодироваться в один и тот же рисунок.
        import struct
        import zlib
        width, height = 4, 5
        pixels = [bytes((x * 40 + y * 7) % 256 for x in range(width * 3)) for y in range(height)]
        stride = width * 3

        def encode(filter_type, row, prev):
            out = bytearray()
            for x in range(stride):
                a = row[x - 3] if x >= 3 else 0
                b = prev[x]
                c = prev[x - 3] if x >= 3 else 0
                if filter_type == 0:
                    predicted = 0
                elif filter_type == 1:
                    predicted = a
                elif filter_type == 2:
                    predicted = b
                elif filter_type == 3:
                    predicted = (a + b) >> 1
                else:
                    p = a + b - c
                    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                    predicted = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                out.append((row[x] - predicted) & 255)
            return bytes([filter_type]) + bytes(out)

        raw, prev = b"", bytes(stride)
        for y, row in enumerate(pixels):
            raw += encode(y % 5, row, prev)
            prev = row
        data = (b"\x89PNG\r\n\x1a\n" + png_tools._chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
                png_tools._chunk(b"IDAT", zlib.compress(raw)) + png_tools._chunk(b"IEND", b""))
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "f.png"
            path.write_bytes(data)
            self.assertEqual([bytes(r) for r in png_tools.read_png(path).rows], pixels)

    def test_rejects_garbage(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "bad.png"
            path.write_bytes(b"not a png at all")
            with self.assertRaises(ValueError):
                png_tools.read_png(path)

    def test_stored_deflate_from_engine_style_writer(self):
        # Несжатые блоки deflate (как пишет движок) — обычный zlib-поток и читаются так же.
        import struct
        import zlib
        raw = b"\x00" + bytes(range(12))
        store = b"\x78\x01" + b"\x01" + struct.pack("<HH", len(raw), ~len(raw) & 0xFFFF) + raw + struct.pack(">I", zlib.adler32(raw))
        data = (b"\x89PNG\r\n\x1a\n" + png_tools._chunk(b"IHDR", struct.pack(">IIBBBBB", 4, 1, 8, 2, 0, 0, 0)) +
                png_tools._chunk(b"IDAT", store) + png_tools._chunk(b"IEND", b""))
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "s.png"
            path.write_bytes(data)
            self.assertEqual(bytes(png_tools.read_png(path).rows[0]), bytes(range(12)))


class CompareTests(unittest.TestCase):
    def test_identical_images(self):
        d = png_tools.compare(solid(8, 8, bytes([5, 6, 7])), solid(8, 8, bytes([5, 6, 7])))
        self.assertEqual((d.mean_abs, d.bad_fraction, d.max_abs), (0.0, 0.0, 0))

    def test_small_noise_is_not_bad_but_large_change_is(self):
        a, b = solid(10, 10, bytes([100, 100, 100])), solid(10, 10, bytes([102, 100, 99]))
        d = png_tools.compare(a, b)
        self.assertAlmostEqual(d.mean_abs, 1.0, places=6)  # (2 + 0 + 1) / 3
        self.assertEqual(d.bad_fraction, 0.0)
        c = solid(10, 10, bytes([100, 100, 100]))
        for x in range(0, 30, 3):
            c.rows[0][x] = 200  # весь первый ряд сильно отличается
        d2 = png_tools.compare(c, a)
        self.assertAlmostEqual(d2.bad_fraction, 0.1)
        self.assertEqual(d2.max_abs, 100)

    def test_size_mismatch_is_an_error(self):
        with self.assertRaises(ValueError):
            png_tools.compare(solid(4, 4, bytes([0, 0, 0])), solid(5, 4, bytes([0, 0, 0])))

    def test_difference_image_highlights_changes(self):
        a, b = solid(3, 3, bytes([50, 50, 50])), solid(3, 3, bytes([50, 50, 50]))
        b.rows[1][3:6] = bytes([60, 50, 55])
        diff = png_tools.difference_image(a, b, gain=8)
        self.assertEqual(bytes(diff.rows[0]), bytes(9))
        self.assertEqual(bytes(diff.rows[1][3:6]), bytes([80, 0, 40]))


class SceneListTests(unittest.TestCase):
    def test_scenes_are_defined_and_have_valid_args(self):
        self.assertGreaterEqual(len(run.SCENES), 6)
        for name, args in run.SCENES.items():
            self.assertRegex(name, r"^[a-z0-9_]+$")
            self.assertTrue(all(isinstance(a, str) for a in args), name)
            for flag, count in (("--sun", 3), ("--camera", 5)):
                if flag in args:
                    i = args.index(flag)
                    self.assertLessEqual(i + count, len(args) - 1, f"{name}: у {flag} не хватает чисел")
                    for value in args[i + 1:i + 1 + count]:
                        float(value)  # числа, а не другой флаг

    def test_goldens_exist_for_every_scene(self):
        missing = [n for n in run.SCENES if not (run.GOLDEN / f"{n}.png").is_file()]
        self.assertEqual(missing, [], "нет эталонов: запустите run.py --update на машине с видеокартой")


if __name__ == "__main__":
    unittest.main()
