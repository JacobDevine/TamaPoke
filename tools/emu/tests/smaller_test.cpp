// Phase 11's smaller ideas (android/FEATURES_PLAN.md): the card frames
// (fork.h section 16), the scrub (17) and the help (18). Each through its pure
// rules first, then through the REAL sketch at the phone's canvas: the frame
// chip's tap, a scrub won and a scrub abandoned by real fingers, and the help
// opened by a new save's starter and by the menu's tile.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "dex.h"
#include "fork.h"
#include "fork_i18n.h"
#include "i18n.h"
#include "ckpt.h"
#include "sdmon.h"
#include <string.h>
#include <vector>

uint32_t g_seed = 0x511;
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
extern bool gPanelRound, playerOpen, menuOpen, helpOpen;
extern uint8_t playerPage, helpPage;
extern uint32_t bathUntil, choiceUntil;
extern uint8_t choiceKind;
extern void (*gHostSaveBackup)();
extern void (*gHostSaveRestore)();
uint8_t uiCurrentScreen();
extern const char *const SCREEN_NAME[];
int uiSceneOy();
void emuAdvanceMs(uint32_t ms);
void emuSetClockPaused(bool paused);
void rtcSetEpoch(uint32_t e);
uint32_t rtcEpoch();
bool uiFrameChipRect(int *x, int *y, int *w, int *h);
bool uiBdayChipRect(int *x, int *y, int *w, int *h);
void uiButtonAt(int i, int *cx, int *cy, int *half);
bool uiScrubSpotAt(uint8_t i, int *x, int *y);
void uiStarterRowRect(int i, int *x, int *y, int *w, int *h);
void uiMenuRowRect(int i, int *x, int *y, int *w, int *h);
int uiMenuRowCount();
int uiTileNamed(const char *n);
int uiTileCount();
int uiRegionRowCenterY(int row);
int uiRegionRowX();
uint8_t uiHelpPageForTile(int tile);
uint8_t uiRimPages();
int uiTapFinger();
int uiTabBarTop();
int uiPetX();

