package com.heartsoul.recomp;

import android.content.pm.ApplicationInfo;
import android.hardware.display.DisplayManager;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.Display;
import android.view.WindowManager;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;

/**
 * Runs the native game. SDL loads libmain.so and calls its SDL_main. On a
 * device with a second display, that display shows the companion screen. On
 * a device with one display the game runs as it would without any of this.
 */
public class HeartSoulActivity extends SDLActivity {
    private static final String TAG = "HeartSoul";
    private static final long POLL_INTERVAL_MS = 100;
    private static final int DISPLAY_CHECK_TICKS = 10; // look for a display once a second

    private final Handler handler = new Handler(Looper.getMainLooper());
    private BottomScreenPresentation presentation;
    private String lastJson;
    private boolean bridgeBroken;
    private int ticksUntilDisplayCheck;

    private final Runnable poll = new Runnable() {
        @Override
        public void run() {
            // The system can take the second display away and dismiss the
            // presentation (screen off, a system panel), so keep looking
            // instead of trying once.
            if (presentation != null && !presentation.isShowing()) {
                presentation = null;
            }
            if (presentation == null && --ticksUntilDisplayCheck <= 0) {
                ticksUntilDisplayCheck = DISPLAY_CHECK_TICKS;
                showBottomScreen();
            }
            if (presentation != null && !bridgeBroken) {
                try {
                    String json = previewJson();
                    if (json == null) {
                        json = DualScreenBridge.nativeGetStateJson();
                    }
                    if (json != null && !json.equals(lastJson)) {
                        lastJson = json;
                        presentation.setState(BottomScreenState.parse(json));
                    }
                } catch (UnsatisfiedLinkError e) {
                    // The native library did not load. SDL reports that itself.
                    Log.e(TAG, "bottom screen bridge unavailable", e);
                    bridgeBroken = true;
                }
            }
            handler.postDelayed(this, POLL_INTERVAL_MS);
        }
    };

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }

    @Override
    protected void onResume() {
        super.onResume();
        ticksUntilDisplayCheck = 0;
        handler.removeCallbacks(poll);
        handler.post(poll);
    }

    @Override
    protected void onPause() {
        handler.removeCallbacks(poll);
        dismissBottomScreen();
        super.onPause();
    }

    @Override
    protected void onDestroy() {
        handler.removeCallbacks(poll);
        dismissBottomScreen();
        super.onDestroy();
    }

    private void showBottomScreen() {
        DisplayManager manager = (DisplayManager) getSystemService(DISPLAY_SERVICE);
        if (manager == null) {
            return;
        }
        int ownDisplayId = getWindowManager().getDefaultDisplay().getDisplayId();
        Display target = null;
        for (Display display : manager.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION)) {
            if (display.getDisplayId() != ownDisplayId) {
                target = display;
                break;
            }
        }
        if (target == null) {
            return; // one screen
        }
        BottomScreenPresentation created = new BottomScreenPresentation(this, target);
        try {
            created.show();
        } catch (WindowManager.InvalidDisplayException e) {
            Log.w(TAG, "second display went away", e);
            return;
        }
        presentation = created;
        lastJson = null;
    }

    /**
     * Debug builds only: if the app's files folder holds
     * bottom_screen_preview.json, the bottom screen shows that state instead
     * of the game's. It lets a layout be checked on the device for battles
     * and parties that are not in front of you, with adb push and screencap
     * and no input. Delete the file to go back to the live state.
     */
    private String previewJson() {
        if ((getApplicationInfo().flags & ApplicationInfo.FLAG_DEBUGGABLE) == 0) {
            return null;
        }
        File dir = getExternalFilesDir(null);
        File file = dir != null ? new File(dir, "bottom_screen_preview.json") : null;
        if (file == null || !file.isFile() || file.length() > 64 * 1024) {
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

    private void dismissBottomScreen() {
        if (presentation != null) {
            presentation.dismiss();
            presentation = null;
        }
    }
}
