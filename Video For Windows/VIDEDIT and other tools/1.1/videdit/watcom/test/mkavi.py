# mkavi.py OUT.AVI - synthetic test AVI: 30 frames 160x120 8-bit uncompressed ('DIB '), 15 fps,
# 256-colour palette, moving shapes and a frame number bar; 11025 Hz 8-bit mono PCM interleaved.
import struct, sys
W, H, N, FPS, RATE = 160, 120, 30, 15, 11025
def chunk(fcc, data):
    d = fcc + struct.pack('<I', len(data)) + data
    return d + (b'\0' if len(data) & 1 else b'')
def lst(typ, data): return chunk(b'LIST', typ + data)
pal = b''
for i in range(256):
    if i < 16:   r, g, b = [(0,0,0),(128,0,0),(0,128,0),(128,128,0),(0,0,128),(128,0,128),(0,128,128),(192,192,192),
                            (128,128,128),(255,0,0),(0,255,0),(255,255,0),(0,0,255),(255,0,255),(0,255,255),(255,255,255)][i]
    else:        r, g, b = (i * 7) & 255, (i * 13) & 255, (255 - i) & 255
    pal += bytes((b, g, r, 0))
rowb = (W + 3) & ~3
frames = []
for f in range(N):
    px = bytearray(rowb * H)
    for y in range(H):
        for x in range(W):
            c = 16 + ((x + y + f * 3) % 240)
            if 20 + f * 3 <= x < 50 + f * 3 and 30 <= y < 70: c = 9          # red box moving right
            if (x - 120) ** 2 + (y - 40 - f) ** 2 < 150: c = 12             # blue disc moving down
            if y < 8 and x < (f + 1) * 5: c = 15                            # progress bar (rows at the bottom)
            px[y * rowb + x] = c
    frames.append(bytes(px))
spf = RATE // FPS                         # 735 samples per frame
audio = [bytes(((128 + int(60 * ((i * (200 + f * 20) // RATE) % 2 * 2 - 1))) & 255) for i in range(spf)) for f in range(N)]
bih = struct.pack('<IiiHHIIiiII', 40, W, H, 1, 8, 0, rowb * H, 0, 0, 256, 0)
vstrh = struct.pack('<4s4sIHHIIIIIIIIhhhh', b'vids', b'DIB ', 0, 0, 0, 0, 1, FPS, 0, N, rowb * H, 0, 0, 0, 0, W, H)
astrh = struct.pack('<4s4sIHHIIIIIIIIhhhh', b'auds', b'\0\0\0\0', 0, 0, 0, 0, 1, RATE, 0, spf * N, spf, 0, 1, 0, 0, 0, 0)
wfx = struct.pack('<HHIIHH', 1, 1, RATE, RATE, 1, 8)
avih = struct.pack('<14I', 1000000 // FPS, (rowb * H + spf) * FPS, 0, 0x10, N, 0, 2, rowb * H, W, H, 0, 0, 0, 0)
hdrl = lst(b'hdrl', chunk(b'avih', avih) + lst(b'strl', chunk(b'strh', vstrh) + chunk(b'strf', bih + pal))
           + lst(b'strl', chunk(b'strh', astrh) + chunk(b'strf', wfx)))
movi = b''; idx = b''
for f in range(N):
    for ck, data in ((b'00db', frames[f]), (b'01wb', audio[f])):
        idx += struct.pack('<4sIII', ck, 0x10 if ck == b'00db' else 0x10, 4 + len(movi), len(data))
        movi += chunk(ck, data)
body = b'AVI ' + hdrl + lst(b'movi', movi) + chunk(b'idx1', idx)
open(sys.argv[1], 'wb').write(b'RIFF' + struct.pack('<I', len(body)) + body)
