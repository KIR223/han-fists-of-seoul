import os
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest import mock

import helpers
import fetcher
import han_fetch
import manifest
from helpers import MockServer, json_body, make_zip, sample_asset


class DownloadTests(unittest.TestCase):
    def test_download_and_hash(self):
        with MockServer({"/a.bin": (200, b"hello", {})}) as server, tempfile.TemporaryDirectory() as tmp:
            digest = fetcher.download(f"{server.url}/a.bin", Path(tmp) / "x" / "a.bin")
            self.assertEqual((Path(tmp) / "x" / "a.bin").read_bytes(), b"hello")
            self.assertEqual(digest, "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824")
            self.assertEqual(list((Path(tmp) / "x").glob("*.part")), [])

    def test_size_limit_removes_partial_file(self):
        with MockServer({"/big": (200, b"x" * 5000, {})}) as server, tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(fetcher.FetchError):
                fetcher.download(f"{server.url}/big", Path(tmp) / "big", max_bytes=1000)
            self.assertEqual(list(Path(tmp).iterdir()), [])

    def test_http_errors_become_fetch_errors(self):
        with MockServer({}) as server, tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(fetcher.FetchError) as ctx:
                fetcher.download(f"{server.url}/нет", Path(tmp) / "f")
            self.assertIn("404", str(ctx.exception))

    def test_https_only_without_local_flag(self):
        with mock.patch.dict(os.environ, {"HAN_ALLOW_LOCAL_HTTP": "0"}):
            with self.assertRaises(fetcher.FetchError):
                fetcher.check_url("http://127.0.0.1:1/x")
            with self.assertRaises(fetcher.FetchError):
                fetcher.check_url("http://example.org/x")
            with self.assertRaises(fetcher.FetchError):
                fetcher.check_url("ftp://example.org/x")
            fetcher.check_url("https://example.org/x")
        with mock.patch.dict(os.environ, {"HAN_ALLOW_LOCAL_HTTP": "1"}):
            fetcher.check_url("http://127.0.0.1:1/x")
            with self.assertRaises(fetcher.FetchError):  # чужой хост по HTTP запрещён даже с флагом
                fetcher.check_url("http://example.org/x")

    def test_redirect_to_unsafe_address_is_refused(self):
        routes = {"/r": (302, b"", {"Location": "http://example.org/evil"})}
        with MockServer(routes) as server, tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(fetcher.FetchError):
                fetcher.download(f"{server.url}/r", Path(tmp) / "f")

    def test_redirect_inside_allowed_hosts_works(self):
        routes = {"/r": (302, b"", {"Location": "/real"}), "/real": (200, b"ok", {})}
        with MockServer(routes) as server, tempfile.TemporaryDirectory() as tmp:
            fetcher.download(f"{server.url}/r", Path(tmp) / "f")
            self.assertEqual((Path(tmp) / "f").read_bytes(), b"ok")


