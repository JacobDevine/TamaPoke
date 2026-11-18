package org.tamapoke.app;

import android.Manifest;
import android.app.AlarmManager;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.os.Build;

// The care reminder: one notification, at a time android_main.cpp worked out
// from the game's own offline drain when the app left (phone_remind.cpp), for
// whichever of hunger, a bath, energy or joy runs out first. And a second,
// separate one when the creature was left sick (the fork's Phase 3).
//
// The alarm is EXACT: an inexact one may come up to ~15 minutes late, which on
// top of the forecast read as a reminder that never came. USE_EXACT_ALARM is
// granted at install on Android 13+ (Play restricts it to alarm and calendar
// apps, but this is a sideload); 12 needs SCHEDULE_EXACT_ALARM, which the
// player can revoke, so without it this falls back to inexact rather than
// throwing. What is scheduled is also kept in
// SharedPreferences, because a reboot clears every alarm; BOOT_COMPLETED puts
// it back, or posts it at once if its time passed while the phone was off.
public class Reminder extends BroadcastReceiver {
    // Two kinds, each with its own stored schedule, alarm, notification id and
    // channel, so a player can silence one and keep the other.
    //
    // CARE keeps the prefs file, id and channel id from when it reminded of
    // hunger alone, so a player who already silenced or tuned that channel
    // keeps that choice; only the name shown in Settings changed.
    // SICK is the fork's sickness (fork.h section 6), EXPED its expeditions
    // (section 8): "it is back".
    static final int CARE = 0, SICK = 1, EXPED = 2;
    private static final int KINDS = 3;
    private static final String[] PREFS = { "reminder", "reminder_sick", "reminder_exped" };
    private static final String[] CHANNEL = { "hunger", "sick", "exped" };
    private static final int[] CHANNEL_NAME = { R.string.reminder_channel, R.string.sick_channel, R.string.exped_channel };
    private static final int[] ID = { 1, 2, 3 };
    private static final String EXTRA_KIND = "kind";

    static void schedule(Context ctx, int kind, long atMs, String title, String text) {
        ctx.getSharedPreferences(PREFS[kind], Context.MODE_PRIVATE).edit()
            .putLong("at", atMs).putString("title", title).putString("text", text).apply();
        arm(ctx, kind, atMs);
    }

    static void cancel(Context ctx, int kind) {
        ctx.getSharedPreferences(PREFS[kind], Context.MODE_PRIVATE).edit().clear().apply();
        AlarmManager am = (AlarmManager) ctx.getSystemService(Context.ALARM_SERVICE);
        if (am != null) am.cancel(alarmIntent(ctx, kind));
        NotificationManager nm = (NotificationManager) ctx.getSystemService(Context.NOTIFICATION_SERVICE);
        if (nm != null) nm.cancel(ID[kind]);   // back in the app: the creature is right there
    }

    // The request code is the kind, so the two alarms are distinct PendingIntents
    // and arming one never replaces the other.
    private static PendingIntent alarmIntent(Context ctx, int kind) {
        Intent i = new Intent(ctx, Reminder.class).putExtra(EXTRA_KIND, kind);
        return PendingIntent.getBroadcast(ctx, kind, i,
            PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
    }

    private static void arm(Context ctx, int kind, long atMs) {
        AlarmManager am = (AlarmManager) ctx.getSystemService(Context.ALARM_SERVICE);
        if (am == null) return;
        if (Build.VERSION.SDK_INT >= 31 && !am.canScheduleExactAlarms())
            am.setAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, atMs, alarmIntent(ctx, kind));
        else
            am.setExactAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, atMs, alarmIntent(ctx, kind));
    }

    @Override
    public void onReceive(Context ctx, Intent intent) {
        if (Intent.ACTION_BOOT_COMPLETED.equals(intent.getAction())) {
            // A reboot cleared every alarm: put back each kind that is pending.
            for (int k = 0; k < KINDS; k++) fire(ctx, k, true);
            return;
        }
        int kind = intent.getIntExtra(EXTRA_KIND, CARE);
        if (kind < 0 || kind >= KINDS) kind = CARE;
        fire(ctx, kind, false);
    }

    private static void fire(Context ctx, int kind, boolean boot) {
        SharedPreferences p = ctx.getSharedPreferences(PREFS[kind], Context.MODE_PRIVATE);
        long at = p.getLong("at", 0);
        if (at == 0) return;   // cancelled since
        if (boot && at > System.currentTimeMillis()) {
            arm(ctx, kind, at);
            return;
        }
        post(ctx, kind, p.getString("title", ""), p.getString("text", ""));
        p.edit().clear().apply();
    }

    private static void post(Context ctx, int kind, String title, String text) {
        if (Build.VERSION.SDK_INT >= 33 &&
            ctx.checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED)
            return;
        NotificationManager nm = (NotificationManager) ctx.getSystemService(Context.NOTIFICATION_SERVICE);
        if (nm == null) return;
        Notification.Builder b;
        if (Build.VERSION.SDK_INT >= 26) {
            nm.createNotificationChannel(new NotificationChannel(CHANNEL[kind],
                ctx.getString(CHANNEL_NAME[kind]),
                NotificationManager.IMPORTANCE_DEFAULT));
            b = new Notification.Builder(ctx, CHANNEL[kind]);
        } else {
            b = new Notification.Builder(ctx);
        }
        Intent open = new Intent(ctx, MainActivity.class)
            .setAction(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_LAUNCHER);
        PendingIntent tap = PendingIntent.getActivity(ctx, 0, open, PendingIntent.FLAG_IMMUTABLE);
        b.setSmallIcon(R.drawable.ic_notify)
         .setContentTitle(title)
         .setContentText(text)
         .setContentIntent(tap)
         .setAutoCancel(true);
        nm.notify(ID[kind], b.build());
    }
}
