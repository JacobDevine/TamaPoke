// The Android fork's expeditions (fork.h section 8).
// See android/FEATURES_PLAN.md, Phase 5.
//
// Driven through the real sketch wherever a rule has a caller that could
// ignore it (CLAUDE.md trap 1): explore's EXPEDITION tab and its button, the
// loop's poll, the live tick and the offline catch-up standing still, the
// pet screen's buttons, the squad, the bag and the evolution offer. The
// switch-off half proves the board sees none of it.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "battle.h"
#include "inventory.h"
#include "fork.h"
#include "fork_i18n.h"
#include "ckpt.h"
#include <cstdio>
#include <cstring>

uint32_t g_seed = 53;
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
void onTap(int16_t x, int16_t y);
uint32_t rtcEpoch();
void rtcSetEpoch(uint32_t e);
const char *statusMsg();
bool uiButtonDisabled(int i);
void uiButtonAt(int i, int *cx, int *cy, int *half);
void uiExploreRects(int *regY, int *regH, int *difY, int *difH, int *btnY, int *btnH);
bool pickExists(uint8_t n);
bool startWildBattle(uint8_t region, bool hard);
void uiTileGo(int i);
int uiTileNamed(const char *n);
uint8_t uiCurrentScreen();
extern const char *const SCREEN_NAME[];
void bagRowRect(int i, int *x, int *y, int *w, int *h);
void bagTap(int16_t x, int16_t y);
extern uint8_t bagPage;
extern Pet pet;
extern Party party;
extern bool exploreOpen, forkExpMode, battleOpen, menuOpen, bagOpen;
extern uint8_t forkExpSite, forkExpLen, btlSquadN;
extern bool btlPetIn;
extern uint16_t squadMask;
extern Arduino_Canvas *gfx;

static int bad = 0;
static void ck(bool ok, const char *what) {
  printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) bad++;
}

// 2026-11-12 at noon, past CLOCK_EPOCH_FLOOR.
static const uint32_t T0 = 20769u * 86400u + 12 * 3600;

static void healthy() {
  pet.dbgHatchAs(4, false);
  pet.ageMinutes = 14 * MINUTES_PER_LEVEL;
  pet.fullness = 80; pet.joy = 80; pet.energy = 90; pet.hygiene = 90;
  pet.poops = 0; pet.weight = 0; pet.sleeping = false; pet.careMistakes = 0;
  while (pet.hasLearnOffer()) pet.declineLearn();
  pet.dbgSetSeen(T0);
}

static uint8_t bagTotal() {
  uint8_t t = 0;
  for (ItemKey k = 1; k < ITEM_COUNT; k++) t = (uint8_t)(t + bag.count(k));
  return t;
}

static int banners() {
  int n = 0;
  const char *l1, *l2;
  // forkBanner() shows each for FORK_BANNER_MS; step past each one to count it
  while (n < 10 && forkBanner(&l1, &l2)) { n++; emuAdvanceMs(FORK_BANNER_MS + 10); }
  return n;
}

static void tapCentre(int y, int h) { onTap(233, (int16_t)(y + h / 2)); }

