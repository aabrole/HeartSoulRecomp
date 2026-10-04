package com.heartsoul.recomp;

import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.graphics.Point;
import android.os.Build;
import android.os.Bundle;
import android.view.Display;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;

/**
 * What the launcher opens: the start screen (StartScreenView), then the game.
 * If the game is already running, it goes straight back to it, since its
 * choices only apply when the game starts.
 */
public class StartActivity extends Activity implements StartScreenView.Listener {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        if (HeartSoulActivity.isRunning()) {
            launchGame();
            return;
        }
        Display display = getWindowManager().getDefaultDisplay();
        Point size = new Point();
        display.getRealSize(size);
        Settings settings = Settings.load(this, size.x, size.y);
        boolean hasSecondScreen = SecondScreen.find(this, display.getDisplayId()) != null;
        StartScreenView view = new StartScreenView(this, settings, size.x, size.y, hasSecondScreen,
                "v" + versionName(), this);
        setContentView(view);
        hideSystemBars();
        view.requestFocus();
    }

    @Override
    public void onStartGame(Settings settings) {
        settings.save(this);
        launchGame();
    }

    private void launchGame() {
        startActivity(new Intent(this, HeartSoulActivity.class));
        overridePendingTransition(0, 0);
        finish();
    }

    private String versionName() {
        try {
            PackageInfo info = getPackageManager().getPackageInfo(getPackageName(), 0);
            return info.versionName;
        } catch (PackageManager.NameNotFoundException e) {
            return "";
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
