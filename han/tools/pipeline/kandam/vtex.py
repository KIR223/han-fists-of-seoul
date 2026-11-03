"""Декодер Source 2 .vtex_c (текстуры CC0 ambientCG из репозитория PatrickJnr/cc0-texture-pack) -> numpy RGBA8.
Поддержка: DXT1, DXT5, RGBA8888, BGRA8888, ATI2N(BC5), ATI1N(BC4), I8, IA88; мипы с LZ4-сжатием."""
import struct
import sys

import numpy as np


def lz4_block(src, out_size):
    dst = bytearray(out_size)
    si = di = 0
    n = len(src)
    while si < n:
        tok = src[si]; si += 1
        lit = tok >> 4
        if lit == 15:
            while True:
                b = src[si]; si += 1; lit += b
                if b != 255:
                    break
        dst[di:di + lit] = src[si:si + lit]; si += lit; di += lit
        if si >= n:
            break
        off = src[si] | (src[si + 1] << 8); si += 2
        ml = tok & 15
        if ml == 15:
            while True:
                b = src[si]; si += 1; ml += b
                if b != 255:
                    break
        ml += 4
        st = di - off
        if off >= ml:
            dst[di:di + ml] = dst[st:st + ml]
        else:
            for k in range(ml):
                dst[di + k] = dst[st + k]
        di += ml
    return bytes(dst[:di])


def _c565(c):
    r = ((c >> 11) & 31).astype(np.float32) * (255 / 31)
    g = ((c >> 5) & 63).astype(np.float32) * (255 / 63)
    b = (c & 31).astype(np.float32) * (255 / 31)
    return np.stack([r, g, b], -1)


def _bc1_rgb(blk):  # blk: (N,8) uint8
    c0 = blk[:, 0].astype(np.uint16) | (blk[:, 1].astype(np.uint16) << 8)
    c1 = blk[:, 2].astype(np.uint16) | (blk[:, 3].astype(np.uint16) << 8)
    p0, p1 = _c565(c0), _c565(c1)
    four = (c0 > c1)[:, None]
    p2 = np.where(four, (2 * p0 + p1) / 3, (p0 + p1) / 2)
    p3 = np.where(four, (p0 + 2 * p1) / 3, 0)
    pal = np.stack([p0, p1, p2, p3], 1)  # N,4,3
    idx = blk[:, 4:8].astype(np.uint32)
    bits = idx[:, 0] | (idx[:, 1] << 8) | (idx[:, 2] << 16) | (idx[:, 3] << 24)
    sel = np.stack([(bits >> (2 * i)) & 3 for i in range(16)], 1)  # N,16
    rgb = np.take_along_axis(pal, sel[..., None].astype(np.int64).repeat(3, -1), 1)
    a = np.where((~four) & (sel == 3), 0, 255)
    return rgb, a


def _bc4(blk):  # (N,8) -> N,16
    a0 = blk[:, 0].astype(np.float32); a1 = blk[:, 1].astype(np.float32)
    big = (a0 > a1)[:, None]
    pal = [a0, a1]
    for i in range(1, 7):
        pal.append(np.where(big[:, 0], ((7 - i) * a0 + i * a1) / 7, 0))
    small = [a0, a1] + [((5 - i) * a0 + i * a1) / 5 for i in range(1, 5)] + [np.zeros_like(a0), np.full_like(a0, 255)]
    pal = np.where(big, np.stack(pal[:2] + pal[2:], 1), np.stack(small, 1))
    bits = np.zeros(len(blk), np.uint64)
    for i in range(6):
        bits |= blk[:, 2 + i].astype(np.uint64) << np.uint64(8 * i)
    sel = np.stack([(bits >> np.uint64(3 * i)) & np.uint64(7) for i in range(16)], 1).astype(np.int64)
    return np.take_along_axis(pal, sel, 1)


