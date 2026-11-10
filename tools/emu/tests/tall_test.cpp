// The phone's full-height canvas (android/PHONE_LAYOUT_PLAN.md), driven through
// the REAL setup()/loop()/handleTouch() at 466 x 1010 -- the shape a 1440x3120
// phone gets -- with a camera-cutout inset.
//
// What it proves, because the emulator's square build cannot:
//   - the pet screen's controls answer at the place they are DRAWN on a tall
//     canvas (buttons, the food popup, the header band), via the same position
//     functions the drawing uses;
//   - a SQUARE screen on the tall canvas still gets the coordinates its tap
//     handlers were written against (the menu, opened over the pet);
//   - the right-edge strip pages a paged screen, and the middle does not;
//   - every frame on the tall canvas flushes, and a square frame says where it
//     sits (gFrameSquareY), which is what the host's band fill reads.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
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
extern bool menuOpen, cardOpen, gymOpen, gymPick;
extern uint8_t cardPage, gymPage;
extern uint32_t feedMenuUntil;
uint8_t uiCurrentScreen();
bool uiTall();
int uiSquareOy();
int uiFeedMenuY();
void uiHeaderBand(int *top, int *bot);
void uiButtonAt(int i, int *cx, int *cy, int *half);
int uiMenuRowCenterY(int i);
uint8_t uiRimPages();
uint8_t uiRimPage();
void uiTileGo(int i);
void startBattle(int16_t dex, uint8_t lvl);
void uiBtlCellRect(int i, int *x, int *y, int *w, int *h);
void uiBtlBackRect(int *x, int *y, int *w, int *h);
extern bool battleOpen, movePickOpen, playerOpen;
extern uint8_t movePickSlot, playerPage, cardPage;
int uiStackPer();
int uiStackOy(int k, int per);
void uiPlayerTallAvatar(int *x, int *y);
int uiMoveRowY(int i);
int uiGymRegions();
extern uint8_t btlMenu, btlMsgCount;
int uiTapFinger();
extern Party party;
extern bool partyOpen, boxOpen;
extern uint8_t partyDetail, boxDetail, boxPage, boxSort;
void partySlotPos(int i, int *cx, int *cy);
int partySlotAt(int16_t x, int16_t y);
bool partyHubAt(int16_t x, int16_t y);
bool partyCellRect(int i, int *x, int *y, int *w, int *h);
void partyButtonRects(int *boxTop, int *boxBot, int *closeTop, int *closeBot);
uint8_t boxPerView();

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
// Canvas coordinates, exactly what the Android host delivers.
static void click(int x, int y) {
  g_touchX = x; g_touchY = y; g_touchDown = true; emuFireInterrupt();
  pump(4);
  g_touchDown = false; emuFireInterrupt();
  pump(3);
}
static void drag(int x0, int y0, int x1, int y1, int steps) {
  g_touchX = x0; g_touchY = y0; g_touchDown = true; emuFireInterrupt();
  pump(2);
  for (int i = 1; i <= steps; i++) {
    g_touchX = x0 + (x1 - x0) * i / steps; g_touchY = y0 + (y1 - y0) * i / steps;
    emuFireInterrupt();
    pump(1);
  }
  g_touchDown = false; emuFireInterrupt();
  pump(3);
}

