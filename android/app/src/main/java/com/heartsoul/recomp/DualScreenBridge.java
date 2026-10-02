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
}
