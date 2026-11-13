// The Android fork's step counter (fork.h section 7).
// See android/FEATURES_PLAN.md, Phase 4.
//
// The phone's half (the sensor, the reboot correction, the midnight split) is
// Java and runs only on a phone. This is the game's half, driven through the
// real sketch where a rule has a caller that could ignore it (CLAUDE.md trap
// 1): the loop's poll, the medals screen's steps page and its permission
// button, the wild shiny roll and the ball's catch chance. The switch-off half
// proves the board sees none of it.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "battle.h"
#include "inventory.h"
#include "wild.h"
#include "fork.h"
#include "fork_i18n.h"
#include "ckpt.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <utility>

uint32_t g_seed = 47;
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
bool startWildBattle(uint8_t region, bool hard);
bool wildRollShiny(uint32_t roll);
uint8_t btlBallChance(uint8_t k);
uint8_t uiRimPages();
uint32_t rtcEpoch();
extern Pet pet;
extern bool medalsOpen, battleOpen;
extern uint8_t medalsPage;
extern Combatant btlFoe, btlYou;
extern uint8_t btlMenu;
extern bool btlOver, wildCaught;
int btlCellIndexAt(int16_t x, int16_t y);
extern Arduino_Canvas *gfx;

static int bad = 0;
static void ck(bool ok, const char *what) {
  printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) bad++;
}

// --- a fake phone -----------------------------------------------------------
static std::vector<ForkStepBucket> gPending;
static uint8_t gStatus = FORK_STEPS_ON;
static int gAsks = 0, gAsksFirst = 0;
static uint8_t fakeTake(ForkStepBucket *out, uint8_t max) {
  uint8_t n = 0;
  while (n < max && !gPending.empty()) { out[n++] = gPending.front(); gPending.erase(gPending.begin()); }
  return n;
}
static uint8_t fakeStatus() { return gStatus; }
static void fakeAsk(bool firstOnly) { gAsks++; if (firstOnly) gAsksFirst++; }

// 2026-11-12, past CLOCK_EPOCH_FLOOR. Noon, so a day is unambiguous.
static const uint32_t D = 20769u;
static uint32_t noon(uint32_t day) { return day * 86400u + 12 * 3600; }

static void walk(uint32_t day, uint32_t steps, uint32_t clockDay) {
  pet.dbgSetSeen(noon(clockDay));
  gPending.push_back(ForkStepBucket{ day, steps });
  forkStepsPoll(pet, noon(clockDay));
}

static void healthy() {
  pet.dbgHatchAs(4, false);
  pet.ageMinutes = 14 * MINUTES_PER_LEVEL;
  pet.fullness = 60; pet.joy = 50; pet.energy = pet.hygiene = 90;
  pet.poops = 0; pet.weight = 0; pet.sleeping = false;
  while (pet.hasLearnOffer()) pet.declineLearn();
}

static uint8_t sumOf(const ItemKey *ks, int n) {
  uint8_t t = 0;
  for (int i = 0; i < n; i++) t = (uint8_t)(t + bag.count(ks[i]));
  return t;
}
static const ItemKey M1[] = { IT_POTION, IT_GREATBALL };
static const ItemKey M2[] = { IT_PROTEIN, IT_IRON, IT_CARBOS, IT_ULTRABALL };

