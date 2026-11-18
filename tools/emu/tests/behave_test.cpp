// The Android fork's pet acting on its own (fork.h section 10).
// See android/FEATURES_PLAN.md, Phase 7.
//
// Self-medication runs through Pet::tick(), self-care through the sketch's
// loop() and through syncClock() then the return hook, the bubbles through the
// loop's poll, and the switch through settings' real tap path: each rule has a
// CALLER that could ignore it (a rule enforced in one path but not its twin),
// so the callers are what get driven. Every "it does not" has a companion proving the same setup does.
//
// The switch-off half is the board: nothing is said, nothing is raised, and the
// planner spends no random(), so upstream's own behaviour sequence is untouched.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "fork.h"
#include "fork_i18n.h"
#include "ckpt.h"
#include <cstdio>
#include <cstring>

uint32_t g_seed = 71;
FakeSerial Serial;
FakeESP ESP;
FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0;
volatile bool g_touchDown = false;
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }
void FakeESP::restart() { exit(0); }

void emuAdvanceMs(uint32_t ms);
void setup();
void loop();
void render();
uint32_t rtcEpoch();
void rtcSetEpoch(uint32_t e);
void uiTileGo(int i);
int uiTileNamed(const char *n);
uint8_t uiCurrentScreen();
bool uiSelfCareRect(int *x, int *y, int *w, int *h);
bool uiSaveBtnRect(int i, int *x, int *y, int *w, int *h);
extern Pet pet;
extern Arduino_Canvas *gfx;
extern int16_t gPanelH, gInsetTop, gInsetBot, gGfxOriginY, gFrameSquareY;
extern bool gPanelRound;
extern bool screenOff, menuOpen, clockOpen, resetConfirm;
extern uint8_t clockPage;
extern uint32_t confirmUntil, lastInteract;
extern void (*gHostSaveBackup)();
extern void (*gHostSaveRestore)();

static int bad = 0;
static void ck(bool ok, const char *what) {
  printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) bad++;
}

static const int H = 1010;   // tall_test's canvas: an S24-shaped phone
// 2026-11-12 at noon.
static const uint32_t DAY = 20769u;
static const uint32_t T0 = DAY * 86400u + 12 * 3600;

static bool gCharging = false;
static bool fakeCharging() { return gCharging; }
static void noBackup() {}

static void poll() { emuAdvanceMs(25); loop(); }
static void tap(int x, int y) {
  // A dimmed panel spends the first touch on waking up, and the dim stage is
  // only re-read inside loop(): one loop with a fresh lastInteract first.
  lastInteract = millis();
  poll();
  g_touchX = x; g_touchY = y; g_touchDown = true; emuFireInterrupt(); poll(); poll();
  g_touchDown = false; emuFireInterrupt(); poll(); poll();
}

// EASYGOING, so no nature drain moves the numbers between two measurements.
static void healthy() {
  pet.dbgHatchAs(4, false);
  pet.ivDef = 20; pet.ivSpe = 7; pet.ivHp = 25;
  for (pet.ivAtk = 0; natureFromIVs(pet.ivAtk, 20, 7, 25, false) != NATURE_EASYGOING; pet.ivAtk++) {}
  pet.ageMinutes = 14 * MINUTES_PER_LEVEL;
  pet.fullness = 40; pet.joy = pet.energy = pet.hygiene = 90;
  pet.poops = 0; pet.weight = 0; pet.sleeping = false; pet.careMistakes = 0;
  while (pet.hasLearnOffer()) pet.declineLearn();
  pet.dbgSetSeen(T0);
  rtcSetEpoch(T0);
  forkData.st.sickKey = 0;
  forkData.st.expKey = 0;
  confirmUntil = 0; menuOpen = false; screenOff = false; clockOpen = false;
}
static void low() { pet.fullness = 30; pet.joy = 20; pet.energy = 35; pet.hygiene = 10; }
static bool allAt(uint8_t v) {
  return pet.fullness == v && pet.joy == v && pet.energy == v && pet.hygiene == v;
}
// A creature with this nature, the IVs searched as healthy() does.
static void withNature(uint8_t nat) {
  for (pet.ivAtk = 0; natureFromIVs(pet.ivAtk, 20, 7, 25, false) != nat; pet.ivAtk++) {}
}
static int count(uint8_t what) {
  int n = 0;
  for (int r = 0; r < 100; r++) n += forkBehPick(pet, (uint8_t)r) == what;
  return n;
}

