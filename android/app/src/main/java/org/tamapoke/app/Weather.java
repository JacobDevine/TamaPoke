package org.tamapoke.app;

import android.content.Context;
import android.content.SharedPreferences;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

// The weather, for the fork's Phase 8b (fork.h section 12).
//
// THE APP'S ONLY USE OF THE NETWORK, AND OFF UNTIL THE PLAYER TURNS IT ON.
// The firmware decides when to fetch (the WEATHER setting, and a place to ask
// about) and calls set(); nothing here runs before that. What is sent:
//
//   https://api.open-meteo.com/v1/forecast?latitude=-27.5&longitude=153.0&...
//
// The coordinates are the time zone's principal city (zone_coords.h), already
// rounded to a tenth of a degree by the firmware, so they say which zone the
// phone is in and nothing finer. No key, no account, no identifier: Open-Meteo
// needs none. No location permission is involved at all.
//
// Fetched only while the app is in front: on coming back if the last report is
// over FETCH_EVERY_MS old, and every FETCH_EVERY_MS while it stays open. A
// report older than STALE_MS is not handed to the game, so a phone that has
// been offline all day draws no weather rather than this morning's. Turning the
// setting off forgets the report.
final class Weather {
    private static final String TAG = "TamaPoke";
    private static final String PREFS = "weather";
    private static final long FETCH_EVERY_MS = 60L * 60 * 1000;
    private static final long RETRY_MS = 10L * 60 * 1000;   // the tick, and so the slow retry
    private static final long QUICK_RETRY_MS = 30L * 1000;  // the first few retries, x1, x2, x3
    private static final int QUICK_RETRIES = 3;
    private static final long STALE_MS = 3L * 60 * 60 * 1000;

    private final Context ctx;
    private final Handler main = new Handler(Looper.getMainLooper());
    private final ExecutorService io = Executors.newSingleThreadExecutor();

    // Written on the main thread only; lat10/lon10 are also read by report().
    private boolean on = false;
    private volatile int lat10, lon10;
    private boolean resumed = false;
    private boolean inFlight = false;
    private int failures = 0;   // in a row; a success or a new place resets it

    // The report: written on the network thread, read on the SDL thread.
    private volatile int code = -1;      // WMO code; -1 = none
    private volatile int temp = 0;
    private volatile char unit = 'C';
    private volatile long fetchedAt = 0; // wall clock
    private volatile int forLat10, forLon10;

    private final Runnable tick = new Runnable() {
        @Override public void run() {
            if (!on || !resumed) return;
            if (!inFlight && System.currentTimeMillis() - fetchedAt >= FETCH_EVERY_MS) fetch();
            main.postDelayed(this, RETRY_MS);
        }
    };

    // A failed fetch is tried again soon, a few times, before the tick's slower
    // rhythm takes over: a first fetch that hits a passing timeout should not
    // leave the sky empty for ten minutes.
    private final Runnable quickRetry = () -> {
        if (on && resumed && !inFlight && System.currentTimeMillis() - fetchedAt >= FETCH_EVERY_MS) fetch();
    };

    Weather(Context ctx) {
        this.ctx = ctx.getApplicationContext();
        SharedPreferences p = this.ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        code = p.getInt("code", -1);
        temp = p.getInt("temp", 0);
        unit = p.getString("unit", "C").charAt(0);
        fetchedAt = p.getLong("at", 0);
        forLat10 = p.getInt("lat10", 0);
        forLon10 = p.getInt("lon10", 0);
    }

    // From native, through MainActivity, on any thread: fetch (on) for these
    // coordinates in tenths of a degree, or stop and forget.
    void set(final boolean wantOn, final int wantLat10, final int wantLon10) {
        main.post(() -> {
            on = wantOn;
            lat10 = wantLat10;
            lon10 = wantLon10;
            main.removeCallbacks(tick);
            main.removeCallbacks(quickRetry);
            failures = 0;
            if (!on) { forget(); return; }
            // a new place: the old report is about somewhere else
            if (wantLat10 != forLat10 || wantLon10 != forLon10) fetchedAt = 0;
            if (resumed) main.post(tick);
        });
    }

