package com.heartsoul.recomp;

import android.content.res.AssetManager;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Rect;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.util.HashMap;
import java.util.Map;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * One of the game's Latin fonts, read from the glyph sheet in
 * graphics/fonts/ (copied into the assets at build time) and drawn with the
 * per-glyph widths of src/fonts.c, scaled up by whole pixels so it stays as
 * crisp as on the GBA.
 *
 * The sheet is 16 glyphs across, each glyph a 16x16 cell, in the game's own
 * character order. Its four palette entries are: 0 outside the glyph, 1 the
 * text color, 2 the shadow (already placed one pixel down and right of the
 * text), 3 the background inside the glyph. Only 1 and 2 are drawn.
 *
 * Text arrives as UTF-8 from the game (dualscreen_bridge.c) and is mapped
 * back to game characters with charmap.txt. A character in the private use
 * range U+E000 to U+E0FF is drawn as game character (c - U+E000), which is
 * how the view asks for symbols such as the Lv glyph.
 */
final class GbaFont {
    static final int CELL = 16;
    static final int GLYPHS = 256;
    /** Game character codes the view uses directly. */
    static final char LV = (char) (0xE000 + 0x34);
    static final char ELLIPSIS = (char) (0xE000 + 0xB0);
    private static final int CHAR_QUESTION = 0xAC;
    private static final int CHAR_SPACE = 0x00;

    final String name;
    /** First cell row that holds text ink for layout (accents may go above). */
    final int boxTop;
    /** Rows from boxTop to the bottom of descenders and shadow. */
    final int boxHeight;
    /** Row of the middle of capital letters, for centring a line on a point. */
    final int capCenter;

    private final int[] widths = new int[GLYPHS];
    /** Palette index per pixel of the first 256 glyphs (a 256x256 sheet). */
    private final byte[] sheet = new byte[CELL * 16 * CELL * 16];
    private final Map<Long, Bitmap> colored = new HashMap<>();
    private final Paint paint = new Paint();
    private final Rect src = new Rect();
    private final Rect dst = new Rect();

    private static Map<Character, Integer> charmap;

    private GbaFont(String name, int boxTop, int boxHeight, int capCenter) {
        this.name = name;
        this.boxTop = boxTop;
        this.boxHeight = boxHeight;
        this.capCenter = capCenter;
        paint.setFilterBitmap(false);
        paint.setAntiAlias(false);
    }

    /** Loads fonts/<name>.png and fonts/<name>.widths from the assets. */
    static GbaFont load(AssetManager assets, String name, int boxTop, int boxHeight, int capCenter) throws IOException {
        loadCharmap(assets);
        GbaFont font = new GbaFont(name, boxTop, boxHeight, capCenter);
        try (InputStream in = assets.open("fonts/" + name + ".widths")) {
            for (int i = 0; i < GLYPHS; i++) {
                int value = in.read();
                if (value < 0) {
                    throw new IOException(name + ".widths is short");
                }
                font.widths[i] = value;
            }
        }
        BitmapFactory.Options options = new BitmapFactory.Options();
        options.inScaled = false;
        options.inPremultiplied = false;
        options.inPreferredConfig = Bitmap.Config.ARGB_8888;
        Bitmap png;
        try (InputStream in = assets.open("fonts/" + name + ".png")) {
            png = BitmapFactory.decodeStream(in, null, options);
        }
        if (png == null || png.getWidth() < CELL * 16 || png.getHeight() < CELL * 16) {
            throw new IOException(name + ".png is not a 16x16-cell glyph sheet");
        }
        int size = CELL * 16;
        int[] argb = new int[size * size];
        png.getPixels(argb, 0, size, 0, 0, size, size);
        png.recycle();
        for (int i = 0; i < argb.length; i++) {
            font.sheet[i] = (byte) paletteIndex(argb[i]);
        }
        return font;
    }

    /** The sheets' palette: outside (light blue), text, shadow, inside. */
    private static final int[] SHEET_PALETTE = { 0x90C8FF, 0x383838, 0xD8D8D8, 0xFFFFFF };

