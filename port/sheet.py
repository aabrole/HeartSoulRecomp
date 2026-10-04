#!/usr/bin/env python3
"""Tiles headless screenshots into one image. Usage: port/sheet.py OUT.png FRAME [FRAME...]
Reads port/out/shot_NNNNN.bmp; needs macOS sips for the PNG step. The cell size
comes from the first shot found, so 288x160 widescreen and 288x216 tall shots work too."""
import struct, subprocess, sys, os
out, frames = sys.argv[1], [int(x) for x in sys.argv[2:]]
d = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'out')
W, H, cols = 240, 160, 4
for n in frames:
    path = os.path.join(d, 'shot_%05d.bmp' % n)
    if os.path.exists(path):
        W, H = struct.unpack('<ii', open(path, 'rb').read(26)[18:26])
        H = abs(H)
        break
rows = (len(frames) + cols - 1) // cols
sheet = bytearray(W * cols * H * rows * 3)
for i, n in enumerate(frames):
    path = os.path.join(d, 'shot_%05d.bmp' % n)
    if not os.path.exists(path):
        continue
    b = open(path, 'rb').read()
    off = struct.unpack('<I', b[10:14])[0]
    w = struct.unpack('<i', b[18:22])[0]
    h = struct.unpack('<i', b[22:26])[0]
    if w != W or abs(h) != H:
        continue  # a shot from a run at another size
    stride = (W * 3 + 3) & ~3
    cx, cy = i % cols, i // cols
    for y in range(H):
        sy = (H - 1 - y) if h > 0 else y
        o = ((cy * H + y) * W * cols + cx * W) * 3
        sheet[o:o + W * 3] = b[off + sy * stride: off + sy * stride + W * 3]
SW, SH = W * cols, H * rows
body = b''.join(bytes(sheet[(SH - 1 - y) * SW * 3:(SH - y) * SW * 3]) for y in range(SH))
hdr = b'BM' + struct.pack('<IHHI', 54 + len(body), 0, 0, 54) + struct.pack('<IiiHHIIiiII', 40, SW, SH, 1, 24, 0, len(body), 2835, 2835, 0, 0)
tmp = out + '.bmp'
open(tmp, 'wb').write(hdr + body)
subprocess.run(['sips', '-s', 'format', 'png', tmp, '--out', out], capture_output=True)
os.remove(tmp)
