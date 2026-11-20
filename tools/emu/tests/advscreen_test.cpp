// Adventures, step 12c part 2: the run's screens (fork_adv.h, the sketch's
// "adventures" section). A whole run is played by TAPPING the real screens on
// the phone's canvas, aimed through uiAdvRect() rather than retyped
// coordinates, so a screen that drew one thing and answered another fails here.
//
// Also: the tab exists only with the fork's features on, every phase flushes
// the panel (a missing flush is invisible to screenshots), the keep banks the
// creature through the same path a wild capture uses and pays once, a second
// run the same day costs coins, and GIVE UP asks twice.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "battle.h"
#include "inventory.h"
#include "items.h"
#include "fork.h"
#include "fork_adv.h"
#include <cstdio>
#include <cstring>

uint32_t g_seed = 61;
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
uint8_t uiCurrentScreen();
void uiExploreRects(int *regY, int *regH, int *difY, int *difH, int *btnY, int *btnH);
void uiAdvRect(uint8_t what, uint8_t i, int *x, int *y, int *w, int *h);
void uiExploreTabRect(int i, int *x, int *y, int *w, int *h);
static void tapTab(int i) {
  int x, y, w, h;
  uiExploreTabRect(i, &x, &y, &w, &h);
  onTap((int16_t)(x + w / 2), (int16_t)(y + h / 2));
}
uint32_t rtcEpoch();
void rtcSetEpoch(uint32_t e);
extern Pet pet;
extern Party party;
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

enum { R_DIFF, R_START, R_LOAN, R_DOOR, R_MOVE, R_TWO, R_KEEP, R_GIVEUP };
static void tapRect(uint8_t what, uint8_t i) {
  int x, y, w, h;
  uiAdvRect(what, i, &x, &y, &w, &h);
  onTap((int16_t)(x + w / 2), (int16_t)(y + h / 2));
}

// The panel was flushed by this render: frameReady is set by gfx->flush().
static bool flushes() {
  gfx->frameReady = false;
  render();
  return gfx->frameReady;
}

// Taps a move until the fight ends, the narration paged through between.
static void fightOut(bool winIt) {
  for (int t = 0; t < 80 && gAdv.phase == ADV_FIGHT; t++) {
    if (winIt) {
      RaidBoss &k = gAdv.battle.boss;
      k.shields = 0; k.barrier = RAID_BARRIERS_MAX; k.mon.hp = 1;
    }
    tapRect(R_MOVE, 0);
    for (int p = 0; p < 20 && gAdv.phase == ADV_FIGHT; p++) {   // page the narration
      int x, y, w, h;
      uiAdvRect(R_MOVE, 0, &x, &y, &w, &h);
      const uint16_t turn = gAdv.battle.turn;
      onTap((int16_t)(x + w / 2), (int16_t)(y + h / 2));
      if (gAdv.battle.turn != turn) break;   // that tap was a move: the narration had closed
    }
  }
}

static uint16_t creatures() {
  return (uint16_t)(party.count() + party.boxCount());
}

