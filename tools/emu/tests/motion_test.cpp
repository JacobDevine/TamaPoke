// The phone's screen transitions (android/app/src/main/cpp/phone_motion.h),
// against the REAL firmware at the phone's canvas: which transition joins two
// screens is read from what the sketch says it is showing, so a screen that
// moves, or a tile that is reordered, is checked here rather than trusted.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "phone_motion.h"
#include <chrono>
#include <thread>

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
extern Pet pet;
extern Arduino_Canvas *gfx;
extern int16_t gPanelH, gInsetTop, gInsetBot, gGfxOriginY, gFrameSquareY;
extern bool gPanelRound;
extern bool menuOpen, cardOpen, gymPick, battleOpen;
extern uint8_t partyDetail;
uint8_t uiCurrentScreen();
void uiTileGo(int i);
int uiTileNamed(const char *n);
int uiTileCount();
void uiBackKey();
void uiMenuPanelRect(int *x, int *y, int *w, int *h);
void uiHeaderBand(int *top, int *bot);
void startBattle(int16_t dex, uint8_t lvl);
int uiTabBarTop();

static const int H = 1010;
static int fails = 0;
#define CHECK(c, ...) do { if (c) { printf("ok   "); printf(__VA_ARGS__); printf("\n"); } \
                           else { printf("FAIL "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static void pump(int n) {
  for (int i = 0; i < n; i++) {
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    loop();
  }
}
static void click(int x, int y) {
  g_touchX = x; g_touchY = y; g_touchDown = true; emuFireInterrupt();
  pump(4);
  g_touchDown = false; emuFireInterrupt();
  pump(3);
}
static const char *kindName(MotionKind k) {
  static const char *n[] = { "none", "slide", "fade", "deeper", "shallower", "sheet-in", "sheet-out" };
  return k <= MOTION_SHEET_OUT ? n[k] : "?";
}
// Captures the view, runs `act`, captures again, and returns the transition.
template <typename F> static Motion across(F act, MotionView *from = nullptr, MotionView *to = nullptr) {
  const MotionView a = motionViewNow();
  act();
  pump(2);
  const MotionView b = motionViewNow();
  if (from) *from = a;
  if (to) *to = b;
  return motionClassify(a, b);
}

int main() {
  gfx->resize(466, H);
  gPanelH = H;
  gPanelRound = false;
  gInsetTop = 42;
  gInsetBot = 0;
  gGfxOriginY = gFrameSquareY = (H - 466) / 2;
  setup();
  pump(3);
  if (pet.awaitingStarter()) pet.chooseStarter(4);
  if (pet.isEgg()) pet.dbgHatchAs(4, false);
  pet.sleeping = false;
  pump(3);
  CHECK(!strcmp(motionViewNow().scr, "main"), "starts on the pet screen (%s)", motionViewNow().scr);

  // ---- the tile axis ----
  const int home = uiTileNamed("pet"), explore = uiTileNamed("explore"), party = uiTileNamed("party");
  const int dex = uiTileNamed("dex"), player = uiTileNamed("player");
  Motion m = across([&] { uiTileGo(explore); });
  CHECK(m.kind == MOTION_SLIDE && m.dir == explore - home,
        "pet -> explore (its right-hand neighbour) slides, dir %d (%s)", m.dir, kindName(m.kind));
  m = across([&] { uiTileGo(home); });
  CHECK(m.kind == MOTION_SLIDE && m.dir == home - explore, "and back slides the other way (dir %d)", m.dir);
  m = across([&] { uiTileGo(party); });
  CHECK(m.kind == MOTION_SLIDE && m.dir == -1, "pet -> party, its left neighbour, slides left (dir %d)", m.dir);
  m = across([&] { uiTileGo(dex); });
  CHECK(m.kind == MOTION_FADE, "party -> pokedex, several tiles away, fades (%s)", kindName(m.kind));
  m = across([&] { uiTileGo(player); });
  CHECK(m.kind == MOTION_FADE, "pokedex -> player, the far end, fades (%s)", kindName(m.kind));
  m = across([&] { uiTileGo(player); });
  CHECK(m.kind == MOTION_NONE, "the same tile again is no transition (%s)", kindName(m.kind));

  // A slide keeps the tab bar still, so both ends must know where it is.
  MotionView f, t;
  uiTileGo(home); pump(2);
  m = across([&] { uiTileGo(explore); }, &f, &t);
  CHECK(f.tabTop == uiTabBarTop() && t.tabTop == uiTabBarTop() && t.tabTop > H / 2,
        "both tile roots carry the tab bar's top (%d, %d)", f.tabTop, t.tabTop);

  // ---- depth ----
  uiTileGo(home); pump(2);
  m = across([&] { cardOpen = true; });
  CHECK(m.kind == MOTION_DEEPER, "opening the creature's card goes deeper (%s)", kindName(m.kind));
  m = across([&] { uiBackKey(); });
  CHECK(m.kind == MOTION_SHALLOWER && !cardOpen, "Back from it comes out (%s)", kindName(m.kind));

  uiTileGo(uiTileNamed("gym")); pump(2);
  CHECK(!strcmp(motionViewNow().scr, "gympick"), "the gym tile opens on its chooser (%s)", motionViewNow().scr);
  m = across([&] { gymPick = false; });
  CHECK(m.kind == MOTION_DEEPER, "chooser -> a region's ladder goes deeper (%s)", kindName(m.kind));
  m = across([&] { uiBackKey(); });
  CHECK(m.kind == MOTION_SHALLOWER && gymPick, "Back to the chooser comes out (%s)", kindName(m.kind));

  // A sheet inside a screen is a level of its own, though the screen is the same.
  uiTileGo(party); pump(2);
  m = across([&] { partyDetail = 1; });
  CHECK(m.kind == MOTION_DEEPER, "a party slot's sheet goes deeper (%s)", kindName(m.kind));
  m = across([&] { partyDetail = 0; });
  CHECK(m.kind == MOTION_SHALLOWER, "and closing it comes out (%s)", kindName(m.kind));

  // ---- the menu sheet, opened the way a finger opens it ----
  uiTileGo(home); pump(2);
  int bt, bb;
  uiHeaderBand(&bt, &bb);
  m = across([&] { click(233, (bt + bb) / 2); }, &f, &t);
  int x, y, w, h;
  uiMenuPanelRect(&x, &y, &w, &h);
  CHECK(menuOpen && m.kind == MOTION_SHEET_IN, "the header band opens the menu as a sheet (%s)", kindName(m.kind));
  CHECK(t.sheetX == x && t.sheetY == y && t.sheetW == w && t.sheetH == h && y + h <= H,
        "carrying the menu's own rectangle (%d,%d %dx%d)", t.sheetX, t.sheetY, t.sheetW, t.sheetH);
  MotionPose p = motionPose(m, 0, 466, H, f, t);
  CHECK(t.sheetY + p.newY >= H && p.dimA == 0, "at the start the sheet is below the canvas and nothing is dimmed");
  p = motionPose(m, 1, 466, H, f, t);
  CHECK(p.newY == 0 && p.dimA == 1, "at the end it sits where it is drawn, fully dimmed behind");
  m = across([&] { uiBackKey(); }, &f, &t);
  CHECK(!menuOpen && m.kind == MOTION_SHEET_OUT && f.sheetY == y,
        "closing it slides the sheet out, from where it was (%s)", kindName(m.kind));

  // ---- the poses ----
  Motion sl{ MOTION_SLIDE, 1 };
  p = motionPose(sl, 0, 466, H, f, t);
  CHECK(p.oldX == 0 && p.newX == 466, "a slide right starts with the new frame a whole width to the right");
  p = motionPose(sl, 1, 466, H, f, t);
  CHECK(p.oldX == -466 && p.newX == 0, "and ends with the old frame a width to the left");
  p = motionPose(sl, 0.5f, 466, H, f, t);
  CHECK(p.newX - p.oldX == 466, "the two frames stay joined mid-slide (gap %g)", p.newX - p.oldX - 466);
  CHECK(motionEaseOut(0.5f) > 0.5f, "the ease decelerates (%g at half time)", motionEaseOut(0.5f));
  for (int k = MOTION_SLIDE; k <= MOTION_SHEET_OUT; k++)
    CHECK(motionDurationMs((MotionKind)k) >= 150 && motionDurationMs((MotionKind)k) <= 300,
          "%s lasts %u ms", kindName((MotionKind)k), motionDurationMs((MotionKind)k));

  // ---- anything special fades ----
  uiTileGo(home); pump(2);
  m = across([&] { startBattle(19, 5); }, &f, &t);
  CHECK(battleOpen && t.special && m.kind == MOTION_FADE, "a fight fades in (%s, %s)", t.scr, kindName(m.kind));
  MotionView unknown;
  unknown.scr = "somethingnew";
  motionClassifyScreen(unknown);
  CHECK(unknown.special, "a screen this table does not know is special");
  CHECK(motionClassify(f, unknown).kind == MOTION_FADE, "an unknown screen fades rather than guessing a direction");

  printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
  return fails ? 1 : 0;
}