int main() {
  gRegionArt = 0xFFFF;
  gForkHost.takeSteps = fakeTake;
  gForkHost.stepsStatus = fakeStatus;
  gForkHost.askSteps = fakeAsk;

  // ---- the switch off: the board ----------------------------------------------
  forkSetFeatures(false);
  setup();
  if (pet.awaitingStarter()) pet.dbgHatchAs(4, false);
  healthy();
  bag.clear();
  forkData.st = ForkState();
  gPending.push_back(ForkStepBucket{ D, 6000 });
  forkStepsPoll(pet, noon(D));
  ck(gPending.size() == 1 && forkData.st.stepsTotal == 0 && bag.distinctCount() == 0,
     "off: the poll never asks the phone and pays nothing");
  forkStepsApply(pet, ForkStepBucket{ D, 6000 }, noon(D));
  ck(forkData.st.stepsTotal == 0 && bag.distinctCount() == 0, "off: a bucket handed in directly pays nothing");
  forkData.st.stepDay = D; forkData.st.stepsToday = 6000;
  pet.dbgSetSeen(noon(D));
  ck(forkStepsToday(noon(D)) == 0 && forkStepWildPct(noon(D)) == 0,
     "off: a record saying so still counts no steps");
  ck(!wildRollShiny(WILD_SHINY_BASE) && forkStepCatch(30, noon(D)) == 30,
     "off: the wild odds are upstream's");
  medalsOpen = true; medalsPage = 0;
  ck(uiRimPages() == 2, "off: the medals screen keeps its two pages");
  medalsOpen = false;
  const char *l1 = nullptr, *l2 = nullptr;
  ck(!forkBanner(&l1, &l2), "off: no banner");
  gPending.clear();
  forkData.st = ForkState();

  // ---- on: the phone ------------------------------------------------------------
  forkSetFeatures(true);
  forkData.begin();

  // the record: the new fields round-trip, and a Phase 3 body reads them as zero
  {
    ForkState s; s.coins = 77; s.stepDay = D; s.stepsToday = 1234; s.stepsTotal = 98765;
    s.stepMiles = 5; s.stepJoy = 4; s.stepBond = 1; s.stepRank = 2;
    uint8_t buf[FORK_BLOB_CAP];
    uint16_t n = Fork::encode(s, buf, sizeof(buf), 3);
    ForkState r;
    ck(n && Fork::decode(buf, n, r) && r.coins == 77 && r.stepDay == D && r.stepsToday == 1234 &&
       r.stepsTotal == 98765 && r.stepMiles == 5 && r.stepJoy == 4 && r.stepBond == 1 && r.stepRank == 2,
       "the Phase 4 fields round-trip");
    uint8_t old[64];
    memcpy(old, buf, CKPT_HDR + 16);
    memcpy(old + CKPT_HDR + 16, buf + CKPT_HDR + FORK_BODY_LEN, CKPT_CRC);
    ForkState o;
    ck(Fork::decode(old, CKPT_HDR + 16 + CKPT_CRC, o) && o.coins == 77 && o.stepDay == 0 &&
       o.stepsToday == 0 && o.stepsTotal == 0 && o.stepMiles == 0 && o.stepRank == 0,
       "a record from before Phase 4 loads with no steps and keeps its coins");
  }

  // the milestones, at their boundaries, through the poll
  healthy();
  bag.clear();
  forkBannerClear();
  forkData.st = ForkState();
  walk(D, 499, D);
  ck(forkData.st.stepsTotal == 499 && forkStepsToday(noon(D)) == 499, "499 steps are counted");
  ck(bag.count(IT_POKEBALL) == 0 && !forkBanner(&l1, &l2), "and earn no milestone");
  ck(pet.joy == 51, "but a point of joy (one per 250)");
  walk(D, 1, D);
  ck(bag.count(IT_POKEBALL) == 1, "the 500th step pays a POKE BALL");
  ck(forkBanner(&l1, &l2) && strstr(l1, "500") && !strcmp(l2, "POKE BALL"),
     "and the main screen's banner says so");
  forkBannerClear();
  walk(D, 1499, D);
  ck(sumOf(M1, 2) == 0, "1999 steps: not yet the second");
  walk(D, 1, D);
  ck(sumOf(M1, 2) == 1, "2000 pays a POTION or a GREAT BALL");
  const uint8_t bond0 = pet.bond;
  walk(D, 3000, D);
  ck(sumOf(M2, 4) == 1, "5000 pays a vitamin or an ULTRA BALL");
  ck(pet.joy == 50 + FORK_STEP_JOY_MAX, "joy stops at 20 a day");
  ck(pet.bond == bond0 + 1 && forkData.st.stepBond == FORK_STEP_BOND_MAX,
     "bond reaches 3 a day (the 3rd point arriving at 3000 steps here)");
  const uint8_t pb = bag.count(IT_POKEBALL), joy5 = pet.joy, bond5 = pet.bond;
  walk(D, 20000, D);
  ck(bag.count(IT_POKEBALL) == pb && sumOf(M1, 2) == 1 && sumOf(M2, 4) == 1,
     "each milestone pays once a day");
  ck(pet.joy == joy5 && pet.bond == bond5, "and 25,000 steps give no more joy or bond than 5,000");
  {
    Fork reload; reload.begin();
    ck(reload.st.stepsToday == 25000 && reload.st.stepsTotal == 25000 && reload.st.stepMiles == 7,
       "the day's count and its milestones are on disk");
  }

  // a new day: everything again
  forkBannerClear();
  walk(D + 1, 600, D + 1);
  ck(forkStepsToday(noon(D + 1)) == 600 && forkData.st.stepMiles == 1 && bag.count(IT_POKEBALL) == pb + 1,
     "the next day starts over, and its 500 pays again");
  ck(forkStepsToday(noon(D + 2)) == 0, "a day with no steps reads 0, whatever the record holds");

  // yesterday's last steps arriving this morning: its milestone, but no joy
  forkData.st.stepDay = D + 2; forkData.st.stepsToday = 300; forkData.st.stepMiles = 0;
  forkData.st.stepJoy = 1; forkData.st.stepBond = 0;
  pet.joy = 40;
  const uint8_t pb2 = bag.count(IT_POKEBALL);
  walk(D + 2, 300, D + 3);
  ck(bag.count(IT_POKEBALL) == pb2 + 1, "yesterday's late steps still reach yesterday's milestone");
  ck(pet.joy == 40, "but give no joy: the walk was not today");
  ck(forkStepsToday(noon(D + 3)) == 0, "and are not today's");
  walk(D + 3, 250, D + 3);
  ck(forkStepsToday(noon(D + 3)) == 250 && pet.joy == 41, "today's own steps then start today");
  const uint32_t tot = forkData.st.stepsTotal;
  walk(D - 4, 1000, D + 3);
  ck(forkData.st.stepsTotal == tot + 1000 && forkStepsToday(noon(D + 3)) == 250,
     "an older day's bucket counts only towards the trail");

  // an egg walks too: items, no joy
  {
    healthy();
    pet.dbgSetSeen(noon(D + 4));
    pet.newEgg();
    const uint8_t j = pet.joy, pbE = bag.count(IT_POKEBALL);
    walk(D + 4, 600, D + 4);
    ck(pet.isEgg() && pet.joy == j && bag.count(IT_POKEBALL) == pbE + 1 && forkData.st.stepJoy == 0,
       "an egg's walk pays its milestone and keeps the joy for the creature");
    healthy();
  }

  // the trail ranks
  forkBannerClear();
  forkData.st.stepsTotal = FORK_STEP_RANK[0] - 1; forkData.st.stepRank = 0;
  walk(D + 5, 1, D + 5);
  ck(forkData.st.stepRank == 1, "10,000 steps ever is the first trail rank");
  bool sawRank = false;
  while (forkBanner(&l1, &l2)) {
    if (!strcmp(l1, FT(FS_RANK_BANNER)) && !strcmp(l2, forkStepRankName(1))) sawRank = true;
    forkBannerClear();
  }
  ck(sawRank, "and it is announced");
  walk(D + 5, 1, D + 5);
  ck(!forkBanner(&l1, &l2) || strcmp(l1, FT(FS_RANK_BANNER)), "once");
  ck(forkStepRankFor(9999) == 0 && forkStepRankFor(10000) == 1 && forkStepRankFor(250000) == 4 &&
     forkStepRankFor(0xFFFFFFFFu) == 4, "the rank table's boundaries");

  // a long absence: more buckets than one read holds
  forkData.st = ForkState();
  for (uint32_t d = 0; d < 20; d++) gPending.push_back(ForkStepBucket{ D + 10 + d, 100 });
  pet.dbgSetSeen(noon(D + 29));
  forkStepsPoll(pet, noon(D + 29));
  ck(gPending.empty() && forkData.st.stepsTotal == 2000 && forkStepsToday(noon(D + 29)) == 100,
     "twenty days of buckets all arrive, oldest first");

  // the banner queue: each shows for its time, then the next
  forkBannerClear();
  forkData.st = ForkState();
  walk(D + 30, 5000, D + 30);
  int shown = 0;
  for (int i = 0; i < 4 && forkBanner(&l1, &l2); i++) { shown++; emuAdvanceMs(FORK_BANNER_MS + 10); forkBanner(&l1, &l2); }
  ck(shown == 3, "three milestones at once are three banners, one after another");

  // the caller: the sketch's loop polls the phone
  forkData.st = ForkState();
  gPending.push_back(ForkStepBucket{ rtcEpoch() / 86400, 123 });
  for (int i = 0; i < 40 && !gPending.empty(); i++) { emuAdvanceMs(1000); loop(); }
  ck(gPending.empty() && forkData.st.stepsTotal == 123, "the sketch's loop polls the phone");

  // the wild bonus, through the sketch's own shiny roll and catch chance
  healthy();
  pet.dbgSetSeen(noon(D + 40));
  forkData.st = ForkState();
  forkData.st.stepDay = D + 40;
  ck(wildRollShiny(WILD_SHINY_BASE - 1) && !wildRollShiny(WILD_SHINY_BASE),
     "no steps: the shiny roll is upstream's 1/4096");
  forkData.st.stepsToday = FORK_STEP_FULL / 2;
  ck(wildRollShiny(WILD_SHINY_BASE + WILD_SHINY_BASE / 2 - 1) &&
     !wildRollShiny(WILD_SHINY_BASE + WILD_SHINY_BASE / 2), "2500 steps: half as likely again");
  forkData.st.stepsToday = FORK_STEP_FULL * 3;
  ck(wildRollShiny(2 * WILD_SHINY_BASE - 1) && !wildRollShiny(2 * WILD_SHINY_BASE),
     "5000 or more: twice the base odds, and no further");
  forkData.st.stepDay = D + 39;
  ck(!wildRollShiny(WILD_SHINY_BASE), "yesterday's walk does not count today");

  forkData.st.stepDay = D + 40; forkData.st.stepsToday = 0;
  ck(startWildBattle(REGION_ALL, false), "a wild battle starts");
  btlFoe.hp = btlFoe.maxHp;
  const uint8_t c0 = btlBallChance(IT_POKEBALL);
  forkData.st.stepsToday = FORK_STEP_FULL;
  const uint8_t c1 = btlBallChance(IT_POKEBALL);
  ck(c0 >= 10 && c1 == (c0 + c0 * FORK_STEP_CATCH_PCT / 100 > 95 ? 95 : c0 + c0 * FORK_STEP_CATCH_PCT / 100),
     "5000 steps: a ball catches a tenth more often");
  ck(btlBallChance(IT_MASTERBALL) == 100, "a MASTER BALL is still a sure catch");
  ck(forkStepCatch(95, noon(D + 40)) == 95 && forkStepCatch(94, noon(D + 40)) == 95 &&
     forkStepCatch(0, noon(D + 40)) == 0, "never past upstream's 95, and nothing from nothing");

  // ...and the throw really uses it: a roll between the two chances catches
  // only with the steps. Through the battle bag's own cell, not the function.
  {
    uint32_t seed = 0;
    for (uint32_t s = 1; s < 100000 && !seed; s++) {
      g_seed = s;
      const long r = random(100);
      if (r >= c0 && r < c1) seed = s;
    }
    ck(seed != 0, "(a roll between the two chances exists)");
    long sx = 0, sy = 0, n = 0;
    for (int16_t y = 0; y < 466; y += 2)
      for (int16_t x = 0; x < 466; x += 2)
        if (btlCellIndexAt(x, y) == 0) { sx += x; sy += y; n++; }
    const int16_t cx = (int16_t)(n ? sx / n : 0), cy = (int16_t)(n ? sy / n : 0);
    const Combatant foe0 = btlFoe;   // the battle c0 and c1 were measured in
    auto throwOnce = [&](uint32_t steps) {
      btlFoe = foe0;
      battleOpen = true; btlOver = false; wildCaught = false;
      bag.clear();
      bag.add(IT_POKEBALL, 5);
      forkData.st.stepsToday = steps;
      btlYou.hp = btlYou.maxHp = 999;
      btlMenu = 3;
      const uint8_t want = btlBallChance(IT_POKEBALL);
      g_seed = seed;
      onTap(cx, cy);
      return std::make_pair(want, wildCaught);
    };
    auto a = throwOnce(0);
    auto b = throwOnce(FORK_STEP_FULL);
    ck(a.first == c0 && b.first == c1, "(the same foe and ball both times)");
    ck(!a.second && b.second, "the battle bag's throw catches with today's steps and misses without");
  }
  battleOpen = false;

  // the steps page and its permission button
  medalsOpen = true; medalsPage = 0;
  ck(uiRimPages() == 3, "the medals screen gains a third page");
  gStatus = FORK_STEPS_OFF; gAsks = gAsksFirst = 0;
  medalsPage = 2;
  gfx->frameReady = false;
  render();
  ck(gfx->frameReady, "the steps page flushes");
  ck(gAsks == 1 && gAsksFirst == 1, "seeing it asks for the permission, first time only");
  render();
  ck(gAsks == 1, "once a run, not every frame");
  onTap(233, 103);
  ck(gAsks == 2 && gAsksFirst == 1 && medalsOpen, "its button asks again, and the page stays");
  onTap(233, 420);
  ck(!medalsOpen, "anywhere else backs out, as before");
  gStatus = FORK_STEPS_ON;
  medalsOpen = true; medalsPage = 2;
  render();
  onTap(233, 103);
  ck(!medalsOpen && gAsks == 2, "counting already, there is no button to hit");

  // the banner queue is the phone's: switched off with one waiting, none shows
  forkBannerClear();
  forkData.st = ForkState();
  walk(D + 50, 600, D + 50);
  ck(forkBanner(&l1, &l2), "(a banner is waiting)");
  forkSetFeatures(false);
  ck(!forkBanner(&l1, &l2), "off: a waiting banner is never drawn");
  printf("%s: %d failure(s)\n", bad ? "FAIL" : "PASS", bad);
  return bad ? 1 : 0;
}
