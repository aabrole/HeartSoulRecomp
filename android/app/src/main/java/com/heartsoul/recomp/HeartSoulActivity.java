package com.heartsoul.recomp;

import android.app.ActivityOptions;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.content.pm.ApplicationInfo;
import android.graphics.Point;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.Display;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;

/**
 * Runs the native game. SDL loads libmain.so and calls its SDL_main. On a
 * device with a second display, that display shows the companion screen. On
 * a device with one display the game runs as it would without any of this.
 *
 * The companion screen is a Presentation where the display takes one (the
 * Thor). Where it does not, it is BottomScreenActivity, launched onto that
 * display. The start screen's choices are read here, before SDL starts the
 * game, so a game launched without the start screen still uses them.
 */
public class HeartSoulActivity extends SDLActivity {
    private static final String TAG = "HeartSoul";
    private static final long POLL_INTERVAL_MS = 100;
    private static final int DISPLAY_CHECK_TICKS = 10; // look for a display once a second

    private static boolean running;

    private final Handler handler = new Handler(Looper.getMainLooper());
    private BottomScreenPresentation presentation;
    private Object lastSink;
    private String lastJson;
    private boolean bridgeBroken;
    private int ticksUntilDisplayCheck;
    private boolean bottomScreenEnabled = true;
    // A display whose Presentation was refused: use the activity there.
    private int presentationRefusedDisplay = Display.INVALID_DISPLAY;
    // The activity was launched since the game last became visible. It is not
    // launched again until then, so a player who opens something else on that
    // screen keeps it.
    private boolean bottomActivityLaunched;
    private BottomScreenActivity focusReclaimedFor;
    private boolean resumed;

    private static final long[] RECLAIM_DELAYS_MS = { 300, 800, 1500, 3000 };

    private final Runnable reclaimFocusLater = new Runnable() {
        @Override
        public void run() {
            if (!hasWindowFocus() && BottomScreenActivity.current() != null && resumed) {
                reclaimFocus();
            }
        }
    };

    private final Runnable poll = new Runnable() {
        @Override
        public void run() {
            // The system can take the second display away and dismiss the
            // presentation (screen off, a system panel), so keep looking
            // instead of trying once.
            if (presentation != null && !presentation.isShowing()) {
                presentation = null;
            }
            BottomScreenActivity activity = BottomScreenActivity.current();
            if (bottomScreenEnabled && presentation == null && activity == null && !bottomActivityLaunched
                    && --ticksUntilDisplayCheck <= 0) {
                ticksUntilDisplayCheck = DISPLAY_CHECK_TICKS;
                showBottomScreen();
            }
            if (activity != null && focusReclaimedFor != activity) {
                // Launching it moved key focus to its display. Take it back,
                // after its launch has settled: done at once, the launch
                // finishes afterwards and moves focus over again.
                focusReclaimedFor = activity;
                for (long delay : RECLAIM_DELAYS_MS) {
                    handler.postDelayed(reclaimFocusLater, delay);
                }
            }
            Object sink = presentation != null ? presentation : activity;
            if (sink != lastSink) {
                lastSink = sink;
                lastJson = null;
            }
            if (sink != null && !bridgeBroken) {
                try {
                    String json = previewJson();
                    if (json == null) {
                        json = DualScreenBridge.nativeGetStateJson();
                    }
                    if (json != null && !json.equals(lastJson)) {
                        lastJson = json;
                        BottomScreenState state = BottomScreenState.parse(json);
                        if (presentation != null) {
                            presentation.setState(state);
                        } else {
                            activity.setState(state);
                        }
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

    /** True from the game's creation to its end, for StartActivity. */
    static boolean isRunning() {
        return running;
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        // Before SDL starts the game, which reads them once.
        Display display = getWindowManager().getDefaultDisplay();
        Point size = new Point();
        display.getRealSize(size);
        Settings settings = Settings.load(this, size.x, size.y);
        settings.applyToEnvironment();
        bottomScreenEnabled = settings.bottomScreen;
        running = true;
        super.onCreate(savedInstanceState);
    }

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }

    @Override
    protected void onStart() {
        super.onStart();
        bottomActivityLaunched = false;
    }

    @Override
    protected void onResume() {
        super.onResume();
        resumed = true;
        ticksUntilDisplayCheck = 0;
        handler.removeCallbacks(poll);
        handler.post(poll);
    }

    @Override
    protected void onPause() {
        resumed = false;
        handler.removeCallbacks(reclaimFocusLater);
        handler.removeCallbacks(poll);
        dismissPresentation();
        super.onPause();
    }

    @Override
    protected void onStop() {
        // The activity stays through a pause: on a device that pauses the
        // game while that activity is in front, finishing it on pause would
        // bring the game back and launch it again, over and over.
        BottomScreenActivity.finishCurrent();
        super.onStop();
    }

    @Override
    protected void onDestroy() {
        handler.removeCallbacks(reclaimFocusLater);
        handler.removeCallbacks(poll);
        dismissPresentation();
        BottomScreenActivity.finishCurrent();
        running = false;
        super.onDestroy();
    }

    private void showBottomScreen() {
        Display target = SecondScreen.find(this, getWindowManager().getDefaultDisplay().getDisplayId());
        if (target == null) {
            return; // one screen
        }
        if (target.getDisplayId() != presentationRefusedDisplay && !debugFlag("force_bottom_activity")) {
            try {
                BottomScreenPresentation created = new BottomScreenPresentation(this, target);
                created.show();
                presentation = created;
                return;
            } catch (RuntimeException e) {
                // InvalidDisplayException on a display that is not a
                // presentation display; anything else, same answer.
                Log.w(TAG, "display " + target.getDisplayId() + " refused a presentation", e);
                presentationRefusedDisplay = target.getDisplayId();
            }
        }
        launchBottomActivity(target);
    }

    private void launchBottomActivity(Display target) {
        bottomActivityLaunched = true;
        ActivityOptions options = ActivityOptions.makeBasic();
        options.setLaunchDisplayId(target.getDisplayId());
        Intent intent = new Intent(this, BottomScreenActivity.class);
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_NO_ANIMATION);
        try {
            startActivity(intent, options.toBundle());
            Log.i(TAG, "bottom screen activity launched on display " + target.getDisplayId());
        } catch (SecurityException | ActivityNotFoundException e) {
            Log.w(TAG, "could not launch the bottom screen on display " + target.getDisplayId(), e);
        }
    }

    /**
     * Brings the game's display back in front, and key focus with it. Asking
     * for this activity again does that; moveTaskToFront does not, since the
     * task is already the front one on its own display.
     */
    private void reclaimFocus() {
        Intent intent = new Intent(this, HeartSoulActivity.class);
        intent.addFlags(Intent.FLAG_ACTIVITY_REORDER_TO_FRONT | Intent.FLAG_ACTIVITY_NO_ANIMATION);
        try {
            startActivity(intent);
        } catch (RuntimeException e) {
            Log.w(TAG, "could not take focus back from the bottom screen", e);
        }
    }

    /**
     * Debug builds only: true if the app's files folder holds a file of that
     * name. force_bottom_activity uses BottomScreenActivity even where a
     * Presentation would work, to test that path on a device or emulator
     * whose second display takes one.
     */
    private boolean debugFlag(String name) {
        if ((getApplicationInfo().flags & ApplicationInfo.FLAG_DEBUGGABLE) == 0) {
            return false;
        }
        File dir = getExternalFilesDir(null);
        return dir != null && new File(dir, name).isFile();
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

    private void dismissPresentation() {
        if (presentation != null) {
            presentation.dismiss();
            presentation = null;
        }
    }
}