class ArchiveTests(unittest.TestCase):
    def test_extracts_normal_archive(self):
        with tempfile.TemporaryDirectory() as tmp:
            archive = Path(tmp) / "a.zip"
            make_zip(archive, {"scene.gltf": b"{}", "textures/t.png": b"png"})
            out = Path(tmp) / "out"
            out.mkdir()
            files = fetcher.extract_zip(archive, out)
            self.assertEqual({p.relative_to(out).as_posix() for p in files}, {"scene.gltf", "textures/t.png"})

    def test_rejects_zip_slip_and_absolute_paths(self):
        for evil in ("../evil.txt", "a/../../evil.txt", "/abs.txt", "C:/win.txt", "..\\evil.txt"):
            with self.subTest(name=evil), tempfile.TemporaryDirectory() as tmp:
                archive = Path(tmp) / "a.zip"
                make_zip(archive, {evil: b"x"})
                out = Path(tmp) / "out"
                out.mkdir()
                with self.assertRaises(fetcher.FetchError):
                    fetcher.extract_zip(archive, out)
                self.assertFalse((Path(tmp) / "evil.txt").exists())

    def test_rejects_symlinks(self):
        with tempfile.TemporaryDirectory() as tmp:
            archive = Path(tmp) / "a.zip"
            with zipfile.ZipFile(archive, "w") as zf:
                info = zipfile.ZipInfo("link")
                info.create_system = 3
                info.external_attr = 0o120777 << 16  # символьная ссылка
                zf.writestr(info, "/etc/passwd")
            out = Path(tmp) / "out"
            out.mkdir()
            with self.assertRaises(fetcher.FetchError):
                fetcher.extract_zip(archive, out)

    def test_archive_limits(self):
        with tempfile.TemporaryDirectory() as tmp:
            archive = Path(tmp) / "a.zip"
            make_zip(archive, {f"f{i}.txt": b"x" * 100 for i in range(10)})
            out = Path(tmp) / "out"
            out.mkdir()
            with mock.patch.object(fetcher, "MAX_ARCHIVE_FILES", 5), self.assertRaises(fetcher.FetchError):
                fetcher.extract_zip(archive, out)
            with mock.patch.object(fetcher, "MAX_ARCHIVE_BYTES", 500), self.assertRaises(fetcher.FetchError):
                fetcher.extract_zip(archive, out)

    def test_corrupt_archive(self):
        with tempfile.TemporaryDirectory() as tmp:
            archive = Path(tmp) / "a.zip"
            archive.write_bytes(b"not a zip")
            with self.assertRaises(fetcher.FetchError):
                fetcher.extract_zip(archive, Path(tmp))

    def test_tree_hash_is_stable_and_sensitive(self):
        with tempfile.TemporaryDirectory() as tmp:
            a, b = Path(tmp) / "a", Path(tmp) / "b"
            for d in (a, b):
                (d / "sub").mkdir(parents=True)
                (d / "x.txt").write_bytes(b"1")
                (d / "sub" / "y.txt").write_bytes(b"2")
            self.assertEqual(fetcher.tree_hash(a), fetcher.tree_hash(b))
            (b / "sub" / "y.txt").write_bytes(b"3")
            self.assertNotEqual(fetcher.tree_hash(a), fetcher.tree_hash(b))
            (b / "sub" / "y.txt").write_bytes(b"2")
            (b / "sub" / "y2.txt").write_bytes(b"")
            self.assertNotEqual(fetcher.tree_hash(a), fetcher.tree_hash(b))


