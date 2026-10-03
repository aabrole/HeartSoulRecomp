#!/usr/bin/env python3
"""Draws the app icon: a gold heart with a silver feather across it and
flames rising behind, for Heart & Soul's Gold and Silver. Original pixel art,
generated on a 48x48 grid and scaled with nearest neighbour so pixels stay
crisp.

Writes the Android launcher icons into android/app/src/main/res and a 512px
preview to port/icon/icon-512.png. Run from the repo root."""
import math
import os
from PIL import Image, ImageDraw

G = 48
BACKGROUND = (22, 20, 44)
OUTLINE = (18, 12, 28)
GOLD = [(122, 74, 18), (176, 112, 26), (222, 160, 40), (246, 204, 74), (255, 238, 150), (255, 252, 226)]
SILVER = [(70, 78, 102), (118, 128, 152), (170, 180, 204), (214, 222, 238), (246, 248, 255)]
FIRE = [(150, 30, 30), (214, 62, 36), (244, 122, 40), (252, 186, 64), (255, 236, 150)]


LOBE_R = 8.4
LEFT_LOBE = (15.6, 22.5)
RIGHT_LOBE = (32.4, 22.5)
TIP = (24.0, 42.5)


def heart_inside(x, y):
    """Two round lobes and a point, which reads as a heart even when small."""
    for cx, cy in (LEFT_LOBE, RIGHT_LOBE):
        if (x - cx) ** 2 + (y - cy) ** 2 <= LOBE_R ** 2:
            return True
    if y < LEFT_LOBE[1] or y > TIP[1]:
        return False
    # Between the tangent lines from the lobes' outer edges to the tip.
    t = (y - LEFT_LOBE[1]) / (TIP[1] - LEFT_LOBE[1])
    left = (LEFT_LOBE[0] - LOBE_R * 0.92) * (1 - t) + TIP[0] * t
    right = (RIGHT_LOBE[0] + LOBE_R * 0.92) * (1 - t) + TIP[0] * t
    return left <= x <= right


def flames():
    """A flame aura around the heart: thin at the sides, rising in tongues
    above it, hottest where it touches the heart."""
    inside = {(x, y) for x in range(G) for y in range(G) if heart_inside(x + 0.5, y + 0.5)}
    layer = {}
    cx, cy = 24.0, 27.0
    for x in range(G):
        for y in range(G):
            if (x, y) in inside:
                continue
            # Distance to the heart, searched in a small window.
            d = None
            for r in range(1, 12):
                if any((x + ox, y + oy) in inside
                       for ox in range(-r, r + 1) for oy in (-r, r)) or \
                   any((x + ox, y + oy) in inside
                       for oy in range(-r, r + 1) for ox in (-r, r)):
                    d = r
                    break
            if d is None:
                continue
            theta = math.atan2(x + 0.5 - cx, cy - (y + 0.5))   # 0 straight up
            up = math.cos(theta)
            if up < 0.12:
                continue                                   # no fire below the heart
            tongues = (0.5 + 0.5 * math.cos(theta * 7.0 + 0.6)) ** 1.5
            reach = 1.0 + 11.0 * up ** 1.2 * (0.25 + 0.75 * tongues)
            if d > reach:
                continue
            heat = d / reach
            level = 4 if heat < 0.25 else 3 if heat < 0.5 else 2 if heat < 0.75 else 1
            layer[(x, y)] = FIRE[level]
    return layer


def heart():
    layer = {}
    for x in range(G):
        for y in range(G):
            if not heart_inside(x + 0.5, y + 0.5):
                continue
            # Light from the upper left: brighter toward it, darker toward the
            # lower right, with a rim of shadow along the right and bottom edge.
            # Light from the upper left. Pixels near the lower right edge
            # fall into shadow; a band along the upper left catches light.
            shade = 2.6 + 0.045 * ((24 - x) + (24 - y))
            if not heart_inside(x + 2.5, y + 2.5):
                shade -= 1.2
            if not heart_inside(x + 1.5, y + 1.5):
                shade -= 0.8
            if not heart_inside(x - 1.5, y - 1.5):
                shade += 0.9
            level = max(0, min(4, int(shade)))
            layer[(x, y)] = GOLD[level]
    # Shine on the left lobe.
    for x, y in [(13, 19), (14, 18), (15, 17), (16, 17), (13, 20), (14, 19), (17, 16)]:
        if (x, y) in layer:
            layer[(x, y)] = GOLD[5]
    for x, y in [(15, 18), (18, 16), (12, 21)]:
        if (x, y) in layer:
            layer[(x, y)] = GOLD[4]
    return layer


