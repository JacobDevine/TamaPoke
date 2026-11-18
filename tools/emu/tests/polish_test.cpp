// The phone's polish, Tier 2 (android/README.md § "Motion and feel"), driven
// through the REAL sketch at the phone's canvas with the clock paused, so every
// "after N ms" below is exact.
//
// Most of it is about TIME, NOT FRAMES: the phone draws up to six times as
// often as the board, so anything the board moved by a step per frame has to
// move by the clock there, or it runs six times as fast. Those checks render
// the same span of time at 60 fps and at 10 fps and compare what moved.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "battle.h"
#include "fork.h"
#include "trainers.h"
#include <math.h>

uint32_t g_seed = 0xC0FFEE;
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
extern bool gPanelRound, battleOpen, clockOpen, menuOpen;
extern uint32_t feedMenuUntil, lastInteract;
extern Combatant btlYou, btlFoe;
extern uint16_t btlHpShown[2];
extern void (*gHostHaptic)(uint8_t kind);
uint8_t uiCurrentScreen();
void uiTileGo(int i);
int uiTileNamed(const char *n);
int uiTileIndex();
void uiTabCellRect(int i, int *x, int *y, int *w, int *h);
void uiButtonAt(int i, int *cx, int *cy, int *half);
void startBattle(int16_t dex, uint8_t lvl);
void openClock();
void setRowRect(int i, int *x, int *y, int *w, int *h);
void uiClockTallOy(int *oyTime, int *oyOpts, int *split);
void emuAdvanceMs(uint32_t ms);
void emuSetClockPaused(bool paused);

bool uiPolishOn();
uint32_t uiPhonePeriod(uint32_t period, uint32_t now);
float uiWalkStep(uint32_t now);
void uiCareEase(const uint8_t real[4], float shown[4]);
int uiTabPillX(int tab, int targetX);
bool uiPressAt(int16_t *x, int16_t *y);
int uiButtonShakeDx(int i);
bool uiBuzzRect(int *x, int *y, int *w, int *h);
int8_t uiNextLeader(uint8_t region);
float uiBehX();
void uiBehWalk(float from, float to);

