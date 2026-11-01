"""PNG и сравнение кадров на стандартной библиотеке (скриншот-тест E4): чтение, запись со сжатием, метрика отличий, картинка различий."""
from __future__ import annotations

import struct
import zlib
from dataclasses import dataclass
from pathlib import Path


@dataclass
class Image:
    width: int
    height: int
    channels: int  # 3 (RGB) или 4 (RGBA)
    rows: list[bytearray]  # строки сверху вниз, по channels байт на пиксель


def _chunk(kind: bytes, data: bytes) -> bytes:
    body = kind + data
    return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)


def write_png(path: Path, image: Image) -> None:
    """Пишет PNG без фильтров, но со сжатием zlib (эталоны получаются в разы меньше несжатых снимков движка)."""
    raw = b"".join(b"\x00" + bytes(row) for row in image.rows)
    header = struct.pack(">IIBBBBB", image.width, image.height, 8, 6 if image.channels == 4 else 2, 0, 0, 0)
    data = b"\x89PNG\r\n\x1a\n" + _chunk(b"IHDR", header) + _chunk(b"IDAT", zlib.compress(raw, 9)) + _chunk(b"IEND", b"")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def read_png(path: Path) -> Image:
    data = Path(path).read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: это не PNG")
    pos, idat, width, height, ctype = 8, b"", 0, 0, 0
    while pos + 8 <= len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        chunk = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            width, height, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", chunk)
            if depth != 8 or ctype not in (2, 6) or interlace != 0:
                raise ValueError(f"{path}: поддерживаются только 8 бит RGB/RGBA без чересстрочности")
        elif kind == b"IDAT":
            idat += chunk
        pos += 12 + length
    channels = 3 if ctype == 2 else 4
    raw = zlib.decompress(idat)
    stride = width * channels
    if len(raw) != (stride + 1) * height:
        raise ValueError(f"{path}: размер данных не совпадает с заголовком")
    rows: list[bytearray] = []
    previous = bytearray(stride)
    offset = 0
    for _ in range(height):
        filter_type = raw[offset]
        line = bytearray(raw[offset + 1:offset + 1 + stride])
        offset += 1 + stride
        for x in range(stride):
            a = line[x - channels] if x >= channels else 0
            b = previous[x]
            c = previous[x - channels] if x >= channels else 0
            if filter_type == 1:
                line[x] = (line[x] + a) & 255
            elif filter_type == 2:
                line[x] = (line[x] + b) & 255
            elif filter_type == 3:
                line[x] = (line[x] + ((a + b) >> 1)) & 255
            elif filter_type == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else (b if pb <= pc else c))) & 255
        rows.append(line)
        previous = line
    return Image(width, height, channels, rows)


@dataclass
class Difference:
    mean_abs: float          # средний модуль отличия по каналам, 0..255
    bad_fraction: float      # доля пикселей, у которых какой-либо канал отличается больше порога
    max_abs: int             # наибольшее отличие любого канала


def compare(actual: Image, expected: Image, pixel_threshold: int = 24) -> Difference:
    if (actual.width, actual.height, actual.channels) != (expected.width, expected.height, expected.channels):
        raise ValueError(f"размеры не совпадают: {actual.width}x{actual.height}x{actual.channels} и "
                         f"{expected.width}x{expected.height}x{expected.channels}")
    total, bad, biggest = 0, 0, 0
    channels = actual.channels
    for ra, re in zip(actual.rows, expected.rows):
        for x in range(0, len(ra), channels):
            worst = 0
            for c in range(channels):
                d = abs(ra[x + c] - re[x + c])
                total += d
                worst = max(worst, d)
            if worst > pixel_threshold:
                bad += 1
            biggest = max(biggest, worst)
    pixels = actual.width * actual.height
    return Difference(total / (pixels * channels), bad / pixels, biggest)


def difference_image(actual: Image, expected: Image, gain: int = 8) -> Image:
    """Картинка различий: чёрное — одинаково, чем ярче, тем больше отличие (усилено в gain раз)."""
    rows = []
    for ra, re in zip(actual.rows, expected.rows):
        row = bytearray()
        for x in range(0, len(ra), actual.channels):
            for c in range(3):
                row.append(min(255, abs(ra[x + c] - re[x + c]) * gain))
        rows.append(row)
    return Image(actual.width, actual.height, 3, rows)
