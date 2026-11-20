// The phone's UDP link transport (android/app/src/main/cpp/phone_net.cpp, plan
// step 12e), over a real socket on localhost. phone_net drives the HOST side;
// the guest is a plain socket with its own Link, standing in for a second phone.
// What it pins: the host says nothing until a peer exists, waits for one past
// the 20 s pairing timeout, locks onto the first one and drops everyone else,
// and a whole handshake completes over the wire.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "battle.h"
#include "link.h"
#include "phone_net.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#ifndef TAMA_NATIVE_TEST
uint32_t g_seed=17; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false; bool wasPressed=false;
uint32_t millis(){return 0;} void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;} String FakeSerial::readStringUntil(char){return String("");}
void sfxPlay(uint8_t){}
#endif
static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

static int openSock() {
  int s = socket(AF_INET, SOCK_DGRAM, 0);
  sockaddr_in me; memset(&me, 0, sizeof(me));
  me.sin_family = AF_INET; me.sin_addr.s_addr = htonl(INADDR_LOOPBACK); me.sin_port = 0;
  bind(s, (sockaddr*)&me, sizeof(me));
  fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
  return s;
}
static sockaddr_in hostAddr() {
  sockaddr_in a; memset(&a, 0, sizeof(a));
  a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = htons(NET_PORT);
  return a;
}

static int gGuestSock = -1;
static void guestSend(void*, const uint8_t *b, uint8_t n) {
  sockaddr_in a = hostAddr();
  sendto(gGuestSock, b, n, 0, (sockaddr*)&a, sizeof(a));
}
static int drain(int s, Link *l) {   // what the host sent the guest
  uint8_t buf[512]; int got = 0;
  for (;;) {
    ssize_t n = recv(s, buf, sizeof(buf), 0);
    if (n <= 0) break;
    got++;
    if (l) l->onPacket(buf, (uint8_t)n);
  }
  return got;
}
static void settle() { usleep(2000); }

static LinkMon mon(int16_t dex, const char *nm) {
  LinkMon m; memset(&m, 0, sizeof(m));
  m.dex = dex; m.level = 20; m.maxHp = 60;
  for (int i = 0; i < SI_COUNT; i++) m.base[i] = 40;
  m.moves[0] = 1;
  snprintf(m.name, sizeof(m.name), "%s", nm);
  return m;
}

int main() {
  // --- addresses -----------------------------------------------------------
  netRole(false, "G");
  ck(netJoin("192.168.1.20"), "a bare address parses");
  ck(netJoin("10.0.2.2:5000"), "an address with a port parses");
  ck(!netJoin("tamapoke"), "a name is not an address");
  ck(!netJoin("1.2.3.4:0") && !netJoin("1.2.3.4:70000") && !netJoin("1.2.3.4:"),
     "a port out of range or missing is refused");
  netRole(true, "H");
  ck(!netJoin("192.168.1.20"), "a host cannot join anybody");
  ck(!netTargeted(), "a host is never 'targeted'");

  // --- the host list -------------------------------------------------------
  netHostsClear();
  netHostSeen("ASH", "192.168.1.5", NET_PORT);
  netHostSeen("MISTY", "192.168.1.6", NET_PORT);
  netHostSeen("ASH", "192.168.1.9", 4000);    // moved: updated, not doubled
  netHostSeen("BAD", "not-an-ip", NET_PORT);   // never listed
  NetHost hs[NET_HOSTS_MAX];
  int n = netHosts(hs, NET_HOSTS_MAX);
  ck(n == 2 && !strcmp(hs[0].addr, "192.168.1.9:4000"), "a host seen again is updated in place");
  netHostGone("ASH");
  n = netHosts(hs, NET_HOSTS_MAX);
  ck(n == 1 && !strcmp(hs[0].name, "MISTY"), "a host that leaves is dropped");
  for (int i = 0; i < 20; i++) { char nm[8]; snprintf(nm, 8, "H%d", i); netHostSeen(nm, "10.0.0.1", 1); }
  ck(netHosts(hs, NET_HOSTS_MAX) == NET_HOSTS_MAX, "the list is capped, never overrun");
  netHostsClear();

  // --- hosting over a real socket -----------------------------------------
  static Link H, G;
  netRole(true, "HOST");
  // the hello carries peerName as OUR name until theirs replaces it; lanOffer
  // sets it the same way
  H.begin(true, "HOST"); strcpy(H.peerName, "HOST");
  H.addMon(mon(25, "PIKA"));
  ck(netBegin(&H) && netUp(), "the host binds its port");
  H.start();
  gGuestSock = openSock();
  uint32_t t = 1000;
  H.tick(t);
  for (int i = 0; i < 10; i++) { t += 1000; netPoll(); H.tick(t); }
  settle();
  ck(netStats().tx == 0 && drain(gGuestSock, nullptr) == 0, "a host with no peer sends nothing");
  // past the pairing timeout with nobody there: still waiting, not LOST
  for (int i = 0; i < 30; i++) { t += 1000; netPoll(); H.tick(t); }
  ck(H.state != LINK_LOST, "a host waits for its first guest past the 20 s pairing timeout");

  // the guest arrives
  G.begin(false, "GUEST"); strcpy(G.peerName, "GUEST");
  G.addMon(mon(4, "CHAR"));
  G.send = guestSend;
  G.start();
  for (int i = 0; i < 200 && !(H.ready() && G.ready()); i++) {
    settle();
    netPoll();
    drain(gGuestSock, &G);
    t += 50; H.tick(t); G.tick(t);
  }
  ck(netLocked(), "the host locks onto the first guest");
  ck(H.ready() && G.ready(), "the handshake completes over UDP");
  ck(H.theirsN == 1 && H.theirs[0].dex == 4 && G.theirsN == 1 && G.theirs[0].dex == 25,
     "each side holds the other's squad");
  ck(!strcmp(H.peerName, "GUEST") && !strcmp(G.peerName, "HOST"), "names crossed");

  // a stranger on the same network is somebody else's game
  int stranger = openSock();
  static Link S; S.begin(false, "STRANGER"); strcpy(S.peerName, "STRANGER"); S.addMon(mon(7, "SQRT"));
  gGuestSock = stranger; S.send = guestSend; S.start();
  settle();
  uint32_t foreign0 = netStats().foreign;
  netPoll();
  ck(netStats().foreign > foreign0 && H.ready() && !strcmp(H.peerName, "GUEST"),
     "a second device is dropped once a peer is locked");

  // once locked, the ordinary pairing clock applies again
  uint8_t st = H.state;
  H.state = LINK_HANDSHAKE;
  t += LINK_PAIR_TIMEOUT_MS + 1000; netPoll(); H.tick(t);
  ck(H.state == LINK_LOST, "with a peer locked, a silent one times out as before");
  H.state = st;

  netEnd();
  ck(!netUp() && !netLocked() && H.send == nullptr, "netEnd frees the socket and unplugs the link");
  char ip[32];
  printf("info  netMyAddress: %s\n", netMyAddress(ip, sizeof(ip)) ? ip : "(none)");
  close(stranger);
  printf("%s\n", bad ? "net_test FAILED" : "net_test OK");
  return bad ? 1 : 0;
}