int main() {
  gRegionArt = 0xFFFF;
  // What android_main.cpp does before setup().
  gfx->resize(466, H);
  gPanelH = H;
  gPanelRound = false;
  gInsetTop = 42;
  gInsetBot = 0;
  gGfxOriginY = gFrameSquareY = (H - 466) / 2;
  gForkHost.charging = fakeCharging;
  gHostSaveBackup = gHostSaveRestore = noBackup;

  // ---- the switch off: the board ------------------------------------------------
  forkSetFeatures(false);
  setup();
  if (pet.awaitingStarter()) pet.dbgHatchAs(4, false);
  healthy();
  low();
  pet.poops = 3;
  gCharging = true;
  ck(!forkSelfCareLive(pet, 3600000u) && pet.fullness == 30,
     "off: on charge and untouched for an hour, nothing is raised");
  forkData.st.chargeFrom = T0 - 3600;
  ck(!forkChargeReturn(pet, T0) && pet.fullness == 30, "off: nor on coming back");
  ck(forkSayReason(pet, T0) == FORK_SAY_NONE && !forkSaying(millis()), "off: nothing is said");
  {
    g_seed = 99;
    const long a = random(1000000);
    g_seed = 99;
    ck(forkBehNext(pet) == FORK_BEH_NONE && random(1000000) == a,
       "off: the planner spends no random(), so upstream's sequence is untouched");
  }
  ck(!uiSelfCareRect(nullptr, nullptr, nullptr, nullptr), "off: settings have no self-care switch");
  forkData.st = ForkState();
  gCharging = false;

  // ---- on: the phone --------------------------------------------------------------
  forkSetFeatures(true);
  forkData.begin();
  forkTestSetSickness(true);
  healthy();

  // the record: the three new fields round-trip, and an older body reads as zero
  {
    ForkState s;
    s.chargeFrom = 123456; s.stepPrevDay = 20000; s.stepPrev = 4321;
    uint8_t buf[FORK_BLOB_CAP];
    ForkState back;
    const uint16_t n = Fork::encode(s, buf, sizeof(buf), 1);
    ck(n && Fork::decode(buf, n, back) && back.chargeFrom == 123456 &&
       back.stepPrevDay == 20000 && back.stepPrev == 4321, "the record keeps the new fields");
    const uint16_t old = ckptSeal(buf, CKPT_HDR + FORK_OFF_CHARGE_FROM, FORK_MAGIC, FORK_VERSION, 1);
    ck(Fork::decode(buf, old, back) && back.chargeFrom == 0 && back.stepPrev == 0 &&
       back.touchJoy == s.touchJoy, "a Phase 6 record reads them as zero");
    ck(!(ForkState().flags & FORK_FLAG_NO_SELFCARE), "self-care is on in a record that predates it");
  }

  // ---- self-medication, through Pet::tick() -----------------------------------
  forkData.st.medicine = 2;
  forkData.st.sickKey = forkCreatureKey(pet);
  forkData.st.sickMinutes = 0;
  forkBannerClear();
  for (int i = 0; i < FORK_SELFMED_MIN - 1; i++) { pet.fullness = 40; pet.dbgTick(); }
  ck(forkSick(pet) && forkData.st.medicine == 2, "nine sick minutes: still sick, no medicine used");
  pet.dbgTick();
  {
    const char *l1 = nullptr, *l2 = nullptr;
    const bool b = forkBanner(&l1, &l2);
    ck(!forkSick(pet) && forkData.st.medicine == 1, "the tenth: it takes one medicine by itself");
    ck(b && !strcmp(l1, FT(FS_SELFMED)) && !strcmp(l2, FT(FS_CURED)), "and a banner says so");
  }
  forkBannerClear();
  forkData.st.medicine = 0;
  forkData.st.sickKey = forkCreatureKey(pet);
  forkData.st.sickMinutes = 0;
  for (int i = 0; i < 3 * FORK_SELFMED_MIN; i++) { pet.fullness = 40; pet.dbgTick(); }
  ck(forkSick(pet), "with no medicine in the bag it stays sick");
  forkData.st.medicine = 1;
  forkData.st.sickMinutes = 0;
  pet.sleeping = true;
  for (int i = 0; i < 3 * FORK_SELFMED_MIN; i++) pet.dbgTick();
  ck(forkSick(pet) && forkData.st.medicine == 1, "asleep, no minutes pass and none is taken");
  healthy();

  // ---- self-care, live --------------------------------------------------------
  const uint32_t IDLE = (uint32_t)FORK_SELFCARE_IDLE_MIN * 60000u;
  gCharging = true;
  low();
  ck(!forkSelfCareLive(pet, IDLE - 1) && pet.fullness == 30, "on charge, nine minutes untouched: nothing yet");
  {
    const uint8_t bond = pet.bond, mist = pet.careMistakes;
    const uint16_t streak = pet.streak;
    ck(forkSelfCareLive(pet, IDLE) && allAt(FORK_SELFCARE_TO), "ten minutes: every need under 40 is raised to 50");
    ck(pet.bond == bond && pet.streak == streak &&
       pet.careMistakes == mist && !pet.showHeart(), "and it is not care: no bond, streak or heart");
  }
  pet.fullness = 45; pet.joy = 39; pet.energy = 40; pet.hygiene = 90;
  forkSelfCareLive(pet, IDLE);
  ck(pet.fullness == 45 && pet.joy == 50 && pet.energy == 40 && pet.hygiene == 90,
     "only the needs under 40 move");
  low(); gCharging = false;
  ck(!forkSelfCareLive(pet, IDLE * 6) && pet.fullness == 30, "unplugged, nothing");
  gCharging = true;
  forkSetSelfCare(false);
  ck(!forkSelfCareLive(pet, IDLE) && pet.fullness == 30, "with the setting off, nothing");
  forkSetSelfCare(true);
  forkExpSend(pet, FORK_EXP_COAST, 0, T0);
  pet.fullness = 30;
  ck(forkAway(pet) && !forkSelfCareLive(pet, IDLE) && pet.fullness == 30, "out on an expedition, nothing");
  forkData.st.expKey = 0;
  ck(forkSelfCareLive(pet, IDLE) && pet.fullness == 50, "and back home, the same call does");

  // the caller: the sketch's loop, on its 30 s clock
  healthy(); low();
  uiTileGo(uiTileNamed("pet"));
  lastInteract = millis();
  for (int i = 0; i < 31000 / 25; i++) { poll(); lastInteract = millis(); }
  ck(pet.joy < 50, "loop(): touched a moment ago, nothing is raised");
  emuAdvanceMs(IDLE);
  for (int i = 0; i < 31000 / 25; i++) poll();
  ck(pet.joy >= 45 && pet.hygiene >= 45, "loop(): left alone on charge, it looks after itself");

  // ---- self-care on coming back -------------------------------------------------
  healthy(); low();
  gCharging = true;
  forkChargeLeave(T0);
  ck(forkData.st.chargeFrom == T0, "leaving on charge is remembered");
  {
    Fork reload;
    reload.begin();
    ck(reload.st.chargeFrom == T0, "on disk, so a process killed meanwhile still knows");
  }
  ck(forkChargeReturn(pet, T0 + 11 * 60) && allAt(FORK_SELFCARE_TO) && forkData.st.chargeFrom == 0,
     "back after eleven minutes still on charge: raised, and the mark is spent");
  low();
  ck(!forkChargeReturn(pet, T0 + 22 * 60) && pet.fullness == 30, "spent: a second return raises nothing");
  forkChargeLeave(T0);
  ck(!forkChargeReturn(pet, T0 + 5 * 60) && pet.fullness == 30 && !forkData.st.chargeFrom,
     "back after five minutes: nothing, and spent");
  forkChargeLeave(T0);
  gCharging = false;
  ck(!forkChargeReturn(pet, T0 + 60 * 60) && pet.fullness == 30 && !forkData.st.chargeFrom,
     "unplugged by the time it is back: nothing");
  forkChargeLeave(T0);
  ck(!forkData.st.chargeFrom, "leaving unplugged marks nothing");
  gCharging = true;
  ck(!forkChargeReturn(pet, T0 + 60 * 60) && pet.fullness == 30,
     "so plugged in only after leaving does not count");
  forkSetSelfCare(false);
  forkChargeLeave(T0);
  ck(!forkChargeReturn(pet, T0 + 60 * 60) && pet.fullness == 30, "with the setting off, nothing");
  forkSetSelfCare(true);
  // through syncClock(): eight hours away, its floors first, then self-care
  healthy();
  pet.lastSeenEpoch = T0;
  forkChargeLeave(T0);
  pet.syncClock(T0 + 8 * 3600);
  const bool dropped = pet.fullness < FORK_SELFCARE_BELOW;
  ck(dropped && forkChargeReturn(pet, T0 + 8 * 3600) && pet.fullness == FORK_SELFCARE_TO,
     "after syncClock()'s eight hours of drain, the low needs come back to 50");

  // ---- the setting, through settings' own tap -----------------------------------
  {
    healthy();
    clockOpen = true; clockPage = 0; resetConfirm = false;
    render();
    int x, y, w, h, bx, by, bw, bh;
    const bool have = uiSelfCareRect(&x, &y, &w, &h);
    ck(have && h >= 44 && uiSaveBtnRect(0, &bx, &by, &bw, &bh) && by > y + h,
       "settings show the switch, with the backup buttons below it");
    if (have) {
      tap(x + w / 2, y + h / 2);
      ck(clockOpen && !forkSelfCareOn(), "a tap turns self-care off");
      Fork reload;
      reload.begin();
      ck(reload.st.flags & FORK_FLAG_NO_SELFCARE, "and it stays off on disk");
      tap(x + w / 2, y + h / 2);
      ck(forkSelfCareOn(), "a second tap turns it back on");
      tap(x + w / 2, y - 6);
      ck(forkSelfCareOn() && !resetConfirm, "just above it is neither the switch nor RESET");
    }
    clockOpen = false;
  }

  // ---- yesterday's steps ---------------------------------------------------------
  {
    forkData.st.stepDay = 0; forkData.st.stepsToday = 0;
    forkData.st.stepPrevDay = 0; forkData.st.stepPrev = 0;
    forkStepsApply(pet, ForkStepBucket{ DAY, 3000 }, T0);
    ck(forkStepsYesterday(T0) == 0, "today's walk is not yesterday's");
    ck(forkStepsYesterday(T0 + 86400) == 3000, "the next day, before any step: yesterday's 3000");
    forkStepsApply(pet, ForkStepBucket{ DAY + 1, 10 }, T0 + 86400);
    ck(forkStepsYesterday(T0 + 86400) == 3000, "after the first step of the day, still");
    ck(forkStepsYesterday(T0 + 2 * 86400) == 10, "two days on, yesterday is the day after's 10");
    ck(forkStepsYesterday(T0 + 3 * 86400) == 0, "and three days on, nothing");
    forkBannerClear();
    forkData.st.stepPrevDay = 0; forkData.st.stepPrev = 0;
    forkData.st.stepDay = 0; forkData.st.stepsToday = 0;
  }

  // ---- the bubbles: what -----------------------------------------------------------
  healthy();
  forkSayReset();
  ck(forkSayReason(pet, T0) == FORK_SAY_NONE, "a cared-for creature has nothing to say");
  pet.hygiene = 10;
  ck(forkSayReason(pet, T0) == FORK_SAY_DIRTY, "dirty, it says so");
  pet.fullness = 10;
  ck(forkSayReason(pet, T0) == FORK_SAY_HUNGRY, "hunger comes before the bath");
  forkData.st.sickKey = forkCreatureKey(pet);
  ck(forkSayReason(pet, T0) == FORK_SAY_SICK, "and being sick before both");
  forkData.st.sickKey = 0;
  pet.sleeping = true;
  ck(forkSayReason(pet, T0) == FORK_SAY_NONE, "asleep, nothing");
  healthy();

  // ---- the bubbles: when, driven through loop() ---------------------------------
  forkSayReset();
  uiTileGo(uiTileNamed("pet"));
  pet.fullness = 10;
  const uint32_t t0 = millis();
  auto runFor = [](uint32_t ms) {
    const uint32_t end = millis() + ms;
    while ((int32_t)(millis() - end) < 0) poll();
  };
  runFor(FORK_SAY_FIRST_MS - 1000);
  ck(!forkSaying(millis()), "not in the first seconds on the pet screen");
  runFor(2000);
  const char *said = forkSaying(millis());
  ck(said && !strcmp(said, FT(FS_SAY_HUNGRY)), "then it says it is hungry");
  render();   // the bubble draws without tripping anything
  runFor(FORK_SAY_SHOW_MS);
  ck(!forkSaying(millis()), "for a few seconds only");
  // A second reason now, so the gap is the only thing keeping it quiet.
  pet.hygiene = 10;
  {
    bool spoke = false;
    const uint32_t end = millis() + FORK_SAY_GAP_MS - FORK_SAY_SHOW_MS - 2000;
    while ((int32_t)(millis() - end) < 0) { poll(); spoke |= forkSaying(millis()) != nullptr; }
    ck(!spoke, "with something new to say, still nothing inside the gap");
  }
  runFor(4000);
  said = forkSaying(millis());
  ck(said && !strcmp(said, FT(FS_SAY_DIRTY)),
     "after the gap, still hungry: the next reason, since hunger was just said");
  runFor(FORK_SAY_GAP_MS);
  ck(!forkSaying(millis()) || strcmp(forkSaying(millis()), FT(FS_SAY_HUNGRY)) != 0,
     "hunger is not repeated inside its quarter of an hour");
  (void)t0;
  // off the pet screen: nothing, and arriving restarts the wait
  healthy(); forkSayReset();
  pet.fullness = 10;
  uiTileGo(uiTileNamed("pet"));
  const uint8_t mainScr = uiCurrentScreen();
  uiTileGo(uiTileNamed("gym"));
  {
    bool spoke = false;
    const uint32_t end = millis() + FORK_SAY_FIRST_MS * 3;
    while ((int32_t)(millis() - end) < 0) { poll(); spoke |= forkSaying(millis()) != nullptr; }
    ck(uiCurrentScreen() != mainScr && !spoke, "on another screen it says nothing, the whole time");
  }

  // morning: waking up is greeted once
  healthy(); forkSayReset();
  uiTileGo(uiTileNamed("pet"));
  pet.sleeping = true; poll();
  pet.sleeping = false;
  runFor(FORK_SAY_FIRST_MS + 1000);
  said = forkSaying(millis());
  ck(said && !strcmp(said, FT(FS_SAY_MORNING)), "woken up, it says good morning");
  runFor(FORK_SAY_GAP_MS + 1000);
  ck(forkSayReason(pet, T0) == FORK_SAY_NONE, "and only once");

  // after self-care, it says so
  healthy(); forkSayReset(); low();
  const bool cared = forkSelfCareLive(pet, IDLE);
  ck(cared && forkSayReason(pet, T0) == FORK_SAY_SELFCARE, "having looked after itself, it says so");

  // yesterday's walk
  healthy(); forkSayReset();
  forkData.st.stepDay = DAY - 1; forkData.st.stepsToday = 4200;
  ck(forkSayReason(pet, T0) == FORK_SAY_WALKED, "a good walk yesterday is mentioned");
  runFor(FORK_SAY_FIRST_MS + 1000);
  said = forkSaying(millis());
  char want[40];
  snprintf(want, sizeof(want), FT(FS_SAY_WALKED_FMT), 4200ul);
  ck(said && !strcmp(said, want), "with the count");
  ck(forkSayReason(pet, T0) == FORK_SAY_NONE, "once a day");
  forkData.st.stepsToday = FORK_SAY_STEPS - 1;
  forkSayReset();
  ck(forkSayReason(pet, T0) == FORK_SAY_NONE, "a short walk is not");
  forkData.st.stepDay = 0; forkData.st.stepsToday = 0;

  // never over a reaction's marks
  healthy(); forkSayReset();
  pet.fullness = 10;
  runFor(FORK_SAY_FIRST_MS - 500);
  forkPetTap(pet, 233, 300, true, millis());
  runFor(1000);
  ck(forkReact(millis()) != FORK_REACT_NONE && !forkSaying(millis()), "not while a reaction shows");
  runFor(FORK_REACT_MS);
  ck(forkSaying(millis()) != nullptr, "and right after it");

  // ---- the planner --------------------------------------------------------------
  healthy();
  pet.energy = 20;
  const int napEasy = count(FORK_BEH_NAP);
  withNature(NATURE_LAZY);
  const int napLazy = count(FORK_BEH_NAP);
  pet.energy = 90;
  const int napLazyRested = count(FORK_BEH_NAP);
  withNature(NATURE_EASYGOING);
  const int napRested = count(FORK_BEH_NAP);
  ck(napEasy > 0 && napLazy > napEasy, "tired, it naps; a LAZY one more often");
  ck(napRested == 0 && napLazyRested > 0, "rested, only a LAZY one ever naps");
  pet.joy = 90;
  const int hopEasy = count(FORK_BEH_HOP);
  withNature(NATURE_PLAYFUL);
  const int hopPlay = count(FORK_BEH_HOP);
  pet.joy = 30;
  ck(hopEasy > 0 && hopPlay > hopEasy && count(FORK_BEH_HOP) == 0,
     "happy, it hops, a PLAYFUL one more; unhappy, never");
  withNature(NATURE_EASYGOING);
  ck(count(FORK_BEH_MESS) == 0, "no poops, no visit to them");
  pet.poops = 2;
  const int messEasy = count(FORK_BEH_MESS);
  withNature(NATURE_TIDY);
  ck(messEasy > 0 && count(FORK_BEH_MESS) > messEasy, "poops draw it over; a TIDY one more");
  {
    withNature(NATURE_LAZY);
    pet.energy = 10; pet.joy = 90; pet.poops = 3;
    ck(count(FORK_BEH_NONE) >= 20, "whatever it wants, upstream's own wandering keeps a share");
    pet.sleeping = true;
    ck(count(FORK_BEH_NONE) == 100, "asleep, the planner chooses nothing");
  }
  healthy();

  forkSetFeatures(false);
  printf("%s\n", bad ? "behave_test FAILED" : "behave_test passed");
  return bad ? 1 : 0;
}