int main() {
  // What android_main.cpp does before setup().
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
  pet.fullness = 10;
  pump(3);

  CHECK(uiCurrentScreen() != 0 && uiTall(), "the pet screen is full height");
  gfx->frameReady = false;
  pump(2);
  CHECK(gfx->frameReady, "the full-height pet screen flushes");
  CHECK(gFrameSquareY == -1, "and says it is not a square frame (%d)", gFrameSquareY);

  // the food button, where drawButtons() put it
  int cx, cy, half;
  uiButtonAt(0, &cx, &cy, &half);
  CHECK(cy > H / 2, "the button row is in the lower half (cy=%d)", cy);
  click(cx, cy);
  CHECK(feedMenuUntil != 0, "tapping the FOOD button opens the food popup");
  const uint8_t before = pet.fullness;
  click(101 + 33, uiFeedMenuY() + 32);   // the popup's first item
  CHECK(pet.fullness > before, "the popup's first item feeds (%u -> %u)", before, pet.fullness);
  pump(60);   // let the eating animation finish

  // the header band opens the menu, below the cutout
  int ht, hb;
  uiHeaderBand(&ht, &hb);
  CHECK(ht >= gInsetTop, "the header band is below the cutout (%d >= %d)", ht, gInsetTop);
  click(233, (ht + hb) / 2);
  CHECK(menuOpen, "tapping the header opens the menu");
  // The menu is a SQUARE screen: centred, and its handler sees square coords.
  gfx->frameReady = false;
  pump(2);
  CHECK(gFrameSquareY == -1, "the pet screen under the menu is still drawn full height");
  click(233, uiMenuRowCenterY(0) + uiSquareOy());
  CHECK(!menuOpen && cardOpen && cardPage == 1, "the menu's STATS row lands, centred on the canvas");

  // The creature card stacks its board pages. cardPage keeps its BOARD
  // meaning, so STATS (page 1) is on the first view, under the profile.
  gfx->frameReady = false;
  pump(5);
  CHECK(gfx->frameReady && gFrameSquareY == -1 && uiTall(), "the card is full height");
  const int per = uiStackPer();
  CHECK(per >= 2, "a 1010-tall canvas stacks at least two card pages (%d)", per);
  CHECK(uiRimPage() == 0 && uiRimPages() == (4 + per - 1) / per,
        "STATS shares the first view, and the card has fewer views (%u of %u)", uiRimPage(), uiRimPages());
  drag(466 - 20, 300, 466 - 20, 300 + 3 * 40 + 10, 8);
  CHECK(cardOpen && cardPage == per, "the edge strip pages a whole view (cardPage %u)", cardPage);
  // The moves page is now the TOP section: each row answers in its own place.
  // Aimed near each row's LOWER edge and checked by slot: the top section's
  // offset is smaller than a row, so a centred tap would land in the right row
  // even with the section offset forgotten -- passing by coincidence.
  for (int i = 0; i < 4; i++) {
    movePickOpen = false;
    cardOpen = true;
    cardPage = per;
    pump(3);
    click(233, uiStackOy(0, per) + uiMoveRowY(i) + 45);
    CHECK(movePickOpen && movePickSlot == i, "move row %d on the stacked moves page opens slot %d (%d)",
          i, i, movePickOpen ? movePickSlot : -1);
  }
  movePickOpen = false;
  pump(3);
  // ...and the same spot one section down is the progress page, which has no
  // buttons: like any dead spot on a card, it closes it.
  click(233, uiStackOy(1, per) + uiMoveRowY(0) + 25);
  CHECK(!cardOpen && !movePickOpen, "the progress section's matching spot is not a move row");

  // Back is a downward drag, in canvas coordinates (android_main.cpp's).
  cardOpen = true; cardPage = 0;
  pump(3);
  drag(233, H / 2 - 80, 233, H / 2 + 80, 4);
  CHECK(!cardOpen, "a downward drag backs out of the card");

  // The player card: one view for you and every region's badges.
  uiTileGo(0);   // PLAYER
  pump(5);
  CHECK(playerOpen && uiTall() && uiRimPages() >= 2 && uiRimPages() <= 3,
        "the player card is full height with %u views", uiRimPages());
  const uint8_t av = pet.avatar;
  int ax, ay;
  uiPlayerTallAvatar(&ax, &ay);
  click(ax, ay);
  CHECK(playerOpen && pet.avatar != av, "the avatar answers where the overview draws it");
  drag(466 - 20, 300, 466 - 20, 300 + 3 * 40 + 10, 8);
  CHECK(playerOpen && uiRimPage() == 1 && playerPage >= uiGymRegions(),
        "the edge strip goes to the medals (page %u)", playerPage);
  click(ax, ay);
  CHECK(!playerOpen && pet.avatar != av, "the avatar spot does nothing past the overview (it backs out)");

  // The party: a 2x3 grid of cards. Each cell answers over its WHOLE
  // rectangle, so the taps aim near corners -- a centred tap would also land
  // inside the board's ring disc of the same index and prove nothing.
  for (int i = 0; i < 3; i++) {
    PartyMon m; m.dex = (int16_t)(25 + i); m.level = (uint8_t)(20 + i);
    party.replaceAt(i, m);
  }
  for (int i = 0; i < 10; i++) { PartyMon m; m.dex = (int16_t)(60 + i); m.level = 30; party.box[i] = m; }
  { PartyMon m; m.dex = 150; m.level = 70; party.box[19] = m; }
  uiTileGo(1);   // PARTY
  gfx->frameReady = false;
  pump(5);
  CHECK(partyOpen && !boxOpen && uiTall() && gfx->frameReady && gFrameSquareY == -1,
        "the party is full height (square y %d)", gFrameSquareY);
  int px, py, pw, ph, wrong = 0;
  for (int i = 0; i < 6; i++) {
    partyCellRect(i, &px, &py, &pw, &ph);
    const int cx4[4] = { px + 4, px + pw - 5, px + 4, px + pw - 5 };
    const int cy4[4] = { py + 4, py + 4, py + ph - 5, py + ph - 5 };
    for (int c = 0; c < 4; c++) if (partySlotAt(cx4[c], cy4[c]) != i) wrong++;
  }
  CHECK(wrong == 0, "every party card answers at all four corners (%d wrong)", wrong);
  CHECK(pw >= uiTapFinger() && ph >= uiTapFinger(), "a party card is finger-sized (%dx%d)", pw, ph);
  partyCellRect(2, &px, &py, &pw, &ph);
  click(px + 8, py + ph - 8);
  CHECK(partyDetail == 3 && !uiTall(), "slot 3's lower-left corner opens its sheet, which is square");
  partyDetail = 0;
  pump(3);
  int bt, bb, ct, cb;
  partyButtonRects(&bt, &bb, &ct, &cb);
  CHECK(bb < ct && ct > py + ph, "BOX sits under the grid and above CLOSE (%d..%d, %d..%d)", bt, bb, ct, cb);
  CHECK(bb - bt >= uiTapFinger(), "the BOX pill is finger-sized (%d)", bb - bt);
  click(233, (bt + bb) / 2);
  CHECK(boxOpen && uiTall(), "the BOX pill opens the box, full height");

  // The box: several board pages per view, paged by the edge strip.
  const uint8_t bper = boxPerView();
  CHECK(bper > 6 && bper % 6 == 0 && uiRimPages() == 36 / bper,
        "the box shows %u slots a view, %u views", bper, uiRimPages());
  int sx, sy;
  partySlotPos(8, &sx, &sy);
  click(sx, sy);
  CHECK(boxDetail == 9 && !uiTall(), "cell 9 of the first view opens box slot 9's sheet (%u)", boxDetail);
  boxDetail = 0;
  pump(3);
  drag(466 - 20, 300, 466 - 20, 300 + 3 * 40 + 10, 8);
  CHECK(boxOpen && uiRimPage() == 1 && boxPage == bper / 6, "the edge strip pages a whole view (boxPage %u)", boxPage);
  partySlotPos(1, &sx, &sy);
  click(sx, sy);
  CHECK(boxDetail == 20, "cell 2 of the second view is box slot 20, not slot 2 (%u)", boxDetail);
  boxDetail = 0;
  pump(3);
  int hy = -1;
  for (int y = H - 1; y > 0 && hy < 0; y--) if (partyHubAt(233, (int16_t)y)) hy = y - 10;
  const uint8_t sort0 = boxSort;
  click(233, hy);
  CHECK(boxOpen && boxSort != sort0, "the sort pill cycles the order (%u -> %u)", sort0, boxSort);
  boxSort = 0;
  click(233, H - 20);
  CHECK(!boxOpen && partyOpen, "a tap off the grid backs out to the party");
  pump(3);
  click(233, (ct + cb) / 2);
  CHECK(!partyOpen, "CLOSE answers where it is drawn");
  pump(3);

  // Paging on a rectangle: a drag down the right-edge strip.
  uiTileGo(4);   // GYM
  pump(2);
  gymPick = false;
  pump(2);
  CHECK(gymOpen && uiRimPages() > 1, "the gym ladder is open and paged (%u pages)", uiRimPages());
  gfx->frameReady = false;
  pump(5);
  CHECK(gfx->frameReady && gFrameSquareY == uiSquareOy(),
        "a square screen flushes and says where it sits (%d)", gFrameSquareY);
  const uint8_t p0 = uiRimPage();
  drag(466 - 20, 300, 466 - 20, 300 + 3 * 40 + 10, 8);
  CHECK(gymOpen && uiRimPage() > p0, "a drag down the edge strip pages (%u -> %u)", p0, uiRimPage());
  // ... and the same drag at the LEFT edge does not. On the round panel that
  // corner is rim and would page; on a rectangle it is an ordinary down-swipe,
  // which backs out of the ladder to its region chooser.
  const uint8_t p1 = uiRimPage();
  drag(40, 300, 40, 300 + 3 * 40 + 10, 8);
  CHECK(gymOpen && gymPick, "a drag at the left edge is a back swipe, not paging (page %u -> %u)",
        p1, uiRimPage());

  // The battle screen: full height, with its grid answering where it is drawn.
  drag(233, H / 2 - 80, 233, H / 2 + 80, 4);   // back out of the ladder
  drag(233, H / 2 - 80, 233, H / 2 + 80, 4);
  startBattle(25, 20);
  pump(3);
  while (btlMsgCount) { click(233, H / 2); }   // the opening narration
  CHECK(battleOpen && uiTall(), "the battle is full height");
  gfx->frameReady = false;
  pump(5);   // past one 100 ms render period
  CHECK(gfx->frameReady && gFrameSquareY == -1, "it flushes as a full-height frame (%d)", gFrameSquareY);
  int bx, by, bw, bh;
  uiBtlCellRect(2, &bx, &by, &bw, &bh);
  CHECK(by > H / 2 && by + bh <= H - gInsetBot, "the grid sits in the lower half, on the canvas (y=%d)", by);
  CHECK(bh >= uiTapFinger(), "a cell is finger-sized (%d >= %d)", bh, uiTapFinger());
  // Where FIGHT is on the board, shifted as a square screen would be: on the
  // full-height battle that is scenery, so nothing may happen.
  click(69 + 80, 270 + 27 + uiSquareOy());
  CHECK(btlMenu == 0, "a tap at the board's FIGHT position does nothing here");
  uiBtlCellRect(0, &bx, &by, &bw, &bh);
  click(bx + bw / 2, by + bh / 2);
  CHECK(btlMenu == 1, "FIGHT answers where it is drawn");
  uiBtlBackRect(&bx, &by, &bw, &bh);
  click(bx + bw / 2, by + bh / 2);
  CHECK(btlMenu == 0, "BACK answers where it is drawn");
  uiBtlCellRect(2, &bx, &by, &bw, &bh);
  click(bx + bw / 2, by + bh - 4);
  CHECK(btlMenu == 2, "the bottom row (POKEMON) answers near its lower edge");

  printf(fails ? "\n%d FAILED\n" : "\nPASS tall_test\n", fails);
  return fails ? 1 : 0;
}
