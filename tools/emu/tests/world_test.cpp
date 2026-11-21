// The world from the clock (fork.h section 11; android/FEATURES_PLAN.md Phase
// 8a): the calendar, the sun, the moon, the seasons, the holidays and their
// gifts, the player's birthday, the creature's hatch day, and the standby
// clock -- the last driven through the REAL sketch at the phone's canvas.
//
// The sun and the calendar are checked against PUBLISHED values (almanac
// sunrises, the dates of Easter, known new and full moons), not against a
// second copy of the formulas: a test that re-implements NOAA's equations
// would only prove they were typed twice the same way.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "fork.h"
#include "fork_i18n.h"
#include "inventory.h"
#include "ckpt.h"
#include <math.h>
#include <string.h>

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
extern bool gPanelRound, clockOpen, playerOpen;
extern uint32_t feedMenuUntil, lastInteract;
extern void (*gHostSaveBackup)();
extern void (*gHostSaveRestore)();
uint8_t uiCurrentScreen();
void uiTileGo(int i);
int uiTileNamed(const char *n);
int uiTileIndex();
void uiButtonAt(int i, int *cx, int *cy, int *half);
void openClock();
void emuAdvanceMs(uint32_t ms);
void emuSetClockPaused(bool paused);
void rtcSetEpoch(uint32_t e);
uint32_t rtcEpoch();
bool uiStandby();
bool uiSelfCareRect(int *x, int *y, int *w, int *h);
bool uiStandbyRect(int *x, int *y, int *w, int *h);
bool uiSaveBtnRect(int i, int *x, int *y, int *w, int *h);
bool uiBdayChipRect(int *x, int *y, int *w, int *h);
bool playerTallNameHit(int16_t x, int16_t y);
uint8_t sceneSky();
bool sceneNight();

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

// --- the fake phone ---
static float gLat = 0, gLon = 0;
static int32_t gZone = 0;
static bool gHavePlace = false;
static bool fakePlace(float *lat, float *lon, int32_t *z) {
  if (!gHavePlace) return false;
  *lat = gLat; *lon = gLon; *z = gZone;
  return true;
}
static void at(float lat, float lon, float zoneH) {
  gLat = lat; gLon = lon; gZone = (int32_t)(zoneH * 3600); gHavePlace = true;
}
static bool gCharging = false;
static bool fakeCharging() { return gCharging; }
static int gAsked = 0;
static void fakeAskBirthday(uint8_t, uint8_t) { gAsked++; }
static void fakeBackup() {}

static uint32_t localEpoch(int y, int m, int d, int hh, int mm = 0) {
  return forkDayNumber((uint16_t)y, (uint8_t)m, (uint8_t)d) * 86400u + hh * 3600u + mm * 60u;
}
static int hm(int h, int m) { return h * 60 + m; }

// Sunrise and sunset against a published almanac, within `tol` minutes.
static void sunCheck(const char *where, int y, int mo, int d, float lat, float lon, float zoneH,
                     int rise, int set, int tol = 4) {
  int16_t r, s;
  const uint8_t k = forkSunCalc((uint16_t)y, (uint8_t)mo, (uint8_t)d, lat, lon, (int32_t)(zoneH * 3600), &r, &s);
  CHECK(k == FORK_SUN_NORMAL && abs(r - rise) <= tol && abs(s - set) <= tol,
        "%s %04d-%02d-%02d: sunrise %02d:%02d (almanac %02d:%02d), sunset %02d:%02d (almanac %02d:%02d)",
        where, y, mo, d, r / 60, r % 60, rise / 60, rise % 60, s / 60, s % 60, set / 60, set % 60);
}

