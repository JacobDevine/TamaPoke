package org.tamapoke.app;

import android.os.Build;
import android.view.DisplayCutout;
import android.view.View;
import android.view.WindowInsets;

import org.libsdl.app.SDLActivity;

// Everything is native: SDLActivity hosts the surface and calls main() in
// libmain.so (android_main.cpp), which runs the real firmware. This class
// gives the app its own package, and reports the screen's insets.
public class MainActivity extends SDLActivity {
    // The rows the camera cutout covers, in physical pixels. The app draws
    // under the cutout (SDL asks for SHORT_EDGES when fullscreen), so the
    // full-height layout keeps its header clear of these. android_main.cpp
    // polls them: they are only known once the window has been laid out.
    public static volatile int insetTop = 0;
    public static volatile int insetBottom = 0;

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        readInsets();
    }

    private void readInsets() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.P) return;
        View decor = getWindow().getDecorView();
        WindowInsets wi = decor.getRootWindowInsets();
        if (wi == null) return;
        DisplayCutout dc = wi.getDisplayCutout();
        insetTop = dc != null ? dc.getSafeInsetTop() : 0;
        insetBottom = dc != null ? dc.getSafeInsetBottom() : 0;
    }
}
