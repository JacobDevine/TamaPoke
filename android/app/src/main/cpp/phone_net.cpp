#include "phone_net.h"
#include "link.h"
#include "fork_raidnet.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <mutex>

// See phone_net.h. Everything except the host list runs on the game loop.

static int gSock = -1;
static Link *gLink = nullptr;
static bool gHost = false;
static char gName[NET_NAME_LEN] = "";
static bool gLocked = false;
static sockaddr_in gPeer;
static NetStats gStats;
static uint8_t gKind = NET_LINK;
static uint16_t gPort = NET_PORT;
void netTestPort(uint16_t port) { gPort = port ? port : NET_PORT; }
// The raid's side (netRaidBegin()): a host's peer slots, by address.
static RaidNet *gRaidIn = nullptr;
static sockaddr_in gSlot[RNET_PEERS];
static bool gSlotUsed[RNET_PEERS];

void netRole(bool host, const char *name, uint8_t kind) {
  gHost = host;
  snprintf(gName, sizeof(gName), "%s", name ? name : "");
  gLocked = false;
  gKind = kind == NET_RAID ? NET_RAID : NET_LINK;
}
bool netHosting() { return gHost; }
uint8_t netKind() { return gKind; }

static bool sameAddr(const sockaddr_in &a, const sockaddr_in &b) {
  return a.sin_addr.s_addr == b.sin_addr.s_addr && a.sin_port == b.sin_port;
}

static void netSend(void *, const uint8_t *buf, uint8_t len) {
  if (gSock < 0) return;
  // Before a peer is locked there is nobody to say anything to. The link
  // resends whatever it last said, so the first hello after locking still goes.
  if (!gLocked) return;
  ssize_t n = sendto(gSock, buf, len, 0, (const sockaddr *)&gPeer, sizeof(gPeer));
  if (n == (ssize_t)len) gStats.tx++;
  else gStats.txFail++;
}

// The socket either game uses: NET_PORT for a host, any port for a guest.
static int openSock() {
  int s = socket(AF_INET, SOCK_DGRAM, 0);
  if (s < 0) return -1;
  int one = 1;
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  sockaddr_in me;
  memset(&me, 0, sizeof(me));
  me.sin_family = AF_INET;
  me.sin_addr.s_addr = htonl(INADDR_ANY);
  me.sin_port = htons(gHost ? gPort : 0);
  if (bind(s, (const sockaddr *)&me, sizeof(me)) != 0 ||
      fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK) != 0) {
    close(s);
    return -1;
  }
  return s;
}

bool netBegin(Link *l) {
  netEnd();
  const int s = openSock();
  if (s < 0) return false;
  gSock = s;
  gLink = l;
  gStats = NetStats{};
  l->send = netSend;
  l->ctx = nullptr;
  return true;
}

void netEnd() {
  if (gSock >= 0) close(gSock);
  gSock = -1;
  if (gLink) { gLink->send = nullptr; gLink = nullptr; }
  if (gRaidIn) { gRaidIn->send = nullptr; gRaidIn = nullptr; }
  for (bool &u : gSlotUsed) u = false;
  gLocked = false;
}

bool netUp() { return gSock >= 0; }

void netPoll() {
  if (gSock < 0 || !gLink) return;
  uint8_t buf[2 + LINK_MAX_PAYLOAD + 8];
  for (int guard = 0; guard < 32; guard++) {
    sockaddr_in from;
    socklen_t fl = sizeof(from);
    ssize_t n = recvfrom(gSock, buf, sizeof(buf), 0, (sockaddr *)&from, &fl);
    if (n < 0) break;              // EAGAIN: drained (or an error: try next frame)
    if (n == 0 || n > 2 + LINK_MAX_PAYLOAD) continue;
    if (!gLocked) {
      if (!gHost) continue;        // a guest with no host hears nobody
      gPeer = from;
      gLocked = true;
    } else if (from.sin_addr.s_addr != gPeer.sin_addr.s_addr ||
               from.sin_port != gPeer.sin_port) {
      gStats.foreign++;            // somebody else's game
      continue;
    }
    gStats.rx++;
    gLink->onPacket(buf, (uint8_t)n);
  }
  // See phone_net.h: nobody has been invited yet, so nobody is late.
  if (!gLocked) gLink->armed = false;
}

// --- the raid ---------------------------------------------------------------

static void raidSend(void *, uint8_t peer, const uint8_t *buf, uint16_t len) {
  if (gSock < 0) return;
  const sockaddr_in *to = nullptr;
  if (gHost) { if (peer < RNET_PEERS && gSlotUsed[peer]) to = &gSlot[peer]; }
  else if (gLocked) to = &gPeer;
  if (!to) return;
  ssize_t n = sendto(gSock, buf, len, 0, (const sockaddr *)to, sizeof(*to));
  if (n == (ssize_t)len) gStats.tx++;
  else gStats.txFail++;
}

bool netRaidBegin(RaidNet *r) {
  netEnd();
  if (!r) return false;
  const int s = openSock();
  if (s < 0) return false;
  gSock = s;
  gRaidIn = r;
  gStats = NetStats{};
  r->send = raidSend;
  r->ctx = nullptr;
  return true;
}

// A host's slot for this sender: its own, else a free one, else one the
// protocol has let go of. -1 when all four are somebody's.
static int raidSlotFor(const sockaddr_in &from) {
  for (int i = 0; i < RNET_PEERS; i++)
    if (gSlotUsed[i] && sameAddr(gSlot[i], from)) return i;
  int pick = -1;
  for (int i = 0; i < RNET_PEERS && pick < 0; i++)
    if (!gSlotUsed[i]) pick = i;
  for (int i = 0; i < RNET_PEERS && pick < 0; i++)
    if (!gRaidIn->peerUsed((uint8_t)i)) pick = i;
  if (pick < 0) return -1;
  gSlot[pick] = from;
  gSlotUsed[pick] = true;
  return pick;
}

