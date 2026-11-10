package org.tamapoke.app;

import org.libsdl.app.SDLActivity;

// Everything is native: SDLActivity hosts the surface and calls main() in
// libmain.so (android_main.cpp), which runs the real firmware. This class
// exists only so the app has its own package rather than SDL's.
public class MainActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }
}
