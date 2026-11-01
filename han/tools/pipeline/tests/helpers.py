"""Общие помощники тестов конвейера: пути, локальный HTTP-сервер, готовые записи манифеста."""
from __future__ import annotations

import http.server
import json
import os
import sys
import threading
import zipfile
from pathlib import Path

PIPELINE_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PIPELINE_DIR))
os.environ["HAN_ALLOW_LOCAL_HTTP"] = "1"  # HTTP разрешён только для 127.0.0.1 и только в тестах


class MockServer:
    """Локальный сервер: routes — {путь: (код, тело bytes, заголовки dict)}. Работает в отдельном потоке."""

    def __init__(self, routes: dict[str, tuple[int, bytes, dict[str, str]]]):
        self.routes = routes
        self.requests: list[tuple[str, dict[str, str]]] = []
        outer = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def do_GET(self):  # noqa: N802
                outer.requests.append((self.path, {k.lower(): v for k, v in self.headers.items()}))
                code, body, headers = outer.routes.get(self.path, (404, b"not found", {}))
                self.send_response(code)
                for key, value in headers.items():
                    self.send_header(key, value)
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

            def log_message(self, *args):  # тишина в выводе тестов
                pass

        self.httpd = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.url = f"http://127.0.0.1:{self.httpd.server_address[1]}"
        self.thread = threading.Thread(target=self.httpd.serve_forever, daemon=True)

    def __enter__(self):
        self.thread.start()
        return self

    def __exit__(self, *exc):
        self.httpd.shutdown()
        self.httpd.server_close()


def json_body(obj) -> tuple[int, bytes, dict[str, str]]:
    return 200, json.dumps(obj).encode("utf-8"), {"Content-Type": "application/json"}


def make_zip(path: Path, files: dict[str, bytes]) -> None:
    with zipfile.ZipFile(path, "w") as zf:
        for name, data in files.items():
            zf.writestr(name, data)


def sample_asset(**overrides) -> dict:
    asset = {
        "id": "pr_test_barrel", "name": "PR_TestBarrel", "kind": "model", "zone": "A",
        "source": {"type": "direct", "page": "https://example.org/asset/1", "url": "https://example.org/barrel.glb"},
        "author": "Автор Тестов", "license": "CC0", "retrieved": "2026-11-01", "sha256": "",
        "used_in": "тестовые пропсы", "modified": "да — упрощён, добавлены LOD",
    }
    asset.update(overrides)
    return asset
