"""Загрузка ассетов (часть 76.3): только HTTPS, лимиты размера, безопасная распаковка, резолверы источников.

HTTP разрешён исключительно для 127.0.0.1/localhost и только при переменной HAN_ALLOW_LOCAL_HTTP=1 — для тестов на локальном сервере.
"""
from __future__ import annotations

import hashlib
import json
import os
import shutil
import stat
import urllib.error
import urllib.parse
import urllib.request
import zipfile
from dataclasses import dataclass, field
from pathlib import Path, PurePosixPath
from typing import Any

USER_AGENT = "HAN-fetch/1.0 (+https://github.com/KIR223/han-fists-of-seoul)"
MAX_DOWNLOAD_BYTES = 512 * 1024 * 1024
MAX_ARCHIVE_FILES = 4000
MAX_ARCHIVE_BYTES = 2 * 1024 * 1024 * 1024
TIMEOUT_SECONDS = 60

POLYHAVEN_API = "https://api.polyhaven.com"
SKETCHFAB_API = "https://api.sketchfab.com/v3"


class FetchError(Exception):
    pass


def _local_http_allowed(url: str) -> bool:
    parsed = urllib.parse.urlparse(url)
    return os.environ.get("HAN_ALLOW_LOCAL_HTTP") == "1" and parsed.scheme == "http" and parsed.hostname in ("127.0.0.1", "localhost")


def check_url(url: str) -> None:
    if url.startswith("https://") or _local_http_allowed(url):
        return
    raise FetchError(f"разрешён только HTTPS: {url}")


class _SafeRedirect(urllib.request.HTTPRedirectHandler):
    """Не даёт перенаправить с HTTPS на небезопасный адрес."""

    def redirect_request(self, req, fp, code, msg, headers, newurl):  # noqa: D102
        check_url(newurl)
        return super().redirect_request(req, fp, code, msg, headers, newurl)


def _quote_url(url: str) -> str:
    """Кодирует не-ASCII символы пути и запроса (кириллица, пробелы), не трогая уже закодированное."""
    parts = urllib.parse.urlsplit(url)
    return urllib.parse.urlunsplit((parts.scheme, parts.netloc, urllib.parse.quote(parts.path, safe="/%:@+,;=~!$&'()*-._"),
                                    urllib.parse.quote(parts.query, safe="=&%:@+,;/?~!$'()*-._"), parts.fragment))


def _open(url: str, headers: dict[str, str] | None = None):
    check_url(url)
    request = urllib.request.Request(_quote_url(url), headers={"User-Agent": USER_AGENT, **(headers or {})})
    opener = urllib.request.build_opener(_SafeRedirect)
    try:
        return opener.open(request, timeout=TIMEOUT_SECONDS)
    except urllib.error.HTTPError as e:
        raise FetchError(f"{url}: HTTP {e.code}") from e
    except urllib.error.URLError as e:
        raise FetchError(f"{url}: {e.reason}") from e


