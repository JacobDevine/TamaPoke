// The farm and the album (fork.h section 13; android/FEATURES_PLAN.md Phase 9):
// every ending recorded through the real ceremony in Pet::update(), its dates,
// the dedupe of a replayed ceremony, the cap, the record through a restart and
// a later build's layout, the album's pages on the medals screen, who is out on
// the farm and their daily turns, the FARM tab through the real party screen,
// the visitor on the pet screen -- and that none of it changes a banked
// creature, which upstream keeps frozen on purpose (CLAUDE.md § "B3").
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "dex.h"
#include "save.h"
#include "fork.h"
#include "fork_i18n.h"
#include "ckpt.h"
#include <string.h>
#include <set>
#include <vector>

uint32_t g_seed = 0xF4A2;
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
extern bool gPanelRound;
extern bool partyOpen, boxOpen, medalsOpen, partyPick;
extern uint8_t medalsPage;
extern uint8_t partyDetail;
extern void (*gHostSaveBackup)();
extern void (*gHostSaveRestore)();
uint8_t uiCurrentScreen();
int uiTileNamed(const char *n);
int uiTileIndex();
void uiTileGo(int i);
void emuAdvanceMs(uint32_t ms);
void emuSetClockPaused(bool paused);
void rtcSetEpoch(uint32_t e);
uint32_t rtcEpoch();
bool forkFarmShowing();
bool uiFarmSpot(int i, int *cx, int *cy);
int8_t uiFarmSelected();
bool partyHubAt(int16_t x, int16_t y);
bool partyCloseAt(int16_t x, int16_t y);

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

static uint32_t localEpoch(int y, int m, int d, int hh, int mm = 0) {
  return forkDayNumber((uint16_t)y, (uint8_t)m, (uint8_t)d) * 86400u + hh * 3600u + mm * 60u;
}

static std::vector<uint16_t> frame() {
  render();
  const uint16_t *b = gfx->buffer();
  return std::vector<uint16_t>(b, b + 466 * H);
}

// FARM_SHOTS=<dir> writes the frames it looks at as PPMs, for a person to see.
static void dump(const char *name, const std::vector<uint16_t> &f) {
  const char *dir = getenv("FARM_SHOTS");
  if (!dir) return;
  char path[512];
  snprintf(path, sizeof(path), "%s/%s.ppm", dir, name);
  FILE *o = fopen(path, "wb");
  if (!o) return;
  fprintf(o, "P6 466 %d 255\n", H);
  for (uint16_t c : f) {
    const uint8_t rgb[3] = { (uint8_t)((c >> 11) << 3), (uint8_t)(((c >> 5) & 63) << 2), (uint8_t)((c & 31) << 3) };
    fwrite(rgb, 1, 3, o);
  }
  fclose(o);
}

// A hatched, awake creature of this species, dated.
static void fresh(int16_t dex, uint32_t epoch) {
  pet.dbgHatchAs(dex, false);
  while (pet.hasLearnOffer()) pet.declineLearn();
  pet.sleeping = false;
  pet.fullness = pet.joy = pet.energy = pet.hygiene = 70;
  pet.lastSeenEpoch = epoch;
  forkHolidayPoll(pet, epoch);   // stamps the hatch day, as the 30 s clock does
}

// Lets the ceremony run out through the sketch's own loop.
static void finishCeremony() {
  step(600, 20);   // 12 s > CEREMONY_MS
}

static PartyMon mon(int16_t dex, uint16_t level, const char *nick = "") {
  PartyMon m;
  m.dex = dex;
  m.level = level;
  m.ivAtk = (uint8_t)(dex % 32); m.ivDef = 9; m.ivSpe = 12; m.ivHp = 20;
  m.stateVersion = 1;
  snprintf(m.nick, sizeof(m.nick), "%s", nick);
  return m;
}

static std::vector<uint8_t> banked() {
  std::vector<uint8_t> v((const uint8_t *)party.slots, (const uint8_t *)(party.slots + PARTY_SLOTS));
  if (party.box.data())
    v.insert(v.end(), (const uint8_t *)party.box.data(), (const uint8_t *)(party.box.data() + BOX_SLOTS));
  return v;
}