int main() {
  gRegionArt = 0xFFFF;
  gfx->resize(466, H);
  gPanelH = H;
  gPanelRound = false;
  gInsetTop = 42;
  gInsetBot = 0;
  gGfxOriginY = gFrameSquareY = (H - 466) / 2;

  // ---- off: the board's explore has no third tab ------------------------------
  forkSetFeatures(false);
  setup();
  if (pet.awaitingStarter()) pet.chooseStarter(0);
  rtcSetEpoch(T0);
  pet.dbgSetSeen(T0);
  exploreOpen = true;
  forkAdvMode = false;
  {
    int regY, regH, difY, difH, btnY, btnH;
    uiExploreRects(&regY, &regH, &difY, &difH, &btnY, &btnH);
    (void)regY; (void)regH; (void)difY; (void)difH; (void)btnY; (void)btnH;
  }
  tapTab(2);   // where the third tab would be
  ck(!forkAdvMode && !forkExpMode, "off: explore has no adventure tab");

  // ---- on ---------------------------------------------------------------------
  forkSetFeatures(true);
  forkData.begin();
  forkTestSetSickness(false);
  advBegin();
  forkData.st.coins = 100;
  tapTab(2);
  ck(forkAdvMode && !forkExpMode, "the third tab opens the adventure");
  ck(flushes(), "the idle tab flushes the panel");
  tapTab(1);
  ck(forkExpMode && !forkAdvMode, "the middle tab is still the expedition");
  tapTab(2);

  forkAdvDiff = RAID_EASY;
  tapRect(R_DIFF, RAID_NORMAL);
  ck(forkAdvDiff == RAID_NORMAL, "tapping a difficulty chooses it");
  tapRect(R_START, 0);
  ck(gAdv.phase == ADV_LOAN && gAdv.diff == RAID_NORMAL && forkData.st.coins == 100,
     "START begins a normal run, free, at the loan");
  ck(flushes(), "the loan flushes");

  tapRect(R_LOAN, 2);
  ck(gAdv.phase == ADV_DOOR && gAdv.seat[0].dex == gAdv.loan[2].dex, "tapping the third loan takes it");
  ck(flushes(), "the doors flush");
  const AdvMon behind = gAdv.door[1];
  tapRect(R_DOOR, 1);
  ck(gAdv.phase == ADV_FIGHT && gAdv.battle.boss.mon.dex == behind.dex, "tapping the right door fights what is behind it");
  ck(flushes(), "the fight flushes");

  {
    const uint16_t turn = gAdv.battle.turn;
    tapRect(R_MOVE, 0);
    ck(gAdv.battle.turn == turn + 1, "tapping a move plays a turn");
    ck(flushes(), "the narration flushes");
    tapRect(R_MOVE, 0);   // pages it, never a second turn
    ck(gAdv.battle.turn == turn + 1, "a tap on the narration pages it rather than playing again");
  }

  fightOut(true);
  ck(gAdv.phase == ADV_CATCH && gAdv.caughtN == 1, "winning the fight shows the catch");
  ck(flushes(), "the catch flushes");
  tapRect(R_TWO, 0);   // the same finger, still tapping
  ck(gAdv.phase == ADV_CATCH, "a tap straight after the fight does not answer the catch");
  emuAdvanceMs(1000);
  tapRect(R_TWO, 0);
  ck(gAdv.phase == ADV_DOOR && gAdv.seat[0].dex == gAdv.caught[0].dex, "SWAP takes the catch");
  for (int s = 1; s < ADV_STAGES - 1; s++) {
    tapRect(R_DOOR, 0);
    fightOut(true);
    emuAdvanceMs(1000);
    tapRect(R_TWO, 1);   // KEEP MINE
  }
  ck(gAdv.phase == ADV_FIGHT && gAdv.battle.boss.final, "after four stages, the boss");
  const AdvMon boss = gAdv.boss;
  fightOut(true);
  ck(gAdv.phase == ADV_KEEP && gAdv.won && gAdv.caughtN == ADV_STAGES, "beating the boss ends at the keep");
  emuAdvanceMs(1000);
  ck(flushes(), "the keep flushes");

  // ---- the keep: the boss, banked as a wild catch is ---------------------------
  {
    const uint16_t before = creatures();
    const uint32_t coins = forkData.st.coins;
    const uint8_t ultra = bag.count(IT_ULTRABALL);
    tapRect(R_TWO, 0);
    ck(gAdv.phase == ADV_KEEP, "DONE with nothing chosen does nothing");
    tapRect(R_KEEP, ADV_STAGES - 1);
    tapRect(R_TWO, 0);
    ck(gAdv.phase == ADV_IDLE, "choosing the boss and DONE ends the run");
    ck(creatures() == before + 1, "the creature kept is in the party or the box");
    bool found = false;
    for (uint8_t i = 0; i < PARTY_SLOTS; i++) {
      const PartyMon &m = party.slots[i];
      if (m.dex == boss.dex && m.level == 50 && m.ivAtk == boss.iv[0] && m.ivHp == boss.iv[3] &&
          (m.shiny != 0) == boss.shiny && m.stateVersion == 1)
        found = true;
    }
    for (uint16_t i = 0; i < BOX_SLOTS && !found; i++) {
      const PartyMon &m = party.box[i];
      if (m.dex == boss.dex && m.level == 50 && m.ivAtk == boss.iv[0]) found = true;
    }
    ck(found, "it is the boss itself: its species, IVs and the run's level");
    if (boss.form)
      ck(forkKeptForm(forkKeyOf(boss.iv[0], boss.iv[1], boss.iv[2], boss.iv[3], boss.shiny), boss.dex) == boss.form,
         "its form came home with it");
    ck(forkData.st.coins == coins + 40 && bag.count(IT_ULTRABALL) == ultra + 1,
       "a normal win paid 40 coins and an ULTRA BALL, once");
    tapRect(R_TWO, 0);   // where DONE was is START now
    ck(forkData.st.coins == coins + 40 && creatures() == before + 1 && gAdv.phase == ADV_IDLE,
       "a second tap on DONE pays nothing more and starts nothing");
    emuAdvanceMs(1000);
  }

  // ---- a second run the same day costs ------------------------------------------
  {
    forkData.st.coins = ADV_RUN_PRICE - 1;
    tapRect(R_START, 0);
    ck(gAdv.phase == ADV_IDLE, "short of coins, a second run does not start");
    forkData.st.coins = ADV_RUN_PRICE + 5;
    tapRect(R_START, 0);
    ck(gAdv.phase == ADV_IDLE && forkData.st.coins == ADV_RUN_PRICE + 5, "a paid run asks first");
    ck(flushes(), "the asking flushes");
    tapRect(R_START, 0);
    ck(gAdv.phase == ADV_LOAN && forkData.st.coins == 5, "a second tap starts it and takes its price");
  }

  // ---- GIVE UP asks twice ---------------------------------------------------------
  {
    tapRect(R_GIVEUP, 0);
    ck(gAdv.phase == ADV_LOAN, "one tap on GIVE UP only asks");
    ck(flushes(), "the asking flushes");
    emuAdvanceMs(4000);
    tapRect(R_GIVEUP, 0);
    ck(gAdv.phase == ADV_LOAN, "a second tap after the question lapsed asks again");
    emuAdvanceMs(300);
    tapRect(R_GIVEUP, 0);
    ck(gAdv.phase == ADV_KEEP && !gAdv.won, "two taps in a row give up");
    tapRect(R_KEEP, 0);   // with nothing caught, row 0 is NOTHING
    tapRect(R_TWO, 0);
    ck(gAdv.phase == ADV_IDLE && forkData.st.coins == 5, "giving up at the loan ends it with nothing paid");
  }

  // ---- a run survives the app being killed -------------------------------------------
  {
    forkData.st.coins = ADV_RUN_PRICE;
    emuAdvanceMs(1000);
    tapRect(R_START, 0);
    tapRect(R_START, 0);
    tapRect(R_LOAN, 0);
    tapRect(R_DOOR, 0);
    tapRect(R_MOVE, 1);
    const uint16_t turn = gAdv.battle.turn;
    const uint16_t hp = gAdv.battle.boss.mon.hp;
    gAdv = AdvRun();
    setup();   // a cold start, as the phone's relaunch
    ck(gAdv.phase == ADV_FIGHT && gAdv.battle.turn == turn && gAdv.battle.boss.mon.hp == hp,
       "a relaunch resumes the fight on the same turn, the boss as it was");
    exploreOpen = true;
    forkAdvMode = true;
    ck(flushes(), "and draws it");
  }

  printf("%s\n", bad ? "SOME CHECKS FAILED" : "all passed");
  return bad ? 1 : 0;
}
