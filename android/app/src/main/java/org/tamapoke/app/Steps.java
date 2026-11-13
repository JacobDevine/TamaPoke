package org.tamapoke.app;

import android.Manifest;
import android.app.AlarmManager;
import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.provider.Settings;
import android.util.Log;

import java.util.Calendar;
import java.util.TimeZone;
import java.util.TreeMap;

// The phone's step counter, for the fork's Phase 4 (fork.h section 7).
//
// TYPE_STEP_COUNTER is kept by the phone's own low-power sensor hub: it counts
// every step since the phone BOOTED, whether or not any app is running. So the
// game never has to be open to count. Each reading is turned into "steps since
// the last reading" and added to the bucket for the day it was taken; the game
// takes the buckets (drain()) and forgets them.
//
// Three catches, each handled here:
//
// 1. A REBOOT RESETS THE COUNTER TO ZERO. Settings.Global.BOOT_COUNT tells a
//    reboot apart from a count that merely went up, and after one the reading
//    itself is the steps since that reboot. The steps between our last reading
//    and the reboot are lost; a reading lower than the last one is also taken
//    as a reboot, in case the count is not available.
// 2. THE COUNTER HAS NO TIMESTAMPS. Steps are put in the day they were READ, so
//    a day the app was never opened would land on the next one. The midnight
//    alarm (Alarm, below) takes a reading at 23:59 with no app open, so each
//    day keeps its own count. It is best effort: a reading that does not come
//    in time just rolls those steps into the next day.
// 3. ACTIVITY_RECOGNITION is a runtime permission from Android 10. The game
//    asks the first time its steps page is opened (askSteps), never at launch.
//
// THE FIRST READING IS A BASELINE: nothing walked before the player allowed the
// counting is credited, or a phone that had been on for a month would hand a
// new player a month of milestones at once.
final class Steps {
    private static final String TAG = "TamaPoke";
    private static final String PREFS = "steps";
    // Bucket strings are "day:steps;day:steps"; a week's worth is a few
    // hundred bytes. A bucket older than this many days is dropped rather than
    // kept forever if the game is never opened again.
    private static final int KEEP_DAYS = 30;

    static final int NONE = 0, OFF = 1, ON = 2;   // fork.h FORK_STEPS_*

    private Steps() {}

    static Sensor sensor(Context ctx) {
        SensorManager sm = (SensorManager) ctx.getSystemService(Context.SENSOR_SERVICE);
        return sm == null ? null : sm.getDefaultSensor(Sensor.TYPE_STEP_COUNTER);
    }

    static boolean allowed(Context ctx) {
        return Build.VERSION.SDK_INT < 29 ||
            ctx.checkSelfPermission(Manifest.permission.ACTIVITY_RECOGNITION) == PackageManager.PERMISSION_GRANTED;
    }

    static int status(Context ctx) {
        if (sensor(ctx) == null) return NONE;
        return allowed(ctx) ? ON : OFF;
    }

    // The firmware's day: its epoch is local wall-clock seconds (phone_clock.h),
    // so a day is (UTC + offset) / 86400, which is what this is.
    static long localDay(long utcMs) {
        return (utcMs + TimeZone.getDefault().getOffset(utcMs)) / 86400000L;
    }

    private static int bootCount(Context ctx) {
        try {
            return Settings.Global.getInt(ctx.getContentResolver(), Settings.Global.BOOT_COUNT);
        } catch (Exception e) {
            return -1;
        }
    }

    // One reading of the counter. Called from the live listener, the midnight
    // alarm and the boot receiver, on the main thread, and synchronized with
    // drain(), which the game's thread calls.
    static synchronized void record(Context ctx, long counter) {
        SharedPreferences p = ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        long last = p.getLong("last", -1);
        int boot = bootCount(ctx);
        int lastBoot = p.getInt("boot", -1);
        SharedPreferences.Editor ed = p.edit().putLong("last", counter).putInt("boot", boot);
        if (last < 0) {   // the baseline: see the class comment
            ed.apply();
            return;
        }
        boolean rebooted = (boot >= 0 && lastBoot >= 0 && boot != lastBoot) || counter < last;
        long delta = rebooted ? counter : counter - last;
        if (delta > 0) {
            TreeMap<Long, Long> days = parse(p.getString("days", ""));
            long day = localDay(System.currentTimeMillis());
            Long have = days.get(day);
            days.put(day, (have == null ? 0 : have) + delta);
            while (!days.isEmpty() && days.firstKey() < day - KEEP_DAYS) days.remove(days.firstKey());
            ed.putString("days", format(days));
        }
        ed.apply();
    }

    // Up to `max` days counted since the last call, OLDEST first, as
    // [day0, steps0, day1, steps1, ...], and forgotten; the rest wait for the
    // next call (forkStepsPoll() keeps calling while it gets a full batch).
    // Null when nothing.
    static synchronized long[] drain(Context ctx, int max) {
        SharedPreferences p = ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        TreeMap<Long, Long> days = parse(p.getString("days", ""));
        if (days.isEmpty() || max <= 0) return null;
        int n = Math.min(max, days.size());
        long[] out = new long[n * 2];
        for (int i = 0; i < n; i++) {
            java.util.Map.Entry<Long, Long> e = days.pollFirstEntry();
            out[2 * i] = e.getKey();
            out[2 * i + 1] = e.getValue();
        }
        p.edit().putString("days", format(days)).apply();
        return out;
    }

