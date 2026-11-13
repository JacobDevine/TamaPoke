package org.tamapoke.app;

import android.Manifest;
import android.app.AlertDialog;
import android.content.ClipData;
import android.content.ComponentName;
import android.content.ContentValues;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.content.res.Configuration;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.provider.MediaStore;
import android.util.Log;
import android.view.DisplayCutout;
import android.view.View;
import android.view.WindowInsets;
import android.widget.Toast;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.Locale;

import org.libsdl.app.SDLActivity;

// Everything is native: SDLActivity hosts the surface and calls main() in
// libmain.so (android_main.cpp), which runs the real firmware. This class
// gives the app its own package, reports the screen's insets, and does the
// few things only Android can: the share sheet and file picker for the save
// backup, the care reminder, and relaunching after a restore.
//
// The methods below marked "from native" are called on the SDL thread through
// JNI. Anything touching a view or a dialog hops to the UI thread; what the UI
// thread hands back goes through the two native methods, which only queue it
// for the game loop -- game state is never touched from this thread.
public class MainActivity extends SDLActivity {
    private static final String TAG = "TamaPoke";
    private static final int REQ_RESTORE = 41;
    private static final int REQ_NOTIFY = 42;
    private static final int REQ_STEPS = 43;
    // A save is at most 12 KB, so its .tpsave is about 25 KB of text. Anything
    // much larger is not one, and is not read into memory to find that out.
    private static final int MAX_BACKUP_BYTES = 256 * 1024;

    // The rows the camera cutout covers, in physical pixels. The app draws
    // under the cutout (SDL asks for SHORT_EDGES when fullscreen), so the
    // full-height layout keeps its header clear of these. android_main.cpp
    // polls them: they are only known once the window has been laid out.
    public static volatile int insetTop = 0;
    public static volatile int insetBottom = 0;

    // The game's language, the firmware's Lang enum by index (i18n.h; a
    // static_assert in android_main.cpp pins the order). Dialogs and toasts
    // speak the game's language rather than the phone's.
    private static final String[] LANGS = { "es", "en", "fr", "de", "it", "pt" };

