#!/usr/bin/env python3
"""Draws the app icon: a pixel heart with a blue soul flame rising from it,
over two small screens for the dual-screen handhelds. Original art on a 32x32
grid, scaled up with nearest neighbour so the pixels stay crisp.

Writes the Android launcher icons into android/app/src/main/res and a 512px
preview to port/icon/icon-512.png. Run from the repo root."""
import os
from PIL import Image, ImageDraw

GRID = 32
# Pixel map. Each character is one grid pixel.
ART = [
    "................................",
    "................................",
    "...............c................",
    "..............cC................",
    "..............cCc...............",
    ".............cCWc...............",
    ".............cCWCc..............",
    "............cCWWCc..............",
    "............cCWWCc..............",
    ".............cCCc...............",
    "..............cc................",
    "......kkkk.........kkkk.........",
    ".....kRRRRk.......kRRRRk........",
    "....kRRwwRRk.....kRRRRRRk.......",
    "...kRRwwRRRRk...kRRRRRRRRk......",
    "...kRwwRRRRRRk.kRRRRRRRRRk......",
    "...kRwRRRRRRRRkRRRRRRRRRRk......",
    "...kRRRRRRRRRRRRRRRRRRRRdk......",
    "...kRRRRRRRRRRRRRRRRRRRRdk......",
    "....kRRRRRRRRRRRRRRRRRRdk.......",
    ".....kRRRRRRRRRRRRRRRRdk........",
    "......kRRRRRRRRRRRRRRdk.........",
    ".......kRRRRRRRRRRRRdk..........",
    "........kRRRRRRRRRRdk...........",
    ".........kRRRRRRRRdk............",
    "..........kRRRRRRdk.............",
    "...........kRRRRdk..............",
    "............kRRdk...............",
    ".............kkk................",
    "................................",
    "................................",
    "................................",
]
PALETTE = {
    "k": (24, 18, 38),      # outline
    "R": (232, 54, 72),     # heart
    "d": (168, 28, 52),     # heart shade
    "w": (255, 214, 220),   # heart shine
    "c": (40, 120, 200),    # flame edge
    "C": (96, 196, 255),    # flame
    "W": (226, 248, 255),   # flame core
}
BACKGROUND = (30, 36, 64)
SCREEN = (58, 70, 118)
SCREEN_LIT = (120, 210, 160)


def draw_art(size):
    """The foreground art on a transparent square of the given size."""
    scale = size // GRID
    img = Image.new("RGBA", (GRID * scale, GRID * scale), (0, 0, 0, 0))
    px = img.load()
    for y, row in enumerate(ART):
        assert len(row) == GRID, (y, len(row))
        for x, ch in enumerate(row):
            if ch == ".":
                continue
            color = PALETTE[ch] + (255,)
            for dy in range(scale):
                for dx in range(scale):
                    px[x * scale + dx, y * scale + dy] = color
    # Shift right so the heart sits in the middle (the art is drawn left of centre).
    shifted = Image.new("RGBA", img.size, (0, 0, 0, 0))
    shifted.paste(img, (scale * 3, scale))
    return shifted.resize((size, size), Image.NEAREST)


def draw_screens(size):
    """Two stacked screens, top one wide, under the heart."""
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    u = size / GRID
    d.rectangle([9 * u, 27.5 * u, 23 * u - 1, 29 * u - 1], fill=SCREEN_LIT)
    d.rectangle([11 * u, 29.6 * u, 21 * u - 1, 30.8 * u - 1], fill=SCREEN)
    return img


def full_icon(size, round_mask=False):
    base = Image.new("RGBA", (size, size), BACKGROUND + (255,))
    art = draw_art(size)
    base.alpha_composite(draw_screens(size))
    base.alpha_composite(art)
    mask = Image.new("L", (size, size), 0)
    md = ImageDraw.Draw(mask)
    if round_mask:
        md.ellipse([0, 0, size - 1, size - 1], fill=255)
    else:
        md.rounded_rectangle([0, 0, size - 1, size - 1], radius=size // 5, fill=255)
    base.putalpha(mask)
    return base


def adaptive_foreground(size):
    """Adaptive icons crop to the middle 72/108; keep the art inside it."""
    canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    inner = int(size * 66 / 108)
    art = Image.new("RGBA", (inner, inner), (0, 0, 0, 0))
    art.alpha_composite(draw_screens(inner))
    art.alpha_composite(draw_art(inner))
    offset = (size - inner) // 2
    canvas.alpha_composite(art, (offset, offset))
    return canvas


RES = "android/app/src/main/res"
DENSITIES = {"mdpi": 1, "hdpi": 1.5, "xhdpi": 2, "xxhdpi": 3, "xxxhdpi": 4}

for name, factor in DENSITIES.items():
    folder = os.path.join(RES, "mipmap-" + name)
    os.makedirs(folder, exist_ok=True)
    legacy = int(48 * factor)
    full_icon(legacy).save(os.path.join(folder, "ic_launcher.png"))
    full_icon(legacy, round_mask=True).save(os.path.join(folder, "ic_launcher_round.png"))
    adaptive_foreground(int(108 * factor)).save(os.path.join(folder, "ic_launcher_foreground.png"))

os.makedirs(os.path.join(RES, "mipmap-anydpi-v26"), exist_ok=True)
for name in ("ic_launcher", "ic_launcher_round"):
    with open(os.path.join(RES, "mipmap-anydpi-v26", name + ".xml"), "w") as f:
        f.write('<?xml version="1.0" encoding="utf-8"?>\n'
                '<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">\n'
                '    <background android:drawable="@color/ic_launcher_background" />\n'
                '    <foreground android:drawable="@mipmap/ic_launcher_foreground" />\n'
                '</adaptive-icon>\n')
with open(os.path.join(RES, "values", "ic_launcher_background.xml"), "w") as f:
    f.write('<?xml version="1.0" encoding="utf-8"?>\n<resources>\n'
            '    <color name="ic_launcher_background">#%02X%02X%02X</color>\n</resources>\n' % BACKGROUND)

full_icon(512).save("port/icon/icon-512.png")
print("icons written")
