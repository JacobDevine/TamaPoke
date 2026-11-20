#pragma once
// The phone's link transport: UDP on the local network (plan step 12e).
//
// A board pairs over ESP-NOW (linknow.cpp). A phone has no ESP-NOW, but
// link.cpp takes its transport as a function pointer and assumes nothing about
// delivery (turn stamps, resends, absolute state), so plain UDP fits it as is.
//
// HOSTING binds NET_PORT and waits. The first device whose packet arrives is
// locked in by its address, and from then on everything goes to it alone and
// anything from anyone else is dropped -- linknow.cpp's MAC lock, for the same
// reason: two pairs on one network must not join each other's fights.
// JOINING binds any port and is locked to the host from the start, once one is
// chosen (from the NSD list or typed in).
//
// THE PAIRING CLOCK STARTS WHEN THERE IS SOMEBODY TO PAIR WITH. link.cpp gives
// up on a silent peer after LINK_PAIR_TIMEOUT_MS (20 s), which on a board is
// measured from both players tapping. Here a host waits for a friend who is
// still picking a team or scrolling a list, so until a peer is locked the
// poll keeps re-arming the link's clock. BACK is how a host stops waiting.
//
// Discovery is NOT here: Android's NsdManager does it in Java and reports
// hosts through netHostSeen()/netHostGone(). The desktop emulator has no NSD
// and joins by address only. No Android dependency, so the desktop emulator and
// tools/emu/tests/net_test.cpp compile this file as is.
#include <stdint.h>

struct Link;

#define NET_PORT 47474                // where a host listens
#define NET_SERVICE "_tamapoke._udp"  // the NSD service type
#define NET_NAME_LEN 16
#define NET_ADDR_LEN 24               // "255.255.255.255:65535"
#define NET_HOSTS_MAX 8

// Set before linkNowBegin(): which side this phone is, and the trainer name a
// host advertises under. A guest's earlier target is forgotten.
void netRole(bool host, const char *name);
bool netHosting();

// The linkNow*() interface (linknow.h), as link_radio.h plugs it in.
bool netBegin(Link *l);   // opens the socket; false if it cannot
void netEnd();
bool netUp();
void netPoll();           // drains the socket into the link; main loop only

// A guest's host: "a.b.c.d" or "a.b.c.d:port" (NET_PORT when omitted), set
// AFTER netBegin() (which forgets any peer). False for anything that does not
// parse, or when hosting.
bool netJoin(const char *addr);
bool netTargeted();       // a guest has a host to talk to
bool netLocked();         // either side has a peer

// This device's own IPv4 address on the network, for a host to show so a guest
// can type it. False when there is none (no Wi-Fi).
bool netMyAddress(char *out, int n);

// The hosts NSD has found. Thread-safe: Java's NSD callbacks run on their own
// thread. A host seen again under the same name updates its address.
struct NetHost {
  char name[NET_NAME_LEN];
  char addr[NET_ADDR_LEN];
};
void netHostSeen(const char *name, const char *ip, int port);
void netHostGone(const char *name);
void netHostsClear();
int netHosts(NetHost *out, int max);

// Diagnostics, as linknow.cpp keeps them for the board.
struct NetStats { uint32_t rx, tx, txFail, foreign; };
NetStats netStats();
