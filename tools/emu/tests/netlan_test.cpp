// Phone to phone (plan step 12e): upstream's LAN battle screen, driven through
// the real sketch with the app's real UDP transport (phone_net.cpp) plugged in,
// against a second "phone" played by this test over localhost.
//
//   - unplugged, the LAN screen is the board's (JOIN -> FIGHT -> "no radio")
//   - a guest lists the hosts found, says nothing until one is tapped, then
//     pairs with it over the wire; TYPE AN ADDRESS asks the phone's dialog
//   - a host shows its address, waits past the 20 s pairing timeout, and
//     pairs with the first guest to arrive
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "battle.h"
#include "link.h"
#include "fork.h"
#include "fork_i18n.h"
#include "i18n.h"
#include "link_radio.h"
#include "phone_net.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>

uint32_t g_seed = 29;
FakeSerial Serial;
FakeESP ESP;
FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0;
volatile bool g_touchDown = false;
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }
void FakeESP::restart() { exit(0); }

void emuAdvanceMs(uint32_t ms);
void setup();
void loop();
void render();
void onTap(int16_t x, int16_t y);
void lanLeave();
const char *lanStatusText();
void uiLanButtonRect(int i, int *x, int *y, int *w, int *h);
void uiLanActionRect(int *x, int *y, int *w, int *h);
bool uiLanHostRect(int i, int *x, int *y, int *w, int *h);
void uiPickButtonRect(bool fight, int *x, int *y, int *w, int *h);
extern Pet pet;
extern Party party;
extern Link lan;
extern bool lanOpen, pickOpen;
extern uint16_t squadMask;
extern Arduino_Canvas *gfx;
extern int16_t gPanelH, gInsetTop, gInsetBot, gGfxOriginY, gFrameSquareY;
extern bool gPanelRound;

static int bad = 0;
static void ck(bool ok, const char *what) {
  printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) bad++;
}
static const int H = 1010;

static void tapMid(int x, int y, int w, int h) { onTap((int16_t)(x + w / 2), (int16_t)(y + h / 2)); }
static void tapLanButton(int i) { int x, y, w, h; uiLanButtonRect(i, &x, &y, &w, &h); tapMid(x, y, w, h); }
static void tapFight() { int x, y, w, h; uiPickButtonRect(true, &x, &y, &w, &h); tapMid(x, y, w, h); }
static bool flushes() { gfx->frameReady = false; render(); return gfx->frameReady; }

// --- the other phone: a socket and a Link of its own ------------------------
static int gSock = -1;
static sockaddr_in gTo;
static bool gToKnown = false;
static Link R;
static void rSend(void *, const uint8_t *b, uint8_t n) {
  if (gToKnown) sendto(gSock, b, n, 0, (sockaddr *)&gTo, sizeof(gTo));
}
static uint16_t openSock() {
  if (gSock >= 0) close(gSock);
  gSock = socket(AF_INET, SOCK_DGRAM, 0);
  sockaddr_in me; memset(&me, 0, sizeof(me));
  me.sin_family = AF_INET; me.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  bind(gSock, (sockaddr *)&me, sizeof(me));
  fcntl(gSock, F_SETFL, fcntl(gSock, F_GETFL, 0) | O_NONBLOCK);
  socklen_t l = sizeof(me);
  getsockname(gSock, (sockaddr *)&me, &l);
  return ntohs(me.sin_port);
}
static int rDrain() {
  uint8_t buf[512]; int got = 0;
  for (;;) {
    sockaddr_in from; socklen_t fl = sizeof(from);
    ssize_t n = recvfrom(gSock, buf, sizeof(buf), 0, (sockaddr *)&from, &fl);
    if (n <= 0) break;
    got++;
    if (!gToKnown) { gTo = from; gToKnown = true; }   // a host learns its guest
    R.onPacket(buf, (uint8_t)n);
  }
  return got;
}
static LinkMon mon(int16_t dex, const char *nm) {
  LinkMon m; memset(&m, 0, sizeof(m));
  m.dex = dex; m.level = 20; m.maxHp = 60;
  for (int i = 0; i < SI_COUNT; i++) m.base[i] = 40;
  m.moves[0] = 1;
  snprintf(m.name, sizeof(m.name), "%s", nm);
  return m;
}
static uint32_t gNow = 0;
static void step(int frames = 1) {
  for (int i = 0; i < frames; i++) {
    emuAdvanceMs(50); gNow += 50;
    loop();
    usleep(500);
    rDrain();
    R.tick(gNow);
  }
}

// --- the hooks, as android_main.cpp plugs them ------------------------------
static int gAsked = 0;
static char gAskTitle[40] = "";
static uint8_t hosts(ForkNetHost *out, uint8_t max) {
  NetHost hs[NET_HOSTS_MAX];
  int n = netHosts(hs, max < NET_HOSTS_MAX ? max : NET_HOSTS_MAX);
  for (int i = 0; i < n; i++) {
    snprintf(out[i].name, sizeof(out[i].name), "%s", hs[i].name);
    snprintf(out[i].addr, sizeof(out[i].addr), "%s", hs[i].addr);
  }
  return (uint8_t)n;
}
static void ask(const char *t) { gAsked++; snprintf(gAskTitle, sizeof(gAskTitle), "%s", t); }
static bool mine(char *o, uint8_t n) { snprintf(o, n, "10.1.2.3"); return true; }
static void endNet() { netEnd(); }
static void plug() {
  gLinkRadio.begin = netBegin; gLinkRadio.end = endNet;
  gLinkRadio.up = netUp; gLinkRadio.poll = netPoll;
  gForkHost.netRole = netRole; gForkHost.netHosts = hosts;
  gForkHost.netJoin = netJoin; gForkHost.netTargeted = netTargeted;
  gForkHost.netLocked = netLocked; gForkHost.netAskAddress = ask;
  gForkHost.netMyAddress = mine;
}

