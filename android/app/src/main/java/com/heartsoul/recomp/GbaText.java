package com.heartsoul.recomp;

import android.content.res.AssetManager;
import android.graphics.Canvas;
import android.util.Log;

import java.io.IOException;

/**
 * The game's fonts as families, and the rules for making a string fit a box.
 *
 * A family is a font and its narrower cuts (normal, narrow, narrower), which
 * is how Heart & Soul itself fits long names. To fit a string the fitter
 * tries, from the largest allowed scale down: each cut of the family at that
 * scale, widest first. The narrowest cut is kept for scales at or below the
 * base scale, so a big label never gets squeezed before it gets smaller. If
 * nothing fits at the smallest allowed scale, the string is cut short and
 * ends in the game's ellipsis.
 */
final class GbaText {
    private static final String TAG = "HeartSoul";

    /** A string laid out to fit: what to draw, with which font, how big. */
    static final class Fit {
        String text = "";
        GbaFont font;
        int scale = 1;
        /** Screen pixels. */
        int width;

        int height() {
            return font.boxHeight * scale;
        }
    }

    /** normal, narrow, narrower: names and labels. */
    final GbaFont[] main;
    /** small, small narrow, small narrower: secondary details. */
    final GbaFont[] small;

    private GbaText(GbaFont[] main, GbaFont[] small) {
        this.main = main;
        this.small = small;
    }

    /** Returns null if the font assets are missing, so the caller can fall back. */
    static GbaText load(AssetManager assets) {
        try {
            GbaFont[] main = {
                GbaFont.load(assets, "latin_normal", 2, 13, 8),
                GbaFont.load(assets, "latin_narrow", 2, 13, 8),
                GbaFont.load(assets, "latin_narrower", 2, 13, 8),
            };
            GbaFont[] small = {
                GbaFont.load(assets, "latin_small", 3, 10, 8),
                GbaFont.load(assets, "latin_small_narrow", 3, 10, 8),
                GbaFont.load(assets, "latin_small_narrower", 3, 10, 8),
            };
            return new GbaText(main, small);
        } catch (IOException e) {
            Log.e(TAG, "game fonts unavailable", e);
            return null;
        }
    }

    /**
     * The largest scale from maxScale down to minScale at which every one of
     * the strings fits maxWidth in some cut of the family, and the family's
     * box fits maxHeight. Used so a set of buttons shares one text size.
     */
    static int groupScale(GbaFont[] family, String[] texts, int maxWidth, int maxHeight,
                          int maxScale, int baseScale, int minScale) {
        for (int scale = maxScale; scale > minScale; scale--) {
            if (family[0].boxHeight * scale > maxHeight) {
                continue;
            }
            boolean all = true;
            for (String text : texts) {
                if (text != null && pick(family, text, maxWidth, scale, baseScale) == null) {
                    all = false;
                    break;
                }
            }
            if (all) {
                return scale;
            }
        }
        return minScale;
    }

    /** Fits one string; see the class comment. Never wider than maxWidth. */
    static Fit fit(GbaFont[] family, String text, int maxWidth, int maxHeight,
                   int maxScale, int baseScale, int minScale) {
        Fit fit = new Fit();
        minScale = Math.max(1, Math.min(minScale, maxScale));
        for (int scale = maxScale; scale >= minScale; scale--) {
            if (family[0].boxHeight * scale > maxHeight && scale > minScale) {
                continue;
            }
            GbaFont font = pick(family, text, maxWidth, scale, baseScale);
            if (font != null) {
                fit.text = text;
                fit.font = font;
                fit.scale = scale;
                fit.width = font.width(text) * scale;
                return fit;
            }
        }
        // Too long even at the smallest size: cut it and add an ellipsis.
        GbaFont font = family[family.length - 1];
        fit.font = font;
        fit.scale = minScale;
        String cut = text;
        while (!cut.isEmpty()) {
            cut = cut.substring(0, cut.length() - 1);
            String candidate = cut.trim() + GbaFont.ELLIPSIS;
            if (font.width(candidate) * minScale <= maxWidth) {
                fit.text = candidate;
                fit.width = font.width(candidate) * minScale;
                return fit;
            }
        }
        fit.text = "";
        fit.width = 0;
        return fit;
    }

    private static GbaFont pick(GbaFont[] family, String text, int maxWidth, int scale, int baseScale) {
        int cuts = scale > baseScale ? Math.min(2, family.length) : family.length;
        for (int i = 0; i < cuts; i++) {
            if (family[i].width(text) * scale <= maxWidth) {
                return family[i];
            }
        }
        return null;
    }

    /** Draws a fitted string with the middle of its capitals on centerY. */
    static void drawCentered(Canvas canvas, Fit fit, int x, int centerY, int color, int shadow) {
        if (fit.font == null || fit.text.isEmpty()) {
            return;
        }
        int top = centerY - (fit.font.capCenter - fit.font.boxTop) * fit.scale;
        fit.font.draw(canvas, fit.text, x, top, fit.scale, color, shadow);
    }
}