void netRaidPoll() {
  if (gSock < 0 || !gRaidIn) return;
  static uint8_t buf[NET_RAID_DGRAM];
  for (int guard = 0; guard < 32; guard++) {
    sockaddr_in from;
    socklen_t fl = sizeof(from);
    ssize_t n = recvfrom(gSock, buf, sizeof(buf), 0, (sockaddr *)&from, &fl);
    if (n < 0) break;
    if (n == 0) continue;
    int peer = 0;
    if (gHost) {
      peer = raidSlotFor(from);
      if (peer < 0) { gStats.foreign++; continue; }   // a fifth phone
    } else {
      if (!gLocked) continue;
      if (!sameAddr(from, gPeer)) { gStats.foreign++; continue; }
    }
    gStats.rx++;
    gRaidIn->onPacket((uint8_t)peer, buf, (uint16_t)n);
  }
}

int netRaidPeers() {
  int n = 0;
  for (int i = 0; i < RNET_PEERS; i++) n += gSlotUsed[i] && gRaidIn && gRaidIn->peerUsed((uint8_t)i);
  return n;
}

static bool parseAddr(const char *addr, sockaddr_in *out) {
  if (!addr) return false;
  char ip[NET_ADDR_LEN];
  snprintf(ip, sizeof(ip), "%s", addr);
  int port = NET_PORT;
  char *colon = strchr(ip, ':');
  if (colon) {
    *colon = 0;
    char *end = nullptr;
    long p = strtol(colon + 1, &end, 10);
    if (!end || *end || p < 1 || p > 65535) return false;
    port = (int)p;
  }
  memset(out, 0, sizeof(*out));
  out->sin_family = AF_INET;
  out->sin_port = htons((uint16_t)port);
  return inet_pton(AF_INET, ip, &out->sin_addr) == 1;
}

bool netJoin(const char *addr) {
  if (gHost) return false;
  sockaddr_in a;
  if (!parseAddr(addr, &a)) return false;
  gPeer = a;
  gLocked = true;
  return true;
}

bool netTargeted() { return !gHost && gLocked; }
bool netLocked() { return gLocked; }

bool netMyAddress(char *out, int n) {
  ifaddrs *all = nullptr;
  if (getifaddrs(&all) != 0) return false;
  bool found = false;
  for (ifaddrs *i = all; i && !found; i = i->ifa_next) {
    if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET) continue;
    if (i->ifa_flags & IFF_LOOPBACK) continue;
    if (!(i->ifa_flags & IFF_UP)) continue;
    const sockaddr_in *a = (const sockaddr_in *)i->ifa_addr;
    found = inet_ntop(AF_INET, &a->sin_addr, out, n) != nullptr;
  }
  freeifaddrs(all);
  return found;
}

// --- the host list ----------------------------------------------------------

static std::mutex gHostsMu;
static NetHost gHosts[NET_HOSTS_MAX];
static int gHostsN = 0;

void netHostSeen(const char *name, const char *ip, int port) {
  if (!name || !ip || port < 1 || port > 65535) return;
  NetHost h;
  snprintf(h.name, sizeof(h.name), "%s", name);
  snprintf(h.addr, sizeof(h.addr), "%s:%d", ip, port);
  sockaddr_in check;
  if (!parseAddr(h.addr, &check)) return;
  std::lock_guard<std::mutex> g(gHostsMu);
  for (int i = 0; i < gHostsN; i++)
    if (!strcmp(gHosts[i].name, h.name)) { gHosts[i] = h; return; }
  if (gHostsN < NET_HOSTS_MAX) gHosts[gHostsN++] = h;
}

void netHostGone(const char *name) {
  if (!name) return;
  char key[NET_NAME_LEN];
  snprintf(key, sizeof(key), "%s", name);
  std::lock_guard<std::mutex> g(gHostsMu);
  for (int i = 0; i < gHostsN; i++) {
    if (strcmp(gHosts[i].name, key)) continue;
    for (int j = i + 1; j < gHostsN; j++) gHosts[j - 1] = gHosts[j];
    gHostsN--;
    return;
  }
}

void netHostsClear() {
  std::lock_guard<std::mutex> g(gHostsMu);
  gHostsN = 0;
}

int netHosts(NetHost *out, int max) {
  std::lock_guard<std::mutex> g(gHostsMu);
  int n = gHostsN < max ? gHostsN : max;
  for (int i = 0; i < n; i++) out[i] = gHosts[i];
  return n;
}

NetStats netStats() { return gStats; }

uint32_t netDeviceId(const char *path) {
  static uint32_t cached = 0;
  if (cached) return cached;
  uint32_t id = 0;
  if (path) {
    if (FILE *f = fopen(path, "rb")) {
      if (fread(&id, sizeof(id), 1, f) != 1) id = 0;
      fclose(f);
    }
  }
  if (!id) {
    if (FILE *r = fopen("/dev/urandom", "rb")) {
      if (fread(&id, sizeof(id), 1, r) != 1) id = 0;
      fclose(r);
    }
    if (!id) id = (uint32_t)time(nullptr) ^ ((uint32_t)getpid() << 16);
    if (!id) id = 1;
    if (path) {
      if (FILE *f = fopen(path, "wb")) { fwrite(&id, sizeof(id), 1, f); fclose(f); }
    }
  }
  cached = id;
  return id;
}
