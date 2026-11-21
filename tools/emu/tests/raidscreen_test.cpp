// Adventures with other players, step 12e part 3: the SCREENS, driven through
// the real sketch with the app's real UDP transport (phone_net.cpp, raid mode)
// plugged in, against a second phone played by this test over localhost. The
// other phone has its own run record and store, swapped in while it runs (as
// raidnet_test does), and talks only through its own socket.
//
//   - unplugged, the tab has no HOST or JOIN and the run is solo
//   - the sketch HOSTS: the lobby, START, the door, a turn each, the turn
//     deadline, a guest gone quiet, and a host finishing alone
//   - the sketch JOINS: the host list, TYPE AN ADDRESS, the price, the lobby,
//     a door it may not pick, a turn, the host lost, REJOIN, a refused rejoin,
//     and giving up
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "battle.h"
#include "fork.h"
#include "fork_adv.h"
#include "fork_raidnet.h"
#include "phone_net.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <utility>

uint32_t g_seed = 37;
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
void uiAdvRect(uint8_t what, uint8_t i, int *x, int *y, int *w, int *h);
uint32_t rtcEpoch();
void rtcSetEpoch(uint32_t e);
extern Pet pet;
extern bool exploreOpen, forkExpMode, forkAdvMode;
extern uint8_t forkAdvDiff;
extern uint16_t gRegionArt;
extern Arduino_Canvas *gfx;
extern int16_t gPanelH, gInsetTop, gInsetBot, gGfxOriginY, gFrameSquareY;
extern bool gPanelRound;

static int bad = 0;
static void ck(bool ok, const char *what) {
  printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) bad++;
}

static const int H = 1010;
static const uint32_t T0 = 20769u * 86400u + 12 * 3600;
static const uint32_t DAY = T0 / 86400;
static const uint16_t PORT = 47481;   // not NET_PORT: netlan_test may be running

enum { R_DIFF, R_START, R_LOAN, R_DOOR, R_MOVE, R_TWO, R_KEEP, R_GIVEUP,
       R_FRIEND, R_SEAT, R_LSTART, R_HOSTROW, R_TYPE, R_REJOIN };
static void tapRect(uint8_t what, uint8_t i = 0) {
  int x, y, w, h;
  uiAdvRect(what, i, &x, &y, &w, &h);
  onTap((int16_t)(x + w / 2), (int16_t)(y + h / 2));
}
static bool flushes() {
  gfx->frameReady = false;
  render();
  return gfx->frameReady;
}

// --- the other phone ---------------------------------------------------------
struct Other {
  AdvRun run;
  NvsStore store;
  RaidNet net;
  int sock = -1;
  sockaddr_in to;            // a guest's host
  bool toKnown = false;
  sockaddr_in slot[RNET_PEERS];   // a host's guests, by peer number, as phone_net.cpp keeps them
  int slots = 0;
  bool silent = false;     // hears nothing, says nothing, does not tick (asleep)
};
static Other O;
static void enter() {
  std::swap(gAdv, O.run);
  gAdv.battle.hearts = &gAdv.hearts;
  std::swap(nvs(), O.store);
}
static void leave() {
  std::swap(nvs(), O.store);
  std::swap(gAdv, O.run);
  gAdv.battle.hearts = &gAdv.hearts;
  O.run.battle.hearts = &O.run.hearts;
}
template <typename F> static void as(F f) { enter(); f(); leave(); }

