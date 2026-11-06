import math
import tempfile
from pathlib import Path
import unittest
import numpy as np
from PIL import Image
import helpers  # noqa: F401
import image_sharpness as sh


def step_image(width=64, height=32, blur=0):
    row = np.where(np.arange(width) < width // 2, 40.0, 220.0)
    for _ in range(blur):
        row = np.convolve(np.pad(row, 1, mode="edge"), [0.25, 0.5, 0.25], mode="valid")
    return np.tile(row, (height, 1))


class SharpnessTests(unittest.TestCase):
    def test_blur_lowers_gradient_ratio_and_identical_gives_infinite_psnr(self):
        sharp, soft = step_image(), step_image(blur=3)
        self.assertEqual(sh.compare(sharp, sharp)["psnr"], math.inf)
        r = sh.compare(sharp, soft)
        self.assertLess(r["gradient_ratio"], 0.8)
        self.assertLess(r["psnr"], 60)

    def test_downsample_averages_blocks_and_rejects_bad_factor(self):
        image = np.arange(16, dtype=float).reshape(4, 4)
        small = sh.box_downsample(image, 2)
        self.assertEqual(small.shape, (2, 2))
        self.assertEqual(small[0, 0], (0 + 1 + 4 + 5) / 4)
        with self.assertRaises(ValueError):
            sh.box_downsample(image, 0)

    def test_crop_and_shape_mismatch_are_checked(self):
        image = step_image()
        with self.assertRaises(ValueError):
            sh.crop(image, (0, 0, 1000, 10))
        with self.assertRaises(ValueError):
            sh.compare(image, image[:, :-2])
        self.assertEqual(sh.crop(image, (4, 2, 10, 8)).shape, (8, 10))

    def test_cli_reads_png_files_with_supersampled_reference(self):
        with tempfile.TemporaryDirectory() as tmp:
            big = np.repeat(np.repeat(step_image(), 2, axis=0), 2, axis=1)
            ref, cand = Path(tmp) / "r.png", Path(tmp) / "c.png"
            Image.fromarray(np.stack([big] * 3, axis=-1).astype(np.uint8)).save(ref)
            Image.fromarray(np.stack([step_image()] * 3, axis=-1).astype(np.uint8)).save(cand)
            self.assertEqual(sh.main([str(ref), str(cand), "--reference-downsample", "2", "--json"]), 0)
            result = sh.compare(sh.box_downsample(sh.load_luma(ref), 2), sh.load_luma(cand))
            self.assertAlmostEqual(result["gradient_ratio"], 1.0, places=3)


if __name__ == "__main__":
    unittest.main()