int main() {
  // ===================== the board: none of it =====================
  {
    ForkFarmMon out[FORK_FARM_SHOWN];
    uint16_t total = 99;
    CHECK(forkFarmPick(party, 1, out, &total) == 0 && total == 0, "switch off: nobody on the farm");
    CHECK(forkAlbumPages() == 0 && forkAlbumCount() == 0, "switch off: the album has no pages");
    forkVisitPoll(pet, party, true, 100000, 7);
    CHECK(!forkVisitor(100001, nullptr, nullptr), "switch off: no visitor");
  }
  {
    bool a = false, b = false;
    for (uint16_t i = 0; i < SAVE_FIELD_COUNT; i++) {
      if (!strcmp(SAVE_FIELDS[i].key, "albA")) a = true;
      if (!strcmp(SAVE_FIELDS[i].key, "albB")) b = true;
    }
    CHECK(a && b, "both album slots ride in every backup (SAVE_FIELDS)");
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
  const uint32_t day0 = localEpoch(2026, 3, 12, 10);
  rtcSetEpoch(day0);
  setup();
  emuSetClockPaused(true);
  step(3);
  if (pet.awaitingStarter()) pet.chooseStarter(4);
  CHECK(forkAlbumCount() == 0 && forkAlbumPages() == 1, "a new save: an empty album, with one page to say so");

  // ---- every ending, through the real ceremony ----
  fresh(25, day0);                         // PIKACHU, hatched 12 March
  snprintf(pet.nick, sizeof(pet.nick), "SPARKY");
  pet.medals = 3;
  const uint32_t left1 = localEpoch(2026, 3, 20, 9);
  pet.lastSeenEpoch = left1;
  const uint16_t lv1 = (uint16_t)pet.level();
  const uint32_t key1 = forkCreatureKey(pet);
  pet.startRunaway();
  CHECK(forkAlbumCount() == 0, "nothing is written while the ceremony is still on screen");
  finishCeremony();
  ForkAlbumEntry e;
  CHECK(forkAlbumCount() == 1 && forkAlbumGet(0, &e), "a runaway is in the album once its ceremony ends");
  CHECK(e.ending == FORK_END_RUNAWAY && e.dex == 25 && e.level == lv1 && e.medals == 3 &&
        !strcmp(e.nick, "SPARKY") && e.key == key1, "with its species, level, medals, nickname and key");
  CHECK(e.hatchDay == day0 / 86400 && e.leftDay == left1 / 86400,
        "and the days it hatched and left (%lu, %lu)", (unsigned long)e.hatchDay, (unsigned long)e.leftDay);
  CHECK(pet.isEgg(), "and the ending itself still happened: an egg is waiting");

  fresh(4, left1);
  pet.release();
  finishCeremony();
  CHECK(forkAlbumCount() == 2 && forkAlbumGet(0, &e) && e.ending == FORK_END_RELEASED && e.dex == 4,
        "a release, newest first");

  fresh(7, left1);
  CHECK(pet.canRetireNow() && !pet.retireIsFree(), "a young creature can be retired early");
  pet.startRetire();
  finishCeremony();
  CHECK(forkAlbumCount() == 3 && forkAlbumGet(0, &e) && e.ending == FORK_END_RETIRED,
        "an early retire is RETIRED, not a farewell (the ceremony is the same one)");
  partyPick = false;

  fresh(1, left1);
  pet.startFarewell();
  finishCeremony();
  CHECK(forkAlbumCount() == 4 && forkAlbumGet(0, &e) && e.ending == FORK_END_FAREWELL && e.dex == 1,
        "an earned farewell is a FAREWELL");
  partyPick = false;
  CHECK(forkAlbumEver() == 4 && forkAlbumPages() == 2, "four lives: two pages of three");

  // ---- a ceremony replayed after a cut is not entered twice ----
  fresh(133, left1);
  pet.ceremony = CER_RUNAWAY;
  CHECK(forkAlbumRecord(pet, left1), "recorded once");
  CHECK(!forkAlbumRecord(pet, left1) && forkAlbumCount() == 5,
        "the same creature leaving the same way again is the same entry");
  pet.ceremony = CER_NONE;
  CHECK(!forkAlbumRecord(pet, left1), "outside a ceremony there is nothing to record");

  // ---- through a restart ----
  {
    std::vector<ForkAlbumEntry> before;
    for (uint16_t i = 0; i < forkAlbumCount(); i++) { forkAlbumGet(i, &e); before.push_back(e); }
    const uint32_t ever = forkAlbumEver();
    forkData.begin();
    bool same = forkAlbumCount() == before.size() && forkAlbumEver() == ever;
    for (uint16_t i = 0; same && i < before.size(); i++) {
      forkAlbumGet(i, &e);
      same = !memcmp(&e, &before[i], sizeof(e));
    }
    CHECK(same, "the whole album reads back after a restart (%u entries)", (unsigned)forkAlbumCount());
  }

  // ---- the codec: a later build's layout ----
  {
    static uint8_t buf[FORK_ALBUM_BLOB_CAP];
    const uint16_t n = forkAlbumEncode(buf, sizeof(buf), 7);
    const uint16_t count = forkAlbumCount();
    // The same entries at 40 bytes each, the extra 8 a later build's.
    static uint8_t big[FORK_ALBUM_BLOB_CAP];
    memset(big, 0, sizeof(big));
    uint8_t *b = big + CKPT_HDR;
    ckptWr16(b + 0, 40);
    ckptWr16(b + 2, count);
    ckptWr32(b + 4, 9);
    for (uint16_t i = 0; i < count; i++) {
      memcpy(b + FORK_ALBUM_HDR + i * 40, buf + CKPT_HDR + FORK_ALBUM_HDR + i * FORK_ALBUM_ENTRY, FORK_ALBUM_ENTRY);
      memset(b + FORK_ALBUM_HDR + i * 40 + FORK_ALBUM_ENTRY, 0xAB, 8);
    }
    // ...and one more whose species this build does not have.
    uint8_t *x = b + FORK_ALBUM_HDR + count * 40;
    memcpy(x, buf + CKPT_HDR + FORK_ALBUM_HDR, FORK_ALBUM_ENTRY);
    ckptWr16(x + 4, (uint16_t)(DEX_COUNT + 5));
    ckptWr16(b + 2, (uint16_t)(count + 1));
    const uint16_t bn = ckptSeal(big, CKPT_HDR + FORK_ALBUM_HDR + (count + 1) * 40, FORK_ALBUM_MAGIC, 2, 8);
    std::vector<ForkAlbumEntry> want;
    for (uint16_t i = 0; i < count; i++) { forkAlbumGet(i, &e); want.push_back(e); }
    CHECK(forkAlbumDecode(big, bn), "a later build's bigger entries decode");
    bool same = forkAlbumCount() == count && forkAlbumEver() == 9;
    for (uint16_t i = 0; same && i < count; i++) { forkAlbumGet(i, &e); same = !memcmp(&e, &want[i], sizeof(e)); }
    CHECK(same, "by their prefix, and an entry for a species past DEX_COUNT is skipped, not read");
    ckptWr16(big + CKPT_HDR, 20);
    const uint16_t sn = ckptSeal(big, CKPT_HDR + FORK_ALBUM_HDR + (count + 1) * 40, FORK_ALBUM_MAGIC, 2, 8);
    CHECK(!forkAlbumDecode(big, sn), "an entry too short to hold today's fields is refused, not half read");
    CHECK(forkAlbumDecode(buf, n) && forkAlbumCount() == count, "today's own blob reads back");
  }

  // ---- the cap: the newest are kept ----
  {
    const uint32_t ever = forkAlbumEver();
    for (int i = 0; i < FORK_ALBUM_MAX + 6; i++) {
      fresh((int16_t)(10 + i), left1);
      pet.ceremony = CER_RELEASE;
      forkAlbumRecord(pet, left1);
      pet.ceremony = CER_NONE;
    }
    ForkAlbumEntry newest, oldest;
    forkAlbumGet(0, &newest);
    forkAlbumGet(FORK_ALBUM_MAX - 1, &oldest);
    CHECK(forkAlbumCount() == FORK_ALBUM_MAX && forkAlbumEver() == ever + FORK_ALBUM_MAX + 6,
          "full at %d, still counting lives ended (%lu)", FORK_ALBUM_MAX, (unsigned long)forkAlbumEver());
    CHECK(newest.dex == 10 + FORK_ALBUM_MAX + 5 && oldest.dex == 16,
          "the oldest dropped off the end (newest %d, oldest %d)", newest.dex, oldest.dex);
    forkData.begin();
    CHECK(forkAlbumCount() == FORK_ALBUM_MAX, "and a full album reads back whole");
  }

  // ---- the album on the medals screen ----
  {
    fresh(25, left1);
    step(3);
    uiTileGo(uiTileNamed("medals"));
    step(3);
    CHECK(medalsOpen, "the medals screen is open");
    medalsPage = 3;   // goals, medals, steps, then the album
    const std::vector<uint16_t> full = frame();
    dump("album", full);
    // Two albums of one entry each: the same page count, so the same scrollbar,
    // and only the album's own drawing can tell them apart.
    forkAlbumClear();
    // ...and the same live creature on screen both times (the tab bar shows it)
    fresh(25, left1); pet.ceremony = CER_RELEASE; forkAlbumRecord(pet, left1); pet.ceremony = CER_NONE;
    fresh(1, left1);
    const std::vector<uint16_t> one = frame();
    forkAlbumClear();
    fresh(4, left1); pet.ceremony = CER_RUNAWAY; forkAlbumRecord(pet, left1); pet.ceremony = CER_NONE;
    fresh(1, left1);
    const std::vector<uint16_t> other = frame();
    CHECK(one != other, "the album page draws the album: two one-entry albums look different");
    forkData.begin();
    CHECK(forkAlbumCount() == 1, "(reloaded: the last album written)");
    medalsPage = 2 + forkAlbumPages();
    frame();
    CHECK(medalsOpen, "the last album page draws");
    tap(233, 500);
    CHECK(!medalsOpen, "a tap backs out, as on every medals page");
  }

  // ===================== the farm =====================
  for (int i = 0; i < PARTY_SLOTS; i++) party.releaseAt(i);
  for (uint16_t i = 0; i < BOX_SLOTS; i++) if (!party.box[i].empty()) party.boxReleaseAt(i);
  {
    ForkFarmMon out[FORK_FARM_SHOWN];
    uint16_t total = 9;
    CHECK(forkFarmPick(party, 100, out, &total) == 0 && total == 0, "nobody banked: nobody on the farm");
    party.add(mon(25, 30, "A"));
    party.add(mon(4, 20));
    party.boxAdd(mon(7, 12));
    const uint8_t n = forkFarmPick(party, 100, out, &total);
    CHECK(n == 3 && total == 3 && !out[0].inBox && !out[1].inBox && out[2].inBox,
          "a few: all of them, the party first");
    for (int i = 0; i < 22; i++) party.boxAdd(mon((int16_t)(100 + i), 10));
    std::set<int16_t> seenAll;
    bool distinct = true, varies = false;
    int16_t first0 = -1;
    for (uint32_t d = 1000; d < 1000 + 60; d++) {
      const uint8_t k = forkFarmPick(party, d, out, &total);
      std::set<int16_t> today;
      for (int i = 0; i < k; i++) {
        const PartyMon *m = forkFarmMon(party, out[i]);
        today.insert(m->dex);
        seenAll.insert(m->dex);
      }
      if (k != FORK_FARM_SHOWN || today.size() != k) distinct = false;
      if (first0 < 0) first0 = forkFarmMon(party, out[0])->dex;
      else if (forkFarmMon(party, out[0])->dex != first0) varies = true;
    }
    CHECK(total == 25 && distinct, "many: %d a day, never the same one twice", FORK_FARM_SHOWN);
    forkSetFeatures(false);
    CHECK(forkFarmPick(party, 1000, out, &total) == 0 && total == 0, "switch off: nobody on the farm, with 25 banked");
    forkVisitReset();
    forkVisitPoll(pet, party, true, 1, 7);
    forkVisitPoll(pet, party, true, 1 + FORK_VISIT_FIRST_MS + 10, 7);
    CHECK(!forkVisitor(2 + FORK_VISIT_FIRST_MS + 10, nullptr, nullptr), "switch off: no visitor, with 25 banked");
    forkSetFeatures(true);
    forkVisitReset();
    CHECK(varies && seenAll.size() == 25, "a different run each day, and everybody gets a turn (%u of 25)",
          (unsigned)seenAll.size());
    std::set<int16_t> soon;
    for (uint32_t d = 5000; d < 5000 + (25 + FORK_FARM_SHOWN - 1) / FORK_FARM_SHOWN; d++) {
      const uint8_t k = forkFarmPick(party, d, out, &total);
      for (int i = 0; i < k; i++) soon.insert(forkFarmMon(party, out[i])->dex);
    }
    CHECK(soon.size() == 25, "within ceil(25/%d) days, from any day (%u of 25)", FORK_FARM_SHOWN, (unsigned)soon.size());
  }

  // ---- the FARM tab, through the real party screen ----
  const std::vector<uint8_t> bankBefore = banked();
  {
    uiTileGo(uiTileNamed("party"));
    step(3);
    CHECK(partyOpen && !forkFarmShowing(), "the party screen opens on PARTY");
    const std::vector<uint16_t> partyFrame = frame();
    tap(466 * 3 / 4, gInsetTop + 22 + 10);
    CHECK(forkFarmShowing(), "the right half of the title is the FARM tab");
    const std::vector<uint16_t> farmFrame = frame();
    dump("farm", farmFrame);
    CHECK(farmFrame != partyFrame, "and it draws the farm, not the party");
    int cx, cy;
    CHECK(uiFarmSpot(2, &cx, &cy), "creature 2 is out");
    tap(cx, cy);
    step(10);
    dump("farm_sel", frame());
    CHECK(uiFarmSelected() == 2, "tapping a creature where it is drawn picks it (%d)", uiFarmSelected());
    // the BOX hub's place on the party tab: nothing of the party answers here
    int hx = 233, hy = -1;
    for (int y = 100; y < H && hy < 0; y += 4) if (partyHubAt(hx, y)) hy = y + 10;
    CHECK(hy > 0, "found the party's BOX hub (y %d)", hy);
    tap(hx, hy);
    CHECK(partyOpen && !boxOpen && forkFarmShowing(), "on the farm, the hidden BOX hub does not open the box");
    tap(466 / 4, gInsetTop + 22 + 10);
    CHECK(partyOpen && !forkFarmShowing(), "the left half goes back to PARTY");
    tap(hx, hy);
    CHECK(boxOpen, "where the hub works again");
    boxOpen = false;
    step(3);
    tap(466 * 3 / 4, gInsetTop + 22 + 10);
    int clx = 233, cly = -1;
    for (int y = H - 1; y > 300 && cly < 0; y -= 4) if (partyCloseAt(clx, y)) cly = y - 10;
    tap(clx, cly);
    CHECK(!partyOpen, "the farm's CLOSE closes the party screen");
    partyPick = true;
    uiTileGo(uiTileNamed("party"));
    step(3);
    CHECK(partyOpen && !forkFarmShowing(), "a newcomer waiting for a slot: no farm, whatever tab was last");
    partyPick = false;
    uiTileGo(uiTileNamed("pet"));
    step(3);
  }

  // ===================== the visitor =====================
  {
    pet.sleeping = false;
    forkVisitReset();
    step(2);
    CHECK(uiTileIndex() == uiTileNamed("pet") && !partyOpen && !medalsOpen, "on the pet screen");
    uint32_t t0 = millis();
    while (millis() - t0 < FORK_VISIT_FIRST_MS - 1000) step(50, 20);
    CHECK(!forkVisitor(millis(), nullptr, nullptr), "nobody comes in the first %u s", FORK_VISIT_FIRST_MS / 1000);
    step(150, 20);
    ForkVisitor v;
    int x0 = 0, x1 = 0;
    CHECK(forkVisitor(millis(), &v, &x0), "then a banked creature comes by");
    bool known = false;
    for (int i = 0; i < PARTY_SLOTS; i++) if (party.slots[i].dex == v.dex) known = true;
    for (uint16_t i = 0; i < BOX_SLOTS; i++) if (party.box[i].dex == v.dex) known = true;
    CHECK(known, "one of yours (dex %d, %s)", v.dex, v.name);
    const std::vector<uint16_t> with = frame();
    step(100, 20);
    forkVisitor(millis(), nullptr, &x1);
    CHECK(v.dir > 0 ? x1 > x0 : x1 < x0, "walking the way it faces (%d -> %d)", x0, x1);
    t0 = millis();
    while (forkVisitor(millis(), nullptr, nullptr) && millis() - t0 < FORK_VISIT_MS) step(1, 20);
    CHECK(!forkVisitor(millis(), nullptr, nullptr), "and gone once across");
    t0 = millis();   // the gap runs from here
    while (millis() - t0 < FORK_VISIT_GAP_MS - 2000) step(100, 20);
    CHECK(!forkVisitor(millis(), nullptr, nullptr), "the next waits %u s", FORK_VISIT_GAP_MS / 1000);
    step(200, 20);
    CHECK(forkVisitor(millis(), nullptr, nullptr), "then another comes");
    std::vector<uint16_t> visited = frame();
    dump("visitor", visited);
    forkVisitReset();
    std::vector<uint16_t> without = frame();
    CHECK(visited != without, "a visitor is drawn on the pet screen");
    (void)with;

    // asleep: nobody
    forkVisitReset();
    pet.sleeping = true;
    step(10);
    t0 = millis();
    while (millis() - t0 < FORK_VISIT_FIRST_MS + 2000) step(100, 20);
    CHECK(!forkVisitor(millis(), nullptr, nullptr), "nobody visits a sleeping creature");
    pet.sleeping = false;
    step(5);
    // leaving the screen sends it home
    t0 = millis();
    while (millis() - t0 < FORK_VISIT_FIRST_MS + 2000) step(100, 20);
    CHECK(forkVisitor(millis(), nullptr, nullptr), "awake again, a visitor");
    uiTileGo(uiTileNamed("medals"));
    step(3);
    CHECK(!forkVisitor(millis(), nullptr, nullptr), "leaving the pet screen sends it home");
    medalsOpen = false;
    uiTileGo(uiTileNamed("pet"));
    step(3);
    // nobody banked: nobody visits
    for (int i = 0; i < PARTY_SLOTS; i++) party.releaseAt(i);
    for (uint16_t i = 0; i < BOX_SLOTS; i++) if (!party.box[i].empty()) party.boxReleaseAt(i);
    forkVisitReset();
    t0 = millis();
    while (millis() - t0 < FORK_VISIT_FIRST_MS + 2000) step(100, 20);
    CHECK(!forkVisitor(millis(), nullptr, nullptr), "with nobody banked, nobody visits");
  }

  // ---- and none of it touched a banked creature ----
  CHECK(bankBefore.size() > 0, "(the farm's creatures were captured)");
  {
    // restore the farm set and look at it and be visited by it again, then compare
    party.add(mon(25, 30, "A"));
    party.add(mon(4, 20));
    party.boxAdd(mon(7, 12));
    for (int i = 0; i < 22; i++) party.boxAdd(mon((int16_t)(100 + i), 10));
    const std::vector<uint8_t> b0 = banked();
    CHECK(b0 == bankBefore, "(the same farm again)");
    uiTileGo(uiTileNamed("party"));
    step(3);
    tap(466 * 3 / 4, gInsetTop + 22 + 10);
    for (int i = 0; i < 8; i++) { int cx, cy; if (uiFarmSpot(i, &cx, &cy)) tap(cx, cy); }
    step(500, 50);
    uiTileGo(uiTileNamed("pet"));
    step(3);
    uint32_t t0 = millis();
    while (millis() - t0 < FORK_VISIT_FIRST_MS + FORK_VISIT_MS + 2000) step(100, 20);
    CHECK(banked() == b0, "looking at the farm and being visited changes no banked creature (frozen)");
  }

  printf("%s (%d failures)\n", fails ? "FAILED" : "PASSED", fails);
  return fails ? 1 : 0;
}
