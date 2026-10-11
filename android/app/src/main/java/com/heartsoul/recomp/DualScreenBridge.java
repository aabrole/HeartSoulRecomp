package com.heartsoul.recomp;

/**
 * JNI boundary to the game in libmain.so (src/platform/dualscreen_bridge.c).
 * Every call is safe from the UI thread: the game thread publishes its state
 * as text under a lock and picks requests up at its next frame.
 */
public final class DualScreenBridge {
    private DualScreenBridge() {}

    /** Tap kinds, matching enum DualScreenTap. */
    public static final int TAP_MOVE = 1;
    public static final int TAP_ACTION = 2;

    /** Action menu slots, matching enum DualScreenAction. */
    public static final int ACTION_FIGHT = 0;
    public static final int ACTION_BAG = 1;
    public static final int ACTION_POKEMON = 2;
    public static final int ACTION_RUN = 3;

    /** The latest state snapshot as JSON. */
    public static native String nativeGetStateJson();

    /**
     * Asks the game to choose a move (index 0 to 3) or an action menu entry.
     * The game presses the buttons a player would. A tap made while no
     * battle menu is open is ignored.
     */
    public static native void nativeTap(int kind, int index);

    /**
     * Holds GBA buttons down for a number of frames. Masks: A=1 B=2 SELECT=4
     * START=8 RIGHT=16 LEFT=32 UP=64 DOWN=128 R=256 L=512.
     */
    public static native void nativeInjectKeys(int keys, int frames);

    /** Documents, matching enum DualScreenDoc. */
    public static final int DOC_NONE = 0;
    public static final int DOC_BAG = 1;
    public static final int DOC_GUIDE = 2;
    public static final int DOC_DEX = 3;
    public static final int DOC_DEX_ENTRY = 4; // param: species
    public static final int DOC_MAP = 5;

    /**
     * Which document the bottom screen shows. The game builds it at its next
     * frame and keeps it fresh while it stays wanted; DOC_NONE stops that.
     */
    public static native void nativeSetWantedDoc(int kind, int param);

    /** The last document built, as JSON with its "doc" and "param". */
    public static native String nativeGetDocJson();

    /**
     * The last document's picture as width, height and then the ARGB
     * pixels, or null when it has none.
     */
    public static native int[] nativeGetDocImage();

    /** The type chart as JSON (see TypeChart). */
    public static native String nativeGetTypeChartJson();

    /** A Pokémon's menu icon as 32x32 ARGB pixels, or null. */
    public static native int[] nativeGetMonIcon(int species, int personality, boolean egg);

    /**
     * An item's icon as 24x24 ARGB pixels, or null while the game has not
     * decoded it yet (it starts on the first ask).
     */
    public static native int[] nativeGetItemIcon(int item);
}