static void oSend(void *, uint8_t peer, const uint8_t *b, uint16_t n) {
  if (O.silent) return;
  if (O.net.isHost) {
    if (peer < O.slots) sendto(O.sock, b, n, 0, (sockaddr *)&O.slot[peer], sizeof(O.slot[peer]));
    return;
  }
  if (O.toKnown) sendto(O.sock, b, n, 0, (sockaddr *)&O.to, sizeof(O.to));
}
static uint16_t oOpen() {
  if (O.sock >= 0) close(O.sock);
  O.sock = socket(AF_INET, SOCK_DGRAM, 0);
  sockaddr_in me; memset(&me, 0, sizeof(me));
  me.sin_family = AF_INET; me.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  bind(O.sock, (sockaddr *)&me, sizeof(me));
  fcntl(O.sock, F_SETFL, fcntl(O.sock, F_GETFL, 0) | O_NONBLOCK);
  socklen_t l = sizeof(me);
  getsockname(O.sock, (sockaddr *)&me, &l);
  return ntohs(me.sin_port);
}
static void oDrain() {
  static uint8_t buf[2048];
  for (;;) {
    sockaddr_in from; socklen_t fl = sizeof(from);
    ssize_t n = recvfrom(O.sock, buf, sizeof(buf), 0, (sockaddr *)&from, &fl);
    if (n <= 0) break;
    if (O.silent) continue;
    int peer = 0;
    if (O.net.isHost) {   // a new address is a new peer (a rejoin comes from a new socket)
      for (peer = 0; peer < O.slots; peer++)
        if (O.slot[peer].sin_port == from.sin_port && O.slot[peer].sin_addr.s_addr == from.sin_addr.s_addr) break;
      if (peer == O.slots) { if (O.slots == RNET_PEERS) continue; O.slot[O.slots++] = from; }
    }
    as([&] { O.net.onPacket((uint8_t)peer, buf, (uint16_t)n); });
  }
}
static void step(int frames = 1) {
  for (int i = 0; i < frames; i++) {
    emuAdvanceMs(50);
    loop();
    usleep(300);
    oDrain();
    if (!O.silent && O.net.state != RNET_OFF) as([] { O.net.tick(millis()); });
  }
}
template <typename F> static bool stepUntil(F done, int frames) {
  for (int i = 0; i < frames; i++) {
    if (done()) return true;
    step();
  }
  return done();
}

// The sketch's move, through its screen: taps page any narration first.
static bool hostPick() {
  for (int t = 0; t < 30; t++) {
    if (gAdv.battle.seat[0].pick) return true;
    tapRect(R_MOVE, 0);
  }
  return gAdv.battle.seat[0].pick != 0;
}
// Pages a turn's narration shut, tapping above the move grid (where nothing
// else answers), as a player reads it before reaching for GIVE UP.
static void closeStory() {
  render();
  int x, y, w, h;
  uiAdvRect(R_MOVE, 0, &x, &y, &w, &h);
  for (int i = 0; i < 20; i++) onTap(233, (int16_t)(y - 120));
}
static void tanky() {   // a shot at the turns, not at a win
  RaidBoss &k = gAdv.battle.boss;
  k.mon.hp = k.mon.maxHp;
  for (RaidSeat &s : gAdv.battle.seat) if (s.occupied()) s.mon.hp = s.mon.maxHp;
}

// --- the hooks, as android_main.cpp plugs them ------------------------------
static int gAsked = 0;
static uint8_t hosts(ForkNetHost *out, uint8_t max) {
  NetHost hs[NET_HOSTS_MAX];
  int n = netHosts(hs, max < NET_HOSTS_MAX ? max : NET_HOSTS_MAX);
  for (int i = 0; i < n; i++) {
    snprintf(out[i].name, sizeof(out[i].name), "%s", hs[i].name);
    snprintf(out[i].addr, sizeof(out[i].addr), "%s", hs[i].addr);
  }
  return (uint8_t)n;
}
static void ask(const char *) { gAsked++; }
static bool mine(char *o, uint8_t n) { snprintf(o, n, "10.1.2.3"); return true; }
static void endNet() { netEnd(); }
static uint32_t devId() { return 0x1111; }
static void plug() {
  gForkHost.netRole = netRole; gForkHost.netHosts = hosts;
  gForkHost.netJoin = netJoin; gForkHost.netTargeted = netTargeted;
  gForkHost.netLocked = netLocked; gForkHost.netAskAddress = ask;
  gForkHost.netMyAddress = mine;
  gForkHost.raidBegin = netRaidBegin; gForkHost.raidPoll = netRaidPoll;
  gForkHost.raidEnd = endNet; gForkHost.deviceId = devId;
}

