// Adventures, step 12f part 2: a kept form FIGHTING at home (android/FEATURES_PLAN.md).
//
// A kept fusion (Kyurem-Black) enters a gym or wild fight as itself, with its
// own stats and typing. A kept Mega (Charizard X) enters as its species and may
// TRANSFORM once a battle from the move screen, before anyone acts. It is plain
// again in the next fight, hard gym fights never offer it, and a LAN squad
// goes out as plain species because the peer may be a board.
//
// Through the CALLERS (CLAUDE.md trap 1, "test the CALLER"): the squads come
// out of the real startTrainerBattle() / startWildBattle() / the LAN picker,
// and the transform is tapped on the real battle screen, not called directly.
//
// Needs the forms pack beside the sprites: `python3 tools/pack_forms.py --unpack`.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "battle.h"
#include "link.h"
#include "dex.h"
#include "fork.h"
#include "fork_adv.h"
#include "sdmon.h"
#include <string.h>
#include <vector>

uint32_t g_seed = 0x12F2;
FakeSerial Serial;
FakeESP ESP;
FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0;
volatile bool g_touchDown = false;
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }

void setup();
void loop();
void render();
void battleTap(int16_t x, int16_t y);
void pickTap(int16_t x, int16_t y);
void pickDefault(uint8_t cap);
uint8_t squadCap(uint8_t, bool);
void startTrainerBattle(uint8_t idx, bool hard);
bool startWildBattle(uint8_t region, bool hard);
bool btlCanTransform();
void uiBtlCellRect(int i, int *x, int *y, int *w, int *h);
void uiBtlBackRect(int *x, int *y, int *w, int *h);
void uiBtlFormRect(int *x, int *y, int *w, int *h);
void uiPickButtonRect(bool go, int *x, int *y, int *w, int *h);
uint32_t monKey(const PartyMon &m);
void emuAdvanceMs(uint32_t ms);
void emuSetClockPaused(bool paused);
extern Pet pet;
extern Arduino_Canvas *gfx;
extern int16_t gPanelH, gInsetTop, gInsetBot, gGfxOriginY, gFrameSquareY;
extern bool gPanelRound;
extern bool battleOpen, btlOver, btlFormUsed, btlFormArmed, pickOpen, pickHard, lanOpen, lanWantHost;
extern Combatant btlYou, btlSquad[];
extern uint8_t btlSquadN, btlSquadAt, btlMenu, btlMsgCount, pickTrainer, pickPage, gymRegion;
extern uint16_t squadMask;
extern PmdMon btlPmd[2];
extern char btlMsg[6][40];
extern Link lan;
#define PICK_LAN_ID 0xFF