int main() {
  // ===================== the pure maths, switch off =====================
  // ---- the calendar ----
  {
    bool round = true;
    for (uint32_t day = 0; day < 40000; day += 7) {
      const ForkDate d = forkDate(day * 86400u + 43200u);
      if (forkDayNumber(d.year, d.month, d.day) != day) { round = false; break; }
    }
    CHECK(round, "a day number and its date go there and back, 1970 to 2079");
    const ForkDate t = forkDate(localEpoch(2026, 11, 18, 9));
    CHECK(t.year == 2026 && t.month == 11 && t.day == 18 && t.wday == 3, "18 November 2026 is a Wednesday");
    const ForkDate leap = forkDate(localEpoch(2028, 2, 29, 23, 59));
    CHECK(leap.month == 2 && leap.day == 29, "29 February 2028 exists");
    const ForkDate notLeap = forkDate(localEpoch(2100, 2, 28, 12) + 86400u);
    CHECK(notLeap.month == 3 && notLeap.day == 1, "2100 is not a leap year: 28 February is followed by 1 March");
  }
  // ---- Easter, against the published dates ----
  {
    static const int E[][3] = { { 2019, 4, 21 }, { 2024, 3, 31 }, { 2025, 4, 20 }, { 2026, 4, 5 },
                                { 2027, 3, 28 }, { 2038, 4, 25 }, { 2285, 3, 22 } };
    int ok = 0;
    for (auto &e : E) {
      uint8_t m, d;
      forkEaster((uint16_t)e[0], &m, &d);
      if (m == e[1] && d == e[2]) ok++;
      else printf("  Easter %d: got %u/%u, expected %d/%d\n", e[0], m, d, e[1], e[2]);
    }
    CHECK(ok == 7, "Easter Sunday lands on the published date in 7 of 7 years (including 2285's 22 March, the earliest)");
  }
  // ---- the sun, against the almanac ----
  sunCheck("Madrid", 2026, 6, 21, 40.4f, -3.7f, 2, hm(6, 44), hm(21, 48));
  sunCheck("London", 2026, 12, 21, 51.5f, -0.1f, 0, hm(8, 4), hm(15, 53));
  sunCheck("Sydney", 2026, 12, 21, -33.9f, 151.2f, 11, hm(5, 41), hm(20, 5));
  sunCheck("Sao Paulo", 2026, 6, 21, -23.5f, -46.6f, -3, hm(6, 47), hm(17, 28));
  {
    int16_t r, s;
    CHECK(forkSunCalc(2026, 6, 21, 69.6f, 18.9f, 7200, &r, &s) == FORK_SUN_UP,
          "Tromso at midsummer: the sun never sets");
    CHECK(forkSunCalc(2026, 12, 21, 69.6f, 18.9f, 3600, &r, &s) == FORK_SUN_DOWN,
          "Tromso at midwinter: it never rises");
  }
  // ---- the sky's phases from the sun ----
  {
    const int rise = hm(7, 0), set = hm(19, 0);
    CHECK(forkSkyPhaseAt(hm(6, 29), FORK_SUN_NORMAL, rise, set) == FORK_SKY_NIGHT &&
          forkSkyPhaseAt(hm(6, 30), FORK_SUN_NORMAL, rise, set) == FORK_SKY_DAWN &&
          forkSkyPhaseAt(hm(7, 59), FORK_SUN_NORMAL, rise, set) == FORK_SKY_DAWN &&
          forkSkyPhaseAt(hm(8, 0), FORK_SUN_NORMAL, rise, set) == FORK_SKY_DAY &&
          forkSkyPhaseAt(hm(17, 59), FORK_SUN_NORMAL, rise, set) == FORK_SKY_DAY &&
          forkSkyPhaseAt(hm(18, 0), FORK_SUN_NORMAL, rise, set) == FORK_SKY_DUSK &&
          forkSkyPhaseAt(hm(19, 29), FORK_SUN_NORMAL, rise, set) == FORK_SKY_DUSK &&
          forkSkyPhaseAt(hm(19, 30), FORK_SUN_NORMAL, rise, set) == FORK_SKY_NIGHT,
          "dawn runs from 30 min before sunrise to an hour after, dusk the mirror at sunset");
    CHECK(forkSkyPhaseAt(hm(10, 20), FORK_SUN_NORMAL, hm(10, 0), hm(11, 0)) == FORK_SKY_DAWN &&
          forkSkyPhaseAt(hm(10, 40), FORK_SUN_NORMAL, hm(10, 0), hm(11, 0)) == FORK_SKY_DUSK,
          "a day too short for both twilights goes straight from dawn to dusk at its middle");
    CHECK(forkSkyPhaseAt(0, FORK_SUN_UP, 0, 1440) == FORK_SKY_DAY &&
          forkSkyPhaseAt(hm(12, 0), FORK_SUN_DOWN, 0, 0) == FORK_SKY_NIGHT,
          "the midnight sun is day at midnight, the polar night is night at noon");
  }
  // ---- off, or nowhere: upstream's fixed hours, exactly ----
  gForkHost.place = fakePlace;
  at(40.4f, -3.7f, 1);
  {
    bool same = true;
    for (int h = 0; h < 24; h++) {
      const uint8_t want = (h < 6 || h >= 20) ? FORK_SKY_NIGHT : h < 8 ? FORK_SKY_DAWN : h < 18 ? FORK_SKY_DAY : FORK_SKY_DUSK;
      if (forkSkyPhase(localEpoch(2026, 12, 21, h, 30)) != want) same = false;
    }
    CHECK(same, "switch off, a place known: every hour is upstream's ladder (the board's shots depend on it)");
    CHECK(forkSeason(localEpoch(2026, 12, 21, 12)) == FORK_SEASON_NONE &&
          forkHoliday(pet, localEpoch(2026, 12, 25, 12)) == FORK_HOL_NONE,
          "switch off: no season and no holiday");
  }
  // ---- the moon, against known new and full moons ----
  {
    const float full = forkMoonAge(localEpoch(2026, 1, 3, 10, 3), 0);   // full moon 10:03 UTC
    const float fresh = forkMoonAge(localEpoch(2026, 1, 18, 19, 52), 0);   // new moon 19:52 UTC
    const float freshFar = forkMoonAge(localEpoch(2026, 1, 19, 4, 52), 9 * 3600);   // the same instant in Tokyo
    CHECK(fabsf(full - 14.77f) < 0.6f, "3 Jan 2026's full moon is %.2f days old (about 14.8)", full);
    CHECK(fresh < 0.6f || fresh > 28.9f, "18 Jan 2026's new moon is %.2f days old (about 0)", fresh);
    CHECK(fabsf(freshFar - fresh) < 0.01f, "and the same instant read in another zone is the same moon");
  }
  // ---- the seasons ----
  CHECK(forkSeasonAt(7, 40.4f) == FORK_SUMMER && forkSeasonAt(7, -33.9f) == FORK_WINTER &&
        forkSeasonAt(12, 51.5f) == FORK_WINTER && forkSeasonAt(12, -33.9f) == FORK_SUMMER &&
        forkSeasonAt(4, 51.5f) == FORK_SPRING && forkSeasonAt(4, -33.9f) == FORK_AUTUMN &&
        forkSeasonAt(10, 51.5f) == FORK_AUTUMN && forkSeasonAt(10, -33.9f) == FORK_SPRING,
        "the seasons follow the hemisphere: July is summer in Madrid and winter in Sydney");
  CHECK(forkSeasonAt(1, 1.3f) == FORK_SEASON_NONE && forkSeasonAt(7, -12.0f) == FORK_SEASON_NONE,
        "the tropics have no seasons drawn");
  // ---- the calendar's holidays ----
  CHECK(forkHolidayOn(2026, 1, 1) == FORK_HOL_NEWYEAR && forkHolidayOn(2026, 2, 14) == FORK_HOL_VALENTINE &&
        forkHolidayOn(2026, 4, 5) == FORK_HOL_EASTER && forkHolidayOn(2026, 10, 31) == FORK_HOL_HALLOWEEN &&
        forkHolidayOn(2026, 12, 24) == FORK_HOL_CHRISTMAS && forkHolidayOn(2026, 12, 25) == FORK_HOL_CHRISTMAS,
        "New Year, Valentine's, Easter, Halloween and Christmas Eve and Day");
  CHECK(forkHolidayOn(2026, 12, 26) == FORK_HOL_NONE && forkHolidayOn(2026, 4, 6) == FORK_HOL_NONE &&
        forkHolidayOn(2027, 4, 5) == FORK_HOL_NONE && forkHolidayOn(2026, 1, 2) == FORK_HOL_NONE,
        "and not the days around them (Easter moves: 5 April 2027 is no holiday)");
  {
    int problems = 0;
    for (uint8_t l = 0; l < LANG_COUNT; l++) {
      setLang((Lang)l);
      for (uint8_t h = FORK_HOL_NEWYEAR; h < FORK_HOL_COUNT; h++) {
        if (strlen(forkHolidayTitle(h)) > 17) { printf("  title too long: %s\n", forkHolidayTitle(h)); problems++; }
        if (strlen(forkHolidayGreeting(h)) > 24 || !*forkHolidayGreeting(h)) { printf("  greeting: %s\n", forkHolidayGreeting(h)); problems++; }
      }
      for (uint8_t m = 1; m <= 12; m++) if (!*forkMonthName(m) || strlen(forkMonthName(m)) > 4) problems++;
      for (uint8_t w = 0; w < 7; w++) if (!*forkWeekdayName(w) || strlen(forkWeekdayName(w)) > 3) problems++;
    }
    setLang(LANG_EN);
    CHECK(problems == 0, "every holiday's banner fits size 3 and its greeting a bubble, in all seven languages");
  }

  // ===================== the phone: the switch on, the sketch =====================
  forkSetFeatures(true);
  gForkHost.charging = fakeCharging;
  gForkHost.askBirthday = fakeAskBirthday;
  gHostSaveBackup = fakeBackup;
  gHostSaveRestore = fakeBackup;
  gfx->resize(466, H);
  gPanelH = H;
  gPanelRound = false;
  gInsetTop = 42;
  gInsetBot = 0;
  gGfxOriginY = gFrameSquareY = (H - 466) / 2;
  rtcSetEpoch(localEpoch(2026, 11, 18, 10));
  setup();
  emuSetClockPaused(true);
  step(3);
  if (pet.awaitingStarter()) pet.chooseStarter(4);
  if (pet.isEgg()) pet.dbgHatchAs(4, false);
  pet.sleeping = false;
  pet.fullness = pet.joy = pet.energy = pet.hygiene = 80;
  pet.lastSeenEpoch = rtcEpoch();
  while (pet.hasLearnOffer()) pet.declineLearn();   // modal, and not what this tests
  bag.clear();
  forkData.st.giftYear = 0;
  forkData.st.giftMask = 0;
  step(3);
  const uint8_t petScreen = uiCurrentScreen();
  CHECK(uiTileIndex() == uiTileNamed("pet"), "starts on the pet screen (screen %u)", (unsigned)petScreen);

  // ---- the sky follows the sun on the phone ----
  at(40.4f, -3.7f, 1);   // Madrid, winter time
  pet.lastSeenEpoch = localEpoch(2026, 12, 21, 7, 30);
  CHECK(sceneSky() == FORK_SKY_NIGHT, "07:30 in Madrid at midwinter is still night (sunrise 08:33), "
                                      "where the fixed hours said dawn");
  pet.lastSeenEpoch = localEpoch(2026, 6, 21, 21, 0);
  at(40.4f, -3.7f, 2);
  CHECK(sceneSky() == FORK_SKY_DUSK && !sceneNight(), "21:00 at midsummer is dusk, not night");
  gHavePlace = false;
  CHECK(sceneNight(), "nowhere known, 21:00 is night again: a zone with no coordinates draws as the board does");
  at(40.4f, -3.7f, 1);
  CHECK(forkSeason(localEpoch(2026, 12, 21, 12)) == FORK_WINTER, "Madrid in December: winter");

  // ---- the player's birthday ----
  forkSetBirthday(3, 14);
  uint8_t bm = 0, bd = 0;
  CHECK(forkBirthday(&bm, &bd) && bm == 3 && bd == 14, "a birthday is kept");
  CHECK(forkHoliday(pet, localEpoch(2027, 3, 14, 9)) == FORK_HOL_BIRTHDAY &&
        forkHoliday(pet, localEpoch(2027, 3, 15, 9)) == FORK_HOL_NONE, "and is a holiday on its day only");
  forkSetBirthday(2, 29);
  CHECK(forkHoliday(pet, localEpoch(2027, 2, 28, 9)) == FORK_HOL_BIRTHDAY &&
        forkHoliday(pet, localEpoch(2028, 2, 28, 9)) == FORK_HOL_NONE &&
        forkHoliday(pet, localEpoch(2028, 2, 29, 9)) == FORK_HOL_BIRTHDAY,
        "29 February is kept on the 28th in a year that has none, and on the day in a leap year");
  forkSetBirthday(2, 30);
  CHECK(!forkBirthday(nullptr, nullptr), "a date that cannot be one clears it rather than storing it");
  forkSetBirthday(12, 25);

  // ---- the gifts, once a year each ----
  {
    const uint8_t p0 = bag.count(IT_PROTEIN), i0 = bag.count(IT_IRON), c0 = bag.count(IT_CARBOS);
    const uint8_t m0 = bag.count(IT_MASTERBALL);
    CHECK(!forkHolidayPoll(pet, localEpoch(2026, 12, 20, 10)), "an ordinary day gives nothing");
    CHECK(forkHolidayPoll(pet, localEpoch(2026, 12, 24, 10)), "Christmas Eve gives a gift");
    CHECK(bag.count(IT_PROTEIN) == p0 + 1 && bag.count(IT_IRON) == i0 + 1 && bag.count(IT_CARBOS) == c0 + 1,
          "one of each vitamin");
    CHECK(!forkHolidayPoll(pet, localEpoch(2026, 12, 24, 18)), "not twice the same day");
    CHECK(forkHolidayPoll(pet, localEpoch(2026, 12, 25, 10)) && bag.count(IT_MASTERBALL) == m0 + 1 &&
          bag.count(IT_PROTEIN) == p0 + 1,
          "Christmas Day, which is also the birthday: the birthday's Master Ball, and no second Christmas");
    const uint32_t coins = forkData.st.coins;
    CHECK(forkHolidayPoll(pet, localEpoch(2027, 1, 1, 0, 5)) && forkData.st.coins == coins + FORK_GIFT_COINS_NEWYEAR,
          "New Year's Day: %d coins", FORK_GIFT_COINS_NEWYEAR);
    CHECK(forkHolidayPoll(pet, localEpoch(2027, 12, 24, 10)) && bag.count(IT_PROTEIN) == p0 + 2,
          "and Christmas again a year later");
    // the record keeps what was given, through a reload
    forkData.save(rtcEpoch());
    forkData.begin();
    CHECK(forkData.st.giftYear == 2027 && !forkHolidayPoll(pet, localEpoch(2027, 12, 24, 20)),
          "after a restart, this year's Christmas is still given");
    CHECK(forkData.st.bdayMonth == 12 && forkData.st.bdayDay == 25, "and the birthday is still set");
  }
  // ---- the creature's hatch day ----
  {
    forkData.st.hatchKey = 0;          // the polls above already met this creature
    pet.ageMinutes = 3 * 1440 + 100;   // three days old
    forkHolidayPoll(pet, localEpoch(2026, 5, 10, 12));
    const uint32_t hd = forkHatchDay(pet);
    CHECK(hd == forkDayNumber(2026, 5, 7), "the hatch day is stamped as today less its age (7 May)");
    CHECK(forkHoliday(pet, localEpoch(2026, 5, 7, 12)) != FORK_HOL_HATCHDAY &&
          forkHoliday(pet, localEpoch(2027, 5, 7, 12)) == FORK_HOL_HATCHDAY,
          "its anniversary is a holiday, the day itself is not");
    const uint32_t coins = forkData.st.coins;
    CHECK(forkHolidayPoll(pet, localEpoch(2027, 5, 7, 12)) && forkData.st.coins == coins + FORK_GIFT_COINS_HATCHDAY,
          "with %d coins", FORK_GIFT_COINS_HATCHDAY);
    const uint8_t iv = pet.ivAtk;
    pet.ivAtk = (uint8_t)((iv + 1) % 32);   // another creature
    CHECK(forkHatchDay(pet) == 0, "another creature does not inherit it");
    forkHolidayPoll(pet, localEpoch(2027, 6, 1, 12));
    CHECK(forkHatchDay(pet) != 0 && forkHatchDay(pet) != hd, "and is stamped for itself");
    pet.ivAtk = iv;
  }
  // ---- the record: new fields round-trip, an older record reads them as defaults ----
  {
    ForkState a;
    a.giftYear = 2031; a.giftMask = 0x55; a.bdayMonth = 7; a.bdayDay = 9;
    a.hatchKey = 0x80001234u; a.hatchDay = 20000;
    uint8_t buf[FORK_BLOB_CAP];
    const uint16_t n = Fork::encode(a, buf, sizeof(buf), 3);
    ForkState b;
    CHECK(n && Fork::decode(buf, n, b) && b.giftYear == 2031 && b.giftMask == 0x55 && b.bdayMonth == 7 &&
          b.bdayDay == 9 && b.hatchKey == 0x80001234u && b.hatchDay == 20000,
          "the new fields round-trip through the record");
    // a body cut back to Phase 7's length, resealed, is what an older build wrote
    ForkState c;
    c.coins = 77;
    uint16_t m = Fork::encode(c, buf, sizeof(buf), 4);
    const size_t oldEnd = CKPT_HDR + 63;   // Phase 7 wrote a 63-byte body
    (void)m;
    m = ckptSeal(buf, oldEnd, FORK_MAGIC, FORK_VERSION, 4);
    ForkState d;
    d.bdayMonth = 99;
    CHECK(m && Fork::decode(buf, m, d) && d.coins == 77 && d.bdayMonth == 0 && d.giftYear == 0 && d.hatchKey == 0,
          "a Phase 7 record loads with no birthday, no gifts given and no hatch day");
  }
  // ---- the greeting ----
  {
    pet.sleeping = false;
    pet.fullness = pet.joy = pet.energy = pet.hygiene = 80;
    pet.poops = 0;
    forkSetBirthday(0, 0);
    CHECK(forkSayReason(pet, localEpoch(2027, 10, 31, 12)) == FORK_SAY_HOLIDAY, "on Halloween it has something to say");
    pet.fullness = 10;
    CHECK(forkSayReason(pet, localEpoch(2027, 10, 31, 12)) == FORK_SAY_HOLIDAY,
          "and says it before it says it is hungry");
    pet.fullness = 80;
    CHECK(forkSayReason(pet, localEpoch(2027, 11, 1, 12)) != FORK_SAY_HOLIDAY, "not the day after");
  }

  // ---- the standby clock ----
  // The hatch-day checks aged it three days, which brought learn prompts: those
  // are a screen of their own, and the clock rightly never covers one.
  while (pet.hasLearnOffer()) pet.declineLearn();
  step(3);
  CHECK(uiCurrentScreen() == petScreen, "back on the pet screen");
  pet.lastSeenEpoch = rtcEpoch();
  pet.sleeping = false;
  gCharging = false;
  step(5);
  lastInteract = millis();
  step(1, FORK_STANDBY_IDLE_MS + 1000);
  CHECK(!uiStandby(), "unplugged and left alone: no standby clock");
  CHECK(!forkKeepScreenOn(), "and the screen is not kept on");
  gCharging = true;
  lastInteract = millis();
  step(1, FORK_STANDBY_IDLE_MS - 2000);
  CHECK(!uiStandby(), "plugged in, before %u s: not yet", FORK_STANDBY_IDLE_MS / 1000);
  CHECK(forkKeepScreenOn(), "but the screen is kept on so it can come");
  step(1, 3000);
  // at night, it lets the creature sleep, as the board's PWR does
  {
    rtcSetEpoch(localEpoch(2026, 11, 19, 1));
    pet.lastSeenEpoch = rtcEpoch();
    step(3);
    while (pet.hasLearnOffer()) pet.declineLearn();   // a new day's level brings prompts
    pet.sleeping = false;
    lastInteract = millis();
    step(1, FORK_STANDBY_IDLE_MS + 1000);
    step(2);
    CHECK(uiStandby() && pet.sleeping, "at 01:00 on the charger, standby puts it to sleep");
    tap(233, 600);
    CHECK(!uiStandby() && !pet.sleeping, "and coming back wakes it, as the screen coming on does");
    gCharging = false;
    lastInteract = millis();
    step(1, FORK_STANDBY_IDLE_MS + 1000);
    CHECK(!uiStandby(), "unplugged, no standby");
    rtcSetEpoch(localEpoch(2026, 11, 19, 10));
    pet.lastSeenEpoch = rtcEpoch();
    gCharging = true;
  }
  // only from the pet screen
  {
    openClock();
    step(2);
    lastInteract = millis();
    step(1, FORK_STANDBY_IDLE_MS + 1000);
    CHECK(!uiStandby(), "on another screen (settings) it never starts");
  }

  // ---- the settings: the charging row, split in two ----
  {
    extern uint8_t clockPage;
    extern bool resetConfirm;
    clockOpen = true; clockPage = 0; resetConfirm = false;
    lastInteract = millis();
    step(2);
    int sx, sy, sw, sh, tx, ty, tw, th, bx, by, bw, bh;
    CHECK(uiSelfCareRect(&sx, &sy, &sw, &sh) && uiStandbyRect(&tx, &ty, &tw, &th),
          "settings show the self-care and standby switches");
    CHECK(sy == ty && sh >= 44 && th >= 44 && sx + sw < tx, "side by side on one row, each a finger tall");
    CHECK(uiSaveBtnRect(0, &bx, &by, &bw, &bh) && by >= sy + sh, "the backup buttons still fit below them");
    int worst = 0;
    for (uint8_t l = 0; l < LANG_COUNT; l++) {
      setLang((Lang)l);
      for (FStrId id : { FS_SELFCARE_SHORT_ON, FS_SELFCARE_SHORT_OFF, FS_STANDBY_ON, FS_STANDBY_OFF }) {
        const int w = (int)strlen(FT(id)) * 12;
        if (w > worst) worst = w;
      }
    }
    setLang(LANG_EN);
    lastInteract = millis();   // awake: a first touch after a long idle only wakes the screen
    step(2);
    CHECK(worst <= sw - 8 && worst <= tw - 8, "every language's label fits its half (%d of %d px)", worst, sw);
    const bool before = forkStandbyOn();
    tap(tx + tw / 2, ty + th / 2);
    CHECK(forkStandbyOn() != before, "a tap on the standby half switches it");
    CHECK(!forkKeepScreenOn(), "off: the screen is no longer kept on, even charging");
    const bool care = forkSelfCareOn();
    tap(sx + sw / 2, sy + sh / 2);
    CHECK(forkSelfCareOn() != care && forkStandbyOn() != before, "the self-care half switches self-care only");
    tap(sx + sw / 2, sy + sh / 2);
    tap(tx + tw / 2, ty + th / 2);
    clockOpen = false;
    step(2);
  }
  // off, standby never starts even charging
  {
    forkSetStandby(false);
    lastInteract = millis();
    step(1, FORK_STANDBY_IDLE_MS + 1000);
    CHECK(!uiStandby(), "with the setting off, never");
    forkSetStandby(true);
  }

  // ---- the player card's birthday chip ----
  {
    uiTileGo(uiTileNamed("player"));
    lastInteract = millis();
    step(3);
    int x, y, w, h;
    CHECK(uiBdayChipRect(&x, &y, &w, &h) && h >= 44, "the player card has a birthday chip, a finger tall");
    bool overlap = false;
    for (int yy = y; yy <= y + h; yy += 2)
      for (int xx = x; xx <= x + w; xx += 4)
        if (playerTallNameHit((int16_t)xx, (int16_t)yy)) overlap = true;
    CHECK(!overlap, "it never overlaps the name's rename target");
    gAsked = 0;
    tap(x + w / 2, y + h / 2);
    CHECK(gAsked == 1 && playerOpen, "a tap opens the phone's date picker, and the card stays open");
    gForkHost.askBirthday = nullptr;
    forkSetBirthday(0, 0);
    CHECK(!uiBdayChipRect(nullptr, nullptr, nullptr, nullptr), "with no picker and no birthday, no chip");
    gForkHost.askBirthday = fakeAskBirthday;
  }

  // ---- the board: none of it ----
  forkSetFeatures(false);
  CHECK(!uiStandbyRect(nullptr, nullptr, nullptr, nullptr) && !uiBdayChipRect(nullptr, nullptr, nullptr, nullptr) &&
        !forkKeepScreenOn(), "switch off: no standby switch, no birthday chip, the screen never kept on");
  CHECK(!forkHolidayPoll(pet, localEpoch(2028, 12, 24, 10)), "and no gifts");

  printf("%s: %d failure(s)\n", fails ? "FAILED" : "PASSED", fails);
  return fails ? 1 : 0;
}
