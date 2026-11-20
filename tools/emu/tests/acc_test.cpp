// Accessories (fork.h section 14; android/FEATURES_PLAN.md Phase 10): the
// record and its prefix rule, every source that pays one, wearing belonging to
// ONE creature, the head anchor drawPmdActFit() notes, the drawing on the pet
// screen and the card, and the wardrobe page's taps -- the last three through
// the REAL sketch at the phone's canvas.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "dex.h"
#include "fork.h"
#include "fork_i18n.h"
#include "fork_acc_art.h"
#include "ckpt.h"
#include "sdmon.h"
#include <string.h>
#include <vector>

uint32_t g_seed = 0xACC;
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
extern Pet pet;
extern Arduino_Canvas *gfx;
extern int16_t gPanelH, gInsetTop, gInsetBot, gGfxOriginY, gFrameSquareY;
extern bool gPanelRound, cardOpen;
extern uint8_t cardPage;
extern void (*gHostSaveBackup)();
extern void (*gHostSaveRestore)();
uint8_t uiCurrentScreen();
extern const char *const SCREEN_NAME[];
int uiSceneOy();
void emuAdvanceMs(uint32_t ms);
void emuSetClockPaused(bool paused);
void rtcSetEpoch(uint32_t e);
uint32_t rtcEpoch();
int uiStackPer();
int uiStackOy(int k, int per);
uint8_t uiWardrobeAt(int x, int y);
void uiWardrobeCenter(uint8_t a, int *x, int *y);
extern PmdMon pmd;
uint8_t uiCardWardrobePage();

