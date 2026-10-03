#!/usr/bin/env python3
"""Draws the app icon: an open dual-screen handheld with a gold heart on the
top screen and a silver soul on the bottom one. Hand-placed pixel art on a
48x48 grid, scaled with nearest neighbour so pixels stay crisp.

Writes the Android launcher icons into android/app/src/main/res and a 512px
preview to port/icon/icon-512.png. Run from the repo root."""
import os
from PIL import Image, ImageDraw

G = 48
BACKGROUND = (34, 26, 62)

PALETTE = {
    "k": (20, 16, 34),      # outline
    # shell (light, like a white handheld)
    "1": (232, 234, 244),   # shell highlight
    "2": (198, 202, 220),   # shell
    "3": (150, 154, 182),   # shell shade
    "4": (104, 106, 138),   # shell deep shade / hinge
    # screens
    "s": (16, 20, 40),      # screen
    "g": (34, 42, 74),      # screen glare
    "b": (24, 30, 56),      # screen bezel
    # gold heart
    "W": (255, 250, 214),
    "Y": (250, 206, 70),
    "y": (230, 166, 38),
    "O": (176, 106, 24),
    # silver soul
    "V": (250, 252, 255),
    "S": (196, 212, 236),
    "T": (138, 160, 200),
    "D": (86, 104, 150),
    # controls
    "r": (214, 72, 84),     # A button
    "u": (84, 134, 220),    # B button
    "p": (60, 62, 88),      # d-pad
    "h": (26, 26, 44),      # gold glow, far
    "H": (52, 44, 46),      # gold glow, near
    "c": (20, 28, 54),      # silver glow, far
    "C": (30, 44, 82),      # silver glow, near
}

TOP_SHELL = (7, 2, 40, 23)       # x0, y0, x1, y1 inclusive
TOP_SCREEN = (10, 5, 37, 20)
HINGE = (9, 24, 38, 25)
BOTTOM_SHELL = (4, 26, 43, 46)
BOTTOM_SCREEN = (14, 29, 33, 44)

HEART = [
    "..kkkk...kkkk..",
    ".kWWYYk.kYYYyk.",
    "kWWYYYYkYYYYyOk",
    "kWYYYYYYYYYYyOk",
    "kYYYYYYYYYYYyOk",
    "kYYYYYYYYYYyyOk",
    ".kYYYYYYYYYyOk.",
    "..kYYYYYYYyOk..",
    "...kYYYYYyOk...",
    "....kYYYyOk....",
    ".....kYyOk.....",
    "......kOk......",
    ".......k.......",
]

SOUL = [
    ".....k.....",
    "....kVk....",
    "....kVk....",
    "...kVSk....",
    "...kVSSk...",
    "..kVSSSk.k.",
    "..kVSSSSkTk",
    ".kVSSSSSSTk",
    ".kVSSVVSSTk",
    "kVSSVVVVSTk",
    "kVSSVVVVSTk",
    "kVSSSVVSSTk",
    ".kSSSSSSTk.",
    "..kTSSSTk..",
    "...kkkkk...",
]


def fill_rect(px, box, ch):
    x0, y0, x1, y1 = box
    for x in range(x0, x1 + 1):
        for y in range(y0, y1 + 1):
            px[(x, y)] = ch


def rounded_shell(px, box):
    """A shell with a dark outline, rounded corners, light top-left edge and
    shaded bottom-right edge."""
    x0, y0, x1, y1 = box
    for x in range(x0, x1 + 1):
        for y in range(y0, y1 + 1):
            corner = (x in (x0, x1)) and (y in (y0, y1))
            if corner:
                continue
            edge = x in (x0, x1) or y in (y0, y1) or \
                (x in (x0 + 1, x1 - 1) and y in (y0 + 1, y1 - 1))
            if edge:
                px[(x, y)] = "k"
            elif x == x0 + 1 or y == y0 + 1:
                px[(x, y)] = "1"
            elif x == x1 - 1 or y == y1 - 1:
                px[(x, y)] = "3"
            else:
                px[(x, y)] = "2"


def screen(px, box):
    x0, y0, x1, y1 = box
    fill_rect(px, (x0 - 1, y0 - 1, x1 + 1, y1 + 1), "k")
    fill_rect(px, box, "s")
    # Glare along the top-left corner.
    for i in range(4):
        px[(x0 + i, y0)] = "g"
    for i in range(3):
        px[(x0, y0 + i)] = "g"


def sprite(px, art, box, glow=None):
    """Centres a sprite in a box, with an optional glow on the screen
    around it (near, far)."""
    x0, y0, x1, y1 = box
    h, w = len(art), len(art[0])
    ox = x0 + (x1 - x0 + 1 - w) // 2
    oy = y0 + (y1 - y0 + 1 - h) // 2
    for y, row in enumerate(art):
        assert len(row) == w, (row, w)
        for x, ch in enumerate(row):
            if ch != ".":
                px[(ox + x, oy + y)] = ch
    if glow is None:
        return
    filled = {(ox + x, oy + y) for y, row in enumerate(art) for x, ch in enumerate(row) if ch != "."}
    for ring, ch in ((1, glow[0]),):
        for (fx, fy) in list(filled):
            for dx in range(-ring, ring + 1):
                for dy in range(-ring, ring + 1):
                    p = (fx + dx, fy + dy)
                    if px.get(p) == "s" and abs(dx) + abs(dy) <= ring:
                        px[p] = ch


def compose():
    px = {}
    rounded_shell(px, BOTTOM_SHELL)
    # Hinge between the halves.
    fill_rect(px, (HINGE[0], HINGE[1] - 1, HINGE[2], HINGE[3]), "k")
    fill_rect(px, (HINGE[0] + 1, HINGE[1], HINGE[2] - 1, HINGE[3] - 1 + 1), "4")
    rounded_shell(px, TOP_SHELL)
    screen(px, TOP_SCREEN)
    screen(px, BOTTOM_SCREEN)
    sprite(px, HEART, TOP_SCREEN, glow=("H", "h"))
    sprite(px, SOUL, BOTTOM_SCREEN, glow=("C", "c"))
    # D-pad on the left of the bottom half.
    for x, y in [(8, 34), (8, 35), (8, 36), (7, 35), (9, 35)]:
        px[(x, y)] = "p"
    # A and B buttons on the right.
    px[(39, 34)] = "r"
    px[(37, 36)] = "u"
    # Start/select dots under the screen.
    px[(22, 45)] = "4"
    px[(25, 45)] = "4"
    img = Image.new("RGBA", (G, G), (0, 0, 0, 0))
    pix = img.load()
    for (x, y), ch in px.items():
        if 0 <= x < G and 0 <= y < G:
            pix[x, y] = PALETTE[ch] + (255,)
    return img


def scaled(art, size):
    return art.resize((size, size), Image.NEAREST)


def full_icon(art, size, round_mask=False):
    base = Image.new("RGBA", (size, size), BACKGROUND + (255,))
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
