// The weather (fork.h section 12; android/FEATURES_PLAN.md Phase 8b): the WMO
// codes, how each type takes each weather, the hourly joy nudge and its daily
// caps through a restart, the setting that keeps the network off until asked,
// the fetch request the phone is sent, the bubble, the settings switch, and the
// drawing -- the last three through the REAL sketch at the phone's canvas.
//
// The WMO table is checked against WMO 4677's own code groups as Open-Meteo
// documents them, code by code, not by a second copy of the ranges.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "dex.h"
#include "fork.h"
#include "fork_i18n.h"
#include "ckpt.h"
#include <math.h>
#include <string.h>
#include <vector>

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
extern bool gPanelRound, clockOpen;
extern uint32_t lastInteract;
extern void (*gHostSaveBackup)();
extern void (*gHostSaveRestore)();
extern Lang gLang;
uint8_t uiCurrentScreen();
int uiTileNamed(const char *n);
int uiTileIndex();
void emuAdvanceMs(uint32_t ms);
void emuSetClockPaused(bool paused);
void rtcSetEpoch(uint32_t e);
uint32_t rtcEpoch();
bool uiWeatherRect(int *x, int *y, int *w, int *h);
void uiClockTallOy(int *oyTime, int *oyOpts, int *split);
void renderStandby();

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
static bool fakePlace(float *lat, float *lon, int32_t *z) {
  *lat = -27.4679f; *lon = 153.0281f; *z = 10 * 3600;   // Brisbane, unrounded on purpose
  return true;
}
static bool gHave = false;
static uint8_t gWmo = 0;
static int16_t gTemp = 20;
static char gUnit = 'C';
static bool fakeWeather(uint8_t *w, int16_t *t, char *u) {
  if (!gHave) return false;
  *w = gWmo; *t = gTemp; *u = gUnit;
  return true;
}
static void report(uint8_t wmo) { gHave = true; gWmo = wmo; gTemp = 20; gUnit = 'C'; }
static int gFetchCalls = 0;
static bool gFetchOn = false;
static float gFetchLat = 0, gFetchLon = 0;
static void fakeFetch(bool on, float lat, float lon) {
  gFetchCalls++; gFetchOn = on; gFetchLat = lat; gFetchLon = lon;
}
static void fakeBackup() {}

static uint32_t localEpoch(int y, int m, int d, int hh, int mm = 0) {
  return forkDayNumber((uint16_t)y, (uint8_t)m, (uint8_t)d) * 86400u + hh * 3600u + mm * 60u;
}

static std::vector<uint16_t> frame() {
  render();
  const uint16_t *b = gfx->buffer();
  return std::vector<uint16_t>(b, b + 466 * H);
}