    private static TreeMap<Long, Long> parse(String s) {
        TreeMap<Long, Long> m = new TreeMap<>();
        if (s == null || s.isEmpty()) return m;
        for (String part : s.split(";")) {
            int c = part.indexOf(':');
            if (c <= 0) continue;
            try {
                m.put(Long.parseLong(part.substring(0, c)), Long.parseLong(part.substring(c + 1)));
            } catch (NumberFormatException ignored) {
            }
        }
        return m;
    }

    private static String format(TreeMap<Long, Long> m) {
        StringBuilder b = new StringBuilder();
        for (java.util.Map.Entry<Long, Long> e : m.entrySet()) {
            if (b.length() > 0) b.append(';');
            b.append(e.getKey()).append(':').append(e.getValue());
        }
        return b.toString();
    }

    // ---- live, while the app is open ----

    static final class Live implements SensorEventListener {
        private final Context ctx;
        Live(Context ctx) { this.ctx = ctx.getApplicationContext(); }

        void start() {
            if (status(ctx) != ON) return;
            SensorManager sm = (SensorManager) ctx.getSystemService(Context.SENSOR_SERVICE);
            if (sm != null) sm.registerListener(this, sensor(ctx), SensorManager.SENSOR_DELAY_NORMAL);
        }

        void stop() {
            SensorManager sm = (SensorManager) ctx.getSystemService(Context.SENSOR_SERVICE);
            if (sm != null) sm.unregisterListener(this);
        }

        @Override public void onSensorChanged(SensorEvent e) { record(ctx, (long) e.values[0]); }
        @Override public void onAccuracyChanged(Sensor s, int a) {}
    }

    // ---- the midnight split ----

    // The next 23:59 local. A minute short of midnight, so the reading lands in
    // the day it closes; the minute's steps after it go to the next day.
    static long nextSplitMs(long nowMs) {
        Calendar c = Calendar.getInstance();
        c.setTimeInMillis(nowMs);
        c.set(Calendar.HOUR_OF_DAY, 23);
        c.set(Calendar.MINUTE, 59);
        c.set(Calendar.SECOND, 0);
        c.set(Calendar.MILLISECOND, 0);
        if (c.getTimeInMillis() <= nowMs) c.add(Calendar.DAY_OF_MONTH, 1);
        return c.getTimeInMillis();
    }

    static void armSplit(Context ctx) {
        if (status(ctx) != ON) return;
        AlarmManager am = (AlarmManager) ctx.getSystemService(Context.ALARM_SERVICE);
        if (am == null) return;
        Intent i = new Intent(ctx, Alarm.class);
        PendingIntent pi = PendingIntent.getBroadcast(ctx, 0, i,
            PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
        long at = nextSplitMs(System.currentTimeMillis());
        if (Build.VERSION.SDK_INT >= 31 && !am.canScheduleExactAlarms())
            am.setAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, at, pi);
        else
            am.setExactAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, at, pi);
    }

    // Takes one reading with no app open: at 23:59, and after a boot (which
    // also re-arms the alarm, since a reboot clears every alarm). The sensor
    // hands over its current count soon after a listener registers; if it has
    // not within a few seconds, those steps simply go to the next reading.
    public static final class Alarm extends BroadcastReceiver {
        private static final long WAIT_MS = 8000;

        @Override
        public void onReceive(Context context, Intent intent) {
            final Context ctx = context.getApplicationContext();
            armSplit(ctx);   // tomorrow's, first: whatever happens to this one
            if (status(ctx) != ON) return;
            final SensorManager sm = (SensorManager) ctx.getSystemService(Context.SENSOR_SERVICE);
            if (sm == null) return;
            final Handler h = new Handler(Looper.getMainLooper());
            OneReading r = new OneReading(ctx, sm, goAsync(), h);
            sm.registerListener(r, sensor(ctx), SensorManager.SENSOR_DELAY_NORMAL, h);
            h.postDelayed(r, WAIT_MS);   // the give-up, if no reading comes
        }
    }

    // One reading, then done; or, run by the timeout, done without one. Both
    // on the main looper, so `finished` needs no lock.
    private static final class OneReading implements SensorEventListener, Runnable {
        private final Context ctx;
        private final SensorManager sm;
        private final BroadcastReceiver.PendingResult done;
        private final Handler h;
        private boolean finished;

        OneReading(Context ctx, SensorManager sm, BroadcastReceiver.PendingResult done, Handler h) {
            this.ctx = ctx; this.sm = sm; this.done = done; this.h = h;
        }

        @Override public void onSensorChanged(SensorEvent e) {
            if (finished) return;
            record(ctx, (long) e.values[0]);
            finish();
        }

        @Override public void run() {
            if (finished) return;
            Log.i(TAG, "steps: no reading at the split; those steps go to the next one");
            finish();
        }

        private void finish() {
            finished = true;
            sm.unregisterListener(this);
            h.removeCallbacks(this);
            done.finish();
        }

        @Override public void onAccuracyChanged(Sensor s, int a) {}
    }
}
