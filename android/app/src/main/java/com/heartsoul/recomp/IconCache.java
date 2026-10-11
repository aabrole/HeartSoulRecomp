package com.heartsoul.recomp;

import android.graphics.Bitmap;

import java.util.HashMap;
import java.util.Map;

/**
 * The game's own menu icons for Pokémon and items, as bitmaps, read from the
 * game through DualScreenBridge and kept. Item icons are decoded by the game
 * thread on request, so the first ask for one returns null; ask again on a
 * later draw.
 */
final class IconCache {
    private static final int MON_SIZE = 32;
    private static final int ITEM_SIZE = 24;
    private static final int MAX_ENTRIES = 256;

    private final Map<String, Bitmap> mons = new HashMap<>();
    private final Map<Integer, Bitmap> items = new HashMap<>();
    private boolean broken;

    Bitmap mon(int species, int personality, boolean egg) {
        String key = species + ":" + personality + ":" + egg;
        Bitmap bitmap = mons.get(key);
        if (bitmap != null || broken) {
            return bitmap;
        }
        try {
            int[] argb = DualScreenBridge.nativeGetMonIcon(species, personality, egg);
            bitmap = toBitmap(argb, MON_SIZE);
        } catch (UnsatisfiedLinkError e) {
            broken = true;
        }
        if (bitmap != null) {
            if (mons.size() >= MAX_ENTRIES) {
                mons.clear();
            }
            mons.put(key, bitmap);
        }
        return bitmap;
    }

    Bitmap item(int item) {
        Bitmap bitmap = items.get(item);
        if (bitmap != null || broken) {
            return bitmap;
        }
        try {
            bitmap = toBitmap(DualScreenBridge.nativeGetItemIcon(item), ITEM_SIZE);
        } catch (UnsatisfiedLinkError e) {
            broken = true;
        }
        if (bitmap != null) {
            if (items.size() >= MAX_ENTRIES) {
                items.clear();
            }
            items.put(item, bitmap);
        }
        return bitmap;
    }

    private static Bitmap toBitmap(int[] argb, int size) {
        if (argb == null || argb.length != size * size) {
            return null;
        }
        return Bitmap.createBitmap(argb, size, size, Bitmap.Config.ARGB_8888);
    }
}
