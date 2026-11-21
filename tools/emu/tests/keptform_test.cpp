// Adventures, step 12f part 1: a kept form DRAWN at home (android/FEATURES_PLAN.md).
//
// A kept fusion (Kyurem-Black) must look like itself on the party, the box's
// farm, the party sheet, the squad picker, the tabs and the main screen; a kept
// Mega is only permission to transform and must look like its species; and a
// creature with no kept form must draw exactly as it always did.
//
// Pixels, not pointers (CLAUDE.md § 3): sprite_test once asserted the dex it
// ASKED for and passed with the bug in. So the thumbnails are compared by their
// bytes, and the screens by whole frames rendered through the real sketch --
// the CALLERS, which is where trap 1 lives, not just creatureThumb().
//
// Needs formthumbs.bin beside the sprites: `python3 tools/pack_forms.py --unpack`.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "dex.h"
#include "fork.h"
#include "fork_adv.h"
#include "sdmon.h"
#include <string.h>
#include <vector>

uint32_t g_seed = 0x12F1;
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
void ensureMon();
extern Pet pet;
extern PmdMon pmd;
extern Arduino_Canvas *gfx;
extern int16_t gPanelH, gInsetTop, gInsetBot, gGfxOriginY, gFrameSquareY;
extern bool gPanelRound;
extern bool partyOpen, pickOpen, pickHard;
extern uint8_t partyDetail, pickTrainer, pickPage;
#define PICK_LAN_ID 0xFF   // the sketch's PICK_LAN: the uncapped picker, party slots and all
bool forkFarmShowing();
extern void (*gHostSaveBackup)();
extern void (*gHostSaveRestore)();
const uint8_t *creatureThumb(int16_t dex, uint32_t key);
const uint8_t *monThumb(const PartyMon &m);
const uint8_t *petThumb();
uint32_t monKey(const PartyMon &m);
void emuAdvanceMs(uint32_t ms);
void emuSetClockPaused(bool paused);

