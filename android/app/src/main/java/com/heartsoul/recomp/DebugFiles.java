package com.heartsoul.recomp;

import android.content.Context;
import android.content.pm.ApplicationInfo;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;

/**
 * Debug builds only: files pushed with adb into the app's external files
 * folder that change what the app does, so a layout can be checked on a
 * device for states that are not in front of you, with no input.
 *
 * bottom_screen_preview.json  the bottom screen shows this state, not the game's
 * bottom_bag_preview.json     the BAG tab shows this bag
 * force_bottom_activity       use BottomScreenActivity, not a Presentation
 * dump_bottom_screen          save the bottom screen as bottom_screen.png about once a second
 */
final class DebugFiles {
    private static final int MAX_TEXT = 256 * 1024;

    private DebugFiles() {}

    private static File file(Context context, String name) {
        if ((context.getApplicationInfo().flags & ApplicationInfo.FLAG_DEBUGGABLE) == 0) {
            return null;
        }
        File dir = context.getExternalFilesDir(null);
        return dir != null ? new File(dir, name) : null;
    }

    /** Where a debug output file goes, or null in a release build. */
    static File output(Context context, String name) {
        return file(context, name);
    }

    static boolean flag(Context context, String name) {
        File file = file(context, name);
        return file != null && file.isFile();
    }

    /** The file's text, or null in a release build or when there is none. */
    static String text(Context context, String name) {
        File file = file(context, name);
        if (file == null || !file.isFile() || file.length() > MAX_TEXT) {
            return null;
        }
        try (FileInputStream in = new FileInputStream(file)) {
            byte[] bytes = new byte[(int) file.length()];
            int read = 0;
            while (read < bytes.length) {
                int n = in.read(bytes, read, bytes.length - read);
                if (n < 0) {
                    break;
                }
                read += n;
            }
            return new String(bytes, 0, read, StandardCharsets.UTF_8);
        } catch (IOException e) {
            return null;
        }
    }
}
