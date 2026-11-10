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
int gymRowsPerPage();
void gymRowRect(int i, int *x, int *y, int *w, int *h);
void gymLanRect(int *x, int *y, int *w, int *h);
void uiGymDifRect(int *x, int *y, int *w, int *h);
void rpickLanRect(int *x, int *y, int *w, int *h);
int rpickBackTop(uint8_t mode);
int uiRegionRowCenterY(int row);
int uiRegionRowX();
int uiRegionRowsPerPage();
uint8_t rpickPageCount(uint8_t mode);
uint8_t rpickModeNow();
void uiGalleryCellRect(int i, int *x, int *y, int *w, int *h);
void uiGalleryFilterBand(int *top, int *bot);
uint16_t galleryPerPage();
uint8_t galleryPages();
int16_t galleryAt(uint16_t visual);
extern bool pickOpen, lanOpen, gymHard, galleryOpen, galleryPick, galleryDirty, clockOpen;
extern uint8_t pickTrainer, gymRegion, rpickPage, galleryPage, galleryRegion, galleryFilter, clockPage;
extern int16_t galleryDetail;

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

  // ---- the lists: gym ladder, region chooser, Pokedex (Phase 4 step 4) ----
  // The region chooser is where the back swipe above left us.
  {
    CHECK(uiTall() && rpickModeNow() != 0xFF, "the gym region chooser is full height");
    gfx->frameReady = false;
    pump(3);
    CHECK(gfx->frameReady && gFrameSquareY == -1, "and flushes as a full-height frame (%d)", gFrameSquareY);
    const int per = uiRegionRowsPerPage();
    CHECK(per > 3, "it shows more than the board's three regions a view (%d)", per);
    CHECK(rpickPageCount(rpickModeNow()) * per >= uiGymRegions(),
          "and every ladder is on some view (%u x %d >= %d)", rpickPageCount(rpickModeNow()), per, uiGymRegions());
    // The LAST row, aimed near its lower edge: a row-0-only or a board-pitch
    // mapping cannot land this one by coincidence.
    const int last = per - 1;
    rpickPage = 0;
    const int rowPitch = uiRegionRowCenterY(1) - uiRegionRowCenterY(0);
    click(uiRegionRowX(), uiRegionRowCenterY(last) + rowPitch / 2 - 18);
    CHECK(gymOpen && !gymPick && gymRegion == last, "the last region row answers where it is drawn (region %u)", gymRegion);
    // back to the chooser; its LAN button and its BACK band
    drag(233, H / 2 - 80, 233, H / 2 + 80, 4);
    CHECK(gymOpen && gymPick, "down backs out of the ladder to the chooser");
    int lx, ly, lw, lh;
    rpickLanRect(&lx, &ly, &lw, &lh);
    CHECK(ly > H / 2 && lh >= 64, "LAN BATTLE sits low and is finger-sized (y=%d h=%d)", ly, lh);
    click(lx + lw / 2, ly + lh - 4);
    CHECK(lanOpen && !gymOpen, "the chooser's LAN BATTLE answers where it is drawn");
    lanOpen = false;
    gymOpen = true; gymPick = true; rpickPage = 0;
    pump(2);
    click(233, rpickBackTop(0) - 4);   // just above the BACK band: inside the gap, nothing
    CHECK(gymOpen && gymPick, "a tap just above the BACK band does not back out");
    click(233, rpickBackTop(0) + 10);
    CHECK(!gymOpen, "a tap in the BACK band does");
  }
  {
    // The ladder: Kanto, the first nine easy leaders beaten so the second view
    // is open too.
    gymOpen = true; gymPick = false; gymRegion = 0; gymPage = 0; gymHard = false;
    pet.badges = 0x01FF;
    pump(3);
    CHECK(uiTall(), "the gym ladder is full height");
    const int per = gymRowsPerPage();
    CHECK(per > 5, "it shows more than the board's five leaders a view (%d)", per);
    CHECK(uiRimPages() == (13 + per - 1) / per, "and its view count follows (%u)", uiRimPages());
    int rx, ry, rw, rh;
    gymRowRect(per - 1, &rx, &ry, &rw, &rh);
    CHECK(rh >= 80 && rx + rw <= 466 - 44, "a row is tall and stays out of the edge strip (h=%d right=%d)", rh, rx + rw);
    click(rx + rw - 10, ry + rh - 4);
    CHECK(pickOpen && pickTrainer == per - 1, "the view's last leader answers near its corner (trainer %u)", pickTrainer);
    pickOpen = false; gymOpen = true; gymPick = false;
    pump(2);
    // Second view: its first row is leader `per`, not leader 0.
    gymPage = 1;
    pump(2);
    gymRowRect(0, &rx, &ry, &rw, &rh);
    click(rx + 20, ry + 6);
    CHECK(pickOpen && pickTrainer == per, "the second view's first row is leader %d, not 0 (got %u)", per, pickTrainer);
    pickOpen = false; gymOpen = true; gymPick = false; gymPage = 0;
    pump(2);
    int dx, dy, dw, dh;
    uiGymDifRect(&dx, &dy, &dw, &dh);
    click(233, dy + dh - 4);
    CHECK(gymOpen && gymHard, "the EASY/HARD pill answers where it is drawn");
    click(233, dy + dh - 4);
    CHECK(!gymHard, "and toggles back");
    gymLanRect(&dx, &dy, &dw, &dh);
    click(dx + dw / 2, dy + 4);
    CHECK(lanOpen && !gymOpen, "the ladder's LAN BATTLE answers where it is drawn");
    lanOpen = false;
    pet.badges = 0;
  }
  {
    // The Pokedex: its chooser, then a taller grid.
    galleryOpen = true; galleryPick = true; rpickPage = 0; galleryDetail = 0; galleryDirty = true;
    pump(3);
    CHECK(uiTall() && uiRegionRowsPerPage() > 3, "the Pokedex chooser is full height (%d a view)", uiRegionRowsPerPage());
    click(uiRegionRowX(), uiRegionRowCenterY(0));
    CHECK(galleryOpen && !galleryPick && galleryRegion == 0, "its first row opens Kanto");
    galleryFilter = 0; galleryPage = 0; galleryDirty = true;
    pump(3);
    CHECK(uiTall(), "the grid is full height");
    const int per = galleryPerPage();
    CHECK(per > 16 && galleryPages() < 10, "more than 16 a view, so Kanto takes fewer than ten (%d, %u views)",
          per, galleryPages());
    // Second view, the last cell's lower-right corner.
    galleryPage = 1; galleryDirty = true;
    pump(2);
    int cx2, cy2, cw, ch;
    uiGalleryCellRect(per - 1, &cx2, &cy2, &cw, &ch);
    CHECK(cy2 + ch < H - 60 && cx2 + cw <= 466 - 44, "the grid stays on the canvas and out of the strip");
    click(cx2 + cw - 4, cy2 + ch - 4);
    CHECK(galleryDetail == galleryAt((uint16_t)(per + per - 1)),
          "the second view's last cell opens #%d (got #%d)", galleryAt((uint16_t)(2 * per - 1)), galleryDetail);
    CHECK(!uiTall(), "an entry's detail stays the board's square");
    click(233, H / 2);   // back to the grid
    pump(2);
    int ft, fb;
    uiGalleryFilterBand(&ft, &fb);
    click(233, fb - 3);
    CHECK(galleryFilter == 1 && galleryOpen, "the filter pill answers where it is drawn");
    galleryFilter = 0;
    click(233, ft - 6);
    CHECK(!galleryOpen, "a tap above the pill is the header, which leaves");
    galleryDirty = true;
  }

  // A SQUARE paged screen on the tall canvas: settings.
  clockOpen = true; clockPage = 0;
  gfx->frameReady = false;
  pump(5);
  CHECK(gfx->frameReady && gFrameSquareY == uiSquareOy(),
        "a square screen flushes and says where it sits (%d)", gFrameSquareY);
  clockOpen = false;
  pump(2);

  // The battle screen: full height, with its grid answering where it is drawn.
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
