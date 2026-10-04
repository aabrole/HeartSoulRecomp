package com.heartsoul.recomp;

import android.content.Context;
import android.content.SharedPreferences;
import android.system.ErrnoException;
import android.system.Os;
import android.util.Log;

/**
 * The choices made on the start screen, kept between launches, and how they
 * reach the game: as environment variables that src/platform/sdl2.c reads
 * when the game starts (HNS_WIDESCREEN, HNS_TALL, HNS_SCALE). They are set in
 * this process before SDL starts the game, so a change applies from the next
 * start of the game.
 */
final class Settings {
    private static final String TAG = "HeartSoul";
    private static final String PREFS = "settings";

    /** The size of the game's picture. */
    static final int PICTURE_ORIGINAL = 0;   // 240x160, as on a GBA
    static final int PICTURE_WIDESCREEN = 1; // 288x160
    static final int PICTURE_ZOOMED_OUT = 2; // 288x216, 4:3
    static final int PICTURE_COUNT = 3;

    /** How the picture fills the screen. */
    static final int SCALE_FIT = 0;
    static final int SCALE_INTEGER = 1;
    static final int SCALE_STRETCH = 2;
    static final int SCALE_COUNT = 3;

    private static final String KEY_PICTURE = "picture";
    private static final String KEY_SCALE = "scale";
    private static final String KEY_BOTTOM_SCREEN = "bottom_screen";

    int picture;
    int scale;
    boolean bottomScreen;

    /** Game pixels across and down for a picture choice. */
    static int frameWidth(int picture) {
        return picture == PICTURE_ORIGINAL ? 240 : 288;
    }

    static int frameHeight(int picture) {
        return picture == PICTURE_ZOOMED_OUT ? 216 : 160;
    }

    /**
     * Before anything is chosen: widescreen on a screen at least 16:10 (the
     * Thor), the zoomed out 4:3 picture on anything squarer (the RG DS).
     */
    static int defaultPicture(int screenWidth, int screenHeight) {
        int longSide = Math.max(screenWidth, screenHeight);
        int shortSide = Math.min(screenWidth, screenHeight);
        if (shortSide <= 0 || longSide * 10 >= shortSide * 16) {
            return PICTURE_WIDESCREEN;
        }
        return PICTURE_ZOOMED_OUT;
    }

    static Settings load(Context context, int screenWidth, int screenHeight) {
        SharedPreferences prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        Settings settings = new Settings();
        settings.picture = clamp(prefs.getInt(KEY_PICTURE, defaultPicture(screenWidth, screenHeight)),
                PICTURE_COUNT, PICTURE_WIDESCREEN);
        settings.scale = clamp(prefs.getInt(KEY_SCALE, SCALE_FIT), SCALE_COUNT, SCALE_FIT);
        settings.bottomScreen = prefs.getBoolean(KEY_BOTTOM_SCREEN, true);
        return settings;
    }

    void save(Context context) {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
                .putInt(KEY_PICTURE, picture)
                .putInt(KEY_SCALE, scale)
                .putBoolean(KEY_BOTTOM_SCREEN, bottomScreen)
                .apply();
    }

    /** Hands the choices to the game. Call before SDL starts it. */
    void applyToEnvironment() {
        setenv("HNS_WIDESCREEN", picture == PICTURE_ORIGINAL ? "0" : "1");
        setenv("HNS_TALL", picture == PICTURE_ZOOMED_OUT ? "1" : "0");
        setenv("HNS_SCALE", scale == SCALE_INTEGER ? "integer" : scale == SCALE_STRETCH ? "stretch" : "fit");
    }

    private static void setenv(String name, String value) {
        try {
            Os.setenv(name, value, true);
        } catch (ErrnoException e) {
            Log.w(TAG, "could not set " + name, e);
        }
    }

    private static int clamp(int value, int count, int fallback) {
        return value >= 0 && value < count ? value : fallback;
    }
}