int main() {
  gfx->resize(466, H);
  gPanelH = H;
  gPanelRound = false;
  gInsetTop = 42;
  gInsetBot = 0;
  gGfxOriginY = gFrameSquareY = (H - 466) / 2;
  setup();
  if (pet.awaitingStarter()) pet.chooseStarter(0);
  if (pet.isEgg()) pet.dbgHatchAs(6, false);
  for (int i = 0; i < 2; i++) {
    PartyMon m; m.dex = 25 + i; m.level = 30;
    m.ivAtk = m.ivDef = m.ivSpe = m.ivHp = 20;
    party.replaceAt(i, m);
  }

  // ---- unplugged: the board's screen, untouched -------------------------------
  lanOpen = true; lan.state = LINK_OFF; render();
  tapLanButton(1);
  ck(pickOpen, "JOIN opens the team select");
  tapFight();
  ck(lanOpen && lan.state == LINK_REFUSED && !strcmp(lanStatusText(), T(S_LAN_NORADIO)),
     "unplugged, FIGHT still says there is no radio");
  ck(!forkNetChoosing(lan) && !forkNetAwaitingGuest(lan), "and no phone list or address appears");
  lan.state = LINK_OFF;

  plug();

  // ---- a guest -----------------------------------------------------------------
  const uint16_t rPort = openSock();
  R.begin(true, "RIVAL"); strcpy(R.peerName, "RIVAL");
  R.addMon(mon(7, "SQUIRT"));
  R.send = rSend; gToKnown = false;
  R.start();

  netHostsClear();
  tapLanButton(1);
  tapFight();
  ck(lan.state == LINK_LISTENING && forkNetChoosing(lan), "a phone guest waits to choose a host");
  ck(!strcmp(lanStatusText(), FT(FS_NET_LOOKING)), "and says it is looking");
  ck(flushes(), "the host list flushes the panel");
  step(10);
  ck(rDrain() == 0 && lan.state == LINK_LISTENING, "it sends nothing before a host is chosen");
  step(500);   // 25 s
  ck(lan.state == LINK_LISTENING, "and is still choosing after the 20 s pairing timeout");

  {
    int x, y, w, h;
    uiLanActionRect(&x, &y, &w, &h);
    tapMid(x, y, w, h);
    ck(gAsked == 1 && !strcmp(gAskTitle, FT(FS_NET_TYPE)), "TYPE AN ADDRESS asks the phone's dialog");
  }
  char rAddr[16];
  snprintf(rAddr, sizeof(rAddr), "%d", rPort);
  netHostSeen("RIVAL", "127.0.0.1", rPort);
  netHostSeen("OTHER", "127.0.0.1", 9);
  render();
  {
    int x, y, w, h;
    ck(uiLanHostRect(0, &x, &y, &w, &h) && uiLanHostRect(1, &x, &y, &w, &h), "both hosts have a row");
    int ax, ay, aw, ah;
    uiLanActionRect(&ax, &ay, &aw, &ah);
    ck(y + h < ay, "the rows stay above the bottom button");
    uiLanHostRect(0, &x, &y, &w, &h);
    tapMid(x, y, w, h);
  }
  ck(netTargeted(), "tapping a host chooses it");
  for (int i = 0; i < 100 && !(lan.ready() && R.ready()); i++) step();
  ck(lan.ready() && R.ready(), "the guest pairs with the host it tapped, over UDP");
  ck(!strcmp(lan.peerName, "RIVAL") && !strcmp(R.peerName, pet.trainerName),
     "each side shows the other's name");
  ck(lan.theirsN == 1 && lan.theirs[0].dex == 7, "the host's squad arrived");
  ck(!strcmp(lanStatusText(), T(S_LAN_READY)), "and the screen moves on to READY");

  lanLeave();
  ck(!netUp(), "leaving frees the socket");

  // ---- a host ------------------------------------------------------------------
  openSock();
  gToKnown = false;
  tapLanButton(0);
  tapFight();
  ck(lan.isHost && forkNetAwaitingGuest(lan), "a phone host waits for a guest");
  ck(!strcmp(lanStatusText(), FT(FS_NET_WAIT_GUEST)), "and says so");
  ck(flushes(), "the waiting screen flushes the panel");
  step(600);   // 30 s
  ck(lan.live() && lan.state != LINK_LOST, "a host waits past the 20 s pairing timeout");

  gTo.sin_family = AF_INET; gTo.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  gTo.sin_port = htons(NET_PORT); gToKnown = true;
  R.begin(false, "GUESTY"); strcpy(R.peerName, "GUESTY");
  R.addMon(mon(1, "BULBA"));
  R.start();
  for (int i = 0; i < 100 && !(lan.ready() && R.ready()); i++) step();
  ck(lan.ready() && R.ready(), "the host pairs with the guest that arrived");
  ck(!strcmp(lan.peerName, "GUESTY"), "and shows the guest's name");
  ck(!forkNetAwaitingGuest(lan), "and stops showing its address");
  lanLeave();

  close(gSock);
  printf("%s\n", bad ? "netlan_test FAILED" : "netlan_test OK");
  return bad ? 1 : 0;
}