static const int H = 1010;
static int fails = 0;
#define CHECK(c, ...) do { if (c) { printf("ok   "); printf(__VA_ARGS__); printf("\n"); } \
                           else { printf("FAIL "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static void step(int n) { for (int i = 0; i < n; i++) { emuAdvanceMs(20); loop(); } }
static void fakeBackup() {}
extern void (*gHostSaveBackup)();
extern void (*gHostSaveRestore)();

static uint8_t rowOf(int16_t dex, const char *label) {
  for (uint16_t id = 1; id <= FORM_COUNT; id++)
    if (FORM_TBL[id].dex == dex && !strcmp(FORM_TBL[id].label, label)) return (uint8_t)id;
  return 0;
}

static PartyMon mon(int16_t dex, uint8_t ivAtk) {
  PartyMon m;
  m.dex = dex;
  m.level = 50;
  m.ivAtk = ivAtk; m.ivDef = 9; m.ivSpe = 12; m.ivHp = 20;
  m.stateVersion = 1;
  Pet p;   // its real learnset at its level, so the move grid has moves to tap
  p.dbgHatchAs(dex, false);
  p.ageMinutes = (uint32_t)(m.level - 1) * MINUTES_PER_LEVEL;
  p.relearnFromLevel();
  for (int i = 0; i < MOVE_SLOTS; i++) m.moves[i] = p.moves[i];
  return m;
}

// The same creature built plain, at the level the fight caps it to.
static Combatant plainOf(PartyMon m, uint8_t cap) {
  if (cap && m.level > cap) m.level = cap;
  Combatant c;
  combatantFromParty(c, m);
  return c;
}

static bool sameStats(const Combatant &a, const Combatant &b) {
  return a.maxHp == b.maxHp && !memcmp(a.base, b.base, sizeof(a.base));
}

static void tapRect(void (*rect)(int, int *, int *, int *, int *), int i) {
  int x, y, w, h;
  rect(i, &x, &y, &w, &h);
  battleTap(x + w / 2, y + h / 2);
}
static void tapCell(int i) { tapRect(uiBtlCellRect, i); }
static bool formRectHit(int x, int y) {
  int fx, fy, fw, fh;
  uiBtlFormRect(&fx, &fy, &fw, &fh);
  return x >= fx && x <= fx + fw && y >= fy && y <= fy + fh;
}
static void tapForm() {
  int x, y, w, h;
  uiBtlFormRect(&x, &y, &w, &h);
  battleTap(x + w / 2, y + h / 2);
}
static void drain() {   // page every queued line, as a player's taps would
  for (int i = 0; i < 12 && btlMsgCount && battleOpen; i++) { battleTap(233, 600); step(1); }
}
static void endFight() {
  battleOpen = false;
  btlOver = false;
  btlMsgCount = 0;
  step(2);
}
static std::vector<uint16_t> frame() {
  render();
  const uint16_t *b = gfx->buffer();
  return std::vector<uint16_t>(b, b + 466 * H);
}

int main() {
  const uint8_t black = rowOf(646, "BLACK"), megaX = rowOf(6, "MEGA X"), megaY = rowOf(6, "G-MAX");
  CHECK(black && forkFormUsable(black) && megaX && forkFormUsable(megaX) && megaY && forkFormUsable(megaY),
        "the rows this test needs exist (Kyurem-Black %u, Mega Charizard X %u, G-Max %u)", black, megaX, megaY);

  gRegionArt = 0xFFFF;
  gHostSaveBackup = fakeBackup;
  gHostSaveRestore = fakeBackup;
  gfx->resize(466, H);
  gPanelH = H;
  gPanelRound = false;
  gInsetTop = 42;
  gInsetBot = 0;
  gGfxOriginY = gFrameSquareY = (H - 466) / 2;
  forkSetFeatures(true);
  setup();
  emuSetClockPaused(true);
  step(3);
  if (pet.awaitingStarter()) pet.chooseStarter(0);
  pet.dbgHatchAs(25, false);   // a live pet that can fight; the squads below leave it out
  while (pet.hasLearnOffer()) pet.declineLearn();
  forkKeptClear();
  gymRegion = 0;

  // party: 0 Kyurem kept Black, 1 Charizard kept Mega X, 2 a plain Kyurem,
  // 3 another Charizard kept G-Max
  const PartyMon kyB = mon(646, 3), zardX = mon(6, 5), kyPlain = mon(646, 4), zardY = mon(6, 7);
  for (int i = 0; i < PARTY_SLOTS; i++) party.slots[i] = PartyMon();
  party.slots[0] = kyB; party.slots[1] = zardX; party.slots[2] = kyPlain; party.slots[3] = zardY;
  CHECK(forkKeepForm(monKey(kyB), 646, black) && forkKeepForm(monKey(zardX), 6, megaX) &&
        forkKeepForm(monKey(zardY), 6, megaY), "kept: Kyurem-Black, Mega X, G-Max");
  uint8_t top = 0;   // Brock's best: both ladders cap your level to it
  {
    const Trainer &tr = TRAINER_SETS[0].list[0];
    for (int k = 0; k < tr.count; k++) if (tr.team[k].level > top) top = tr.team[k].level;
  }

  // ---- a fusion enters a gym fight as itself ------------------------------------
  squadMask = (1 << 1) | (1 << 3);   // party slots 0 and 2: Black and the plain Kyurem
  startTrainerBattle(0, false);
  CHECK(battleOpen && btlSquadN == 2, "a gym fight opens with the two Kyurem");
  {
    const Combatant &b = btlSquad[0], &p = btlSquad[1];
    Combatant want = plainOf(kyB, top);
    CHECK(b.typed && b.type1 == FORM_TBL[black].type1 && b.type2 == FORM_TBL[black].type2 &&
          b.type1 == T_DRAGON && b.type2 == T_ICE, "the kept Black fights as DRAGON/ICE");
    CHECK(b.form == black, "and is IN the form (so its sprite follows)");
    forkApplyForm(want, black);
    CHECK(sameStats(b, want) && !sameStats(b, plainOf(kyB, top)),
          "with Black's stats at the capped level (ATK %u, plain %u)", b.base[SI_ATK], plainOf(kyB, top).base[SI_ATK]);
    CHECK(!p.typed && !p.form && sameStats(p, plainOf(kyPlain, top)),
          "the Kyurem with nothing kept fights exactly as before");
    CHECK(!b.canForm && !btlCanTransform(), "a fusion is never offered TRANSFORM");
    CHECK(btlPmd[0].loaded && btlPmd[0].form == FORM_TBL[black].sub,
          "the battle sprite is Kyurem-Black's (sub %u)", btlPmd[0].form);
  }
  endFight();

  // ---- a kept Mega: TRANSFORM once a battle --------------------------------------
  squadMask = (1 << 2) | (1 << 4);   // party slots 1 and 3: the two Charizard
  startTrainerBattle(0, false);
  CHECK(battleOpen && btlSquadN == 2, "a gym fight opens with the two Charizard");
  const Combatant plainX = plainOf(zardX, top);
  CHECK(btlYou.dex == 6 && !btlYou.typed && !btlYou.form && sameStats(btlYou, plainX),
        "the Mega X creature enters as plain CHARIZARD");
  CHECK(btlYou.canForm == megaX && btlCanTransform(), "and may transform");
  int bx, by, bw, bh;
  uiBtlBackRect(&bx, &by, &bw, &bh);
  const int fullBackW = bw;
  {
    // on the main menu nothing is split
    CHECK(btlMenu == 0, "the fight opens on FIGHT/BAG/POKEMON/RUN");
    tapCell(0);
    CHECK(btlMenu == 1 && !btlFormArmed, "FIGHT opens the moves, disarmed");
    uiBtlBackRect(&bx, &by, &bw, &bh);
    int fx, fy, fw, fh;
    uiBtlFormRect(&fx, &fy, &fw, &fh);
    CHECK(bw < fullBackW && fx > bx + bw && fw >= 100 && fh >= 44,
          "the move screen splits the BACK bar: BACK %d wide, TRANSFORM %dx%d beside it", bw, fw, fh);
    const std::vector<uint16_t> off = frame();
    tapForm();
    CHECK(btlMenu == 1 && btlFormArmed, "tapping TRANSFORM arms it and stays on the moves");
    CHECK(frame() != off, "and the armed toggle is drawn differently");
    tapForm();
    CHECK(!btlFormArmed, "a second tap disarms it");
    tapForm();
    tapCell(0);   // the first move, with the toggle armed
    CHECK(btlYou.form == megaX && btlYou.typed && btlYou.type1 == T_FIRE && btlYou.type2 == T_DRAGON,
          "the move transformed it first: FIRE/DRAGON");
    Combatant want = plainX;
    forkApplyForm(want, megaX);
    CHECK(btlYou.maxHp == want.maxHp && btlYou.base[SI_ATK] == want.base[SI_ATK] &&
          btlYou.base[SI_SPE] == want.base[SI_SPE], "with Mega X's stats (ATK %u, was %u)",
          btlYou.base[SI_ATK], plainX.base[SI_ATK]);
    CHECK(btlFormUsed && !btlFormArmed && !btlCanTransform(), "and the battle's one TRANSFORM is spent");
    CHECK(btlMsgCount && strstr(btlMsg[0], FORM_TBL[megaX].label) && strstr(btlMsg[0], btlYou.name),
          "and says so first: \"%s\"", btlMsg[0]);
    CHECK(btlPmd[0].loaded && btlPmd[0].form == FORM_TBL[megaX].sub,
          "the battle sprite is Mega Charizard X's (sub %u)", btlPmd[0].form);
  }
  drain();
  if (battleOpen && !btlOver && btlYou.form == megaX && !btlYou.fainted()) {
    tapCell(0);
    CHECK(btlMenu == 1, "FIGHT again");
    uiBtlBackRect(&bx, &by, &bw, &bh);
    CHECK(bw == fullBackW, "no TRANSFORM now: BACK is whole again");
    const uint8_t at = btlSquadAt;
    tapCell(0);
    CHECK(btlSquadAt != at || btlYou.form == megaX, "still Mega X after another turn, never transformed twice");
    drain();
  } else {
    CHECK(false, "the Charizard should still be standing after one turn against Brock");
  }
  // the OTHER Charizard: a second Mega in the same battle is not allowed
  if (battleOpen && !btlOver) {
    btlSquad[1].hp = btlSquad[1].maxHp;   // whatever happened, it is up
    btlMenu = 2;
    tapCell(1);   // switch to party slot 3's Charizard (squad index 1)
    drain();
    CHECK(btlSquadAt == 1 && btlYou.canForm == megaY, "switched to the G-Max Charizard");
    CHECK(!btlCanTransform(), "and it may NOT transform: one a battle, already spent");
    btlMenu = 2;
    tapCell(0);
    drain();
    CHECK(btlSquadAt == 0 && btlYou.form == megaX, "the first comes back still Mega X: it lasts the battle");
  }
  endFight();

  // ---- the next fight: plain again ------------------------------------------------
  startTrainerBattle(0, false);
  CHECK(battleOpen && btlYou.dex == 6 && !btlYou.form && !btlYou.typed && sameStats(btlYou, plainX),
        "the next fight: plain CHARIZARD again, nothing written back");
  CHECK(!btlFormUsed && btlCanTransform(), "with its TRANSFORM back");
  CHECK(party.slots[1].dex == 6 && forkKeptForm(monKey(party.slots[1]), 6) == megaX,
        "the stored creature still carries its kept Mega");
  endFight();

  // ---- HARD gym fights: never --------------------------------------------------------
  startTrainerBattle(0, true);
  CHECK(battleOpen && btlYou.canForm == megaX, "a HARD gym fight: the Mega X Charizard is in it");
  CHECK(!btlCanTransform(), "and may not transform there");
  tapCell(0);
  uiBtlBackRect(&bx, &by, &bw, &bh);
  CHECK(btlMenu == 1 && bw == fullBackW, "the move screen shows no TRANSFORM in hard mode");
  {
    int fx, fy, fw, fh;
    uiBtlFormRect(&fx, &fy, &fw, &fh);
    battleTap(bx + bw - 4, by + bh / 2);   // the right end of the whole BACK bar
    CHECK(!btlFormArmed && btlMenu == 0, "where the toggle would be is BACK, not a hidden toggle");
  }
  endFight();

  // ---- wild fights: yes, hard included -------------------------------------------------
  CHECK(startWildBattle(0, false) && btlCanTransform(), "a wild fight offers TRANSFORM");
  endFight();
  CHECK(startWildBattle(0, true) && btlCanTransform(), "a hard wild fight does too (only hard GYMS refuse)");
  endFight();

  // ---- a LAN squad goes out plain ------------------------------------------------------
  {
    squadMask = (1 << 1) | (1 << 2);   // Black and the Mega X Charizard
    lanWantHost = true;
    pickTrainer = PICK_LAN_ID; pickHard = false; pickPage = 0;
    pickOpen = true;
    step(2);
    int x, y, w, h;
    uiPickButtonRect(true, &x, &y, &w, &h);
    pickTap(x + w / 2, y + h / 2);
    CHECK(!pickOpen && lanOpen && lan.mineN == 2, "the LAN picker offers the two chosen");
    Combatant ky, zd;
    linkMonTo(ky, lan.mine[0]);
    linkMonTo(zd, lan.mine[1]);
    CHECK(ky.dex == 646 && sameStats(ky, plainOf(kyB, 0)),
          "the kept Kyurem-Black goes out with plain Kyurem's stats (ATK %u)", ky.base[SI_ATK]);
    CHECK(zd.dex == 6 && sameStats(zd, plainOf(zardX, 0)), "and the Mega X Charizard as plain Charizard");
    lanOpen = false;
    step(2);
  }

  // ---- the switch off (the board): nothing ---------------------------------------------
  forkSetFeatures(false);
  squadMask = (1 << 1) | (1 << 2);
  startTrainerBattle(0, false);
  CHECK(battleOpen && !btlSquad[0].typed && !btlSquad[0].form && sameStats(btlSquad[0], plainOf(kyB, top)),
        "switch off: the kept Black fights as plain Kyurem");
  CHECK(!btlSquad[1].canForm && !btlCanTransform(), "and nothing may transform");
  endFight();
  forkSetFeatures(true);

  printf("%s (%d failed)\n", fails ? "FAIL" : "PASS", fails);
  return fails ? 1 : 0;
}