def _tile(px, w, h, ch):  # px: (N,16,ch) blocks -> image
    bw, bh = max(1, (w + 3) // 4), max(1, (h + 3) // 4)
    im = px.reshape(bh, bw, 4, 4, ch).transpose(0, 2, 1, 3, 4).reshape(bh * 4, bw * 4, ch)
    return im[:h, :w]


def decode(path, want_mip=0):
    b = open(path, "rb").read()
    rsize, hv, ver, boff, bcount = struct.unpack("<IHHII", b[:16])
    blocks = {}
    for i in range(bcount):
        o = 8 + boff + i * 12
        nm = b[o:o + 4].decode()
        off, sz = struct.unpack("<II", b[o + 4:o + 12])
        blocks[nm] = (o + 4 + off, sz)
    d = blocks["DATA"][0]
    w, h, dep = struct.unpack("<HHH", b[d + 20:d + 26])
    fmt, mips = b[d + 26], b[d + 27]
    eoff, ecount = struct.unpack("<II", b[d + 32:d + 40])
    comp = None
    for i in range(ecount):
        o = d + 32 + eoff + i * 12
        t, off, sz = struct.unpack("<III", b[o:o + 12])
        if t == 4:
            p = o + 4 + off
            _, _, mc = struct.unpack("<III", b[p:p + 12])
            comp = struct.unpack("<%dI" % mc, b[p + 12:p + 12 + 4 * mc])
    bpb = {1: 8, 2: 16, 21: 16, 27: 8, 20: 16}.get(fmt)
    bpp = {4: 4, 28: 4, 3: 1, 22: 2}.get(fmt)

    def raw_size(m):
        mw, mh = max(1, w >> m), max(1, h >> m)
        if bpb:
            return max(1, (mw + 3) // 4) * max(1, (mh + 3) // 4) * bpb
        return mw * mh * bpp

    # данные мипов — от самого маленького к самому большому
    pos = rsize
    data = {}
    for m in range(mips - 1, -1, -1):
        sz = comp[m] if comp else raw_size(m)
        chunk = b[pos:pos + sz]; pos += sz
        if m == want_mip:
            rs = raw_size(m)
            data = lz4_block(chunk, rs) if (comp and sz < rs) else chunk
            break
    mw, mh = max(1, w >> want_mip), max(1, h >> want_mip)
    a = np.frombuffer(data, np.uint8)
    if fmt == 1:
        rgb, al = _bc1_rgb(a.reshape(-1, 8))
        img = np.concatenate([rgb, al[..., None]], -1)
        img = _tile(img, mw, mh, 4)
    elif fmt == 2:
        blk = a.reshape(-1, 16)
        rgb, _ = _bc1_rgb(blk[:, 8:])
        al = _bc4(blk[:, :8])
        img = _tile(np.concatenate([rgb, al[..., None]], -1), mw, mh, 4)
    elif fmt == 21:
        blk = a.reshape(-1, 16)
        r = _bc4(blk[:, :8]); g = _bc4(blk[:, 8:])
        x = r / 127.5 - 1; y = g / 127.5 - 1
        z = np.sqrt(np.clip(1 - x * x - y * y, 0, 1)) * 127.5 + 127.5
        img = _tile(np.stack([r, g, z, np.full_like(r, 255)], -1), mw, mh, 4)
    elif fmt == 27:
        r = _bc4(a.reshape(-1, 8))
        img = _tile(np.stack([r, r, r, np.full_like(r, 255)], -1), mw, mh, 4)
    elif fmt == 20:  # BC7 — через встроенный декодер Pillow (обёртка DDS DX10)
        import io
        from PIL import Image
        bw, bh = max(1, (mw + 3) // 4) * 4, max(1, (mh + 3) // 4) * 4
        hdr = struct.pack("<4sIIIIIII44sIIIIIIIIIIIII", b"DDS ", 124, 0x81007, bh, bw, len(data), 0, 1, b"\0" * 44, 32, 0x4,
                          0x30315844, 0, 0, 0, 0, 0, 0x1000, 0, 0, 0, 0)
        dx10 = struct.pack("<IIIII", 98, 3, 0, 1, 0)
        im = Image.open(io.BytesIO(hdr + dx10 + bytes(data))); im.load()
        img = np.asarray(im.convert("RGBA"), np.float32)[:mh, :mw]
    elif fmt in (4, 28):
        img = a.reshape(mh, mw, 4).astype(np.float32)
        if fmt == 28:
            img = img[..., [2, 1, 0, 3]]
    elif fmt == 3:
        g = a.reshape(mh, mw).astype(np.float32)
        img = np.stack([g, g, g, np.full_like(g, 255)], -1)
    elif fmt == 22:
        g = a.reshape(mh, mw, 2).astype(np.float32)
        img = np.stack([g[..., 0], g[..., 0], g[..., 0], g[..., 1]], -1)
    else:
        raise ValueError("формат %d не поддержан (%s)" % (fmt, path))
    return np.clip(img + 0.5, 0, 255).astype(np.uint8), dict(w=w, h=h, fmt=fmt, mips=mips)


def info(path):
    b = open(path, "rb").read(4096)
    rsize, hv, ver, boff, bcount = struct.unpack("<IHHII", b[:16])
    for i in range(bcount):
        o = 8 + boff + i * 12
        if b[o:o + 4] == b"DATA":
            off, sz = struct.unpack("<II", b[o + 4:o + 12]); d = o + 4 + off
            w, h, _ = struct.unpack("<HHH", b[d + 20:d + 26])
            return w, h, b[d + 26], b[d + 27]


if __name__ == "__main__":
    from PIL import Image
    im, meta = decode(sys.argv[1], int(sys.argv[3]) if len(sys.argv) > 3 else 0)
    print(meta)
    Image.fromarray(im).save(sys.argv[2])