static const int H = 1010;
static int fails = 0;
#define CHECK(c, ...) do { if (c) { printf("ok   "); printf(__VA_ARGS__); printf("\n"); } \
                           else { printf("FAIL "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static void step(int n) { for (int i = 0; i < n; i++) { emuAdvanceMs(20); loop(); } }
static void tap(int x, int y) {
  g_touchX = x; g_touchY = y; g_touchDown = true; emuFireInterrupt(); step(3);
  g_touchDown = false; emuFireInterrupt(); step(3);
}
static void fakeBackup() {}

static std::vector<uint16_t> frame() {
  render();
  const uint16_t *b = gfx->buffer();
  return std::vector<uint16_t>(b, b + 466 * H);
}

// A thumbnail blob's bytes: w, h, palette, pixels.
static size_t thumbLen(const uint8_t *t) { return 3 + t[2] * 2 + (size_t)t[0] * t[1]; }
static bool sameThumb(const uint8_t *a, const uint8_t *b) {
  if (!a || !b) return a == b;
  return thumbLen(a) == thumbLen(b) && !memcmp(a, b, thumbLen(a));
}

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
  return m;
}

int main() {
  const uint8_t black = rowOf(646, "BLACK"), megaX = rowOf(6, "MEGA X"), ultra = rowOf(800, "ULTRA");
  CHECK(black && forkFormUsable(black) && megaX && forkFormUsable(megaX) && ultra,
        "the rows this test needs exist (Kyurem-Black %u, Mega Charizard X %u, Ultra Necrozma %u)",
        black, megaX, ultra);

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
  forkKeptClear();

  const PartyMon kyB = mon(646, 3), kyPlain = mon(646, 4), zard = mon(6, 5);
  const uint32_t kB = monKey(kyB), kPlain = monKey(kyPlain), kZard = monKey(zard);
  CHECK(thumbs.get(646) && thumbs.get(646)[0] && thumbs.get(6) && thumbs.get(6)[0],
        "the species thumbnails are there to compare against");

  // ---- the rule, as pixels ----------------------------------------------------
  CHECK(sameThumb(monThumb(kyB), thumbs.get(646)), "nothing kept: Kyurem draws as Kyurem");
  CHECK(forkKeepForm(kB, 646, black), "keep Kyurem-Black on one Kyurem");
  CHECK(!sameThumb(monThumb(kyB), thumbs.get(646)) && monThumb(kyB)[0],
        "the kept Kyurem-Black's thumbnail is not plain Kyurem's");
  const uint8_t *blackThumb = monThumb(kyB);   // the form's own blob, to compare the pet against
  CHECK(sameThumb(monThumb(kyPlain), thumbs.get(646)),
        "another Kyurem (other IVs, no kept form) still draws as Kyurem");
  CHECK(sameThumb(creatureThumb(25, kB), thumbs.get(25)),
        "the same key on another species is not that creature");
  CHECK(forkKeepForm(kZard, 6, megaX), "keep Mega X on a Charizard");
  CHECK(sameThumb(monThumb(zard), thumbs.get(6)), "a kept MEGA draws as its species: it is a Mega only mid-fight");
  CHECK(forkDrawnForm(kZard, 6) == 0 && forkDrawnForm(kB, 646) == black,
        "forkDrawnForm(): the fusion, never the state");
  {
    const PartyMon nec = mon(800, 6);
    CHECK(forkKeepForm(monKey(nec), 800, ultra) && sameThumb(monThumb(nec), thumbs.get(800)),
          "Ultra Necrozma is a state (forkFormIsState), so it draws as Necrozma too");
  }
  forkSetFeatures(false);
  CHECK(sameThumb(monThumb(kyB), thumbs.get(646)) && forkDrawnForm(kB, 646) == 0,
        "switch off (the board): the kept record changes nothing");
  forkSetFeatures(true);

  // ---- the party screen, through the sketch ----------------------------------
  party.slots[0] = kyB;
  party.slots[1] = zard;
  partyOpen = true;
  partyDetail = 0;
  step(2);
  const std::vector<uint16_t> withB = frame();
  forkKeptClear();
  const std::vector<uint16_t> without = frame();
  CHECK(withB != without, "the party grid draws the kept Kyurem-Black differently from plain Kyurem");
  CHECK(forkKeepForm(kZard, 6, megaX), "Mega X kept again on the Charizard");
  CHECK(frame() == without, "a kept Mega leaves the party grid exactly as it was");
  CHECK(forkKeepForm(kPlain, 646, black), "a Kyurem NOT in the party is Black");
  CHECK(frame() == without, "and the party, which does not hold that one, is unchanged");
  forkKeptClear();

  // ---- the party sheet -------------------------------------------------------
  partyDetail = 1;
  step(2);
  const std::vector<uint16_t> sheetPlain = frame();
  CHECK(forkKeepForm(kB, 646, black), "Kyurem-Black kept again");
  CHECK(frame() != sheetPlain, "the party sheet draws the kept fusion");
  partyDetail = 0;
  step(2);

  // ---- the farm (the party screen's FARM tab) ---------------------------------
  tap(466 * 3 / 4, gInsetTop + 22 + 10);
  CHECK(partyOpen && forkFarmShowing(), "the FARM tab is open");
  {
    const std::vector<uint16_t> farmB = frame();
    forkKeptClear();
    CHECK(frame() != farmB, "the farm draws the kept fusion");
    CHECK(forkKeepForm(kB, 646, black), "Kyurem-Black kept again");
  }
  tap(466 / 4, gInsetTop + 22 + 10);
  partyOpen = false;
  step(2);

  // ---- the squad picker ----------------------------------------------------------
  pickTrainer = PICK_LAN_ID;
  pickHard = false;
  pickPage = 0;
  pickOpen = true;
  step(2);
  {
    const std::vector<uint16_t> pickB = frame();
    forkKeptClear();
    CHECK(frame() != pickB, "the squad picker draws the kept fusion");
    CHECK(forkKeepForm(kB, 646, black), "Kyurem-Black kept again");
  }
  pickOpen = false;
  step(2);

  // ---- the live pet: the main screen's PMD sprite and its tab thumbnail ------
  pet.dbgHatchAs(646, false);
  while (pet.hasLearnOffer()) pet.declineLearn();
  pet.ivAtk = kyPlain.ivAtk; pet.ivDef = kyPlain.ivDef; pet.ivSpe = kyPlain.ivSpe; pet.ivHp = kyPlain.ivHp;
  pet.shiny = false;
  forkKeptClear();
  ensureMon();
  CHECK(pmd.loaded && pmd.form == 0 && pmd.dex == 646, "a plain Kyurem as the pet: the species' sprite");
  CHECK(sameThumb(petThumb(), thumbs.get(646)), "and its thumbnail is Kyurem's");
  CHECK(forkKeepForm(forkCreatureKey(pet), 646, black), "now it is a kept Kyurem-Black");
  ensureMon();
  CHECK(pmd.loaded && pmd.form == FORM_TBL[black].sub, "ensureMon() reloads it as the form (sub %u)", pmd.form);
  CHECK(sameThumb(petThumb(), blackThumb) && !sameThumb(petThumb(), thumbs.get(646)),
        "and the pet's thumbnail is Kyurem-Black's");
  {
    PmdMon plain;
    CHECK(plain.load(646, false) && pmd.palCount && pmd.acts[PMD_IDLE].data && plain.acts[PMD_IDLE].data &&
          !(pmd.acts[PMD_IDLE].w == plain.acts[PMD_IDLE].w && pmd.acts[PMD_IDLE].h == plain.acts[PMD_IDLE].h &&
            !memcmp(pmd.acts[PMD_IDLE].data, plain.acts[PMD_IDLE].data,
                    (size_t)plain.acts[PMD_IDLE].w * plain.acts[PMD_IDLE].h)),
          "the loaded idle frame is not plain Kyurem's pixels");
  }
  forkKeptClear();
  ensureMon();
  CHECK(pmd.loaded && pmd.form == 0, "the record gone: back to the species' sprite");

  printf("%s (%d failed)\n", fails ? "FAIL" : "PASS", fails);
  return fails ? 1 : 0;
}