    static native void nativeRestoreOffered(String text);
    static native void nativeRestoreConfirmed();

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        askForNotificationsOnce();
        Steps.armSplit(this);   // a no-op until the player has allowed counting
    }

    // The step counter is read live while the app is in front (Steps.Live);
    // in the background its sensor hub keeps counting on its own.
    private Steps.Live stepsLive;

    @Override
    protected void onResume() {
        super.onResume();
        if (stepsLive == null) stepsLive = new Steps.Live(this);
        stepsLive.start();
    }

    @Override
    protected void onPause() {
        if (stepsLive != null) stepsLive.stop();
        super.onPause();
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

    private Context inLang(int lang) {
        Configuration c = new Configuration(getResources().getConfiguration());
        c.setLocale(new Locale(LANGS[lang >= 0 && lang < LANGS.length ? lang : 1]));
        return createConfigurationContext(c);
    }

    // ---- the save backup ----

    // From native: share a .tpsave that android_main.cpp has already verified.
    public void shareBackup(final String fileName, final String text, final int lang) {
        runOnUiThread(() -> {
            try {
                File dir = new File(getCacheDir(), SaveProvider.DIR);
                dir.mkdirs();
                File[] old = dir.listFiles();
                if (old != null) for (File f : old) f.delete();   // one at a time
                File f = new File(dir, fileName);
                try (OutputStream o = new FileOutputStream(f)) {
                    o.write(text.getBytes(StandardCharsets.US_ASCII));
                }
                Uri uri = SaveProvider.uriFor(fileName);
                Intent send = new Intent(Intent.ACTION_SEND);
                send.setType(SaveProvider.MIME);
                send.putExtra(Intent.EXTRA_STREAM, uri);
                send.putExtra(Intent.EXTRA_TITLE, fileName);
                send.setClipData(ClipData.newRawUri(fileName, uri));
                send.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
                startActivity(Intent.createChooser(send, inLang(lang).getString(R.string.backup_share_title)));
            } catch (Exception e) {
                Log.e(TAG, "backup: share failed", e);
                toastNow(lang, R.string.backup_failed);
            }
        });
    }

    // From native: let the player pick a backup to restore.
    public void pickRestore() {
        runOnUiThread(() -> {
            Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            i.addCategory(Intent.CATEGORY_OPENABLE);
            i.setType("*/*");   // .tpsave has no registered type
            try {
                startActivityForResult(i, REQ_RESTORE);
            } catch (Exception e) {
                Log.e(TAG, "restore: no file picker", e);
            }
        });
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQ_RESTORE || resultCode != RESULT_OK || data == null || data.getData() == null) return;
        String text = "";
        try (InputStream in = getContentResolver().openInputStream(data.getData())) {
            ByteArrayOutputStream b = new ByteArrayOutputStream();
            byte[] buf = new byte[8192];
            int n;
            while (in != null && (n = in.read(buf)) > 0) {
                b.write(buf, 0, n);
                if (b.size() > MAX_BACKUP_BYTES) { b.reset(); break; }
            }
            text = b.toString("US-ASCII");
        } catch (Exception e) {
            Log.e(TAG, "restore: could not read the file", e);
        }
        // Empty is passed on too: the native side says it is not a save.
        nativeRestoreOffered(text);
    }

    // From native: the file verified. Ask before replacing anything.
    public void confirmRestore(final int lang) {
        runOnUiThread(() -> {
            Context c = inLang(lang);
            // the phone's own dialog style, not the activity's fullscreen Holo theme
            new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                .setTitle(c.getString(R.string.restore_title))
                .setMessage(c.getString(R.string.restore_message))
                .setPositiveButton(c.getString(R.string.restore_yes), (d, w) -> nativeRestoreConfirmed())
                .setNegativeButton(c.getString(R.string.restore_no), null)
                .show();
        });
    }

    // From native, before a restore: keep the save being replaced where the
    // player can find it, in Downloads/TamaPoke. A restore is the one action
    // here that destroys a save, so it does not go ahead unless this worked.
    public boolean keepReplaced(String fileName, String text) {
        byte[] bytes = text.getBytes(StandardCharsets.US_ASCII);
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                ContentValues v = new ContentValues();
                v.put(MediaStore.Downloads.DISPLAY_NAME, fileName);
                v.put(MediaStore.Downloads.MIME_TYPE, SaveProvider.MIME);
                v.put(MediaStore.Downloads.RELATIVE_PATH, Environment.DIRECTORY_DOWNLOADS + "/TamaPoke");
                Uri uri = getContentResolver().insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, v);
                if (uri == null) return false;
                try (OutputStream o = getContentResolver().openOutputStream(uri)) {
                    if (o == null) return false;
                    o.write(bytes);
                }
            } else {
                // Before Android 10 there is no Downloads collection to write to
                // without a storage permission; keep it beside the app's files.
                File dir = new File(getExternalFilesDir(null), "replaced");
                dir.mkdirs();
                try (OutputStream o = new FileOutputStream(new File(dir, fileName))) {
                    o.write(bytes);
                }
            }
            return true;
        } catch (Exception e) {
            Log.e(TAG, "restore: could not keep the replaced save", e);
            return false;
        }
    }

    // From native: a toast in the game's language. which: 0 backup failed,
    // 1 not a save, 2 restore failed, 3 the current save could not be kept.
    public void toast(final int lang, final int which) {
        final int[] ids = { R.string.backup_failed, R.string.restore_not_a_save,
                            R.string.restore_failed, R.string.restore_not_kept };
        if (which < 0 || which >= ids.length) return;
        runOnUiThread(() -> toastNow(lang, ids[which]));
    }

    // From native: a short buzz on a minigame hit (0) or a wrong answer (1).
    // The vibrator itself, not performHapticFeedback(): that follows the phone's
    // touch-vibration setting, which many people turn off for the keyboard and
    // the system -- and with it off, a game hit felt like nothing at all.
    // Vibrator is thread-safe, so this runs on the game's thread.
    private Vibrator vibrator;
    public void haptic(final int kind) {
        if (vibrator == null) vibrator = (Vibrator) getSystemService(Context.VIBRATOR_SERVICE);
        if (vibrator == null || !vibrator.hasVibrator()) return;
        if (Build.VERSION.SDK_INT >= 29) {
            vibrator.vibrate(VibrationEffect.createPredefined(
                kind == 1 ? VibrationEffect.EFFECT_DOUBLE_CLICK : VibrationEffect.EFFECT_CLICK));
        } else if (Build.VERSION.SDK_INT >= 26) {
            vibrator.vibrate(VibrationEffect.createOneShot(kind == 1 ? 60 : 20, VibrationEffect.DEFAULT_AMPLITUDE));
        } else {
            vibrator.vibrate(kind == 1 ? 60 : 20);
        }
    }

    private void toastNow(int lang, int id) {
        Toast.makeText(this, inLang(lang).getString(id), Toast.LENGTH_LONG).show();
    }

    // From native: start the app afresh, as the board reboots after an IMPORT
    // or a reset. The caller exits the process straight after; startActivity
    // has handed the intent to the system by the time it returns.
    public void relaunch() {
        startActivity(Intent.makeRestartActivityTask(new ComponentName(this, MainActivity.class)));
    }

    // ---- the care reminder ----

    // From native, on going to the background: one reminder, delayMs from now.
    public void scheduleReminder(long delayMs, String title, String text) {
        Reminder.schedule(this, Reminder.CARE, System.currentTimeMillis() + delayMs, title, text);
    }

    // From native, on coming back, or when no reminder is due.
    public void cancelReminder() {
        Reminder.cancel(this, Reminder.CARE);
    }

    // ---- the sick reminder: its own notification and channel ----

    public void scheduleSickReminder(long delayMs, String title, String text) {
        Reminder.schedule(this, Reminder.SICK, System.currentTimeMillis() + delayMs, title, text);
    }

    public void cancelSickReminder() {
        Reminder.cancel(this, Reminder.SICK);
    }

    // The expedition's "it is back" (fork.h section 8), from native.
    public void scheduleExpedReminder(long delayMs, String title, String text) {
        Reminder.schedule(this, Reminder.EXPED, System.currentTimeMillis() + delayMs, title, text);
    }

    public void cancelExpedReminder() {
        Reminder.cancel(this, Reminder.EXPED);
    }

    // ---- the step counter (Steps.java; fork.h section 7) ----

    // From native: FORK_STEPS_NONE / OFF / ON.
    public int stepsStatus() {
        return Steps.status(this);
    }

    // From native: up to max buckets counted since the last call, as
    // [day, steps, day, steps, ...] oldest first, or null.
    public long[] drainSteps(int max) {
        return Steps.drain(this, max);
    }

    // From native: the steps page asks for the permission. firstTimeOnly is
    // the page being opened, which asks only if it never has; a tap on its
    // button always asks -- or, once Android has stopped showing the dialog
    // ("don't ask again"), opens the app's settings, where it can still be
    // allowed.
    public void askSteps(final boolean firstTimeOnly) {
        if (Build.VERSION.SDK_INT < 29 || Steps.status(this) != Steps.OFF) return;
        runOnUiThread(() -> {
            SharedPreferences p = getSharedPreferences("app", MODE_PRIVATE);
            boolean asked = p.getBoolean("askedSteps", false);
            if (firstTimeOnly && asked) return;
            if (asked && !shouldShowRequestPermissionRationale(Manifest.permission.ACTIVITY_RECOGNITION)) {
                Intent i = new Intent(android.provider.Settings.ACTION_APPLICATION_DETAILS_SETTINGS,
                                      Uri.fromParts("package", getPackageName(), null));
                try { startActivity(i); } catch (Exception e) { Log.e(TAG, "steps: no settings screen", e); }
                return;
            }
            p.edit().putBoolean("askedSteps", true).apply();
            requestPermissions(new String[] { Manifest.permission.ACTIVITY_RECOGNITION }, REQ_STEPS);
        });
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode != REQ_STEPS) return;
        if (Steps.status(this) != Steps.ON) return;
        // Allowed: start counting now. The first reading is the baseline, so
        // nothing from before this moment is credited.
        if (stepsLive == null) stepsLive = new Steps.Live(this);
        stepsLive.start();
        Steps.armSplit(this);
    }

    // Android 13+ asks before an app may post notifications. Asked once, on the
    // first launch; after that the player's answer lives in system settings.
    private void askForNotificationsOnce() {
        if (Build.VERSION.SDK_INT < 33) return;
        if (checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) == PackageManager.PERMISSION_GRANTED) return;
        SharedPreferences p = getSharedPreferences("app", MODE_PRIVATE);
        if (p.getBoolean("askedNotify", false)) return;
        p.edit().putBoolean("askedNotify", true).apply();
        requestPermissions(new String[] { Manifest.permission.POST_NOTIFICATIONS }, REQ_NOTIFY);
    }
}
