// The phone's full-height canvas (android/PHONE_LAYOUT_PLAN.md), driven through
// the REAL setup()/loop()/handleTouch() at 466 x 1010 -- the shape a 1440x3120
// phone gets -- with a camera-cutout inset.
//
// What it proves, because the emulator's square build cannot:
//   - the pet screen's controls answer at the place they are DRAWN on a tall
//     canvas (buttons, the food popup, the header band), via the same position
//     functions the drawing uses;
//   - a SQUARE screen on the tall canvas still flushes and says where it sits
//     (a minigame, square by design);
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
#include "inventory.h"
extern bool exploreOpen, exploreHard, bagOpen, kbOpen, resetConfirm;
extern uint8_t exploreRegion, bagPage;
extern int clockH;
void uiExploreRects(int *regY, int *regH, int *difY, int *difH, int *btnY, int *btnH);
uint8_t bagPerPage();
uint8_t bagPages();
void bagRowRect(int i, int *x, int *y, int *w, int *h);
void uiClockTallOy(int *oyTime, int *oyOpts, int *split);
void setRowRect(int i, int *x, int *y, int *w, int *h);
void openKeyboardFor(uint8_t target);
void uiKbKeyRect(int i, int *x, int *y, int *w, int *h);
extern char nameBuf[];
void uiSheetMoveRect(int i, int *x, int *y, int *w, int *h);
void uiSheetBtnRect(bool left, int *x, int *y, int *w, int *h);
extern bool releaseConfirm;
extern uint8_t movePickParty;
extern bool trainOpen, memoOpen;
void leaveMemo();
void uiTrainRowRect(int i, int *x, int *y, int *w, int *h);
void uiTrainPanelRect(int *x, int *y, int *w, int *h);
void uiConfirmRects(int *b1Top, int *b1Bot, int *b2Top, int *b2Bot);
uint8_t audioVolume();
void uiMenuRowRect(int i, int *x, int *y, int *w, int *h);
void uiMenuPanelRect(int *x, int *y, int *w, int *h);
uint8_t pickPerPage();
void uiPickCellRect(int i, int *x, int *y, int *w, int *h);
void uiPickButtonRect(bool go, int *x, int *y, int *w, int *h);
void pickDefault(uint8_t cap);
uint8_t pickChosen();
extern uint16_t squadMask;
extern uint8_t pickPage, pickRegion;
extern bool pickHard;
uint8_t movePickPerPage();
void uiMovePickRowRect(int i, int *x, int *y, int *w, int *h);
void uiLearnRowRect(int i, int *x, int *y, int *w, int *h);
void uiLearnSkipRect(int *x, int *y, int *w, int *h);
uint8_t learnableList(uint8_t *out, uint8_t max);
extern uint8_t movePickParty, movePickPage;
extern uint32_t btlWinUntil;
void audioSetVolume(uint8_t v);

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
  // The menu is a sheet across the canvas, CLOSE nearest the thumb, and every
  // row answers where it is drawn. Taps aim near row EDGES: a centred tap
  // would also fall inside the board's row of the same index, shifted down,
  // and pass with the tall layout forgotten.
  gfx->frameReady = false;
  pump(5);
  CHECK(uiTall() && gfx->frameReady && gFrameSquareY == -1, "the menu is full height and flushes as such (%d)", gFrameSquareY);
  {
    int px, py, pw, ph, rx, ry, rw, rh;
    uiMenuPanelRect(&px, &py, &pw, &ph);
    CHECK(py > hb, "the sheet starts below the header band that opened it (%d > %d)", py, hb);
    uiMenuRowRect(5, &rx, &ry, &rw, &rh);
    CHECK(ry + rh <= H - gInsetBot && ry + rh >= H - gInsetBot - 80,
          "CLOSE is the last row, by the bottom edge (bottom %d)", ry + rh);
    CHECK(rh >= uiTapFinger() && rw > 466 / 2 + 100, "a row is finger-sized and spans the width (%dx%d)", rw, rh);
    // the gap between two rows is inside the sheet: nothing happens
    int r1y;
    uiMenuRowRect(0, &rx, &ry, &rw, &rh);
    uiMenuRowRect(1, nullptr, &r1y, nullptr, nullptr);
    click(233, (ry + rh + r1y) / 2);
    CHECK(menuOpen && !cardOpen && !bagOpen, "a tap between rows keeps the menu open");
    // BAG at its top-left corner
    uiMenuRowRect(1, &rx, &ry, &rw, &rh);
    click(rx + 3, ry + 3);
    CHECK(!menuOpen && bagOpen, "BAG answers at its top-left corner");
    bagOpen = false; pump(2);
    // SETTINGS at its bottom-right corner
    menuOpen = true; pump(2);
    uiMenuRowRect(3, &rx, &ry, &rw, &rh);
    click(rx + rw - 3, ry + rh - 3);
    CHECK(!menuOpen && clockOpen, "SETTINGS answers at its bottom-right corner");
    clockOpen = false; pump(2);
    // CLOSE at its lower edge shuts the menu and opens nothing
    menuOpen = true; pump(2);
    uiMenuRowRect(5, &rx, &ry, &rw, &rh);
    click(233, ry + rh - 3);
    CHECK(!menuOpen && !cardOpen && !bagOpen && !clockOpen && !galleryOpen && !trainOpen,
          "CLOSE answers at its lower edge and returns to the pet");
    // above the sheet is the dimmed pet: a tap there closes, as on the board
    menuOpen = true; pump(2);
    click(233, py - 12);
    CHECK(!menuOpen && !cardOpen, "a tap above the sheet closes the menu");
    menuOpen = true; pump(2);
    uiMenuRowRect(0, &rx, &ry, &rw, &rh);
    click(233, ry + rh - 3);
  }
  CHECK(!menuOpen && cardOpen && cardPage == 1, "the menu's STATS row lands near its lower edge");

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
  gfx->frameReady = false;
  pump(5);
  CHECK(partyDetail == 3 && uiTall() && gfx->frameReady && gFrameSquareY == -1,
        "slot 3's lower-left corner opens its sheet, full height (%d)", gFrameSquareY);
  // The sheet: its rows and buttons answer where drawn. Corners, not centres,
  // for the menu's reason -- a centre can coincide with the board's layout.
  {
    int rx, ry, rw, rh, bx, by, bw, bh;
    uiSheetMoveRect(3, &rx, &ry, &rw, &rh);
    uiSheetBtnRect(true, &bx, &by, &bw, &bh);
    CHECK(rh >= uiTapFinger() && ry + rh < by, "a move row is finger-sized and above the buttons (%d tall)", rh);
    CHECK(bh >= uiTapFinger() && by + bh <= H - gInsetBot - 40 && bx < 233 && 233 < bx + bw,
          "the primary is finger-sized, by the bottom edge, and owns the centre line");
    click(rx + rw - 3, ry + rh - 3);
    CHECK(movePickOpen && movePickParty == 3 && movePickSlot == 3,
          "the 4th move row answers at its bottom-right corner (party %u slot %u)", movePickParty, movePickSlot);
    movePickOpen = false;
    pump(3);
    CHECK(partyDetail == 3, "and the picker returns to the sheet");
    uiSheetBtnRect(false, &bx, &by, &bw, &bh);
    click(bx + bw - 3, by + bh - 3);
    CHECK(boxOpen && !partyDetail, "TO BOX answers at its bottom-right corner");
    boxOpen = false;
    pump(3);
  }
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
  pump(3);
  CHECK(boxDetail == 9 && uiTall(), "cell 9 of the first view opens box slot 9's sheet, full height (%u)", boxDetail);
  {
    // RELEASE asks first; the confirm is drawn centred and NO answers there
    int bx, by, bw, bh, c1t, c1b, c2t, c2b;
    uiSheetBtnRect(false, &bx, &by, &bw, &bh);
    click(bx + 3, by + 3);
    CHECK(releaseConfirm && boxDetail == 9, "RELEASE answers at its top-left corner, and asks");
    uiConfirmRects(&c1t, &c1b, &c2t, &c2b);
    click(233, uiSquareOy() + (c2t + c2b) / 2);
    CHECK(!releaseConfirm && boxDetail == 9 && !party.box[8].empty(),
          "NO answers where the centred confirm draws it, and keeps the creature");
  }
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
    pump(5);   // past one 100 ms render period
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

  // ---- explore, the bag, settings (Phase 4 step 5) ----
  {
    uiTileGo(3);   // EXPLORE
    pump(3);
    CHECK(exploreOpen && uiTall(), "explore is full height");
    gfx->frameReady = false;
    pump(5);   // past one 100 ms render period
    CHECK(gfx->frameReady && gFrameSquareY == -1, "and flushes as a full-height frame (%d)", gFrameSquareY);
    int regY, regH, difY, difH, btnY, btnH;
    uiExploreRects(&regY, &regH, &difY, &difH, &btnY, &btnH);
    CHECK(regY > H / 2 && btnY > difY && difY > regY && btnH >= uiTapFinger(),
          "its controls sit in the lower half, EXPLORE nearest the thumb (reg %d dif %d btn %d)", regY, difY, btnY);
    const uint8_t r0 = exploreRegion;
    click(466 - 24 - 10, regY + regH - 4);   // the right arrow, near its lower corner
    CHECK(exploreRegion != r0, "the region's right arrow answers where it is drawn (%u -> %u)", r0, exploreRegion);
    click(30, regY + regH - 4);              // and the left one
    CHECK(exploreRegion == r0, "the left arrow steps back (%u)", exploreRegion);
    const bool h0 = exploreHard;
    click(233, difY + difH - 3);
    CHECK(exploreHard != h0, "the EASY/HARD pill answers near its lower edge");
    click(233, difY + difH - 3);
    click(233, btnY + btnH - 4);
    CHECK(battleOpen && !exploreOpen, "EXPLORE answers near its lower edge and starts a wild fight");
    battleOpen = false;
    pump(2);
  }
  {
    // Every item, so the bag spans more than one view.
    for (ItemKey k = IT_POKEBALL; k < ITEM_COUNT; k++) bag.add(k, 2);
    bagOpen = true; bagPage = 0;
    pump(3);
    CHECK(uiTall(), "the bag is full height");
    const int per = bagPerPage();
    CHECK(per > 5 && bagPages() == (bag.distinctCount() + per - 1) / per,
          "more than the board's five rows a view (%d), and the views follow (%u)", per, bagPages());
    // PROTEIN, wherever it is: its view, then its row near the lower edge.
    uint8_t idx = 0;
    while (idx < bag.distinctCount() && bag.keyAt(idx) != IT_PROTEIN) idx++;
    CHECK(idx >= per, "PROTEIN is past the first view (index %u), so the mapping is exercised", idx);
    bagPage = (uint8_t)(idx / per);
    pump(2);
    int rx, ry, rw, rh;
    bagRowRect(idx % per, &rx, &ry, &rw, &rh);
    pet.trAtk = 0;
    const uint8_t had = bag.count(IT_PROTEIN);
    click(rx + rw - 10, ry + rh - 4);
    CHECK(bag.count(IT_PROTEIN) == had - 1 && pet.trAtk > 0,
          "PROTEIN's row answers where it is drawn (count %u -> %u, atk %u)", had, bag.count(IT_PROTEIN), pet.trAtk);
    pump(80);   // let the message go
    click(233, H - 60);
    CHECK(!bagOpen, "a tap off the rows closes the bag");
  }
  {
    clockOpen = true; clockPage = 0;
    pump(3);
    CHECK(uiTall() && uiRimPages() == 1, "settings is full height on one view (%u)", uiRimPages());
    int oyT, oyO, split;
    uiClockTallOy(&oyT, &oyO, &split);
    // the clock section: HOUR + (board x 170..228, y 190..248)
    const int h0 = clockH;
    click(199, oyT + 248 - 3);
    CHECK(clockH == (h0 + 1) % 24, "the clock's HOUR + answers in the top section (%d -> %d)", h0, clockH);
    // the options section: VOL + at the row's right end
    audioSetVolume(5);
    int rx, ry, rw, rh;
    setRowRect(1, &rx, &ry, &rw, &rh);
    click(rx + rw - 10, oyO + ry + rh - 3);
    CHECK(audioVolume() == 6, "VOL + answers in the lower section (%u)", audioVolume());
    // RESET opens the confirm; NO closes it, where the centred dialog draws it
    setRowRect(4, &rx, &ry, &rw, &rh);
    click(233, oyO + ry + rh / 2);
    CHECK(resetConfirm, "RESET asks first");
    int c1t, c1b, c2t, c2b;
    uiConfirmRects(&c1t, &c1b, &c2t, &c2b);
    click(233, uiSquareOy() + (c2t + c2b) / 2);
    CHECK(clockOpen && !resetConfirm, "NO answers where the centred confirm draws it");
    clockOpen = false;
    pump(2);
  }

  // The training submenu: the menu's sheet arrangement, rows answering where
  // drawn. Taps aim at row EDGES, for the menu's reason: a centred tap would
  // also land in the board's row of the same index shifted into the square.
  {
    trainOpen = true;
    gfx->frameReady = false;
    pump(5);
    CHECK(uiTall() && gfx->frameReady && gFrameSquareY == -1,
          "training is full height and flushes as such (%d)", gFrameSquareY);
    int px, py, pw, ph, rx, ry, rw, rh;
    uiTrainPanelRect(&px, &py, &pw, &ph);
    CHECK(py + ph <= H - gInsetBot && py + ph >= H - gInsetBot - 80,
          "the sheet ends by the bottom edge (%d)", py + ph);
    uiTrainRowRect(0, &rx, &ry, &rw, &rh);
    CHECK(rh >= uiTapFinger() && rw > 466 / 2 + 100,
          "a training row is finger-sized and spans the width (%dx%d)", rw, rh);
    // between two rows: inside the sheet, nothing starts
    int r1y;
    uiTrainRowRect(1, nullptr, &r1y, nullptr, nullptr);
    click(233, (ry + rh + r1y) / 2);
    CHECK(trainOpen && !memoOpen, "a tap between training rows keeps it open");
    // above the sheet is the dimmed pet: a tap there closes it
    click(233, py - 12);
    CHECK(!trainOpen && !memoOpen, "a tap above the sheet closes training");
    // MEMORY, the last row, at its bottom-right corner
    trainOpen = true; pump(2);
    uiTrainRowRect(3, &rx, &ry, &rw, &rh);
    click(rx + rw - 3, ry + rh - 3);
    CHECK(!trainOpen && memoOpen, "MEMORY answers at its bottom-right corner");
    // A SQUARE screen on the tall canvas: the memory game, square by design.
    gfx->frameReady = false;
    pump(5);
    CHECK(!uiTall() && gfx->frameReady && gFrameSquareY == uiSquareOy(),
          "a square screen flushes and says where it sits (%d)", gFrameSquareY);
    leaveMemo();
    pump(2);
  }

  // The keyboard (step 6): bigger keys at the bottom, answering where drawn.
  {
    openKeyboardFor(1);   // KB_TRAINER: commits to the trainer name
    gfx->frameReady = false;
    pump(5);
    CHECK(kbOpen && uiTall() && gfx->frameReady && gFrameSquareY == -1,
          "the keyboard is full height and flushes as such (%d)", gFrameSquareY);
    int kx, ky, kw, kh, ax, ay, aw, ah;
    uiKbKeyRect(29, &kx, &ky, &kw, &kh);
    CHECK(ky + kh <= H - gInsetBot && ky + kh >= H - gInsetBot - 60,
          "the keys end just above the bottom edge, by the thumb (bottom %d)", ky + kh);
    CHECK(kh >= uiTapFinger(), "a key is finger-sized (%d >= %d)", kh, uiTapFinger());
    uiKbKeyRect(0, &ax, &ay, &aw, &ah);
    CHECK(ay > H / 3, "and start below the name field's third (y=%d)", ay);
    // clear whatever name is there, at DEL's top-left corner
    uiKbKeyRect(28, &kx, &ky, &kw, &kh);
    for (int i = 0; i < 12; i++) click(kx + 3, ky + 3);
    CHECK(nameBuf[0] == 0, "DEL answers at its top-left corner (left '%s')", nameBuf);
    // A at its bottom-right corner, Z (25) at its top-left, O (14) in its middle
    click(ax + aw - 3, ay + ah - 3);
    uiKbKeyRect(25, &kx, &ky, &kw, &kh);
    click(kx + 3, ky + 3);
    uiKbKeyRect(14, &kx, &ky, &kw, &kh);
    click(kx + kw / 2, ky + kh / 2);
    CHECK(!strcmp(nameBuf, "AZO"), "keys answer at their corners ('%s')", nameBuf);
    // The board's A, shifted as a square screen would be: under the field on
    // the phone, so it must not type anything. Nor may the band just above A.
    click(40 + 29, 150 + 23 + uiSquareOy());
    click(ax + aw / 2, ay - 8);
    CHECK(!strcmp(nameBuf, "AZO"), "nothing above the keys types ('%s')", nameBuf);
    uiKbKeyRect(29, &kx, &ky, &kw, &kh);
    click(kx + kw - 3, ky + kh - 3);
    CHECK(!kbOpen && !strcmp(pet.trainerName, "AZO"),
          "OK at its bottom-right corner commits ('%s')", pet.trainerName);
    pump(2);
  }

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

  // The move picker: bigger rows, more of them a view, each answering where
  // it is drawn. A banked level 60 member has a learnset long enough to page.
  {
    battleOpen = false;
    pump(2);
    PartyMon m; m.dex = 6; m.level = 60;   // CHARIZARD
    for (int k = 0; k < 4; k++) m.moves[k] = 0;
    party.replaceAt(0, m);
    movePickParty = 1; movePickSlot = 2; movePickPage = 0;
    movePickOpen = true;
    gfx->frameReady = false;
    pump(5);
    uint8_t all[64];
    const uint8_t n = learnableList(all, sizeof(all));
    const uint8_t per = movePickPerPage();
    CHECK(movePickOpen && uiTall() && gfx->frameReady && gFrameSquareY == -1,
          "the move picker is full height and flushes as such (%d)", gFrameSquareY);
    CHECK(n > per && per > 5 && uiRimPages() == (n + per - 1) / per,
          "more than the board's five rows a view (%u of %u), and the views follow (%u)", per, n, uiRimPages());
    int rx, ry, rw, rh;
    uiMovePickRowRect(per - 1, &rx, &ry, &rw, &rh);
    CHECK(rh >= uiTapFinger() - 12 && rw > 466 / 2 + 100 && ry + rh < H - gInsetBot - 60,
          "a row is tall, wide, and the view's last stays above BACK (%dx%d, bottom %d)", rw, rh, ry + rh);
    // the view's last row, at its top-left corner
    click(rx + 3, ry + 3);
    CHECK(!movePickOpen && party.slots[0].moves[2] == all[per - 1],
          "the view's last row answers at its corner (slot holds %u, want %u)", party.slots[0].moves[2], all[per - 1]);
    // the second view's first row, at its bottom-right corner: move `per`, not 5
    movePickOpen = true; movePickPage = 1; movePickSlot = 3;
    pump(3);
    uiMovePickRowRect(0, &rx, &ry, &rw, &rh);
    click(rx + rw - 3, ry + rh - 3);
    CHECK(!movePickOpen && party.slots[0].moves[3] == all[per],
          "the second view's first row is move %u (slot holds %u, want %u)", per, party.slots[0].moves[3], all[per]);
    // the edge strip pages; a tap off the rows backs out
    movePickOpen = true; movePickPage = 0;
    pump(3);
    drag(466 - 20, 300, 466 - 20, 300 + 3 * 40 + 10, 8);
    // the strip SCRUBS, so a long drag may cross several pages; it must move
    CHECK(movePickOpen && movePickPage > 0, "the edge strip pages the picker (page %u)", movePickPage);
    click(233, H - gInsetBot - 20);
    CHECK(!movePickOpen, "a tap off the rows backs out");
    movePickParty = 0; movePickPage = 0;
    pump(2);
  }

  // The learn prompt: the offer at the top as a full row, your four by the
  // thumb, SKIP at the bottom. Raised the way the --shot does it, on the
  // shot's CHARIZARD: a level 1 CHARMANDER knows all its level 1 moves
  // already, so it would have nothing to offer.
  {
    pet.dbgHatchAs(6, false);
    pet.sleeping = false;
    // Re-hatching leaves whatever was queued for the previous creature;
    // answer it first, or the offer below can be a move already known.
    for (int k = 0; k < 16 && pet.hasLearnOffer(); k++) pet.declineLearn();
    pet.relearnFromLevel();
    pet.lastLearnLevel = 0;
    pet.checkLearnGates();
    gfx->frameReady = false;
    pump(5);
    CHECK(pet.hasLearnOffer() && uiTall() && gfx->frameReady && gFrameSquareY == -1,
          "the learn prompt is full height and flushes as such (%d)", gFrameSquareY);
    int rx, ry, rw, rh, sx, sy, sw, sh;
    uiLearnSkipRect(&sx, &sy, &sw, &sh);
    uiLearnRowRect(3, &rx, &ry, &rw, &rh);
    CHECK(sh >= uiTapFinger() && sy + sh >= H - gInsetBot - 80 && ry + rh < sy,
          "SKIP is finger-sized at the bottom, under the fourth row (%d..%d, row ends %d)", sy, sy + sh, ry + rh);
    CHECK(rh >= uiTapFinger(), "a row is finger-sized (%d)", rh);
    // the gap between the offer and the four answers nothing: the prompt is modal
    const uint8_t offer = pet.learnOffer();
    uint8_t before[4];
    for (int k = 0; k < 4; k++) before[k] = pet.moves[k];
    uiLearnRowRect(0, &rx, &ry, &rw, &rh);
    click(233, ry - 20);
    CHECK(pet.hasLearnOffer() && pet.learnOffer() == offer && !memcmp(before, pet.moves, 4),
          "a tap above the four does nothing");
    // the fourth row at its bottom-right corner takes the offer
    uiLearnRowRect(3, &rx, &ry, &rw, &rh);
    click(rx + rw - 3, ry + rh - 3);
    CHECK(before[3] != offer && pet.moves[3] == offer && !memcmp(before, pet.moves, 3),
          "the fourth row answers at its corner (slot 3 holds %u, want %u)", pet.moves[3], offer);
    // SKIP at its top-left corner, for every offer left -- at least one, so
    // the check below cannot pass by there being nothing to skip
    if (!pet.hasLearnOffer()) { pet.lastLearnLevel = 0; pet.checkLearnGates(); }
    CHECK(pet.hasLearnOffer(), "there is an offer left to skip");
    int guard = 0;
    while (pet.hasLearnOffer() && guard++ < 16) {
      uint8_t mv[4];
      for (int k = 0; k < 4; k++) mv[k] = pet.moves[k];
      const uint8_t o = pet.learnOffer();
      pump(2);
      click(sx + 3, sy + 3);
      if (!memcmp(mv, pet.moves, 4) && (!pet.hasLearnOffer() || pet.learnOffer() != o)) continue;
      guard = 99;
    }
    CHECK(!pet.hasLearnOffer() && guard >= 1 && guard < 99,
          "SKIP answers at its corner and changes no move (%d skipped)", guard);
    pump(2);
  }

  // Team select: every candidate in one view, BACK and FIGHT by the thumb.
  // Last in the file because FIGHT starts a battle.
  {
    battleOpen = false;
    pump(2);
    for (int i = 0; i < 6; i++) {
      PartyMon m; m.dex = (int16_t)(1 + 3 * i); m.level = (uint8_t)(30 + i);
      party.replaceAt(i, m);
    }
    pickRegion = 0; gymRegion = 0;
    pickTrainer = 0; pickHard = true;   // BROCK on hard: a cap of 2
    pickPage = 0;
    pickDefault(2);
    pickOpen = true;
    gfx->frameReady = false;
    pump(5);
    CHECK(pickOpen && uiTall() && gfx->frameReady && gFrameSquareY == -1,
          "team select is full height and flushes as such (%d)", gFrameSquareY);
    CHECK(pickPerPage() >= 7 && uiRimPages() == 1,
          "all seven candidates share one view (%u a view, %u views)", pickPerPage(), uiRimPages());
    int cx0, cy0, cw, ch, bx, by, bw, bh;
    uiPickCellRect(6, &cx0, &cy0, &cw, &ch);
    CHECK(cw >= uiTapFinger() && ch >= uiTapFinger(), "a card is finger-sized (%dx%d)", cw, ch);
    uiPickButtonRect(true, &bx, &by, &bw, &bh);
    CHECK(bh >= uiTapFinger() && by > cy0 + ch && by + bh >= H - gInsetBot - 80,
          "FIGHT is finger-sized, under the grid, by the bottom edge (y=%d h=%d)", by, bh);
    // the seventh candidate, at its bottom-right corner: off the board's page
    click(cx0 + cw - 3, cy0 + ch - 3);
    CHECK((squadMask & (1 << 6)) && pickChosen() == 3, "the 7th card answers at its corner (chosen %u)", pickChosen());
    click(bx + bw / 2, by + bh / 2);
    CHECK(pickOpen && !battleOpen, "FIGHT stays inert over the cap");
    // the first card, at its top-left corner, back under the cap
    uiPickCellRect(0, &cx0, &cy0, &cw, &ch);
    click(cx0 + 3, cy0 + 3);
    CHECK(!(squadMask & 1) && pickChosen() == 2, "the 1st card answers at its corner (chosen %u)", pickChosen());
    // BACK at its top-left corner
    uiPickButtonRect(false, &bx, &by, &bw, &bh);
    click(bx + 3, by + 3);
    CHECK(!pickOpen && gymOpen, "BACK answers at its corner and returns to the gym");
    gymOpen = false;
    pickOpen = true; pump(3);
    uiPickButtonRect(true, &bx, &by, &bw, &bh);
    click(bx + bw - 3, by + bh - 3);
    CHECK(!pickOpen && battleOpen, "FIGHT answers at its corner and starts the fight");

    // ...and that fight's win screen, at full height like the battle it
    // follows, not a square dropped into the middle of it.
    btlWinUntil = millis() + 60000;
    gfx->frameReady = false;
    pump(5);
    CHECK(battleOpen && uiTall() && gfx->frameReady && gFrameSquareY == -1,
          "the win screen is full height and flushes as such (%d)", gFrameSquareY);
    click(233, H - gInsetBot - 30);   // the BACK hint at the bottom edge
    CHECK(!battleOpen && btlWinUntil == 0, "a tap at the BACK hint leaves the fight");
  }

  printf(fails ? "\n%d FAILED\n" : "\nPASS tall_test\n", fails);
  return fails ? 1 : 0;
}
