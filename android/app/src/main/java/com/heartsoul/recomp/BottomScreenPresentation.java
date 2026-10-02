package com.heartsoul.recomp;

import android.app.Presentation;
import android.content.Context;
import android.os.Bundle;
import android.view.Display;
import android.view.ViewGroup;
import android.view.WindowManager;

/** Shows the companion view on the second display. */
final class BottomScreenPresentation extends Presentation {
    private BottomScreenView view;

    BottomScreenPresentation(Context context, Display display) {
        super(context, display);
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        // The game's window must keep key focus, or the controller stops
        // reaching the game the moment this screen is touched.
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
                | WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        view = new BottomScreenView(getContext());
        setContentView(view, new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));
    }

    void setState(BottomScreenState state) {
        if (view != null) {
            view.setState(state);
        }
    }
}