int main() {
  gRegionArt = 0xFFFF;

  // ---- the switch off: the board ----------------------------------------------
  forkSetFeatures(false);
  setup();
  if (pet.awaitingStarter()) pet.dbgHatchAs(4, false);
  healthy();
  forkData.st = ForkState();
  ck(forkExpCanSend(pet, 0, T0) == FORK_EXP_OFF && !forkExpSend(pet, 0, 0, T0),
     "off: nothing can be sent");
  forkData.st.expKey = forkCreatureKey(pet);   // a record saying it is out...
  forkData.st.expEnd = T0 + 900;
  ck(!forkAway(pet) && !uiButtonDisabled(0) && pickExists(0) && !pet.forkIsAway(),
     "off: ...is not believed by the buttons, the squad or the pet");
  pet.update(millis() + PET_TICK_MS);
  {
    const uint8_t f0 = pet.fullness;
    for (int i = 0; i < 3; i++) pet.update(millis() + PET_TICK_MS * (i + 2));
    ck(pet.fullness < f0, "off: the tick drains as upstream's");
  }
  ck(forkExpMinutesLeft(pet, T0) == 0, "off: the catch-up skips nothing");
  exploreOpen = true; forkExpMode = false;
  int regY, regH, difY, difH, btnY, btnH;
  uiExploreRects(&regY, &regH, &difY, &difH, &btnY, &btnH);
  onTap(300, 50);   // where the tab would be, on the title
  ck(!forkExpMode, "off: explore has no expedition tab");
  forkData.st = ForkState();
  exploreOpen = false;

  // ---- on: the phone ------------------------------------------------------------
  forkSetFeatures(true);
  forkData.begin();
  forkTestSetSickness(false);   // this suite's neglect must not turn into sickness

  // the record: the new fields round-trip, a Phase 4 body reads as no trip, and
  // a site this build does not know is no trip either
  {
    ForkState s; s.coins = 12; s.stepsTotal = 4321;
    s.expKey = 0x80012345u; s.expEnd = T0 + 1800; s.expSteps = 4000; s.expSite = 2; s.expLen = 1;
    uint8_t buf[FORK_BLOB_CAP];
    uint16_t n = Fork::encode(s, buf, sizeof(buf), 5);
    ForkState r;
    ck(n && Fork::decode(buf, n, r) && r.expKey == 0x80012345u && r.expEnd == T0 + 1800 &&
       r.expSteps == 4000 && r.expSite == 2 && r.expLen == 1 && r.stepsTotal == 4321,
       "the Phase 5 fields round-trip");
    uint8_t old[128];
    memcpy(old, buf, CKPT_HDR + 32);
    memcpy(old + CKPT_HDR + 32, buf + CKPT_HDR + FORK_BODY_LEN, CKPT_CRC);
    ForkState o;
    ck(Fork::decode(old, CKPT_HDR + 32 + CKPT_CRC, o) && o.expKey == 0 && o.expEnd == 0 &&
       o.stepsTotal == 4321, "a record from before Phase 5 loads with no trip and keeps its steps");
    buf[CKPT_HDR + FORK_OFF_EXP_SITE] = FORK_EXP_SITES;   // a later build's site
    ForkState f;
    ck(Fork::decode(buf, n, f) && f.expKey == 0, "a site this build does not know reads as no trip");
  }

  // who may go
  healthy();
  forkData.st.medicine = 0;
  ck(forkExpCanSend(pet, 2, T0) == FORK_EXP_OK, "a healthy creature can go on the longest trip");
  ck(forkExpCanSend(pet, 0, 0) == FORK_EXP_NO_CLOCK, "not with no clock to measure it by");
  pet.sleeping = true;
  ck(forkExpCanSend(pet, 0, T0) == FORK_EXP_ASLEEP, "not asleep");
  pet.sleeping = false;
  forkData.st.sickKey = forkCreatureKey(pet);
  ck(forkExpCanSend(pet, 0, T0) == FORK_EXP_SICK, "not sick");
  forkData.st.sickKey = 0;
  pet.energy = FORK_EXP_ENERGY[2] - 1;
  ck(forkExpCanSend(pet, 2, T0) == FORK_EXP_TIRED && forkExpCanSend(pet, 1, T0) == FORK_EXP_OK,
     "with too little energy for the long trip, the shorter one still goes");
  pet.energy = 90;
  pet.dbgHatchAs(4, false); pet.ageMinutes = 0;
  {
    Pet egg; egg.begin(); egg.newEgg();
    ck(forkExpCanSend(egg, 0, T0) == FORK_EXP_OFF, "an egg cannot go");
  }

  // ---- sending it, through explore's tab and button ----
  healthy();
  bag.clear();
  forkBannerClear();
  forkData.st.stepsTotal = 1000;
  rtcSetEpoch(T0);
  uiTileGo(uiTileNamed("explore"));
  ck(exploreOpen && !forkExpMode, "explore opens on its own tab");
  onTap(300, 50);
  ck(forkExpMode, "the title's right half is the EXPEDITION tab");
  gfx->frameReady = false;
  render();
  ck(gfx->frameReady, "the expedition tab flushes");
  forkExpSite = 0; forkExpLen = 0;
  onTap(70 + 10, (int16_t)(regY + regH / 2));   // the site row's left arrow
  ck(forkExpSite == FORK_EXP_FOREST, "the site row's left arrow wraps to the last site");
  onTap(388 - 10, (int16_t)(regY + regH / 2));
  ck(forkExpSite == FORK_EXP_COAST, "and its right arrow comes back");
  onTap(388 - 10, (int16_t)(regY + regH / 2));
  ck(forkExpSite == FORK_EXP_VOLCANO, "...to the volcano");
  tapCentre(difY, difH);
  ck(forkExpLen == 1, "the length pill steps 15 -> 30 minutes");
  const uint8_t e0 = pet.energy;
  tapCentre(btnY, btnH);
  ck(!exploreOpen && forkAway(pet), "SEND sends it, and returns to the pet screen");
  ck(pet.energy == e0 - FORK_EXP_ENERGY[1], "the trip cost its energy");
  ck(forkData.st.expSite == FORK_EXP_VOLCANO && forkData.st.expLen == 1 &&
     forkData.st.expEnd == T0 + 30 * 60 && forkData.st.expSteps == 1000,
     "the record holds the site, the length, the end and the steps so far");
  {
    Fork reload; reload.begin();
    ck(reload.st.expKey == forkCreatureKey(pet) && reload.st.expEnd == T0 + 30 * 60,
       "the trip is on disk before anything else happens");
  }
  ck(forkExpCanSend(pet, 0, T0) == FORK_EXP_AWAY, "it cannot be sent twice");

  // ---- while it is out ----
  ck(!strcmp(statusMsg(), FT(FS_EXP_AWAY)), "the header says it is out");
  {
    bool all = true;
    for (int i = 0; i < 4; i++) all &= uiButtonDisabled(i);
    ck(all, "every care button is greyed");
    int cx, cy;
    uiButtonAt(0, &cx, &cy, nullptr);
    onTap((int16_t)cx, (int16_t)cy);
    render();
    ck(!menuOpen && forkAway(pet) && !strcmp(SCREEN_NAME[uiCurrentScreen()], "main"), "and a tap on FOOD opens nothing");
  }
  {
    // the pet zone: a caress would give joy and bond to a creature that is not there
    pet.joy = 50;
    const uint8_t b = pet.bond;
    onTap(233, 200);
    ck(pet.joy == 50 && pet.bond == b && !pet.showHeart(), "a tap where it would stand is no caress");
    forkData.st.expKey = 0;
    onTap(233, 200);
    ck(pet.joy > 50, "(the same tap is one at home)");
    forkData.st.expKey = forkCreatureKey(pet);
  }
  ck(!pickExists(0), "it is not offered for a squad");
  {
    PartyMon m; m.dex = 7; m.level = 10;
    m.ivAtk = m.ivDef = m.ivSpe = m.ivHp = 10;
    party.replaceAt(0, m);
    squadMask = 0xFFFF;
    ck(startWildBattle(0, false) && !btlPetIn && btlSquadN == 1,
       "a wild battle goes with the party alone");
    battleOpen = false;
    party.releaseAt(0);
  }
  {
    // a training item from the bag: the first upstream row, after the fork's
    // two (medicine and its shop)
    bag.clear();
    bag.add(IT_PROTEIN);
    const uint8_t tr = pet.trAtk;
    bagOpen = true; bagPage = 0;
    int rx, ry, rw, rh;
    bagRowRect(2, &rx, &ry, &rw, &rh);
    bagTap((int16_t)(rx + rw / 2), (int16_t)(ry + rh / 2));
    ck(bag.count(IT_PROTEIN) == 1 && pet.trAtk == tr, "a PROTEIN from the bag is not used on it");
    forkData.st.expKey = 0;
    bagTap((int16_t)(rx + rw / 2), (int16_t)(ry + rh / 2));
    ck(bag.count(IT_PROTEIN) == 0 && pet.trAtk > tr, "(the same tap uses it at home)");
    forkData.st.expKey = forkCreatureKey(pet);
    bagOpen = false;
  }
  {
    // nothing moves: a live hour of ticks (after catching the tick clock up)
    pet.update(millis());
    const uint8_t f = pet.fullness, j = pet.joy, e = pet.energy, h = pet.hygiene, p = pet.poops;
    const uint32_t age = pet.ageMinutes;
    const uint32_t ms = millis();
    for (int i = 1; i <= 60; i++) pet.update(ms + PET_TICK_MS * i);
    ck(pet.fullness == f && pet.joy == j && pet.energy == e && pet.hygiene == h && pet.poops == p,
       "an hour of live ticks out: no need moves, nothing gets dirty");
    ck(pet.ageMinutes == age + 60, "...but it still ages, a minute a tick");
  }
  {
    // the evolution offer waits
    const uint32_t age = pet.ageMinutes;
    pet.ageMinutes = 40 * MINUTES_PER_LEVEL;
    ck(!pet.wantEvolveButton(), "no evolution is offered while it is out");
    forkData.st.expKey = 0;
    const bool would = pet.wantEvolveButton();
    forkData.st.expKey = forkCreatureKey(pet);
    ck(would, "(and one would be, at home)");
    pet.ageMinutes = age;
  }
  ck(forkExpMinutesLeft(pet, T0) == 30 && forkExpMinutesLeft(pet, T0 + 29 * 60 + 1) == 1 &&
     forkExpMinutesLeft(pet, T0 + 30 * 60) == 0, "the minutes left round a part minute up");

  // ---- coming back, through the loop's poll ----
  forkBannerClear();
  bag.clear();
  pet.dbgSetSeen(T0 + 30 * 60 - 1);
  forkExpPoll(pet, T0 + 30 * 60 - 1);
  ck(forkAway(pet) && bagTotal() == 0, "a second early: still out, nothing found");
  forkData.st.stepsTotal = 1000 + FORK_EXP_WALK[1] - 1;
  rtcSetEpoch(T0 + 30 * 60);
  for (int i = 0; i < 40; i++) { emuAdvanceMs(1000); loop(); }
  ck(!forkAway(pet) && forkData.st.expKey == 0, "the loop's poll brings it back once it is due");
  ck(bagTotal() == FORK_EXP_FINDS[1], "a 30 minute trip finds two things (one step short of the bonus)");
  {
    Fork reload; reload.begin();
    ck(reload.st.expKey == 0, "the return is on disk");
  }
  ck(strcmp(statusMsg(), FT(FS_EXP_AWAY)) != 0 && !uiButtonDisabled(0), "and it is home: the buttons work");

  // the walking bonus, and the medicine from the forest
  healthy();
  bag.clear();
  forkBannerClear();
  forkData.st.medicine = 0;
  forkData.st.stepsTotal = 500;
  ck(forkExpSend(pet, FORK_EXP_FOREST, 2, T0), "a long trip to the forest");
  forkData.st.stepsTotal = 500 + FORK_EXP_WALK[2];
  forkExpPoll(pet, T0 + 60 * 60);
  const int found = bagTotal() + forkData.st.medicine;
  ck(found == FORK_EXP_FINDS[2] + 1, "walking 1200 steps while it was out finds one more");
  ck(banners() == FORK_EXP_FINDS[2] + 1, "a banner for each find");

  // the tables: every entry is something, each site has its own character
  {
    bool valid = true;
    int med = 0, train = 0, heal = 0;
    for (uint8_t site = 0; site < FORK_EXP_SITES; site++)
      for (uint8_t r = 0; r < 100; r++) {
        const uint8_t k = forkExpFind(site, r);
        valid &= k == FORK_EXP_MEDICINE || itemValid(k);
        if (k == FORK_EXP_MEDICINE) med += site == FORK_EXP_FOREST;
        else if (itemEntry(k).category == IC_TRAIN) train += site == FORK_EXP_VOLCANO;
        else if (itemEntry(k).category == IC_HEAL) heal += site == FORK_EXP_COAST;
      }
    ck(valid, "every find is an upstream item or the fork's medicine");
    ck(med == 30 && train == 60 && heal == 60,
       "the forest is 30% medicine, the volcano 60% training, the coast 60% potions");
    ck(forkExpFind(FORK_EXP_COAST, 0) != FORK_EXP_MEDICINE && forkExpFind(FORK_EXP_VOLCANO, 0) != FORK_EXP_MEDICINE,
       "medicine only comes from the forest");
  }

  // ---- the offline catch-up stands still for the minutes it was out ----
  {
    healthy();
    Pet &a = pet;
    ck(forkExpSend(a, FORK_EXP_COAST, 1, T0), "(out for 30 minutes, then the app closes)");
    a.dbgSetSeen(T0);
    a.syncClock(T0 + 100 * 60);
    const uint8_t fa = a.fullness, ja = a.joy, ea = a.energy, ha = a.hygiene;
    forkData.st.expKey = 0;
    healthy();
    a.energy = 90 - FORK_EXP_ENERGY[1];
    a.dbgSetSeen(T0);
    a.syncClock(T0 + 70 * 60);
    ck(a.fullness == fa && a.joy == ja && a.energy == ea && a.hygiene == ha,
       "100 minutes closed with 30 of them out drains exactly what 70 at home does");
  }
  {
    // a trip that ended long before the app came back: only its own minutes stand still
    healthy();
    ck(forkExpSend(pet, FORK_EXP_COAST, 0, T0), "(out for 15 minutes)");
    pet.dbgSetSeen(T0 + 20 * 60);   // the app was open past the end, then closed
    ck(forkExpMinutesLeft(pet, T0 + 20 * 60) == 0, "a closing after it was due skips nothing");
    forkExpPoll(pet, T0 + 20 * 60);
    ck(!forkAway(pet), "and the poll brings it back");
  }
  {
    // a clock moved back by hours does not keep it out for hours
    healthy();
    ck(forkExpSend(pet, FORK_EXP_COAST, 0, T0 + 5 * 3600), "(sent with the clock five hours fast)");
    forkExpPoll(pet, T0);
    ck(!forkAway(pet), "the clock coming back five hours brings it home at once");
    ck(forkExpMinutesLeft(pet, T0) == 0, "and skips no catch-up minutes");
  }

  // ---- called back: swapped, released, or replaced while it was out ----
  {
    healthy();
    bag.clear();
    forkBannerClear();
    ck(forkExpSend(pet, FORK_EXP_FOREST, 2, T0), "(out for an hour)");
    pet.dbgHatchAs(25, false);   // another creature is the live pet now
    ck(!forkAway(pet) && !uiButtonDisabled(0), "the new live pet is not the one that is out");
    forkExpPoll(pet, T0 + 60);
    const char *l1 = nullptr, *l2 = nullptr;
    ck(forkData.st.expKey == 0 && bagTotal() == 0, "the one that left is called back, empty-handed");
    ck(forkBanner(&l1, &l2) && !strcmp(l1, FT(FS_EXP_EARLY)), "and the banner says so");
  }

  // a full bag keeps nothing it cannot hold, and says so. Twelve long forest
  // trips are 36 finds at 30% medicine each, so the medicine's own cap is
  // exercised and not only the bag's (the seed is fixed: the same draws every run).
  {
    bag.clear();
    for (ItemKey k = 1; k < ITEM_COUNT; k++) while (bag.add(k)) {}
    forkData.st.medicine = 99;
    bool capped = true, said = true;
    for (int trip = 0; trip < 12; trip++) {
      healthy();
      forkBannerClear();
      if (!forkExpSend(pet, FORK_EXP_FOREST, 2, T0)) { capped = false; break; }
      forkExpPoll(pet, T0 + 60 * 60);
      const char *l1 = nullptr, *l2 = nullptr;
      while (forkBanner(&l1, &l2)) {
        said &= !strcmp(l2, FT(FS_BAG_FULL));
        emuAdvanceMs(FORK_BANNER_MS + 10);
      }
      capped &= forkData.st.medicine == 99;
    }
    ck(capped, "full: medicine stays at 99 over twelve hours of forest finds");
    ck(said, "every find that did not fit is named as BAG IS FULL");
    bag.clear();
  }

  // the sign on the pet screen draws, and the frame flushes
  {
    healthy();
    ck(forkExpSend(pet, FORK_EXP_VOLCANO, 0, T0), "(out again)");
    exploreOpen = false;
    gfx->frameReady = false;
    render();
    ck(gfx->frameReady, "the pet screen flushes with it out");
    forkExpPoll(pet, T0 + 15 * 60);
  }

  // the switch off with a trip in the record: the board's game is untouched
  {
    healthy();
    ck(forkExpSend(pet, FORK_EXP_VOLCANO, 0, T0), "(out, then the switch goes off)");
    forkSetFeatures(false);
    const uint8_t f = pet.fullness;
    const uint32_t ms = millis();
    for (int i = 1; i <= 5; i++) pet.update(ms + PET_TICK_MS * i);
    ck(pet.fullness < f && !uiButtonDisabled(0) && pickExists(0), "off: the tick and the buttons are upstream's");
    forkSetFeatures(true);
    forkData.st.expKey = 0;
  }

  printf("%s\n", bad ? "FAILURES" : "all good");
  return bad ? 1 : 0;
}
