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

// The hunger reminder: one notification, at a time android_main.cpp worked out
// from the game's own offline drain when the app left (phone_remind.cpp).
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
    private static final String PREFS = "reminder";
    private static final String CHANNEL = "hunger";
    private static final int ID = 1;

    static void schedule(Context ctx, long atMs, String title, String text) {
        ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
            .putLong("at", atMs).putString("title", title).putString("text", text).apply();
        arm(ctx, atMs);
    }

    static void cancel(Context ctx) {
        ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().clear().apply();
        AlarmManager am = (AlarmManager) ctx.getSystemService(Context.ALARM_SERVICE);
        if (am != null) am.cancel(alarmIntent(ctx));
        NotificationManager nm = (NotificationManager) ctx.getSystemService(Context.NOTIFICATION_SERVICE);
        if (nm != null) nm.cancel(ID);   // back in the app: the creature is right there
    }

    private static PendingIntent alarmIntent(Context ctx) {
        Intent i = new Intent(ctx, Reminder.class);
        return PendingIntent.getBroadcast(ctx, 0, i,
            PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
    }

    private static void arm(Context ctx, long atMs) {
        AlarmManager am = (AlarmManager) ctx.getSystemService(Context.ALARM_SERVICE);
        if (am == null) return;
        if (Build.VERSION.SDK_INT >= 31 && !am.canScheduleExactAlarms())
            am.setAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, atMs, alarmIntent(ctx));
        else
            am.setExactAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, atMs, alarmIntent(ctx));
    }

    @Override
    public void onReceive(Context ctx, Intent intent) {
        SharedPreferences p = ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        long at = p.getLong("at", 0);
        if (at == 0) return;   // cancelled since
        if (Intent.ACTION_BOOT_COMPLETED.equals(intent.getAction()) && at > System.currentTimeMillis()) {
            arm(ctx, at);
            return;
        }
        post(ctx, p.getString("title", ""), p.getString("text", ""));
        p.edit().clear().apply();
    }

    private static void post(Context ctx, String title, String text) {
        if (Build.VERSION.SDK_INT >= 33 &&
            ctx.checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED)
            return;
        NotificationManager nm = (NotificationManager) ctx.getSystemService(Context.NOTIFICATION_SERVICE);
        if (nm == null) return;
        Notification.Builder b;
        if (Build.VERSION.SDK_INT >= 26) {
            nm.createNotificationChannel(new NotificationChannel(CHANNEL,
                ctx.getString(R.string.reminder_channel), NotificationManager.IMPORTANCE_DEFAULT));
            b = new Notification.Builder(ctx, CHANNEL);
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
        nm.notify(ID, b.build());
    }
}