def feather():
    """A silver feather sweeping from the lower left to the upper right."""
    layer = {}
    x0, y0, x1, y1 = 11.0, 44.0, 40.0, 17.0
    length = math.hypot(x1 - x0, y1 - y0)
    dx, dy = (x1 - x0) / length, (y1 - y0) / length
    nx, ny = -dy, dx                                  # normal, toward upper left
    for x in range(G):
        for y in range(G):
            px, py = x + 0.5 - x0, y + 0.5 - y0
            s = px * dx + py * dy                     # along the quill, 0..length
            n = px * nx + py * ny                     # across the quill
            t = s / length
            if t < 0 or t > 1:
                continue
            bend = -2.2 * math.sin(t * math.pi)       # gentle curve
            n -= bend
            if t < 0.16:                              # bare quill at the base
                if abs(n) < 0.6:
                    layer[(x, y)] = SILVER[1]
                continue
            # Vane: widest past the middle, pointed at the tip.
            width = 3.9 * math.sin(min(1, (t - 0.16) / 0.84) * math.pi) ** 0.7
            if abs(n) <= 0.55:
                layer[(x, y)] = SILVER[4]             # shaft
            elif abs(n) <= width:
                # Barbs: diagonal stripes, lighter on the upper vane.
                stripe = int((s * 0.9 + abs(n) * 1.1)) % 3 == 0
                level = 3 if n > 0 else 2
                if stripe:
                    level -= 1
                if abs(n) > width - 0.9:
                    level = max(1, level - 1)
                layer[(x, y)] = SILVER[level]
    # A notch in the vane, as real feathers have.
    for x, y in list(layer):
        s = (x + 0.5 - x0) * dx + (y + 0.5 - y0) * dy
        n = (x + 0.5 - x0) * nx + (y + 0.5 - y0) * ny
        if 19.5 < s < 21.0 and n < -1.0:
            del layer[(x, y)]
    return layer


def outlined(layer):
    """Adds a one-pixel dark outline around a layer's shape."""
    out = dict(layer)
    for (x, y) in layer:
        for ox, oy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            p = (x + ox, y + oy)
            if p not in layer and 0 <= p[0] < G and 0 <= p[1] < G:
                out.setdefault(p, OUTLINE)
    return out


def compose():
    img = Image.new("RGBA", (G, G), (0, 0, 0, 0))
    px = img.load()
    for layer in (outlined(flames()), outlined(heart()), outlined(feather())):
        for (x, y), color in layer.items():
            px[x, y] = color + (255,)
    return img


def scaled(art, size):
    return art.resize((size, size), Image.NEAREST)


def full_icon(art, size, round_mask=False):
    base = Image.new("RGBA", (size, size), BACKGROUND + (255,))
    # Faint stars.
    d = ImageDraw.Draw(base)
    u = size / G
    for sx, sy in [(5, 6), (41, 5), (44, 30), (4, 33), (38, 43), (9, 44)]:
        d.rectangle([sx * u, sy * u, (sx + 1) * u - 1, (sy + 1) * u - 1], fill=(90, 92, 150))
    base.alpha_composite(scaled(art, size))
    mask = Image.new("L", (size, size), 0)
    md = ImageDraw.Draw(mask)
    if round_mask:
        md.ellipse([0, 0, size - 1, size - 1], fill=255)
    else:
        md.rounded_rectangle([0, 0, size - 1, size - 1], radius=size // 5, fill=255)
    base.putalpha(mask)
    return base


def adaptive_foreground(art, size):
    """Adaptive icons crop to the middle 72/108; keep the art inside it."""
    canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    inner = int(size * 70 / 108)
    offset = (size - inner) // 2
    canvas.alpha_composite(scaled(art, inner), (offset, offset))
    return canvas


def main():
    art = compose()
    res = "android/app/src/main/res"
    for name, factor in {"mdpi": 1, "hdpi": 1.5, "xhdpi": 2, "xxhdpi": 3, "xxxhdpi": 4}.items():
        folder = os.path.join(res, "mipmap-" + name)
        os.makedirs(folder, exist_ok=True)
        legacy = int(48 * factor)
        full_icon(art, legacy).save(os.path.join(folder, "ic_launcher.png"))
        full_icon(art, legacy, round_mask=True).save(os.path.join(folder, "ic_launcher_round.png"))
        adaptive_foreground(art, int(108 * factor)).save(os.path.join(folder, "ic_launcher_foreground.png"))
    os.makedirs(os.path.join(res, "mipmap-anydpi-v26"), exist_ok=True)
    for name in ("ic_launcher", "ic_launcher_round"):
        with open(os.path.join(res, "mipmap-anydpi-v26", name + ".xml"), "w") as f:
            f.write('<?xml version="1.0" encoding="utf-8"?>\n'
                    '<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">\n'
                    '    <background android:drawable="@color/ic_launcher_background" />\n'
                    '    <foreground android:drawable="@mipmap/ic_launcher_foreground" />\n'
                    '</adaptive-icon>\n')
    with open(os.path.join(res, "values", "ic_launcher_background.xml"), "w") as f:
        f.write('<?xml version="1.0" encoding="utf-8"?>\n<resources>\n'
                '    <color name="ic_launcher_background">#%02X%02X%02X</color>\n</resources>\n' % BACKGROUND)
    full_icon(art, 528).resize((512, 512), Image.NEAREST).save("port/icon/icon-512.png")
    print("icons written")


if __name__ == "__main__":
    main()