static const int H = 1010;
static int fails = 0;
#define CHECK(c, ...) do { if (c) { printf("ok   "); printf(__VA_ARGS__); printf("\n"); } \
                           else { printf("FAIL "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static void step(int n, uint32_t ms = 20) {
  for (int i = 0; i < n; i++) { emuAdvanceMs(ms); loop(); }
}
static void down(int x, int y) { g_touchX = x; g_touchY = y; g_touchDown = true; emuFireInterrupt(); step(3); }
static void up() { g_touchDown = false; emuFireInterrupt(); step(3); }
static void tap(int x, int y) { down(x, y); up(); }
static void fakeBackup() {}

static uint32_t localEpoch(int y, int m, int d, int hh) {
  return forkDayNumber((uint16_t)y, (uint8_t)m, (uint8_t)d) * 86400u + hh * 3600u;
}
static bool bannerSays(const char *l2) {
  const char *a, *b;
  return forkBanner(&a, &b) && !strcmp(a, FT(FS_ACC_NEW)) && !strcmp(b, l2);
}
static void resetAcc() {
  forkData.st.accOwned = 0;
  forkData.st.accWorn = 0;
  forkData.st.accKey = 0;
  forkBannerClear();
}

static std::vector<uint16_t> frame() {
  render();
  const uint16_t *b = gfx->buffer();
  return std::vector<uint16_t>(b, b + 466 * H);
}
struct Box { int x0 = 9999, y0 = 9999, x1 = -1, y1 = -1, n = 0; };
static Box diff(const std::vector<uint16_t> &a, const std::vector<uint16_t> &b) {
  Box r;
  for (int y = 0; y < H; y++)
    for (int x = 0; x < 466; x++)
      if (a[y * 466 + x] != b[y * 466 + x]) {
        r.n++;
        if (x < r.x0) r.x0 = x;
        if (x > r.x1) r.x1 = x;
        if (y < r.y0) r.y0 = y;
        if (y > r.y1) r.y1 = y;
      }
  return r;
}

int main() {
  // ===================== the art and the strings =====================
  {
    int bad = 0;
    for (uint8_t a = 1; a < FORK_ACC_COUNT; a++) {
      const ForkAccArt &art = FORK_ACC_ART[a];
      if (!art.rows || !art.w || !art.h || art.overlap >= art.h) { bad++; continue; }
      for (int r = 0; r < art.h; r++) {
        if ((int)strlen(art.rows[r]) != art.w) { printf("  acc %u row %d is %d wide, not %d\n", a, r, (int)strlen(art.rows[r]), art.w); bad++; }
        for (int c = 0; c < art.w && art.rows[r][c]; c++) {
          const char ch = art.rows[r][c];
          bool known = ch == '.';
          for (const ForkAccInk &k : FORK_ACC_PAL) known |= k.k == ch;
          if (!known) { printf("  acc %u has '%c', not in the palette\n", a, ch); bad++; }
        }
      }
    }
    CHECK(bad == 0, "every map is as wide as it says on every row, and uses only palette letters");
    CHECK(sizeof(FORK_ACC_ART) / sizeof(FORK_ACC_ART[0]) == FORK_ACC_COUNT,
          "one map per accessory, so a new one cannot draw its neighbour's");
  }
  {
    int problems = 0;
    for (uint8_t l = 0; l < LANG_COUNT; l++) {
      setLang((Lang)l);
      for (uint8_t a = 1; a < FORK_ACC_COUNT; a++) {
        char line[64];
        snprintf(line, sizeof(line), FT(FS_ACC_WEARING_FMT), forkAccName(a));
        if (!*forkAccName(a) || strlen(forkAccName(a)) > 16 || !*forkAccHint(a) ||
            strlen(forkAccHint(a)) > 24 || strlen(line) > 24) {
          printf("  lang %u acc %u: '%s' / '%s' / '%s'\n", l, a, forkAccName(a), forkAccHint(a), line);
          problems++;
        }
      }
      if (strlen(FT(FS_ACC_NEW)) > 23 || strlen(FT(FS_WARDROBE)) > 16 || strlen(FT(FS_ACC_TAP)) > 24) problems++;
    }
    setLang(LANG_EN);
    CHECK(problems == 0, "every name, hint and wearing line is present and fits, in all seven languages");
  }
  CHECK(forkAccForSite(FORK_EXP_COAST) == FORK_ACC_BOW && forkAccForSite(FORK_EXP_FOREST) == FORK_ACC_FLOWER &&
        forkAccForSite(FORK_EXP_VOLCANO) == FORK_ACC_GOGGLES && forkAccForSite(FORK_EXP_SITES) == FORK_ACC_NONE,
        "each site finds its own accessory, and a site that is not one finds none");

  // ===================== the head anchor (pure) =====================
  {
    // a 10x10 frame: one stray pixel at row 2, the head's band rows 3-4 at
    // columns 3..6, the body below
    uint8_t f[100];
    memset(f, 0xFF, sizeof(f));
    f[2 * 10 + 5] = 1;
    for (int c = 3; c <= 6; c++) f[3 * 10 + c] = 1;
    for (int r = 5; r < 10; r++) for (int c = 1; c <= 8; c++) f[r * 10 + c] = 1;
    forkHeadNote(f, 10, 10, 100, 200, 4);
    const ForkHead h = forkHeadLast();
    CHECK(h.ok && h.s == 4 && h.y == 200 + 2 * 4, "the head's top is the first opaque row, at the frame's scale (y %d)", h.y);
    CHECK(h.left == 100 + 3 * 4 && h.right == 100 + 7 * 4 && h.x == 100 + 5 * 4,
          "its span is the band below it, so one stray pixel does not decide it alone (%d..%d)", h.left, h.right);
    memset(f, 0xFF, sizeof(f));
    forkHeadNote(f, 10, 10, 100, 200, 4);
    CHECK(!forkHeadLast().ok, "an empty frame has no head, rather than one at its top");
  }

  // ===================== the board: none of it =====================
  CHECK(!forkAccGive(FORK_ACC_CROWN) && !forkAccOwned(FORK_ACC_CROWN),
        "switch off: nothing is given");

  // ===================== the record =====================
  {
    ForkState s;
    s.accOwned = (1u << (FORK_ACC_CROWN - 1)) | (1u << (FORK_ACC_BOW - 1));
    s.accWorn = FORK_ACC_CROWN;
    s.accKey = 0x80012345u;
    uint8_t buf[FORK_BLOB_CAP];
    uint16_t n = Fork::encode(s, buf, sizeof(buf), 3);
    ForkState back;
    CHECK(n && Fork::decode(buf, n, back) && back.accOwned == s.accOwned && back.accWorn == FORK_ACC_CROWN &&
          back.accKey == s.accKey, "the wardrobe and what is worn, by whom, survive the record");
    // A record written before Phase 10: its body stops at the weather.
    const uint16_t old = (uint16_t)ckptSeal(buf, CKPT_HDR + FORK_OFF_ACC_OWNED, FORK_MAGIC, FORK_VERSION, 3);
    CHECK(Fork::decode(buf, old, back) && back.accOwned == 0 && back.accWorn == 0 && back.accKey == 0,
          "a record from before accessories reads as an empty wardrobe");
    s.accWorn = FORK_ACC_SANTA;   // not owned
    n = Fork::encode(s, buf, sizeof(buf), 4);
    CHECK(Fork::decode(buf, n, back) && back.accWorn == 0 && back.accKey == 0 && back.accOwned == s.accOwned,
          "wearing one that is not owned reads as wearing nothing, and keeps the wardrobe");
    s.accWorn = FORK_ACC_COUNT;   // a later build's
    s.accOwned = 0xFFFF;
    n = Fork::encode(s, buf, sizeof(buf), 5);
    CHECK(Fork::decode(buf, n, back) && back.accWorn == 0 && back.accOwned == 0xFFFF,
          "one a later build added is not worn here, and its bit is kept for that build");
  }

  // ===================== the phone, through the sketch =====================
  forkSetFeatures(true);
  gHostSaveBackup = fakeBackup;
  gHostSaveRestore = fakeBackup;
  gfx->resize(466, H);
  gPanelH = H;
  gPanelRound = false;
  gInsetTop = 42;
  gInsetBot = 0;
  gGfxOriginY = gFrameSquareY = (H - 466) / 2;
  rtcSetEpoch(localEpoch(2026, 11, 20, 10));
  setup();
  emuSetClockPaused(true);
  step(3);
  if (pet.awaitingStarter()) pet.chooseStarter(4);
  pet.dbgHatchAs(4, false);   // CHARMANDER
  while (pet.hasLearnOffer()) pet.declineLearn();
  pet.sleeping = false;
  pet.fullness = pet.joy = pet.energy = pet.hygiene = 80;
  pet.lastSeenEpoch = rtcEpoch();
  step(3);
  resetAcc();

  // ---- giving ----
  CHECK(!forkAccOwned(FORK_ACC_CROWN) && forkAccGive(FORK_ACC_CROWN) && forkAccOwned(FORK_ACC_CROWN),
        "given, it is owned");
  CHECK(bannerSays(forkAccName(FORK_ACC_CROWN)), "and a banner names it");
  forkBannerClear();
  CHECK(!forkAccGive(FORK_ACC_CROWN), "never twice");
  {
    const char *a, *b;
    CHECK(!forkBanner(&a, &b), "and the second time says nothing");
  }
  CHECK(!forkAccGive(FORK_ACC_NONE) && !forkAccGive(FORK_ACC_COUNT), "NONE and past the end are not accessories");

  // ---- wearing belongs to one creature ----
  CHECK(!forkAccWear(pet, FORK_ACC_BOW) && forkAccWorn(pet) == FORK_ACC_NONE, "one not owned cannot be put on");
  CHECK(forkAccWear(pet, FORK_ACC_CROWN) && forkAccWorn(pet) == FORK_ACC_CROWN, "an owned one goes on");
  forkData.begin();
  CHECK(forkAccWorn(pet) == FORK_ACC_CROWN && forkAccOwned(FORK_ACC_CROWN), "and is still on after a reload");
  {
    const uint32_t key = forkCreatureKey(pet);
    pet.dbgHatchAs(7, false);
    if (forkCreatureKey(pet) == key) pet.dbgHatchAs(7, true);
    CHECK(forkCreatureKey(pet) != key && forkAccWorn(pet) == FORK_ACC_NONE,
          "the next creature starts bare: what was worn was the last one's");
    CHECK(forkAccOwned(FORK_ACC_CROWN), "and the crown is still the player's");
    CHECK(forkAccWear(pet, FORK_ACC_CROWN) && forkAccWorn(pet) == FORK_ACC_CROWN, "so this one can wear it");
    CHECK(forkAccWear(pet, FORK_ACC_NONE) && forkAccWorn(pet) == FORK_ACC_NONE, "and take it off");
  }

  // ---- the sources ----
  forkData.st.stepsTotal = 9999;
  forkData.st.stepRank = 0;
  forkAccPoll(pet);
  CHECK(!forkAccOwned(FORK_ACC_CAP), "the cap waits for the first trail rank (9999 steps)");
  forkData.st.stepsTotal = 10000;
  forkAccPoll(pet);
  CHECK(forkAccOwned(FORK_ACC_CAP), "and comes at 10000, including for a save that walked them already");
  {
    uint16_t have = pet.registeredCount();
    for (int16_t d = 1; d <= DEX_COUNT && pet.registeredCount() < FORK_ACC_CROWN_DEX - 1; d++) pet.dexReg[(d - 1) >> 3] |= (uint8_t)(1u << ((d - 1) & 7));
    resetAcc();
    forkAccPoll(pet);
    CHECK(pet.registeredCount() == FORK_ACC_CROWN_DEX - 1 && !forkAccOwned(FORK_ACC_CROWN),
          "the crown waits for %u in the Pokedex (has %u, started at %u)", FORK_ACC_CROWN_DEX,
          pet.registeredCount(), have);
    for (int16_t d = 1; d <= DEX_COUNT && pet.registeredCount() < FORK_ACC_CROWN_DEX; d++) pet.dexReg[(d - 1) >> 3] |= (uint8_t)(1u << ((d - 1) & 7));
    forkAccPoll(pet);
    CHECK(forkAccOwned(FORK_ACC_CROWN), "and comes with the %uth", FORK_ACC_CROWN_DEX);
  }
  {
    struct { int m, d; uint8_t acc; } H2[] = {
      { 10, 31, FORK_ACC_WITCH }, { 1, 1, FORK_ACC_TOPHAT }, { 12, 25, FORK_ACC_SANTA },
    };
    for (auto &h : H2) {
      resetAcc();
      forkData.st.giftYear = 0;
      forkData.st.giftMask = 0;
      forkHolidayPoll(pet, localEpoch(2027, h.m, h.d, 12));
      CHECK(forkAccOwned(h.acc) && forkData.st.accOwned == (1u << (h.acc - 1)),
            "%02d/%02d gives %s, and nothing else", h.d, h.m, forkAccName(h.acc));
    }
    resetAcc();
    forkData.st.giftYear = 0;
    forkData.st.giftMask = 0;
    forkHolidayPoll(pet, localEpoch(2027, 3, 3, 12));
    CHECK(forkData.st.accOwned == 0, "an ordinary day gives none");
  }
  {
    // Expeditions: the site's own, at its length's odds, and never anybody else's.
    resetAcc();
    int trips = 0, gotAt = -1;
    uint16_t other = 0;
    for (; trips < 400 && gotAt < 0; trips++) {
      pet.energy = 100;
      pet.sleeping = false;
      const uint32_t t0 = rtcEpoch() + trips * 7200u;
      if (!forkExpSend(pet, FORK_EXP_COAST, FORK_EXP_LENS - 1, t0)) break;
      forkExpPoll(pet, t0 + 3700);
      other |= forkData.st.accOwned & ~(1u << (FORK_ACC_BOW - 1));
      if (forkAccOwned(FORK_ACC_BOW)) gotAt = trips + 1;
    }
    CHECK(gotAt > 0, "a coast trip brings back the bow in time (after %d long trips, odds %u%%)", gotAt,
          FORK_ACC_EXP_PCT[FORK_EXP_LENS - 1]);
    CHECK(other == 0, "and the coast never finds anything but the bow");
    CHECK(FORK_ACC_EXP_PCT[0] < FORK_ACC_EXP_PCT[1] && FORK_ACC_EXP_PCT[1] < FORK_ACC_EXP_PCT[2],
          "a longer trip has better odds");
  }

  // ---- drawn on the head ----
  resetAcc();
  forkAccGive(FORK_ACC_CROWN);
  forkBannerClear();
  pet.dbgHatchAs(4, false);
  while (pet.hasLearnOffer()) pet.declineLearn();
  pet.sleeping = false;
  pet.fullness = pet.joy = pet.energy = pet.hygiene = 80;
  step(3);
  CHECK(!strcmp(SCREEN_NAME[uiCurrentScreen()], "main"), "on the pet screen");
  if (!pmd.loaded) {
    printf("SKIP the drawing checks: no PMD sprite (run tools/unpack_bundle.py)\n");
  } else {
    const std::vector<uint16_t> bare = frame();
    const ForkHead hb = forkHeadLast();
    forkAccWear(pet, FORK_ACC_CROWN);
    const std::vector<uint16_t> worn = frame();
    const ForkHead h = forkHeadLast();
    const Box b = diff(bare, worn);
    const ForkAccArt &art = FORK_ACC_ART[FORK_ACC_CROWN];
    CHECK(hb.ok && h.ok && h.y == hb.y && h.x == hb.x, "the pet screen notes its creature's head (at %d,%d, scale %u)",
          h.x, h.y, h.s);
    CHECK(b.n > 0, "wearing the crown changes the picture (%d pixels)", b.n);
    const int oy = uiSceneOy();   // the pet scene is drawn translated down by this
    CHECK(b.x0 >= h.x - art.w * h.s / 2 - 1 && b.x1 <= h.x + art.w * h.s / 2 + 1 &&
          b.y1 <= h.y + oy + art.overlap * h.s && b.y0 >= h.y + oy - art.h * h.s,
          "and only on the head: x %d..%d around %d, y %d..%d above %d", b.x0, b.x1, h.x, b.y0, b.y1, h.y + oy);
    forkAccWear(pet, FORK_ACC_NONE);
    CHECK(diff(bare, frame()).n == 0, "taken off, the picture is exactly the bare one again");
  }

  // ---- the wardrobe page ----
  forkAccWear(pet, FORK_ACC_NONE);
  cardOpen = true;
  cardPage = uiCardWardrobePage();
  step(3);
  {
    const int per = uiStackPer(), k = cardPage % per, oy = uiStackOy(k, per);
    int x, y;
    uiWardrobeCenter(FORK_ACC_CROWN, &x, &y);
    CHECK(uiWardrobeAt(x, y) == FORK_ACC_CROWN, "the crown's tile is where the page says");
    tap(x, y + oy);
    CHECK(cardOpen && forkAccWorn(pet) == FORK_ACC_CROWN, "tapping an owned tile puts it on, and the card stays");
    tap(x, y + oy);
    CHECK(cardOpen && forkAccWorn(pet) == FORK_ACC_NONE, "tapping it again takes it off");
    uiWardrobeCenter(FORK_ACC_SANTA, &x, &y);
    tap(x, y + oy);
    CHECK(cardOpen && forkAccWorn(pet) == FORK_ACC_NONE && !forkAccOwned(FORK_ACC_SANTA),
          "a locked tile only says how to get it");
    uiWardrobeCenter(FORK_ACC_CROWN, &x, &y);
    tap(x, y + oy);
    int gx = 233, gy = y + 94 / 2 + 3;   // in the gap between the rows
    CHECK(uiWardrobeAt(gx, gy) == FORK_ACC_NONE, "between the rows is no tile");
    tap(gx, gy + oy);
    CHECK(!cardOpen && forkAccWorn(pet) == FORK_ACC_CROWN, "anywhere else closes the card, and keeps what is on");
  }

  printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
  return fails ? 1 : 0;
}
