package org.tamapoke.app;

import android.content.Context;
import android.net.nsd.NsdManager;
import android.net.nsd.NsdServiceInfo;
import android.net.wifi.WifiManager;
import android.util.Log;

import java.net.Inet4Address;
import java.net.InetAddress;

// Finding other phones for a LAN battle (plan step 12e). Discovery ONLY: the
// packets themselves go over a UDP socket in native code (phone_net.cpp), which
// is also what the desktop emulator and the tests run.
//
// A host registers "_tamapoke._udp" (an adventure host "_tamaraid._udp") under
// its trainer name on phone_net's port.
// A guest browses for that type, resolves each service it finds to an address,
// and hands it to native (nativeHostSeen), which lists it on the LAN screen.
// Nothing leaves the local network, and nothing runs unless the LAN screen
// asked: stop() undoes whichever was started.
//
// The multicast lock is held only while browsing or advertising. Many phones
// drop multicast (which mDNS is) with the screen on and the lock released.
final class Net {
    private static final String TAG = "TamaPoke";
    // A LAN battle host, and an adventure host: two types, so each list offers
    // only the game it is for (phone_net.h's NET_SERVICE / NET_RAID_SERVICE).
    private static final String TYPE_LINK = "_tamapoke._udp";
    private static final String TYPE_RAID = "_tamaraid._udp";
    private String type = TYPE_LINK;

    private final Context ctx;
    private final NsdManager nsd;
    private WifiManager.MulticastLock lock;
    private NsdManager.RegistrationListener reg;
    private NsdManager.DiscoveryListener disc;

    Net(Context c) {
        ctx = c.getApplicationContext();
        nsd = (NsdManager) ctx.getSystemService(Context.NSD_SERVICE);
    }

    static native void nativeHostSeen(String name, String ip, int port);
    static native void nativeHostGone(String name);

    synchronized void start(boolean host, String name, int port, boolean raid) {
        stop();
        if (nsd == null) return;
        type = raid ? TYPE_RAID : TYPE_LINK;
        holdLock(true);
        if (host) advertise(name, port);
        else browse();
    }

    synchronized void stop() {
        if (reg != null) {
            try { nsd.unregisterService(reg); } catch (Exception e) { Log.w(TAG, "net: unregister", e); }
            reg = null;
        }
        if (disc != null) {
            try { nsd.stopServiceDiscovery(disc); } catch (Exception e) { Log.w(TAG, "net: stop discovery", e); }
            disc = null;
        }
        holdLock(false);
    }

    private void holdLock(boolean on) {
        if (on && lock == null) {
            WifiManager w = (WifiManager) ctx.getSystemService(Context.WIFI_SERVICE);
            if (w == null) return;
            lock = w.createMulticastLock("tamapoke-lan");
            lock.setReferenceCounted(false);
            try { lock.acquire(); } catch (Exception e) { Log.w(TAG, "net: multicast lock", e); lock = null; }
        } else if (!on && lock != null) {
            try { lock.release(); } catch (Exception ignored) { }
            lock = null;
        }
    }

    private void advertise(String name, int port) {
        NsdServiceInfo info = new NsdServiceInfo();
        info.setServiceName(name == null || name.isEmpty() ? "TamaPoke" : name);
        info.setServiceType(type);
        info.setPort(port);
        reg = new NsdManager.RegistrationListener() {
            @Override public void onServiceRegistered(NsdServiceInfo i) {
                Log.i(TAG, "net: hosting as " + i.getServiceName());
            }
            @Override public void onRegistrationFailed(NsdServiceInfo i, int err) {
                Log.w(TAG, "net: could not advertise (" + err + ")");
            }
            @Override public void onServiceUnregistered(NsdServiceInfo i) { }
            @Override public void onUnregistrationFailed(NsdServiceInfo i, int err) { }
        };
        try { nsd.registerService(info, NsdManager.PROTOCOL_DNS_SD, reg); }
        catch (Exception e) { Log.w(TAG, "net: register", e); reg = null; }
    }

    private void browse() {
        disc = new NsdManager.DiscoveryListener() {
            @Override public void onDiscoveryStarted(String type) { Log.i(TAG, "net: looking for hosts"); }
            @Override public void onDiscoveryStopped(String type) { }
            @Override public void onStartDiscoveryFailed(String type, int err) {
                Log.w(TAG, "net: discovery failed (" + err + ")");
            }
            @Override public void onStopDiscoveryFailed(String type, int err) { }
            @Override public void onServiceFound(NsdServiceInfo i) {
                if (!i.getServiceType().contains(type.substring(0, type.indexOf('.')))) return;
                resolve(i);
            }
            @Override public void onServiceLost(NsdServiceInfo i) {
                nativeHostGone(i.getServiceName());
            }
        };
        try { nsd.discoverServices(type, NsdManager.PROTOCOL_DNS_SD, disc); }
        catch (Exception e) { Log.w(TAG, "net: discover", e); disc = null; }
    }

    // resolveService is the API that exists on every version the app runs on.
    // It allows one resolve at a time on older Androids and reports the second
    // as FAILURE_ALREADY_ACTIVE, so that case simply tries again shortly.
    @SuppressWarnings("deprecation")
    private void resolve(final NsdServiceInfo found) {
        try {
            nsd.resolveService(found, new NsdManager.ResolveListener() {
                @Override public void onResolveFailed(NsdServiceInfo i, int err) {
                    if (err == NsdManager.FAILURE_ALREADY_ACTIVE) {
                        new android.os.Handler(android.os.Looper.getMainLooper())
                            .postDelayed(() -> { synchronized (Net.this) { if (disc != null) resolve(found); } }, 300);
                    }
                }
                @Override public void onServiceResolved(NsdServiceInfo i) {
                    InetAddress a = i.getHost();
                    if (!(a instanceof Inet4Address)) return;   // phone_net is IPv4
                    nativeHostSeen(i.getServiceName(), a.getHostAddress(), i.getPort());
                }
            });
        } catch (Exception e) { Log.w(TAG, "net: resolve", e); }
    }
}
