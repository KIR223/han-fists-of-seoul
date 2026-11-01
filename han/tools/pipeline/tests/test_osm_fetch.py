"""Тесты загрузчика OSM (без сети: ответы подменяются)."""
import io
import sys
import tempfile
import unittest
import urllib.error
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import han_osm_fetch as f  # noqa: E402

GOOD = b'<?xml version="1.0"?><osm version="0.6"><node id="1" lat="1" lon="2"/><way id="5"><nd ref="1"/></way></osm>'


class FakeResponse(io.BytesIO):
    def __enter__(self):
        return self

    def __exit__(self, *exc):
        return False


class BboxTests(unittest.TestCase):
    def test_preset_is_valid(self):
        self.assertEqual(f.validate_bbox(f.PRESETS["mapo"]), f.PRESETS["mapo"])

    def test_rejects_swapped_and_out_of_range(self):
        for bad in [(37.6, 126.9, 37.5, 127.0), (37.5, 127.0, 37.6, 126.9), (-91, 0, 0, 1), (0, 0, 91, 1), (0, -181, 1, 0)]:
            with self.assertRaises(f.FetchError):
                f.validate_bbox(bad)

    def test_rejects_too_large_unless_allowed(self):
        with self.assertRaises(f.FetchError):
            f.validate_bbox((37.0, 126.0, 37.5, 127.0))
        self.assertEqual(f.validate_bbox((37.0, 126.0, 37.5, 127.0), allow_large=True), (37.0, 126.0, 37.5, 127.0))


class QueryTests(unittest.TestCase):
    def test_query_has_all_layers_and_box(self):
        q = f.build_query((37.5, 126.9, 37.51, 126.91))
        for part in ('"highway"', '"building"', '"water"', '"park"', "(37.5,126.9,37.51,126.91)", "out body"):
            self.assertIn(part, q)


class DownloadTests(unittest.TestCase):
    def test_https_only(self):
        with self.assertRaises(f.FetchError):
            f.download("http://example.com/api", "q")

    def test_sends_user_agent_and_returns_body(self):
        with mock.patch("urllib.request.urlopen", return_value=FakeResponse(GOOD)) as m:
            self.assertEqual(f.download("https://example.com/api", "q"), GOOD)
        request = m.call_args[0][0]
        self.assertIn("han-fists-of-seoul", request.get_header("User-agent"))

    def test_http_error_becomes_fetch_error(self):
        err = urllib.error.HTTPError("https://x", 406, "Not Acceptable", {}, None)
        with mock.patch("urllib.request.urlopen", side_effect=err):
            with self.assertRaisesRegex(f.FetchError, "406"):
                f.download("https://example.com/api", "q")

    def test_network_error_becomes_fetch_error(self):
        with mock.patch("urllib.request.urlopen", side_effect=urllib.error.URLError("нет сети")):
            with self.assertRaisesRegex(f.FetchError, "нет связи"):
                f.download("https://example.com/api", "q")

    def test_size_cap(self):
        with mock.patch.object(f, "MAX_BYTES", 100), mock.patch("urllib.request.urlopen", return_value=FakeResponse(b"x" * 500)):
            with self.assertRaisesRegex(f.FetchError, "больше"):
                f.download("https://example.com/api", "q")


class CheckTests(unittest.TestCase):
    def test_good_answer(self):
        self.assertEqual(f.check_osm(GOOD), 1)

    def test_html_error_page_is_rejected(self):
        with self.assertRaisesRegex(f.FetchError, "не похож"):
            f.check_osm(b"<html><title>406 Not Acceptable</title></html>")

    def test_empty_box_is_rejected(self):
        with self.assertRaisesRegex(f.FetchError, "нет ни одной линии"):
            f.check_osm(b'<?xml version="1.0"?><osm version="0.6"></osm>')

    def test_server_runtime_error_is_rejected(self):
        with self.assertRaisesRegex(f.FetchError, "оборвал"):
            f.check_osm(b'<osm><way id="1"/><remark> runtime error: Query timed out </remark></osm>')


class MainTests(unittest.TestCase):
    def test_full_run_writes_file_and_license_note(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "osm" / "t.osm"
            with mock.patch("urllib.request.urlopen", return_value=FakeResponse(GOOD)):
                self.assertEqual(f.main(["--out", str(out)]), 0)
            self.assertEqual(out.read_bytes(), GOOD)
            note = out.with_name("t.osm.license.txt").read_text(encoding="utf-8")
            self.assertIn("ODbL", note)
            self.assertIn("openstreetmap.org/copyright", note)
            self.assertFalse(out.with_suffix(".osm.part").exists())

    def test_existing_file_is_kept_without_force(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "t.osm"
            out.write_bytes(b"old")
            with mock.patch("urllib.request.urlopen") as m:
                self.assertEqual(f.main(["--out", str(out)]), 0)
                m.assert_not_called()
            self.assertEqual(out.read_bytes(), b"old")

    def test_failed_download_leaves_no_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "t.osm"
            with mock.patch("urllib.request.urlopen", return_value=FakeResponse("<html>ошибка</html>".encode("utf-8"))):
                self.assertEqual(f.main(["--out", str(out)]), 1)
            self.assertFalse(out.exists())

    def test_bad_bbox_exits_with_error(self):
        self.assertEqual(f.main(["--bbox", "10", "10", "5", "20", "--out", "x.osm"]), 1)


if __name__ == "__main__":
    unittest.main()