int main() {
  // ===================== the pure rules =====================
  {
    struct { uint8_t wmo, kind; } T[] = {
      { 0, FORK_WX_CLEAR }, { 1, FORK_WX_CLEAR }, { 2, FORK_WX_CLOUDY }, { 3, FORK_WX_CLOUDY },
      { 45, FORK_WX_FOG }, { 48, FORK_WX_FOG },
      { 51, FORK_WX_RAIN }, { 53, FORK_WX_RAIN }, { 55, FORK_WX_RAIN }, { 56, FORK_WX_RAIN }, { 57, FORK_WX_RAIN },
      { 61, FORK_WX_RAIN }, { 63, FORK_WX_RAIN }, { 65, FORK_WX_RAIN }, { 66, FORK_WX_RAIN }, { 67, FORK_WX_RAIN },
      { 80, FORK_WX_RAIN }, { 81, FORK_WX_RAIN }, { 82, FORK_WX_RAIN },
      { 71, FORK_WX_SNOW }, { 73, FORK_WX_SNOW }, { 75, FORK_WX_SNOW }, { 77, FORK_WX_SNOW },
      { 85, FORK_WX_SNOW }, { 86, FORK_WX_SNOW },
      { 95, FORK_WX_STORM }, { 96, FORK_WX_STORM }, { 99, FORK_WX_STORM },
      // codes Open-Meteo never sends: nothing, rather than a guess
      { 4, FORK_WX_NONE }, { 44, FORK_WX_NONE }, { 50, FORK_WX_NONE }, { 70, FORK_WX_NONE },
      { 79, FORK_WX_NONE }, { 90, FORK_WX_NONE }, { 100, FORK_WX_NONE }, { 255, FORK_WX_NONE },
    };
    int bad = 0;
    for (auto &t : T)
      if (forkWeatherKind(t.wmo) != t.kind) { printf("  code %u -> %u, want %u\n", t.wmo, forkWeatherKind(t.wmo), t.kind); bad++; }
    CHECK(bad == 0, "every WMO 4677 code Open-Meteo sends maps to its kind, and unknown codes to none (%d codes)",
          (int)(sizeof(T) / sizeof(T[0])));
  }
  CHECK(forkWeatherMood(FORK_WX_RAIN, T_WATER, T_NONE, true) == 1 &&
        forkWeatherMood(FORK_WX_RAIN, T_FIRE, T_NONE, true) == -1 &&
        forkWeatherMood(FORK_WX_RAIN, T_NORMAL, T_NONE, true) == 0,
        "rain: water likes it, fire does not, normal does not mind");
  CHECK(forkWeatherMood(FORK_WX_RAIN, T_FIRE, T_WATER, true) == 0 &&
        forkWeatherMood(FORK_WX_RAIN, T_WATER, T_FIRE, true) == 0,
        "a type that likes it and one that does not cancel out, either order");
  CHECK(forkWeatherMood(FORK_WX_STORM, T_ELECTRIC, T_NONE, true) == 1 &&
        forkWeatherMood(FORK_WX_STORM, T_NORMAL, T_NONE, true) == -1 &&
        forkWeatherMood(FORK_WX_STORM, T_GRASS, T_POISON, false) == -1,
        "a storm: electric loves it, thunder frightens everybody else");
  CHECK(forkWeatherMood(FORK_WX_CLEAR, T_NORMAL, T_NONE, true) == 1 &&
        forkWeatherMood(FORK_WX_CLEAR, T_NORMAL, T_NONE, false) == 0,
        "a sunny day cheers everybody; a clear night is just night");
  CHECK(forkWeatherMood(FORK_WX_SNOW, T_ICE, T_NONE, true) == 1 &&
        forkWeatherMood(FORK_WX_SNOW, T_FIRE, T_NONE, true) == -1 &&
        forkWeatherMood(FORK_WX_FOG, T_GHOST, T_POISON, true) == 1 &&
        forkWeatherMood(FORK_WX_CLOUDY, T_WATER, T_NONE, true) == 0 &&
        forkWeatherMood(FORK_WX_NONE, T_WATER, T_NONE, true) == 0,
        "snow, fog, cloud and nothing known");
  {
    int problems = 0;
    for (uint8_t l = 0; l < LANG_COUNT; l++) {
      setLang((Lang)l);
      for (uint8_t k = FORK_WX_CLEAR; k < FORK_WX_COUNT; k++) {
        if (!*forkWeatherName(k) || strlen(forkWeatherName(k)) > 7) { printf("  name: %s\n", forkWeatherName(k)); problems++; }
        if (strlen(FT((FStrId)(FS_WXSAY_CLEAR + k - FORK_WX_CLEAR))) > 24) problems++;
      }
      if (strlen(FT(FS_WXSAY_LIKE)) > 24 || strlen(FT(FS_WXSAY_DISLIKE)) > 24) problems++;
    }
    setLang(LANG_EN);
    CHECK(problems == 0, "every kind has a short name and every line fits a bubble, in all seven languages");
  }

  // ===================== the board: none of it =====================
  gForkHost.place = fakePlace;
  gForkHost.weather = fakeWeather;
  gForkHost.weatherFetch = fakeFetch;
  report(61);
  forkSetWeather(true);
  CHECK(!forkWeatherOn() && forkWeather() == FORK_WX_NONE && !forkWeatherWanted(nullptr, nullptr),
        "switch off: the setting cannot be turned on, and no weather even with a report");

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
  rtcSetEpoch(localEpoch(2026, 11, 18, 10));
  setup();
  emuSetClockPaused(true);
  step(3);
  if (pet.awaitingStarter()) pet.chooseStarter(4);
  if (pet.isEgg()) pet.dbgHatchAs(7, false);   // SQUIRTLE: water
  pet.dbgHatchAs(7, false);
  while (pet.hasLearnOffer()) pet.declineLearn();
  pet.sleeping = false;
  pet.fullness = pet.joy = pet.energy = pet.hygiene = 60;
  pet.lastSeenEpoch = rtcEpoch();
  step(3);

  // ---- off until asked ----
  gFetchCalls = 0;
  forkWeatherSync();
  CHECK(!forkWeatherOn(), "a new save has the weather OFF");
  CHECK(gFetchCalls == 1 && !gFetchOn, "and the phone is told not to fetch (and to forget anything it had)");
  CHECK(forkWeather() == FORK_WX_NONE, "a report the phone still holds is not believed while it is off");
  forkWeatherSync();
  CHECK(gFetchCalls == 1, "telling it again with nothing changed is not repeated");
  forkSetWeather(true);
  CHECK(gFetchCalls == 2 && gFetchOn, "turned on, the phone is asked to fetch");
  CHECK(gFetchLat == -27.5f && gFetchLon == 153.0f,
        "for a tenth of a degree only (%.4f, %.4f), whatever the place said", gFetchLat, gFetchLon);
  CHECK(forkWeather() == FORK_WX_RAIN, "and its report is the weather");
  gUnit = 'K';
  CHECK(forkWeather() == FORK_WX_NONE, "a report in a unit that is not C or F is not believed");
  gUnit = 'C'; gTemp = 400;
  CHECK(forkWeather() == FORK_WX_NONE, "nor a temperature no place has");
  report(61);
  {
    int16_t t; char u;
    CHECK(forkWeatherTemp(&t, &u) && t == 20 && u == 'C', "the standby clock gets the temperature and its unit");
  }
  gHave = false;
  CHECK(forkWeather() == FORK_WX_NONE && !forkWeatherKnown(), "no report (stale, or not fetched yet): nothing");
  report(61);

  // ---- the hourly nudge ----
  forkData.st.wxHour = forkData.st.wxDay = 0;
  forkData.st.wxUp = forkData.st.wxDown = 0;
  uint32_t e = localEpoch(2026, 11, 18, 9, 10);
  pet.joy = 60;
  CHECK(forkWeatherPoll(pet, e) && pet.joy == 61, "a water creature in the rain: +1 joy");
  CHECK(!forkWeatherPoll(pet, e + 1200) && pet.joy == 61, "not again in the same hour");
  for (int h = 1; h <= 6; h++) forkWeatherPoll(pet, e + h * 3600);
  CHECK(pet.joy == 60 + FORK_WX_JOY_DAY && forkWeatherJoyUp(e) == FORK_WX_JOY_DAY,
        "seven hours of rain give %d joy, the day's cap", FORK_WX_JOY_DAY);
  forkData.save(e);
  forkData.begin();
  CHECK(forkWeatherJoyUp(e + 7 * 3600) == FORK_WX_JOY_DAY && !forkWeatherPoll(pet, e + 7 * 3600) &&
        pet.joy == 60 + FORK_WX_JOY_DAY, "after a restart the day's cap still holds");
  CHECK(forkWeatherPoll(pet, localEpoch(2026, 11, 19, 8, 5)) && pet.joy == 61 + FORK_WX_JOY_DAY,
        "and the next day it starts again");
  {
    const uint8_t j = pet.joy;
    pet.sleeping = true;
    CHECK(!forkWeatherPoll(pet, localEpoch(2026, 11, 19, 10, 5)) && pet.joy == j, "asleep, no nudge");
    pet.sleeping = false;
  }
  report(3);
  {
    const uint8_t j = pet.joy;
    CHECK(!forkWeatherPoll(pet, localEpoch(2026, 11, 19, 11, 5)) && pet.joy == j, "cloud does nothing");
  }
  // a fire creature in the rain
  pet.dbgHatchAs(4, false);   // CHARMANDER
  while (pet.hasLearnOffer()) pet.declineLearn();
  pet.sleeping = false;
  pet.joy = 50;
  report(63);
  for (int h = 0; h < 8; h++) forkWeatherPoll(pet, localEpoch(2026, 11, 20, 8 + h, 1));
  CHECK(pet.joy == 50 - FORK_WX_JOY_DAY && forkWeatherJoyDown(localEpoch(2026, 11, 20, 20)) == FORK_WX_JOY_DAY,
        "a fire creature in a day of rain loses %d joy, and no more", FORK_WX_JOY_DAY);
  report(0);
  pet.joy = 50;
  CHECK(forkWeatherPoll(pet, localEpoch(2026, 11, 20, 18, 30)) && pet.joy == 51,
        "the same day, the sun coming out still cheers it: up and down are capped apart");
  forkSetWeather(false);
  report(0);
  CHECK(!forkWeatherPoll(pet, localEpoch(2026, 11, 20, 19, 30)) && pet.joy == 51, "the setting off: no nudge");
  CHECK(gFetchCalls >= 3 && !gFetchOn, "and the phone was told to stop");
  forkSetWeather(true);

  // ---- the bubble ----
  pet.dbgHatchAs(7, false);   // SQUIRTLE again
  while (pet.hasLearnOffer()) pet.declineLearn();
  pet.sleeping = false;
  pet.fullness = pet.joy = pet.energy = pet.hygiene = 80;
  pet.poops = 0;
  forkSayReset();
  report(61);
  const uint32_t noon = localEpoch(2026, 11, 21, 12);
  CHECK(forkSayReason(pet, noon) == FORK_SAY_WEATHER, "the rain is worth a word");
  {
    uint32_t ms = 1000;
    const char *said = nullptr;
    for (int i = 0; i < 200 && !said; i++, ms += 1000) {
      forkSayPoll(pet, true, ms, noon);
      said = forkSaying(ms);
    }
    CHECK(said && !strcmp(said, "I love this weather!"), "a water creature says it loves it (\"%s\")", said ? said : "");
  }
  CHECK(forkSayReason(pet, noon) != FORK_SAY_WEATHER, "said once, not again for the same weather");
  report(71);
  CHECK(forkSayReason(pet, noon) == FORK_SAY_WEATHER, "but snow is new");
  report(0);
  CHECK(forkSayReason(pet, localEpoch(2026, 11, 21, 23)) != FORK_SAY_WEATHER,
        "a clear night is not worth a word (its line is about the day)");

  // ---- the drawing ----
  pet.lastSeenEpoch = localEpoch(2026, 11, 21, 13);
  lastInteract = millis();
  step(2);
  CHECK(uiTileIndex() == uiTileNamed("pet"), "on the pet screen");
  forkSetWeather(false);
  gHave = false;
  const std::vector<uint16_t> none = frame();
  report(61);
  const std::vector<uint16_t> offWithReport = frame();
  CHECK(offWithReport == none, "the setting off, a report the phone holds draws nothing at all");
  forkSetWeather(true);
  gHave = false;
  CHECK(frame() == none, "on but no report yet: nothing either");
  report(0);
  const std::vector<uint16_t> clear = frame();
  report(61);
  const std::vector<uint16_t> rain = frame();
  report(3);
  const std::vector<uint16_t> cloudy = frame();
  CHECK(clear == none, "a clear day is the scene as it always was");
  CHECK(rain != none && cloudy != none && rain != cloudy, "rain and cloud each draw something, and not the same");
  {
    // the day's sun, by its own colour (drawScene()'s C565(0xff, 0xe7, 0x9f))
    const uint16_t SUN = (uint16_t)((0xff >> 3) << 11 | (0xe7 >> 2) << 5 | (0x9f >> 3));
    auto suns = [SUN](const std::vector<uint16_t> &f) { long n = 0; for (uint16_t c : f) n += c == SUN; return n; };
    // Some of the UI shares the colour, so it is the DISC's worth that must go:
    // radius 26 is about 2100 pixels.
    CHECK(suns(clear) - suns(cloudy) > 2000 && suns(rain) == suns(cloudy),
          "under cloud the sun is gone (%ld sun-coloured pixels clear, %ld cloudy, %ld rain)",
          suns(clear), suns(cloudy), suns(rain));
  }
  report(95);
  {
    // a storm's flash: the same instant of the cycle, flashing and not
    uint32_t t = millis();
    while (!forkWeatherFlash(t)) t++;
    emuAdvanceMs(t - millis());
    const std::vector<uint16_t> lit = frame();
    emuAdvanceMs(FORK_WX_FLASH_MS + 20);
    CHECK(!forkWeatherFlash(millis()), "a flash ends");
    const std::vector<uint16_t> dark = frame();
    // the upper half of the canvas, summed so a raindrop cannot decide it
    auto lum = [](const std::vector<uint16_t> &f) {
      long sum = 0;
      for (int y = 0; y < H / 2; y++)
        for (int x = 0; x < 466; x++) {
          const uint16_t c = f[y * 466 + x];
          sum += ((c >> 11) & 31) * 2 + ((c >> 5) & 63) + (c & 31) * 2;
        }
      return sum;
    };
    // and it is the SKY that lights, not only the bolt: a bolt and the rain
    // moving on between the frames change a few thousand pixels at most
    long changed = 0;
    for (int i = 0; i < 466 * H / 2; i++) changed += lit[i] != dark[i];
    CHECK(lum(lit) > lum(dark) && changed > 30000,
          "lightning lights the sky (%ld pixels of the upper half change)", changed);
  }

  // ---- the standby clock ----
  {
    report(61);
    render();   // settle
    renderStandby();
    const std::vector<uint16_t> withWx(gfx->buffer(), gfx->buffer() + 466 * H);
    gHave = false;
    renderStandby();
    const std::vector<uint16_t> without(gfx->buffer(), gfx->buffer() + 466 * H);
    CHECK(withWx != without, "the standby clock shows the weather when there is some");
    report(61);
  }

  // ---- the settings: the language row, split in two ----
  {
    extern uint8_t clockPage;
    extern bool resetConfirm;
    clockOpen = true; clockPage = 0; resetConfirm = false;
    lastInteract = millis();
    step(2);
    int wx, wy, ww, wh, oyO;
    CHECK(uiWeatherRect(&wx, &wy, &ww, &wh) && wh >= 44, "settings show a weather switch, a finger tall");
    uiClockTallOy(nullptr, &oyO, nullptr);
    int worst = 0;
    for (uint8_t l = 0; l < LANG_COUNT; l++) {
      setLang((Lang)l);
      for (FStrId id : { FS_WEATHER_ON, FS_WEATHER_OFF }) {
        const int w = (int)strlen(FT(id)) * 12;
        if (w > worst) worst = w;
      }
      char lp[24];
      snprintf(lp, sizeof(lp), "%s  %s >", T(S_LANG_LABEL), "EN");
      if ((int)strlen(lp) * 12 > worst) worst = (int)strlen(lp) * 12;
    }
    setLang(LANG_EN);
    lastInteract = millis();
    step(2);
    CHECK(worst <= ww - 8, "every language's labels fit their half (%d of %d px)", worst, ww);
    const bool before = forkWeatherOn();
    const Lang lang = gLang;
    tap(wx + ww / 2, oyO + wy + wh / 2);
    CHECK(forkWeatherOn() != before && gLang == lang, "a tap on the weather half switches the weather, not the language");
    tap(wx - 60, oyO + wy + wh / 2);   // well inside the language half
    CHECK(forkWeatherOn() != before && gLang != lang, "the language half still cycles the language");
    setLang(lang);
    tap(wx + ww / 2, oyO + wy + wh / 2);
    CHECK(forkWeatherOn() == before, "and back");
    clockOpen = false;
    step(2);
  }

  // ---- and the switch off again ----
  forkSetFeatures(false);
  report(61);
  CHECK(!uiWeatherRect(nullptr, nullptr, nullptr, nullptr) && forkWeather() == FORK_WX_NONE &&
        !forkWeatherPoll(pet, localEpoch(2026, 11, 22, 9)) && !forkWeatherFlash(0),
        "switch off: no switch, no weather, no nudge, no flash");

  printf("%s: %d failure(s)\n", fails ? "FAILED" : "PASSED", fails);
  return fails ? 1 : 0;
}