class ResolverTests(unittest.TestCase):
    def polyhaven_routes(self, base="http://placeholder"):
        return {
            "/info/barrel_01": json_body({"authors": {"Автор Один": "all"}, "name": "Barrel"}),
            "/files/barrel_01": json_body({"gltf": {"1k": {"gltf": {
                "url": f"{base}/dl/barrel_01_1k.gltf", "size": 10,
                "include": {"barrel_01.bin": {"url": f"{base}/dl/barrel_01.bin"}, "textures/diff.jpg": {"url": f"{base}/dl/diff.jpg"}}}}}}),
            "/dl/barrel_01_1k.gltf": (200, b"{gltf}", {}),
            "/dl/barrel_01.bin": (200, b"BIN", {}),
            "/dl/diff.jpg": (200, b"JPG", {}),
        }

    def test_polyhaven_resolve(self):
        with MockServer({}) as server:
            server.routes.update(self.polyhaven_routes(server.url))
            resolved = fetcher.resolve_polyhaven("barrel_01", "1k", api=server.url)
            self.assertEqual(resolved.meta["license"], "CC0")
            self.assertEqual(resolved.meta["author"], "Автор Один")
            self.assertEqual({rel for _, rel in resolved.files}, {"barrel_01_1k.gltf", "barrel_01.bin", "textures/diff.jpg"})

    def test_polyhaven_texture_resolve(self):
        with MockServer({}) as server:
            server.routes["/info/asphalt_02"] = json_body({"authors": {"Rob Tuytel": "all"}})
            server.routes["/files/asphalt_02"] = json_body({
                "Diffuse": {"1k": {"jpg": {"url": f"{server.url}/dl/diff.jpg"}}},
                "nor_gl": {"1k": {"jpg": {"url": f"{server.url}/dl/nor.jpg"}}},
                "Rough": {"1k": {"jpg": {"url": f"{server.url}/dl/rough.jpg"}}},
            })
            resolved = fetcher.resolve_polyhaven_texture("asphalt_02", "1k", api=server.url)
            self.assertEqual(resolved.meta["license"], "CC0")
            self.assertEqual(resolved.meta["author"], "Rob Tuytel")
            self.assertEqual({rel for _, rel in resolved.files}, {"diff.jpg", "nor.jpg", "rough.jpg"})

    def test_polyhaven_texture_needs_albedo_but_not_other_maps(self):
        with MockServer({}) as server:
            server.routes["/info/t"] = json_body({"authors": {}})
            server.routes["/files/t"] = json_body({"Diffuse": {"1k": {"jpg": {"url": f"{server.url}/dl/diff.jpg"}}}})
            resolved = fetcher.resolve_polyhaven_texture("t", "1k", api=server.url)
            self.assertEqual([rel for _, rel in resolved.files], ["diff.jpg"])  # нормалей и шероховатости нет — не ошибка
            server.routes["/files/t"] = json_body({"nor_gl": {"1k": {"jpg": {"url": f"{server.url}/dl/nor.jpg"}}}})
            with self.assertRaises(fetcher.FetchError):
                fetcher.resolve_polyhaven_texture("t", "1k", api=server.url)

    def test_polyhaven_missing_resolution(self):
        with MockServer({}) as server:
            server.routes.update(self.polyhaven_routes(server.url))
            with self.assertRaises(fetcher.FetchError):
                fetcher.resolve_polyhaven("barrel_01", "8k", api=server.url)

    def test_polyhaven_rejects_unsafe_include_path(self):
        with MockServer({}) as server:
            server.routes["/info/x"] = json_body({"authors": {}})
            server.routes["/files/x"] = json_body({"gltf": {"1k": {"gltf": {"url": f"{server.url}/a", "include": {"../evil": {"url": f"{server.url}/b"}}}}}})
            with self.assertRaises(fetcher.FetchError):
                fetcher.resolve_polyhaven("x", "1k", api=server.url)

    def sketchfab_routes(self, base, slug="by", downloadable=True):
        return {
            "/models/abc123": json_body({"isDownloadable": downloadable, "license": {"slug": slug}, "user": {"displayName": "Мастер"},
                                         "viewerUrl": "https://sketchfab.com/3d-models/x-abc123"}),
            "/models/abc123/download": json_body({"gltf": {"url": f"{base}/signed/model.zip"}}),
        }

    def test_sketchfab_needs_token(self):
        with self.assertRaises(fetcher.FetchError):
            fetcher.resolve_sketchfab("abc123", "", api="http://127.0.0.1:1")

    def test_sketchfab_resolve_and_token_header(self):
        with MockServer({}) as server:
            server.routes.update(self.sketchfab_routes(server.url))
            resolved = fetcher.resolve_sketchfab("abc123", "SECRET", api=server.url)
            self.assertEqual(resolved.meta["license"], "CC-BY 4.0")
            self.assertEqual(resolved.meta["author"], "Мастер")
            self.assertTrue(all(headers.get("authorization") == "Token SECRET" for _, headers in server.requests))

    def test_sketchfab_not_downloadable(self):
        with MockServer({}) as server:
            server.routes.update(self.sketchfab_routes(server.url, downloadable=False))
            with self.assertRaises(fetcher.FetchError):
                fetcher.resolve_sketchfab("abc123", "T", api=server.url)


class FetchAssetTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.paths = han_fetch.Paths(self.root)

    def tearDown(self):
        self.tmp.cleanup()

    def test_direct_zip_fetch_sets_and_checks_hash(self):
        archive = self.root / "src.zip"
        make_zip(archive, {"model/barrel.glb": b"GLB!", "model/tex.png": b"PNG"})
        with MockServer({"/barrel.zip": (200, archive.read_bytes(), {})}) as server:
            asset = sample_asset(source={"type": "direct", "page": "https://example.org/p", "url": f"{server.url}/barrel.zip"})
            self.assertTrue(han_fetch.fetch_asset(asset, self.paths))
            self.assertEqual(len(asset["sha256"]), 64)
            self.assertTrue((self.paths.downloads / asset["id"] / "model" / "barrel.glb").is_file())
            self.assertFalse(list((self.paths.downloads / asset["id"]).glob("*.zip")))  # архив удалён, хэш — от содержимого

            again = han_fetch.fetch_asset(asset, self.paths)  # хэш совпадает — повторной загрузки нет
            self.assertFalse(again)
            n = len(server.requests)
            han_fetch.fetch_asset(asset, self.paths)
            self.assertEqual(len(server.requests), n)

    def test_hash_mismatch_is_refused_and_cleaned(self):
        with MockServer({"/f.glb": (200, b"v2", {})}) as server:
            asset = sample_asset(source={"type": "direct", "page": "https://example.org/p", "url": f"{server.url}/f.glb"}, sha256="0" * 64)
            with self.assertRaises(han_fetch.PipelineError) as ctx:
                han_fetch.fetch_asset(asset, self.paths)
            self.assertIn("хэш не совпал", str(ctx.exception))
            self.assertFalse((self.paths.downloads / asset["id"]).exists())
            han_fetch.fetch_asset(asset, self.paths, update_hash=True)  # владелец сознательно принимает новый хэш
            self.assertNotEqual(asset["sha256"], "0" * 64)

    def test_forbidden_and_unapproved_licenses_are_refused_before_network(self):
        with MockServer({}) as server:
            src = {"type": "direct", "page": "https://example.org/p", "url": f"{server.url}/f.glb"}
            with self.assertRaises(han_fetch.PipelineError):
                han_fetch.fetch_asset(sample_asset(source=src, license="CC-BY-NC 4.0"), self.paths)
            with self.assertRaises(han_fetch.PipelineError):
                han_fetch.fetch_asset(sample_asset(source=src, license="CC-BY-SA 4.0"), self.paths)
            self.assertEqual(server.requests, [])

    def test_polyhaven_end_to_end(self):
        with MockServer({}) as server:
            server.routes.update(ResolverTests().polyhaven_routes(server.url))
            asset = sample_asset(source={"type": "polyhaven", "page": "https://polyhaven.com/a/barrel_01", "asset_id": "barrel_01"})
            with mock.patch.dict(os.environ, {"HAN_POLYHAVEN_API": server.url}):
                han_fetch.fetch_asset(asset, self.paths)
            folder = self.paths.downloads / asset["id"]
            self.assertEqual((folder / "barrel_01.bin").read_bytes(), b"BIN")
            self.assertEqual((folder / "textures" / "diff.jpg").read_bytes(), b"JPG")
            self.assertEqual(han_fetch.find_entry(folder, "").name, "barrel_01_1k.gltf")

    def test_sketchfab_end_to_end_and_license_downgrade_detected(self):
        archive = self.root / "sf.zip"
        make_zip(archive, {"scene.gltf": b"{}", "scene.bin": b"B"})
        with MockServer({}) as server:
            server.routes.update(ResolverTests().sketchfab_routes(server.url))
            server.routes["/signed/model.zip"] = (200, archive.read_bytes(), {})
            asset = sample_asset(source={"type": "sketchfab", "page": "https://sketchfab.com/3d-models/x-abc123", "asset_id": "abc123"},
                                 license="CC-BY 4.0")
            env = {"HAN_SKETCHFAB_API": server.url, "SKETCHFAB_TOKEN": "TKN"}
            with mock.patch.dict(os.environ, env):
                han_fetch.fetch_asset(asset, self.paths)
            self.assertTrue((self.paths.downloads / asset["id"] / "scene.gltf").is_file())
            # Токен ушёл только на API, а подписанную ссылку скачали без него.
            signed = [h for p, h in server.requests if p == "/signed/model.zip"]
            self.assertEqual(len(signed), 1)
            self.assertNotIn("authorization", signed[0])

            # Автор сменил лицензию на NC — загрузка отклоняется.
            server.routes.update(ResolverTests().sketchfab_routes(server.url, slug="by-nc"))
            with mock.patch.dict(os.environ, env), self.assertRaises(han_fetch.PipelineError):
                han_fetch.fetch_asset(sample_asset(id="pr_other", name="PR_Other", source=asset["source"], license="CC-BY 4.0"), self.paths)

    def test_local_file(self):
        inbox = self.root / "assets_src" / "inbox"
        inbox.mkdir(parents=True)
        (inbox / "my.glb").write_bytes(b"LOCAL")
        asset = sample_asset(source={"type": "local", "file": "assets_src/inbox/my.glb"})
        han_fetch.fetch_asset(asset, self.paths)
        self.assertEqual((self.paths.downloads / asset["id"] / "my.glb").read_bytes(), b"LOCAL")
        missing = sample_asset(id="pr_missing", name="PR_Missing", source={"type": "local", "file": "assets_src/inbox/none.glb"})
        with self.assertRaises(han_fetch.PipelineError):
            han_fetch.fetch_asset(missing, self.paths)


if __name__ == "__main__":
    unittest.main()
