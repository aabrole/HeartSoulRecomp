package com.heartsoul.recomp;

import android.content.Context;
import android.graphics.Point;
import android.hardware.display.DisplayManager;
import android.util.Log;
import android.view.Display;

/**
 * Finds the display for the companion screen.
 *
 * The AYN Thor's bottom screen is a presentation display. Others are not:
 * a built-in second panel can be an ordinary display with its own launcher
 * (as the Anbernic RG DS's appears to be), and the old lookup, which asked
 * only for presentation displays, never saw it. So: a presentation display
 * if there is one, otherwise any other public display big enough to be a
 * screen. Every display is logged once, so a logcat from a new device says
 * what it has.
 */
final class SecondScreen {
    private static final String TAG = "HeartSoul";
    private static final int MIN_SIDE = 200;
    private static String lastReport;

    private SecondScreen() {}

    static Display find(Context context, int ownDisplayId) {
        DisplayManager manager = (DisplayManager) context.getSystemService(Context.DISPLAY_SERVICE);
        if (manager == null) {
            return null;
        }
        report(manager, ownDisplayId);
        for (Display display : manager.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION)) {
            if (display.getDisplayId() != ownDisplayId && usable(display)) {
                return display;
            }
        }
        for (Display display : manager.getDisplays()) {
            if (display.getDisplayId() != ownDisplayId && usable(display)
                    && (display.getFlags() & Display.FLAG_PRIVATE) == 0) {
                return display;
            }
        }
        return null;
    }

    static boolean isPresentation(Display display) {
        return (display.getFlags() & Display.FLAG_PRESENTATION) != 0;
    }

    private static boolean usable(Display display) {
        if (!display.isValid()) {
            return false;
        }
        Point size = new Point();
        display.getRealSize(size);
        return Math.min(size.x, size.y) >= MIN_SIDE;
    }

    private static void report(DisplayManager manager, int ownDisplayId) {
        StringBuilder text = new StringBuilder("displays (game on ").append(ownDisplayId).append("):");
        for (Display display : manager.getDisplays()) {
            Point size = new Point();
            display.getRealSize(size);
            text.append(String.format(java.util.Locale.US, " [%d \"%s\" %dx%d flags=0x%x state=%d%s]",
                    display.getDisplayId(), display.getName(), size.x, size.y, display.getFlags(),
                    display.getState(), isPresentation(display) ? " presentation" : ""));
        }
        String report = text.toString();
        if (!report.equals(lastReport)) {
            lastReport = report;
            Log.i(TAG, report);
        }
    }
}