    /**
     * The decoder gives colors, not palette indices, so each pixel is
     * matched to the nearest palette color. Transparent pixels are outside.
     */
    private static int paletteIndex(int argb) {
        if (Color.alpha(argb) < 128) {
            return 0;
        }
        int best = 0;
        int bestDistance = Integer.MAX_VALUE;
        for (int i = 0; i < SHEET_PALETTE.length; i++) {
            int dr = Color.red(argb) - ((SHEET_PALETTE[i] >> 16) & 0xFF);
            int dg = Color.green(argb) - ((SHEET_PALETTE[i] >> 8) & 0xFF);
            int db = Color.blue(argb) - (SHEET_PALETTE[i] & 0xFF);
            int distance = dr * dr + dg * dg + db * db;
            if (distance < bestDistance) {
                bestDistance = distance;
                best = i;
            }
        }
        return best;
    }

    private static synchronized void loadCharmap(AssetManager assets) throws IOException {
        if (charmap != null) {
            return;
        }
        // Lines such as:  'é'         = 1B   and   '\''        = B4
        Pattern line = Pattern.compile("^'(\\\\'|[^\\\\])'\\s*=\\s*([0-9A-Fa-f]{2})\\s*(@.*)?$");
        Map<Character, Integer> map = new HashMap<>();
        try (BufferedReader reader = new BufferedReader(
                new InputStreamReader(assets.open("fonts/charmap.txt"), StandardCharsets.UTF_8))) {
            String text;
            while ((text = reader.readLine()) != null) {
                Matcher m = line.matcher(text.trim());
                if (!m.matches()) {
                    continue;
                }
                String key = m.group(1);
                char c = key.equals("\\'") ? '\'' : key.charAt(0);
                if (!map.containsKey(c)) { // the first entry wins, as in the game's tools
                    map.put(c, Integer.parseInt(m.group(2), 16));
                }
            }
        }
        // Forms the bridge writes that charmap.txt spells differently.
        map.put(' ', CHAR_SPACE);
        putIfAbsent(map, '\'', 0xB4);
        putIfAbsent(map, '"', 0xB1);
        putIfAbsent(map, '…', 0xB0);
        charmap = map;
    }

    private static void putIfAbsent(Map<Character, Integer> map, char c, int code) {
        if (!map.containsKey(c)) {
            map.put(c, code);
        }
    }

    /** The game character for one UTF-16 char; '?' when the game has none. */
    static int glyphFor(char c) {
        if (c >= 0xE000 && c <= 0xE0FF) {
            return c - 0xE000;
        }
        Integer code = charmap != null ? charmap.get(c) : null;
        if (code == null) {
            code = charmap != null ? charmap.get(Character.toUpperCase(c)) : null;
        }
        return code != null && code < GLYPHS ? code : CHAR_QUESTION;
    }

    /** Width in font pixels (multiply by the scale for screen pixels). */
    int width(CharSequence text) {
        int total = 0;
        for (int i = 0; i < text.length(); i++) {
            total += widths[glyphFor(text.charAt(i))];
        }
        return total;
    }

    /**
     * Draws text with the top of its layout box (row boxTop of the cell) at
     * y. Returns the x after the last glyph.
     */
    int draw(Canvas canvas, CharSequence text, int x, int y, int scale, int color, int shadow) {
        Bitmap atlas = atlas(color, shadow);
        int top = y - boxTop * scale;
        for (int i = 0; i < text.length(); i++) {
            int glyph = glyphFor(text.charAt(i));
            int width = widths[glyph];
            if (width > 0) {
                int sx = (glyph % 16) * CELL;
                int sy = (glyph / 16) * CELL;
                src.set(sx, sy, sx + Math.min(width, CELL), sy + CELL);
                dst.set(x, top, x + Math.min(width, CELL) * scale, top + CELL * scale);
                canvas.drawBitmap(atlas, src, dst, paint);
            }
            x += width * scale;
        }
        return x;
    }

    /** The sheet recolored for one text color and shadow color. */
    private Bitmap atlas(int color, int shadow) {
        long key = ((long) color << 32) ^ (shadow & 0xFFFFFFFFL);
        Bitmap bitmap = colored.get(key);
        if (bitmap != null) {
            return bitmap;
        }
        int size = CELL * 16;
        int[] pixels = new int[size * size];
        for (int i = 0; i < pixels.length; i++) {
            int index = sheet[i];
            pixels[i] = index == 1 ? color : index == 2 ? shadow : 0;
        }
        bitmap = Bitmap.createBitmap(pixels, size, size, Bitmap.Config.ARGB_8888);
        colored.put(key, bitmap);
        return bitmap;
    }
}