static const int H = 1010;
static int fails = 0;
#define CHECK(c, ...) do { if (c) { printf("ok   "); printf(__VA_ARGS__); printf("\n"); } \
                           else { printf("FAIL "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static void step(int n, uint32_t ms = 20) {
  for (int i = 0; i < n; i++) { emuAdvanceMs(ms); loop(); }
}
static void down(int x, int y) { g_touchX = x; g_touchY = y; g_touchDown = true; emuFireInterrupt(); step(3); }
static void move(int x, int y) { g_touchX = x; g_touchY = y; emuFireInterrupt(); step(2); }
static void up() { g_touchDown = false; emuFireInterrupt(); step(3); }
static void tap(int x, int y) { down(x, y); up(); }
// A drag in `n` steps, released at the end.
static void drag(int x0, int y0, int x1, int y1, int n = 8) {
  down(x0, y0);
  for (int i = 1; i <= n; i++) move(x0 + (x1 - x0) * i / n, y0 + (y1 - y0) * i / n);
  up();
}
static void fakeBackup() {}
static const char *scr() { return SCREEN_NAME[uiCurrentScreen()]; }

static uint32_t localEpoch(int y, int m, int d, int hh) {
  return forkDayNumber((uint16_t)y, (uint8_t)m, (uint8_t)d) * 86400u + hh * 3600u;
}
static void registerFirst(uint16_t n) {
  memset(pet.dexReg, 0, sizeof(pet.dexReg));
  for (int16_t d = 1; d <= DEX_COUNT && d <= n; d++) pet.dexReg[(d - 1) >> 3] |= (uint8_t)(1u << ((d - 1) & 7));
}
static bool bannerIs(const char *l1, const char *l2) {
  const char *a, *b;
  return forkBanner(&a, &b) && !strcmp(a, l1) && !strcmp(b, l2);
}
static bool asciiFits(const char *t, size_t max) {
  if (!t || !*t || strlen(t) > max) return false;
  for (const char *p = t; *p; p++) if (*p < 32 || *p > 126) return false;
  return true;
}
static std::vector<uint16_t> frame() {
  render();
  const uint16_t *b = gfx->buffer();
  return std::vector<uint16_t>(b, b + 466 * H);
}
static int diffIn(const std::vector<uint16_t> &a, const std::vector<uint16_t> &b, int y0, int y1) {
  int n = 0;
  for (int y = y0; y < y1 && y < H; y++)
    for (int x = 0; x < 466; x++) n += a[y * 466 + x] != b[y * 466 + x];
  return n;
}
static void healthyPet() {
  while (pet.hasLearnOffer()) pet.declineLearn();
  pet.sleeping = false;
  pet.fullness = pet.joy = pet.energy = 80;
  pet.lastSeenEpoch = rtcEpoch();
}

int main() {
  // ===================== the strings, all seven languages =====================
  {
    int problems = 0;
    for (uint8_t l = 0; l < LANG_COUNT; l++) {
      setLang((Lang)l);
      for (uint8_t f = 0; f < FORK_FRAME_COUNT; f++) {
        char chip[48];
        snprintf(chip, sizeof(chip), "%s: %s", FT(FS_FRAME), forkFrameName(f));
        // the chip's left half: 48 px of swatch, then size 2 text, inside 418
        if (!asciiFits(forkFrameName(f), 12) || 48 + (int)strlen(chip) * 12 > 418 - 24) {
          printf("  lang %u frame %u: '%s'\n", l, f, chip); problems++;
        }
      }
      char nx[32];
      snprintf(nx, sizeof(nx), FT(FS_FRAME_NEXT_FMT), 1025u);
      if (!asciiFits(FT(FS_FRAME_NEW), 23) || !asciiFits(FT(FS_FRAME), 10) || !asciiFits(nx, 20) ||
          !asciiFits(FT(FS_SCRUB_SAY), 24) || !asciiFits(FT(FS_HELP), 10) || !asciiFits(FT(FS_HELP_GESTURES), 16)) {
        printf("  lang %u: a frame, scrub or help label is missing, too long or not ASCII\n", l); problems++;
      }
      for (uint8_t p = 0; p < FORK_HELP_PAGES; p++) {
        const char *t = forkHelpTitle(p);
        if (!t[0] || (int)strlen(t) * 12 > 466 - 48) { printf("  lang %u help page %u title '%s'\n", l, p, t); problems++; }
        for (uint8_t k = 0; k < FORK_HELP_LINES; k++)
          if (!asciiFits(forkHelpLine(p, k), 30)) {
            printf("  lang %u help %u.%u '%s'\n", l, p, k, forkHelpLine(p, k)); problems++;
          }
      }
    }
    setLang(LANG_EN);
    CHECK(problems == 0, "every frame, scrub and help string is there, ASCII and fits, in all seven languages");
    CHECK(!forkHelpLine(FORK_HELP_PAGES, 0)[0] && !forkHelpLine(0, FORK_HELP_LINES)[0] && !forkFrameName(FORK_FRAME_COUNT)[0],
          "past the end is empty, never another string");
  }

  // ===================== frames: the rules (pure) =====================
  {
    bool rising = true;
    for (uint8_t f = 1; f < FORK_FRAME_COUNT; f++) rising &= forkFrameNeed(f) > forkFrameNeed(f - 1);
    CHECK(rising && forkFrameNeed(FORK_FRAME_NONE) == 0 && forkFrameNeed(FORK_FRAME_MASTER) == DEX_COUNT,
          "thresholds rise from 0, and MASTER is the whole Pokedex (%u)", (unsigned)DEX_COUNT);
    CHECK(forkFrameBest(0) == FORK_FRAME_NONE && forkFrameBest(24) == FORK_FRAME_NONE &&
          forkFrameBest(25) == FORK_FRAME_BRONZE && forkFrameBest(249) == FORK_FRAME_SILVER &&
          forkFrameBest(250) == FORK_FRAME_GOLD && forkFrameBest(DEX_COUNT - 1) == FORK_FRAME_CRYSTAL &&
          forkFrameBest(DEX_COUNT) == FORK_FRAME_MASTER, "the best open frame follows the count, edge by edge");
  }
  CHECK(forkFrameShown(DEX_COUNT) == FORK_FRAME_NONE && forkFrameCycle(DEX_COUNT) == FORK_FRAME_NONE &&
        forkData.st.frame == 0, "switch off: no frame is shown and the chip changes nothing");
  CHECK(!forkScrubWanted(pet) && !forkScrubActive(), "switch off: the bath is no scrub");
  CHECK(!forkHelpFirstRun() && !forkHelpSeen(), "switch off: the help never opens by itself, and nothing is marked");

  // ===================== frames: the record =====================
  {
    ForkState s;
    s.frame = FORK_FRAME_GOLD + 1;
    s.frameSeen = FORK_FRAME_CRYSTAL;
    s.flags = FORK_FLAG_HELP_SEEN;
    uint8_t buf[FORK_BLOB_CAP];
    uint16_t n = Fork::encode(s, buf, sizeof(buf), 3);
    ForkState back;
    CHECK(n && Fork::decode(buf, n, back) && back.frame == s.frame && back.frameSeen == s.frameSeen &&
          (back.flags & FORK_FLAG_HELP_SEEN), "the chosen frame, the one announced and the help flag survive the record");
    const uint16_t old = (uint16_t)ckptSeal(buf, CKPT_HDR + FORK_OFF_FRAME, FORK_MAGIC, FORK_VERSION, 3);
    CHECK(Fork::decode(buf, old, back) && back.frame == 0 && back.frameSeen == 0,
          "a record from before frames reads as 'the best open' and nothing announced");
    s.frame = FORK_FRAME_COUNT + 7;   // a later build's
    s.frameSeen = 200;
    n = Fork::encode(s, buf, sizeof(buf), 4);
    CHECK(Fork::decode(buf, n, back) && back.frame == 0 && back.frameSeen == FORK_FRAME_COUNT - 1,
          "a frame this build does not know reads as the best open, and the announced one is clamped");
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
  rtcSetEpoch(localEpoch(2026, 11, 21, 10));
  setup();
  emuSetClockPaused(true);
  step(3);

  // ---- the help: a new save's first look ----
  CHECK(pet.awaitingStarter() && !helpOpen, "a new save starts on the starter screen, with no help over it");
  tap(uiRegionRowX(), uiRegionRowCenterY(0));   // KANTO
  step(2);
  CHECK(!strcmp(scr(), "starter"), "on the starter cards (%s)", scr());
  {
    int x, y, w, h;
    uiStarterRowRect(0, &x, &y, &w, &h);
    tap(x + w / 2, y + h / 2);
  }
  CHECK(!pet.awaitingStarter(), "a starter was chosen by a tap");
  CHECK(helpOpen && !strcmp(scr(), "help") && helpPage == 0 && forkHelpSeen(),
        "and the help opened by itself, on the gestures page, marked seen");
  CHECK(!forkHelpFirstRun(), "once only: it will not open by itself again");
  {
    // tap through: each tap turns a page, the last tap closes
    int taps = 0;
    while (helpOpen && taps < 20) { tap(233, 500); taps++; }
    CHECK(!helpOpen && taps == FORK_HELP_PAGES, "a tap turns each page and the last closes it (%d taps)", taps);
  }
  forkData.begin();
  CHECK(forkHelpSeen(), "seen survives a reload");

  pet.dbgHatchAs(4, false);   // CHARMANDER
  healthyPet();
  step(3);
  CHECK(!strcmp(scr(), "main"), "on the pet screen");

  // ---- the help's pages are the tiles' own ----
  {
    struct { const char *tile; const char *title; } T2[] = {
      { "player", T(S_TAB_PLAYER) }, { "medals", T(S_GOALS) }, { "party", T(S_PARTY) },
      { "pet", T(S_TAB_HOME) }, { "explore", T(S_EXPLORE) }, { "gym", T(S_GYMS) }, { "dex", T(S_TAB_DEX) },
    };
    int bad = 0;
    for (auto &t : T2) {
      const uint8_t p = uiHelpPageForTile(uiTileNamed(t.tile));
      if (p >= FORK_HELP_PAGES || strcmp(forkHelpTitle(p), t.title)) {
        printf("  tile %s opens help page %u, titled '%s', not '%s'\n", t.tile, p, forkHelpTitle(p), t.title); bad++;
      }
    }
    CHECK(bad == 0 && uiTileCount() + 1 == FORK_HELP_PAGES,
          "each tile's help page carries that tile's own name: the help's order is the axis's");
  }

  // ---- the help: the menu's tile ----
  {
    menuOpen = true;
    step(2);
    CHECK(uiMenuRowCount() == 7, "the phone's shade answers one more tile, HELP (%d)", uiMenuRowCount());
    int rx, ry, rw, rh, hx, hy, hw, hh;
    uiMenuRowRect(4, &rx, &ry, &rw, &rh);
    uiMenuRowRect(uiMenuRowCount() - 1, &hx, &hy, &hw, &hh);
    CHECK(hy == ry && hh == rh && hx > rx + rw && hh >= uiTapFinger(),
          "HELP sits beside RETIRE, apart from it, finger-sized (%dx%d)", hw, hh);
    tap(hx + hw / 2, hy + hh / 2);
    CHECK(helpOpen && !menuOpen && helpPage == uiHelpPageForTile(uiTileNamed("pet")),
          "tapping it opens the help on the pet's page (%u)", helpPage);
    CHECK(uiRimPages() == FORK_HELP_PAGES, "the help is a paged screen of %u", (unsigned)uiRimPages());
    const uint8_t p0 = helpPage;
    drag(233, 700, 233, 420);   // a swipe up: the next page
    CHECK(helpOpen && helpPage == p0 + 1, "a swipe up turns to the next page (%u)", helpPage);
    helpPage = 0;
    drag(233, 420, 233, 700);   // a swipe down on the first page: back
    CHECK(!helpOpen && !strcmp(scr(), "main"), "a swipe down on the first page closes it (%s)", scr());
    menuOpen = true;
    step(2);
    uiMenuRowRect(4, &rx, &ry, &rw, &rh);
    tap(rx + rw / 2, ry + rh / 2);
    CHECK(!helpOpen && choiceKind == 3, "RETIRE's half is still RETIRE: it asks to confirm, and is not HELP");
    choiceKind = 0;   // put back what was borrowed: the dialog would take the next tap
    choiceUntil = 0;
    menuOpen = false;
    step(2);
  }

  // ---- frames on the player card ----
  registerFirst(120);
  forkData.st.frame = 0;
  forkData.st.frameSeen = 0;
  forkBannerClear();
  forkFramePoll(pet);
  CHECK(bannerIs(FT(FS_FRAME_NEW), forkFrameName(FORK_FRAME_SILVER)) && forkData.st.frameSeen == FORK_FRAME_SILVER,
        "with 120 registered, the first poll announces SILVER (the best, not each below it)");
  forkBannerClear();
  forkFramePoll(pet);
  {
    const char *a, *b;
    CHECK(!forkBanner(&a, &b), "and never twice");
  }
  CHECK(forkFrameShown(120) == FORK_FRAME_SILVER, "never chosen, the best open is shown");
  {
    uint8_t seen[8] = { 0 };
    for (int k = 0; k < 6; k++) seen[k] = forkFrameCycle(120);
    CHECK(seen[0] == FORK_FRAME_NONE && seen[1] == FORK_FRAME_BRONZE && seen[2] == FORK_FRAME_SILVER &&
          seen[3] == FORK_FRAME_NONE, "the chip cycles the open frames only, wrapping through NONE");
  }
  forkData.st.frame = FORK_FRAME_GOLD + 1;   // chosen, then the count fell (an older save restored)
  CHECK(forkFrameShown(120) == FORK_FRAME_SILVER, "a chosen frame that is not open shows the best open instead");

  playerOpen = true;
  playerPage = 0;
  forkData.st.frame = FORK_FRAME_NONE + 1;
  step(3);
  CHECK(!strcmp(scr(), "player"), "on the player card");
  {
    int fx, fy, fw, fh, bx, by, bw, bh;
    CHECK(uiFrameChipRect(&fx, &fy, &fw, &fh) && fh >= 44, "the frame chip is there, at least the 44 px floor tall");
    forkSetBirthday(0, 0);
    if (uiBdayChipRect(&bx, &by, &bw, &bh))
      CHECK(by + bh <= fy, "and below the birthday chip, never over it (%d..%d vs %d)", by, by + bh, fy);
    const std::vector<uint16_t> plain = frame();
    tap(fx + fw / 2, fy + fh / 2);
    CHECK(playerOpen && forkData.st.frame == FORK_FRAME_BRONZE + 1, "tapping the chip changes the frame, and the card stays");
    const std::vector<uint16_t> framed = frame();
    CHECK(diffIn(plain, framed, 0, fy) > 0, "the border appears round the header (%d px)", diffIn(plain, framed, 0, fy));
    // down to the tab bar, whose pill eases after any tap whatever it hit
    CHECK(diffIn(plain, framed, fy + fh + 2, uiTabBarTop()) == 0,
          "and nothing between the chip and the tab bar moves: the badge rows are untouched");
    tap(fx + fw / 2, fy + fh + 40);
    // below the chip is the badge rows: not the chip, so it closes as before
    CHECK(!playerOpen, "a tap off the chip still backs out, as it always did");
  }
  forkData.st.frame = 0;
  step(2);

  // ---- the scrub ----
  CHECK(forkScrubSpotsFor(100) == FORK_SCRUB_MIN && forkScrubSpotsFor(0) == FORK_SCRUB_MAX, "3 spots clean, 9 filthy");
  {
    bool mono = true;
    for (int h = 1; h <= 100; h++) mono &= forkScrubSpotsFor((uint8_t)h) <= forkScrubSpotsFor((uint8_t)(h - 1));
    CHECK(mono, "and never fewer for a dirtier creature");
  }
  CHECK(forkScrubPartial(40, 150, 300) == 70 && forkScrubPartial(40, 0, 300) == 40 &&
        forkScrubPartial(40, 300, 300) == 100 && forkScrubPartial(100, 0, 300) == 100 && forkScrubPartial(40, 5, 0) == 40,
        "a stopped scrub pays the share wiped of what was lacking");

  int bx, by;
  uiButtonAt(2, &bx, &by, nullptr);   // BATH
  healthyPet();
  pet.hygiene = 30;
  pet.poops = 2;
  step(2);
  forkData.st.touchDay = 0;
  forkData.st.touchJoy = 0;
  tap(bx, by);
  CHECK(forkScrubActive() && bathUntil == 0, "with the switch on, BATH starts a scrub, not the bath");
  CHECK(forkScrubCount() == forkScrubSpotsFor(30), "%u spots at hygiene 30", forkScrubCount());
  CHECK(!strcmp(scr(), "main"), "on the pet screen still");
  {
    // the spots sit on the creature: inside the pet zone's columns, above the ground
    int off = 0;
    for (uint8_t i = 0; i < forkScrubCount(); i++) {
      int x, y;
      uiScrubSpotAt(i, &x, &y);
      if (x < 60 || x > 406 || y > uiSceneOy() + 340 || y < uiSceneOy() - 40) off++;
    }
    CHECK(off == 0, "every spot is on the creature, none in the sky or the panel");
  }
  {
    // Rub each spot with wide side-to-side strokes: wider than a tile swipe,
    // which a scrub must never be read as.
    const uint8_t joy0 = pet.joy;
    for (int pass = 0; pass < 2 && forkScrubLeft(); pass++)
      for (uint8_t i = 0; i < forkScrubCount(); i++) {
        int16_t dx, dy; uint8_t dirt;
        forkScrubSpot(i, &dx, &dy, &dirt);
        if (!dirt) continue;
        int x, y;
        uiScrubSpotAt(i, &x, &y);
        down(x - 60, y);
        move(x, y); move(x + 60, y); move(x, y); move(x - 60, y); move(x + 60, y);
        up();
        if (strcmp(scr(), "main")) break;
      }
    CHECK(!strcmp(scr(), "main"), "120 px rubs never turned into a swipe off the pet screen (%s)", scr());
    step(2);
    CHECK(!forkScrubActive() && bathUntil != 0, "every spot gone, upstream's bath starts");
    CHECK(pet.joy == (uint8_t)(joy0 + FORK_SCRUB_JOY > 100 ? 100 : joy0 + FORK_SCRUB_JOY),
          "and a little joy for it (%u -> %u)", joy0, pet.joy);
    step(200);   // the bath's three seconds
    CHECK(bathUntil == 0 && pet.hygiene == 100 && pet.poops == 0, "the bath ends clean: hygiene 100, no poops (%u, %u)",
          pet.hygiene, pet.poops);
  }
  {
    // A scrub abandoned: one spot rubbed, then a tap off the creature.
    healthyPet();
    pet.hygiene = 40;
    pet.poops = 1;
    step(2);
    tap(bx, by);
    CHECK(forkScrubActive(), "a second scrub");
    int x, y;
    uiScrubSpotAt(0, &x, &y);
    down(x - 50, y); move(x + 50, y); up();
    uint16_t wiped = 0;
    for (uint8_t i = 0; i < forkScrubCount(); i++) { uint8_t d; forkScrubSpot(i, nullptr, nullptr, &d); wiped += FORK_SCRUB_DIRT - d; }
    const uint16_t total = (uint16_t)forkScrubCount() * FORK_SCRUB_DIRT;
    CHECK(forkScrubActive() && wiped > 0 && wiped < total, "part of it wiped (%u of %u)", wiped, total);
    const uint8_t bond0 = pet.bond;
    tap(30, uiSceneOy() + 150);   // the sky beside it
    CHECK(!forkScrubActive() && bathUntil == 0, "a tap off the creature stops it, with no bath");
    CHECK(pet.hygiene == forkScrubPartial(40, wiped, total) && pet.hygiene > 40 && pet.hygiene < 100,
          "hygiene rises by the share wiped (%u)", pet.hygiene);
    CHECK(pet.poops == 1 && pet.bond == bond0, "and that is all: the poop stays, no bond");
    CHECK(!strcmp(scr(), "main"), "and the tap did nothing else (%s)", scr());
  }
  {
    // Time running out, and leaving the screen, both stop it the same way.
    healthyPet();
    pet.hygiene = 50;
    step(2);
    tap(bx, by);
    CHECK(forkScrubActive(), "a third scrub");
    emuAdvanceMs(FORK_SCRUB_MS + 100);
    step(2);
    CHECK(!forkScrubActive() && pet.hygiene == 50 && bathUntil == 0, "left untouched, it stops by itself, paying nothing");
    tap(bx, by);
    CHECK(forkScrubActive(), "a fourth");
    playerOpen = true;
    step(2);
    CHECK(!forkScrubActive(), "leaving the pet screen stops it");
    playerOpen = false;
    step(2);
  }
  {
    // It stands still while it is scrubbed. Started MID-WALK, so a creature
    // left to its planner would visibly carry on walking.
    healthyPet();
    pet.hygiene = 50;
    bool walking = false;
    for (int k = 0; k < 600 && !walking; k++) {
      const int x0 = uiPetX();
      step(3, 50);
      render();
      walking = uiPetX() != x0;
    }
    CHECK(walking, "the creature was caught walking (the control)");
    tap(bx, by);
    const int sx0 = uiPetX();
    bool still = true;
    for (int k = 0; k < 40; k++) {
      step(5, 50);
      render();
      if (uiPetX() != sx0) still = false;
    }
    CHECK(forkScrubActive() && still, "and it stops for ten seconds of scrub (x %d -> %d)", sx0, uiPetX());
    tap(30, uiSceneOy() + 150);
  }
  {
    // The board keeps its instant bath.
    forkSetFeatures(false);
    healthyPet();
    step(2);
    tap(bx, by);
    CHECK(!forkScrubActive() && bathUntil != 0, "switch off: BATH is upstream's instant bath");
    step(200);
    forkSetFeatures(true);
  }

  printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
  return fails ? 1 : 0;
}