def get_json(url: str, headers: dict[str, str] | None = None) -> Any:
    with _open(url, headers) as response:
        raw = response.read(8 * 1024 * 1024 + 1)
    if len(raw) > 8 * 1024 * 1024:
        raise FetchError(f"{url}: слишком большой JSON")
    try:
        return json.loads(raw.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as e:
        raise FetchError(f"{url}: ответ не JSON") from e


def download(url: str, dest: Path, headers: dict[str, str] | None = None, max_bytes: int = MAX_DOWNLOAD_BYTES) -> str:
    """Скачивает файл через временный .part; возвращает sha256. При превышении лимита файл не остаётся."""
    dest.parent.mkdir(parents=True, exist_ok=True)
    part = dest.with_name(dest.name + ".part")
    digest = hashlib.sha256()
    total = 0
    try:
        with _open(url, headers) as response, open(part, "wb") as out:
            while True:
                chunk = response.read(1 << 20)
                if not chunk:
                    break
                total += len(chunk)
                if total > max_bytes:
                    raise FetchError(f"{url}: файл больше лимита {max_bytes // (1 << 20)} МБ")
                digest.update(chunk)
                out.write(chunk)
        os.replace(part, dest)
    finally:
        if part.exists():
            part.unlink()
    return digest.hexdigest()


def safe_member_path(name: str) -> PurePosixPath:
    """Путь внутри архива без выхода за корень; бросает FetchError на '..', абсолютных путях и дисках."""
    posix = PurePosixPath(name.replace("\\", "/"))
    if posix.is_absolute() or (posix.parts and ":" in posix.parts[0]) or ".." in posix.parts:
        raise FetchError(f"небезопасный путь в архиве: {name}")
    return posix


def extract_zip(archive: Path, out_dir: Path) -> list[Path]:
    """Распаковывает zip с защитой от zip-slip, символьных ссылок и «бомб» (лимиты числа и размера файлов)."""
    extracted: list[Path] = []
    total = 0
    root = out_dir.resolve()
    try:
        zf = zipfile.ZipFile(archive)
    except zipfile.BadZipFile as e:
        raise FetchError(f"{archive.name}: повреждённый архив") from e
    with zf:
        members = zf.infolist()
        if len(members) > MAX_ARCHIVE_FILES:
            raise FetchError(f"{archive.name}: слишком много файлов в архиве ({len(members)})")
        for info in members:
            rel = safe_member_path(info.filename)
            if not rel.parts:
                continue
            mode = info.external_attr >> 16
            if stat.S_ISLNK(mode):
                raise FetchError(f"символьная ссылка в архиве: {info.filename}")
            target = (out_dir / Path(*rel.parts)).resolve()
            if root not in target.parents and target != root:
                raise FetchError(f"небезопасный путь в архиве: {info.filename}")
            if info.is_dir():
                target.mkdir(parents=True, exist_ok=True)
                continue
            total += info.file_size
            if total > MAX_ARCHIVE_BYTES:
                raise FetchError(f"{archive.name}: распакованный размер больше лимита")
            target.parent.mkdir(parents=True, exist_ok=True)
            with zf.open(info) as src, open(target, "wb") as dst:
                shutil.copyfileobj(src, dst)
            extracted.append(target)
    return extracted


def tree_hash(directory: Path) -> str:
    """Детерминированный хэш каталога: sha256 от отсортированных пар «относительный путь + sha256 файла»."""
    digest = hashlib.sha256()
    files = sorted(p for p in directory.rglob("*") if p.is_file() and not p.name.endswith(".part"))
    for path in files:
        file_digest = hashlib.sha256()
        with open(path, "rb") as f:
            for chunk in iter(lambda: f.read(1 << 20), b""):
                file_digest.update(chunk)
        digest.update(path.relative_to(directory).as_posix().encode("utf-8") + b"\0" + file_digest.hexdigest().encode("ascii") + b"\n")
    return digest.hexdigest()


@dataclass
class Resolved:
    """Результат резолвера источника: что скачать и что известно об ассете."""
    files: list[tuple[str, str]] = field(default_factory=list)  # (url, относительный путь в каталоге загрузки)
    headers: dict[str, str] = field(default_factory=dict)
    meta: dict[str, str] = field(default_factory=dict)          # author, license, page — для сверки с манифестом


def resolve_polyhaven(asset_id: str, resolution: str = "1k", api: str = POLYHAVEN_API) -> Resolved:
    """Модель Poly Haven (всё CC0): gltf + bin + текстуры нужного разрешения."""
    quoted = urllib.parse.quote(asset_id, safe="")
    info = get_json(f"{api}/info/{quoted}")
    files = get_json(f"{api}/files/{quoted}")
    gltf = (files.get("gltf") or {}).get(resolution, {}).get("gltf")
    if not gltf:
        raise FetchError(f"Poly Haven: у «{asset_id}» нет модели glTF в разрешении {resolution}")
    resolved = Resolved(meta={
        "author": ", ".join((info.get("authors") or {}).keys()),
        "license": "CC0",  # Poly Haven публикует всё как CC0; значение сверяется со страницей ассета в манифесте
        "page": f"https://polyhaven.com/a/{asset_id}",
    })
    main_name = PurePosixPath(urllib.parse.urlparse(gltf["url"]).path).name
    resolved.files.append((gltf["url"], main_name))
    for rel, item in (gltf.get("include") or {}).items():
        safe_member_path(rel)
        resolved.files.append((item["url"], rel))
    return resolved


def resolve_sketchfab(uid: str, token: str, api: str = SKETCHFAB_API) -> Resolved:
    """Модель Sketchfab: нужен личный токен (SKETCHFAB_TOKEN); качается zip с glTF. Лицензия берётся из карточки модели."""
    from license_rules import from_sketchfab_slug

    if not token:
        raise FetchError("для Sketchfab нужен токен: переменная окружения SKETCHFAB_TOKEN (Sketchfab → Settings → API token)")
    quoted = urllib.parse.quote(uid, safe="")
    headers = {"Authorization": f"Token {token}"}
    model = get_json(f"{api}/models/{quoted}", headers)
    if model.get("isDownloadable") is False:
        raise FetchError(f"Sketchfab: модель {uid} не разрешена к скачиванию автором")
    license_slug = (model.get("license") or {}).get("slug", "") if isinstance(model.get("license"), dict) else str(model.get("license", ""))
    download_info = get_json(f"{api}/models/{quoted}/download", headers)
    url = (download_info.get("gltf") or {}).get("url")
    if not url:
        raise FetchError(f"Sketchfab: нет ссылки на glTF для {uid}")
    return Resolved(
        files=[(url, "sketchfab.zip")],
        meta={
            "author": (model.get("user") or {}).get("displayName", ""),
            "license": from_sketchfab_slug(license_slug),
            "page": model.get("viewerUrl", f"https://sketchfab.com/3d-models/{uid}"),
        },
    )