static const int H = 1010;
static int fails = 0;
#define CHECK(c, ...) do { if (c) { printf("ok   "); printf(__VA_ARGS__); printf("\n"); } \
                           else { printf("FAIL "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static int buzzes[4];
static void countHaptic(uint8_t k) { if (k < 4) buzzes[k]++; }
static int buzzTotal() { return buzzes[0] + buzzes[1] + buzzes[2] + buzzes[3]; }

// Loop passes `ms` apart: the touch poll and the render cadence both need the
// clock to move between them.
static void step(int n, uint32_t ms = 20) {
  for (int i = 0; i < n; i++) { emuAdvanceMs(ms); loop(); }
}
static void down(int x, int y) { g_touchX = x; g_touchY = y; g_touchDown = true; emuFireInterrupt(); step(3); }
static void move(int x, int y) { g_touchX = x; g_touchY = y; step(3); }
static void up() { g_touchDown = false; emuFireInterrupt(); step(3); }
static void tap(int x, int y) { down(x, y); up(); }

static uint16_t px(int x, int y) { return gfx->buffer()[(size_t)y * 466 + x]; }

int main() {
  forkSetFeatures(true);
  gfx->resize(466, H);
  gPanelH = H;
  gPanelRound = false;
  gInsetTop = 42;
  gInsetBot = 0;
  gGfxOriginY = gFrameSquareY = (H - 466) / 2;
  gHostHaptic = countHaptic;
  setup();
  emuSetClockPaused(true);
  step(3);
  if (pet.awaitingStarter()) pet.chooseStarter(4);
  if (pet.isEgg()) pet.dbgHatchAs(4, false);
  pet.sleeping = false;
  pet.fullness = pet.joy = pet.energy = pet.hygiene = 80;
  step(3);
  CHECK(uiTileIndex() == uiTileNamed("pet") && uiPolishOn(), "starts on the pet screen, with the phone's polish on");

  // ---- the board keeps its own values ----
  gPanelRound = true;
  CHECK(!uiPolishOn() && uiPhonePeriod(100, millis()) == 100 && uiWalkStep(millis()) == 3.0f,
        "on the round board: 10 fps and 3 px a frame, as always");
  gPanelRound = false;

  // ---- the render cadence ----
  tap(233, 300);   // the creature: a caress, and a touch
  CHECK(uiPhonePeriod(100, millis()) == 16, "just touched, the pet screen draws at 60 fps");
  step(1, 6000);
  pet.sleeping = false;
  uiBehWalk(150, 150);   // standing still at its target
  step(1, 20);
  // nothing moving and nobody touching: back to the board's rate
  int still = 0;
  for (int i = 0; i < 20 && uiPhonePeriod(100, millis()) != 100; i++) step(1, 300);
  still = uiPhonePeriod(100, millis());
  CHECK(still == 100, "left alone with nothing moving, back to 10 fps (%d ms)", still);

  // ---- the walk is timed by the clock ----
  // The same 500 ms rendered at 60 fps and at 10 fps must cover the same ground.
  auto walk = [](uint32_t frameMs) {
    uiBehWalk(150, 420);
    render();   // a first frame starts the walk's clock
    const float x0 = uiBehX();
    for (uint32_t t = 0; t < 500; t += frameMs) { emuAdvanceMs(frameMs); render(); }
    return uiBehX() - x0;
  };
  const float fast = walk(16), slow = walk(100);
  CHECK(slow > 10 && fabsf(fast - slow) <= 4.0f,
        "500 ms of walking covers %.1f px at 60 fps and %.1f px at 10 fps", fast, slow);
  CHECK(uiPhonePeriod(100, millis()) == 16, "a walking creature is drawn at 60 fps");
  uiBehWalk(uiBehX(), uiBehX());

  // ---- the care bars slide ----
  {
    const uint8_t a[4] = { 80, 80, 80, 80 }, b[4] = { 20, 80, 80, 80 };
    float s[4];
    uiCareEase(a, s);
    emuAdvanceMs(16);
    uiCareEase(b, s);
    CHECK(s[0] < 80 && s[0] > 20, "a bar dropping 80 -> 20 is part way after one frame (%.1f)", s[0]);
    CHECK(uiPhonePeriod(100, millis()) == 16, "and wants every frame while it slides");
    for (int i = 0; i < 40; i++) { emuAdvanceMs(16); uiCareEase(b, s); }
    CHECK(s[0] == 20, "and lands exactly (%.1f)", s[0]);
    emuAdvanceMs(1000);
    uiCareEase(a, s);
    CHECK(s[0] == 80, "after a second away from the screen it snaps rather than slides");
  }

  // ---- a press shows, and a tap still lands ----
  {
    int cx, cy, half;
    uiButtonAt(0, &cx, &cy, &half);   // FOOD
    render();
    const uint16_t idleFill = px(cx - half + 8, cy);
    down(cx, cy);
    int16_t fx, fy;
    CHECK(uiPressAt(&fx, &fy) && fx == cx && fy == cy, "a finger on FOOD is a press at (%d,%d)", fx, fy);
    render();
    const uint16_t pressedFill = px(cx - half + 8, cy);
    CHECK(pressedFill != idleFill, "and FOOD is drawn pressed (%04x -> %04x)", idleFill, pressedFill);
    move(cx + 60, cy);
    CHECK(!uiPressAt(&fx, &fy), "moved 60 px it is no longer a press");
    up();   // let go out there: not a tap
    CHECK(feedMenuUntil == 0, "and letting go there does not feed");
    const int before = buzzes[2];
    tap(cx, cy);
    CHECK(feedMenuUntil > millis() && buzzes[2] == before + 1, "a tap opens the food menu and ticks once");
    feedMenuUntil = 0;
    step(3);
  }

  // ---- asleep: the icons stay, and a tap says why ----
  {
    pet.sleeping = true;
    step(2);
    render();
    int cx, cy, half;
    uiButtonAt(0, &cx, &cy, &half);
    const uint16_t bg = px(cx - half + 4, cy - half + 30);
    int inked = 0;
    for (int y = cy - 20; y < cy + 20; y++)
      for (int x = cx - 20; x < cx + 20; x++) inked += px(x, y) != bg;
    CHECK(inked > 50, "asleep, FOOD still shows its icon (%d px), not an empty box", inked);
    const int deny = buzzes[1];
    tap(cx, cy);
    CHECK(feedMenuUntil == 0 && buzzes[1] == deny + 1, "a tap on it does nothing but buzz twice");
    bool shook = false;
    for (int i = 0; i < 10; i++) { emuAdvanceMs(20); shook |= uiButtonShakeDx(0) != 0; }
    CHECK(shook, "and FOOD shakes");
    step(1, 1000);
    CHECK(uiButtonShakeDx(0) == 0, "and stops");
    pet.sleeping = false;
    step(2);
  }

  // ---- the tab bar ----
  {
    const int home = uiTileNamed("pet"), explore = uiTileNamed("explore");
    int x, y, w, h, x2, y2, w2, h2;
    uiTabCellRect(home, &x, &y, &w, &h);
    uiTabCellRect(explore, &x2, &y2, &w2, &h2);
    const int hx = x + w / 2, ex = x2 + w2 / 2;
    render();
    CHECK(uiTabPillX(home, hx) == hx, "the pill rests on HOME");
    const int ticks = buzzes[2];
    tap(ex, y + 20);
    CHECK(uiTileIndex() == explore && buzzes[2] == ticks + 1, "a tap on EXPLORE goes there and ticks");
    // the tap's own frames have already moved it a little; restart from HOME
    // to measure the slide cleanly
    uiTileGo(home); render();
    emuAdvanceMs(500); render();
    uiTileGo(explore);
    const int p0 = uiTabPillX(explore, ex);
    emuAdvanceMs(80);
    const int p1 = uiTabPillX(explore, ex);
    emuAdvanceMs(200);
    const int p2 = uiTabPillX(explore, ex);
    CHECK(p0 == hx && p1 > hx && p1 < ex && p2 == ex,
          "the pill slides HOME %d -> %d -> EXPLORE %d", p0, p1, p2);
    uiTileGo(home);
    step(3);
  }

  // ---- the haptics switch ----
  {
    openClock();
    step(2);
    int bx, by, bw, bh, oyO;
    CHECK(uiBuzzRect(&bx, &by, &bw, &bh), "settings on the phone has a buzz switch");
    uiClockTallOy(nullptr, &oyO, nullptr);
    tap(bx + bw / 2, oyO + by + bh / 2);
    CHECK(!forkHapticsOn(), "its half of the row turns the buzz off");
    // (the emulator's sound switch is a stub that is always on, so the other
    // half is checked by what it does NOT do)
    int rx, ry, rw, rh;
    setRowRect(0, &rx, &ry, &rw, &rh);
    tap(rx + 20, oyO + ry + rh / 2);
    CHECK(!forkHapticsOn() && rx + 20 < bx, "the sound half leaves the buzz alone");
    clockOpen = false;
    step(2);
    const int n = buzzTotal();
    tap(233, 300);
    uiTileGo(uiTileNamed("pet"));
    int x, y, w, h;
    uiTabCellRect(uiTileNamed("explore"), &x, &y, &w, &h);
    tap(x + w / 2, y + 20);
    CHECK(buzzTotal() == n, "with it off, a tab change does not buzz");
    forkSetHaptics(true);
    uiTileGo(uiTileNamed("pet"));
    tap(x + w / 2, y + 20);
    CHECK(buzzTotal() == n + 1, "and with it on again, it does");
    uiTileGo(uiTileNamed("pet"));
    step(2);
  }

  // ---- the battle's HP drain is timed by the clock ----
  {
    auto drain = [](uint32_t frameMs, uint32_t span) {
      startBattle(9, 30);
      step(2);
      btlFoe.hp = btlFoe.maxHp / 4;
      for (uint32_t t = 0; t < span; t += frameMs) { emuAdvanceMs(frameMs); render(); }
      const int shown = btlHpShown[1];
      battleOpen = false;
      step(2);
      return shown;
    };
    const int full = 0;
    (void)full;
    const int f60 = drain(16, 300), f10 = drain(100, 300);
    startBattle(9, 30);
    const int maxHp = btlFoe.maxHp, gone = maxHp - maxHp / 4;
    battleOpen = false;
    step(2);
    CHECK(f60 < maxHp && f60 > maxHp / 4, "300 ms into a drain the bar is part way (%d of %d)", f60, maxHp);
    CHECK(abs(f60 - f10) <= gone / 6, "and about as far at 60 fps (%d) as at 10 fps (%d)", f60, f10);
    CHECK(drain(16, 3000) == maxHp / 4, "and it lands on the real value");
  }

  // ---- who to beat next ----
  CHECK(!pet.hasBadge(0, 0, false) && uiNextLeader(0) == 0, "a fresh ladder names its first leader");
  for (int i = 0; i < 7; i++) pet.winBadge(0, i, false);
  CHECK(uiNextLeader(0) == 7, "seven badges in, the next is the eighth leader (%d)", uiNextLeader(0));
  for (int i = 0; i < TRAINER_COUNT; i++) pet.winBadge(0, i, false);
  CHECK(uiNextLeader(0) == -1, "past the champion there is nobody");

  printf("%s: %d failure(s)\n", fails ? "FAIL" : "PASS", fails);
  return fails ? 1 : 0;
}
