package com.heartsoul.recomp;

import android.app.Activity;
import android.os.Build;
import android.os.Bundle;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;

/**
 * The companion screen as an activity, for a second display that will not
 * take a Presentation (one without FLAG_PRESENTATION, such as a built-in
 * panel that runs its own launcher). Launched onto that display by
 * HeartSoulActivity, it covers the launcher there.
 *
 * Its window never takes key focus, so the controller keeps reaching the
 * game: HeartSoulActivity brings its own task back to the front once this
 * is up, and a touch here does not move focus. The game feeds it the same
 * state as the Presentation, through current().
 */
public final class BottomScreenActivity extends Activity {
    private static BottomScreenActivity current;

    private BottomScreenView view;

    /** The live instance, or null. UI thread only. */
    static BottomScreenActivity current() {
        return current != null && !current.isFinishing() ? current : null;
    }

    static void finishCurrent() {
        if (current != null) {
            current.finish();
            current = null;
        }
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
                | WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        view = new BottomScreenView(this);
        setContentView(view);
        hideSystemBars();
        current = this;
    }

    @Override
    protected void onDestroy() {
        if (current == this) {
            current = null;
        }
        super.onDestroy();
    }

    void setState(BottomScreenState state) {
        if (view != null) {
            view.setState(state);
        }
    }

    @SuppressWarnings("deprecation")
    private void hideSystemBars() {
        if (Build.VERSION.SDK_INT >= 30) {
            WindowInsetsController controller = getWindow().getInsetsController();
            if (controller != null) {
                controller.hide(WindowInsets.Type.systemBars());
                controller.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        } else {
            getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                    | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
        }
    }
}
