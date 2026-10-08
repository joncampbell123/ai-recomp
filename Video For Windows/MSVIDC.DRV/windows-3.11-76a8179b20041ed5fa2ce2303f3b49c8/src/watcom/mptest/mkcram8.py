#!/usr/bin/env python3
"""Make a Video 1 CRAM8 AVI from an input clip (decoded with ffmpeg).

usage: mkcram8.py input.avi output.avi

Palette: 6x6x6 colour cube plus 40 greys.  Blocks are coded as skip (same as
the previous frame), fill (one colour) or 2-colour (the two most common
colours, each pixel taking the nearer one).  AVI 1.0, video only.
"""
import collections, struct, subprocess, sys

W, H = 320, 240
FPS_US = 66666


def palette():
    pal = []
    for i in range(256):
        if i < 216:
            pal.append(((i // 36) * 51, ((i // 6) % 6) * 51, (i % 6) * 51))
        else:
            v = (i - 216) * 6
            pal.append((v, v, v))
    return pal


PAL = palette()


def quant(r, g, b):
    return ((r + 25) // 51) * 36 + ((g + 25) // 51) * 6 + (b + 25) // 51


def dist(a, b):
    pa, pb = PAL[a], PAL[b]
    return sum((x - y) ** 2 for x, y in zip(pa, pb))


def frames(path):
    raw = subprocess.run(['ffmpeg', '-v', 'error', '-i', path, '-f', 'rawvideo', '-pix_fmt', 'rgb24',
                          '-vf', 'scale=%d:%d' % (W, H), '-'], capture_output=True, check=True).stdout
    n = len(raw) // (W * H * 3)
    for f in range(n):
        base = f * W * H * 3
        img = []
        for y in range(H - 1, -1, -1):                  # bottom-up rows
            row = raw[base + y * W * 3: base + (y + 1) * W * 3]
            img.append([quant(row[x * 3], row[x * 3 + 1], row[x * 3 + 2]) for x in range(W)])
        yield img


def encode(img, prev):
    out = bytearray()
    skip = 0

    def flush():
        nonlocal skip
        while skip:
            n = min(skip, 0x3FF)
            out.extend(struct.pack('<H', 0x8400 | n))
            skip -= n

    for by in range(H // 4):
        for bx in range(W // 4):
            blk = [img[by * 4 + r][bx * 4 + c] for r in range(4) for c in range(4)]
            if prev is not None and blk == [prev[by * 4 + r][bx * 4 + c] for r in range(4) for c in range(4)]:
                skip += 1
                continue
            flush()
            counts = collections.Counter(blk)
            if len(counts) == 1:
                out.extend(struct.pack('<H', 0x8000 | blk[0]))
                continue
            a, b = [c for c, _ in counts.most_common(2)]
            flags = 0
            for i, p in enumerate(blk):
                if dist(p, a) <= dist(p, b):
                    flags |= 1 << i                     # bit set selects colour A
            if flags & 0x8000:                          # keep bit 15 clear
                flags ^= 0xFFFF
                a, b = b, a
            out.extend(struct.pack('<HBB', flags, a, b))
    flush()
    out.extend(b'\0\0')
    return bytes(out)


def chunk(fourcc, data):
    pad = b'\0' if len(data) & 1 else b''
    return fourcc + struct.pack('<I', len(data)) + data + pad


def lst(fourcc, data):
    return chunk(b'LIST', fourcc + data)


def main():
    src, dst = sys.argv[1], sys.argv[2]
    enc = []
    prev = None
    for i, img in enumerate(frames(src)):
        key = (i % 15 == 0)
        enc.append((encode(img, None if key else prev), key))
        prev = img
    n = len(enc)
    maxsz = max(len(e) for e, _ in enc)

    avih = struct.pack('<IIIIIIIIII4I', FPS_US, 0, 0, 0x10, n, 0, 1, maxsz, W, H, 0, 0, 0, 0)
    strh = struct.pack('<4s4sIHHIIIIIIIIhhhh', b'vids', b'MSVC', 0, 0, 0, 0, 1, 15, 0, n, maxsz,
                       0xFFFFFFFF, 0, 0, 0, W, H)
    bih = struct.pack('<IiiHHIIiiII', 40, W, H, 1, 8, 0x4356534D, W * H, 0, 0, 256, 0)
    pal = b''.join(struct.pack('<BBBB', b, g, r, 0) for r, g, b in PAL)
    hdrl = lst(b'hdrl', chunk(b'avih', avih) + lst(b'strl', chunk(b'strh', strh) + chunk(b'strf', bih + pal)))

    movi = bytearray(b'movi')
    idx = bytearray()
    for data, key in enc:
        idx += struct.pack('<4sIII', b'00dc', 0x10 if key else 0, len(movi), len(data))
        movi += chunk(b'00dc', data)
    body = b'AVI ' + hdrl + chunk(b'LIST', bytes(movi)) + chunk(b'idx1', bytes(idx))
    open(dst, 'wb').write(b'RIFF' + struct.pack('<I', len(body)) + body)
    print('wrote', dst, n, 'frames, max frame', maxsz, 'bytes')


if __name__ == '__main__':
    main()