    void resume() {
        resumed = true;
        main.removeCallbacks(tick);
        if (on) main.post(tick);
    }

    void pause() {
        resumed = false;
        main.removeCallbacks(tick);
        main.removeCallbacks(quickRetry);
    }

    // From native: the report packed as an int, or -1 when there is none to
    // believe -- off, never fetched, for another place, or stale.
    //   bits 0-7 the WMO code, bits 8-19 the temperature + 2048, bit 20 Fahrenheit
    int report() {
        final int c = code;
        if (c < 0 || c > 255) return -1;
        if (forLat10 != lat10 || forLon10 != lon10) return -1;
        final long age = System.currentTimeMillis() - fetchedAt;
        if (fetchedAt == 0 || age < 0 || age > STALE_MS) return -1;
        return c | ((temp + 2048) & 0xFFF) << 8 | (unit == 'F' ? 1 << 20 : 0);
    }

    private void forget() {
        code = -1;
        fetchedAt = 0;
        ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().clear().apply();
    }

    // The countries that read the temperature in Fahrenheit.
    private static boolean fahrenheit() {
        String c = Locale.getDefault().getCountry();
        return c.equals("US") || c.equals("LR") || c.equals("MM") || c.equals("BS")
            || c.equals("BZ") || c.equals("KY") || c.equals("PW");
    }

    private void fetch() {
        inFlight = true;
        final int la = lat10, lo = lon10;
        final boolean f = fahrenheit();
        io.execute(() -> {
            boolean ok = false;
            int c = -1, t = 0;
            HttpURLConnection http = null;
            try {
                // Locale.ROOT: a decimal comma would make another place of it
                String q = String.format(Locale.ROOT,
                    "https://api.open-meteo.com/v1/forecast?latitude=%.1f&longitude=%.1f"
                    + "&current=weather_code,temperature_2m%s",
                    la / 10.0, lo / 10.0, f ? "&temperature_unit=fahrenheit" : "");
                http = (HttpURLConnection) new URL(q).openConnection();
                http.setConnectTimeout(10000);
                http.setReadTimeout(10000);
                http.setUseCaches(false);
                if (http.getResponseCode() == 200) {
                    JSONObject cur = new JSONObject(read(http.getInputStream())).getJSONObject("current");
                    c = cur.getInt("weather_code");
                    t = (int) Math.round(cur.getDouble("temperature_2m"));
                    ok = c >= 0 && c <= 255;
                } else {
                    Log.w(TAG, "weather: HTTP " + http.getResponseCode());
                }
            } catch (Exception e) {
                Log.w(TAG, "weather: " + e);
            } finally {
                if (http != null) http.disconnect();
            }
            final boolean good = ok;
            final int gc = c, gt = t;
            main.post(() -> {
                inFlight = false;
                // turned off, or moved, while it was in flight: not ours any more
                if (!on || la != lat10 || lo != lon10) return;
                if (!good) {
                    if (failures < QUICK_RETRIES) {
                        failures++;
                        main.removeCallbacks(quickRetry);
                        main.postDelayed(quickRetry, QUICK_RETRY_MS * failures);
                    }
                    return;
                }
                failures = 0;
                code = gc;
                temp = gt;
                unit = f ? 'F' : 'C';
                forLat10 = la;
                forLon10 = lo;
                fetchedAt = System.currentTimeMillis();
                ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
                    .putInt("code", gc).putInt("temp", gt).putString("unit", f ? "F" : "C")
                    .putLong("at", fetchedAt).putInt("lat10", la).putInt("lon10", lo).apply();
                Log.i(TAG, "weather: code " + gc + ", " + gt + (f ? "F" : "C"));
            });
        });
    }

    private static String read(InputStream in) throws java.io.IOException {
        try (InputStream s = in) {
            ByteArrayOutputStream b = new ByteArrayOutputStream();
            byte[] buf = new byte[4096];
            int n;
            // a forecast is a few hundred bytes; anything far past that is not one
            while ((n = s.read(buf)) > 0) {
                b.write(buf, 0, n);
                if (b.size() > 64 * 1024) throw new java.io.IOException("reply too large");
            }
            return b.toString("UTF-8");
        }
    }
}