int main() {
  gRegionArt = 0xFFFF;
  gfx->resize(466, H);
  gPanelH = H;
  gPanelRound = false;
  gInsetTop = 42;
  gInsetBot = 0;
  gGfxOriginY = gFrameSquareY = (H - 466) / 2;
  forkSetFeatures(true);
  setup();
  if (pet.awaitingStarter()) pet.chooseStarter(0);
  if (pet.isEgg()) pet.dbgHatchAs(6, false);
  rtcSetEpoch(T0);
  pet.dbgSetSeen(T0);
  strcpy(pet.trainerName, "ASH");
  netTestPort(PORT);
  exploreOpen = true;
  forkExpMode = false;
  forkAdvMode = true;
  forkAdvDiff = RAID_EASY;
  gAdv = AdvRun();
  advSave();

  // ---- unplugged: solo, as before part 3 -----------------------------------------
  ck(!forkRaidOn(), "with no transport there is nobody to play with");
  ck(flushes(), "the tab flushes");
  {
    int x, y, w, h;
    uiAdvRect(R_FRIEND, 1, &x, &y, &w, &h);
    onTap((int16_t)(x + w / 2), (int16_t)(y + h / 2));
    ck(forkRaidView() == RAIDV_NONE && gAdv.phase == ADV_IDLE, "where JOIN would be, a tap does nothing");
  }

  plug();
  ck(forkRaidOn(), "plugged in, the phone can play with others");

  // ==== the sketch HOSTS =============================================================
  tapRect(R_FRIEND, 0);
  ck(gAdv.phase == ADV_LOAN && gAdv.net.role == ADV_HOSTED, "HOST starts a run and hosts it");
  ck(gRaid.state == RNET_IN && gRaid.isHost && netHosting() && netKind() == NET_RAID,
     "on the raid socket, as its host");
  ck(gAdv.net.player[0] == 0x1111 && !strcmp(gAdv.net.name[0], "ASH"), "under this phone's number and trainer name");
  ck(flushes(), "the lobby flushes");

  // a guest arrives
  O = Other();
  oOpen();
  O.to.sin_family = AF_INET; O.to.sin_port = htons(PORT);
  O.to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  O.toKnown = true;
  O.net.send = oSend;
  as([] { gAdv = AdvRun(); advBegin(); O.net.join(0x2222, "MISTY", DAY); });
  ck(stepUntil([] { return O.net.state == RNET_IN && gAdv.net.player[1] == 0x2222; }, 100),
     "a guest joins the lobby over the socket");
  ck(O.run.net.role == ADV_GUEST && O.run.phase == ADV_LOAN, "and holds the run's mirror, at its LOAN");
  ck(!strcmp(gAdv.net.name[1], "MISTY"), "its seat carries its name");

  tapRect(R_LSTART);
  ck(gAdv.phase == ADV_LOAN, "START does nothing before anybody has picked");
  tapRect(R_LOAN, 0);
  ck(gRaid.hostLoan == 0 && gAdv.phase == ADV_LOAN, "a loan tap is the host's pick, not the start");
  tapRect(R_LSTART);
  ck(gAdv.phase == ADV_LOAN, "START waits for the guest's pick");
  as([] { O.net.pickLoan(2); });
  ck(stepUntil([] { return gRaid.canStart(); }, 100), "once the guest has picked, the host can start");
  ck(flushes(), "the full lobby flushes");
  tapRect(R_LSTART);
  ck(gAdv.phase == ADV_DOOR, "START begins the run");
  ck(stepUntil([] { return O.run.phase == ADV_DOOR; }, 100), "and the guest is at the door too");
  ck(O.run.seatMon[0].dex == O.run.loan[2].dex && O.run.seatMon[0].dex,
     "with the loan it picked, in its own seat 0");

  tapRect(R_DOOR, 0);
  ck(gAdv.phase == ADV_FIGHT, "the host picks the door");
  ck(stepUntil([] { return O.run.phase == ADV_FIGHT; }, 100), "and the guest follows it in");

  // a turn each
  tanky();
  uint16_t turn = gAdv.battle.turn;
  ck(hostPick(), "the host's move is a tap on its grid");
  ck(!forkRaidMyTurn() && gAdv.battle.turn == turn, "then it waits for the guest");
  ck(flushes(), "the waiting fight flushes");
  tapRect(R_MOVE, 1);
  ck(gAdv.battle.seat[0].pick == 1, "a tap while waiting does not change the pick");
  as([] { O.net.act(0, false); });
  ck(stepUntil([&] { return gAdv.battle.turn != turn; }, 100), "the guest's move resolves the turn");
  ck(gRaid.turnNew, "the turn waits to be told");
  render();
  ck(!gRaid.turnNew, "and the screen tells it");
  ck(stepUntil([] { return O.net.turnNew; }, 100), "the guest is told the same turn");

  // the deadline: the guest says nothing
  tanky();
  turn = gAdv.battle.turn;
  ck(hostPick(), "the host picks the next move");
  step(20 * 44);
  ck(gAdv.battle.turn == turn, "44 s later the turn still waits for the guest");
  step(20 * 2);
  ck(gAdv.battle.turn != turn, "at 45 s the AI plays the guest's turn");
  ck(gAdv.battle.seat[1].chooser == SEAT_REMOTE && (gAdv.net.remote & 2),
     "and gives the seat back to its player for the next");

  // the guest goes quiet
  tanky();
  O.silent = true;
  step(20 * 13);
  ck(!(gAdv.net.remote & 2) && gAdv.battle.seat[1].chooser == SEAT_AI,
     "a guest silent 12 s is dropped, and the AI plays its seat");
  ck(gAdv.net.player[1] == 0x2222, "the seat is still that player's, for a rejoin");
  turn = gAdv.battle.turn;
  hostPick();
  ck(stepUntil([&] { return gAdv.battle.turn != turn; }, 10), "the host's turns no longer wait");

  // the host's app is killed: the run and its record survive, the socket not
  netEnd();
  gRaid.reset();
  ck(gRaid.state == RNET_OFF && gAdv.net.role == ADV_HOSTED, "a killed host comes back with its run and no socket");
  ck(stepUntil([] { return gRaid.state == RNET_IN && gRaid.isHost; }, 10), "and hosts it again by itself");
  O.silent = false;
  as([] { O.net.join(0x2222, "MISTY", DAY); });   // the guest's REJOIN
  ck(stepUntil([] { return (gAdv.net.remote & 2) != 0; }, 100), "the guest rejoins its own seat");
  ck(O.net.state == RNET_IN && O.run.phase == ADV_FIGHT, "and is back in the fight");

  // the host gives up; its guest must have the KEEP before it can finish
  closeStory();
  O.silent = true;   // asleep, not yet timed out
  tapRect(R_GIVEUP); tapRect(R_GIVEUP);
  ck(gAdv.phase == ADV_KEEP, "GIVE UP twice ends the hosted run");
  emuAdvanceMs(1000);
  tapRect(R_KEEP, gAdv.caughtN);   // nothing
  tapRect(R_TWO, 0);
  ck(gAdv.phase == ADV_KEEP, "DONE waits while a connected guest has not got the KEEP");
  ck(flushes(), "and says so");
  O.silent = false;
  ck(stepUntil([] { return gRaid.allSeen(); }, 100), "the guest wakes and has it");
  ck(O.run.phase == ADV_KEEP, "at its own KEEP");
  tapRect(R_TWO, 0);
  ck(gAdv.phase == ADV_IDLE && gAdv.net.role == ADV_SOLO, "then DONE finishes it");
  ck(stepUntil([] { return gRaid.state == RNET_OFF; }, 20), "and the socket closes");
  ck(stepUntil([] { return O.net.state != RNET_IN; }, 20) && O.run.phase == ADV_KEEP,
     "the guest is told, and keeps on its own");

  // ==== the sketch JOINS =============================================================
  O = Other();
  const uint16_t oPort = oOpen();
  O.net.send = oSend;
  as([] {
    gAdv = AdvRun(); advBegin();
    advStart(RAID_EASY, DAY);
    O.net.host(0x3333, "BROCK");
  });
  ck(O.net.state == RNET_IN && O.net.isHost, "another phone hosts a run");

  gAdv.freeDay = rtcEpoch() / 86400;   // today's free run is gone: joining costs
  advSave();
  forkData.st.coins = 120;
  emuAdvanceMs(1000);
  render();
  tapRect(R_FRIEND, 1);
  ck(forkRaidView() == RAIDV_BROWSE, "JOIN looks for hosts");
  ck(!netHosting() && netKind() == NET_RAID, "as a guest on the raid socket");
  ck(forkData.st.coins == 120, "and takes nothing yet");
  netHostsClear();
  ck(flushes(), "the empty host list flushes");
  tapRect(R_HOSTROW, 0);
  ck(!netTargeted(), "an empty row is not a host");
  tapRect(R_TYPE);
  ck(gAsked == 1, "TYPE AN ADDRESS asks the phone's dialog");
  netHostSeen("BROCK", "127.0.0.1", oPort);
  ck(flushes(), "the listed host flushes");
  tapRect(R_HOSTROW, 0);
  ck(netTargeted(), "tapping it picks it");
  ck(stepUntil([] { return gRaid.state == RNET_IN && gAdv.net.role == ADV_GUEST; }, 100),
     "and the next frames join its run");
  ck(gAdv.phase == ADV_LOAN && forkRaidView() == RAIDV_NONE, "the lobby, as the run's mirror");
  ck(forkData.st.coins == 70, "today's price is paid when the run starts for us");
  ck(O.run.net.player[1] == 0x1111 && !strcmp(O.run.net.name[1], "ASH"), "the host seats us by number and name");
  ck(flushes(), "the guest's lobby flushes");

  tapRect(R_LOAN, 1);
  ck(!forkRaidMyTurn(), "a loan tap is sent");
  ck(stepUntil([] { return (O.net.loanMask & 2) != 0; }, 100), "and the host has it");
  ck(stepUntil([] { return gRaid.pendN == 0; }, 100), "and the guest sees it taken");
  tapRect(R_LSTART);
  ck(gAdv.phase == ADV_LOAN, "a guest has no START");
  as([] { O.net.pickLoan(0); });
  as([] { O.net.start(); });
  ck(stepUntil([] { return gAdv.phase == ADV_DOOR; }, 100), "the host starts, and we are at the door");
  ck(gAdv.seatMon[0].dex == gAdv.loan[1].dex, "with the loan we picked");
  ck(flushes(), "the guest's door flushes");
  tapRect(R_DOOR, 0);
  ck(gAdv.phase == ADV_DOOR && O.run.phase == ADV_DOOR, "a guest cannot pick the door");
  as([] { advPickDoor(1); });
  ck(stepUntil([] { return gAdv.phase == ADV_FIGHT; }, 100), "the host's door takes us in");

  as([] { tanky(); });
  step(5);
  turn = gAdv.battle.turn;
  tapRect(R_MOVE, 0);
  ck(!forkRaidMyTurn() && gRaid.pendN, "our move is sent");
  ck(flushes(), "and the screen waits");
  as([] { O.net.act(0, false); });
  ck(stepUntil([&] { return gAdv.battle.turn != turn; }, 100), "the host resolves both, and we see it");
  render();
  ck(!gRaid.turnNew, "the screen narrates the host's turn");

  // the host goes quiet
  O.silent = true;
  step(20 * 13);
  ck(gRaid.state == RNET_LOST && forkRaidView() == RAIDV_AWAY, "a silent host leaves us AWAY");
  ck(gAdv.phase == ADV_FIGHT && gAdv.net.role == ADV_GUEST, "with the mirror kept");
  ck(flushes(), "AWAY flushes");
  tapRect(R_MOVE, 0);
  ck(forkRaidView() == RAIDV_AWAY, "the moves under it take no taps");
  O.silent = false;
  tapRect(R_REJOIN);
  ck(forkRaidView() == RAIDV_BROWSE, "REJOIN looks for the host again");
  tapRect(R_HOSTROW, 0);
  ck(stepUntil([] { return gRaid.state == RNET_IN; }, 200), "and picking it rejoins the run");
  ck(forkRaidView() == RAIDV_NONE && gAdv.phase == ADV_FIGHT, "back in the fight");
  ck(forkData.st.coins == 70, "a rejoin costs nothing");
  ck(O.run.net.remote & 2, "and the host has our seat back");

  // a rejoin the host no longer knows
  O.silent = true;
  step(20 * 13);
  as([] { gAdv.net.runId ^= 1; advSave(); });
  O.silent = false;
  tapRect(R_REJOIN);
  tapRect(R_HOSTROW, 0);
  ck(stepUntil([] { return forkRaidView() == RAIDV_FAILED; }, 200), "a host on another run says no");
  ck(gRaid.refused == RNR_GONE, "because ours is over");
  ck(flushes(), "the refusal flushes");
  tapRect(R_GIVEUP);   // OK
  ck(forkRaidView() == RAIDV_AWAY, "OK goes back to the run we were in");
  tapRect(R_GIVEUP); tapRect(R_GIVEUP);
  ck(gAdv.phase == ADV_KEEP && forkRaidView() == RAIDV_NONE, "GIVE UP twice keeps what we caught");
  emuAdvanceMs(1000);
  tapRect(R_KEEP, gAdv.caughtN);
  tapRect(R_TWO, 0);
  ck(gAdv.phase == ADV_IDLE && gAdv.net.role == ADV_SOLO, "DONE ends it on this phone");

  printf("%s raidscreen_test (%d failed)\n", bad ? "FAIL" : "PASS", bad);
  return bad ? 1 : 0;
}
