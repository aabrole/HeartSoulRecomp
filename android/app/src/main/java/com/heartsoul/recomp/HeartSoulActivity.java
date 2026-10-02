package com.heartsoul.recomp;

import org.libsdl.app.SDLActivity;

/** Runs the native game. SDL loads libmain.so and calls its SDL_main. */
public class HeartSoulActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }
}
