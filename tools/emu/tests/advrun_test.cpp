// Adventures, step 12c part 1: the run (fork_adv.h). Five stages, the loan,
// the doors, catch-and-swap, hearts, the keep and its reward, the daily free
// run, and the record that lets a killed app resume a run exactly.
//
// The run is driven through its own API and the real store (advSave() /
// advBegin()), never by poking its phase: a test that set `phase` itself would
// prove the transitions it typed, not the ones the run makes.
#include "Arduino.h"
#include "Preferences.h"
#include "fork.h"
#include "fork_adv.h"
#include "battle.h"
#include "ckpt.h"
#include "pet.h"
#include "noart.h"
#include "items.h"
#include <cstdio>
#include <cstring>

#ifndef TAMA_NATIVE_TEST
uint32_t g_seed = 11;
FakeSerial Serial;
FakeESP ESP;
FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0;
volatile bool g_touchDown = false;
bool wasPressed = false;
static uint32_t gNowMs = 0;
uint32_t millis() { return gNowMs; }
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }
void sfxPlay(uint8_t) {}
#endif

extern uint16_t gRegionArt;

static int bad = 0;
static void ck(bool ok, const char *what) {
  printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) bad++;
}

static uint32_t zero(uint32_t) { return 0; }
static uint32_t top(uint32_t n) { return n - 1; }
// The first two SPECIES picks of a roll come out as the same species; every
// other roll is the top one (never shiny). Recognised by the pool size it is
// asked for, so two creatures rolled back to back are identical unless the run
// rerolls them -- which is what the dedupe rules must do.
static uint32_t gStagePool = 0, gSticky = 0;
static uint32_t sticky(uint32_t n) {
  if (n != gStagePool) return n - 1;
  const uint32_t k = gSticky++;
  return k < 2 ? 0 : k;
}

static const uint32_t DAY = 20412;

// A cold start: memory forgotten, the record read back out of the store.
static void reload() {
  gAdv = AdvRun();
  advBegin();
}

// Ends whatever run is on, without paying anything.
static void endRun() {
  advForfeit();
  advFinish();
}

static uint16_t encoded(uint8_t *buf, size_t cap) { return advEncode(gAdv, buf, cap, 1); }

// Field by field, NOT through the encoder: a field the encoder forgot would
// vanish from both sides of an encoded comparison and still match.
static bool sameMon(const Combatant &a, const Combatant &b) {
  bool ok = a.dex == b.dex && a.level == b.level && a.maxHp == b.maxHp && a.hp == b.hp &&
            a.ailment == b.ailment && a.ailTurns == b.ailTurns && a.confuseTurns == b.confuseTurns &&
            a.recharge == b.recharge && a.charging == b.charging && a.shiny == b.shiny &&
            !strcmp(a.name, b.name) && a.typed == b.typed && a.type1 == b.type1 && a.type2 == b.type2;
  for (int i = 0; i < SI_COUNT; i++) ok &= a.base[i] == b.base[i] && a.stage[i] == b.stage[i];
  for (int i = 0; i < MOVE_SLOTS; i++) ok &= a.moves[i] == b.moves[i];
  return ok;
}
static bool sameAdv(const AdvMon &a, const AdvMon &b) {
  return a.dex == b.dex && a.form == b.form && a.shiny == b.shiny && !memcmp(a.iv, b.iv, 4);
}
static bool sameRun(const AdvRun &a, const AdvRun &b) {
  bool ok = a.phase == b.phase && a.diff == b.diff && a.stage == b.stage && a.hearts == b.hearts &&
            a.freeDay == b.freeDay && a.won == b.won && a.caughtN == b.caughtN &&
            a.doorPicked == b.doorPicked && sameAdv(a.boss, b.boss);
  for (int i = 0; i < ADV_LOANS; i++) ok &= sameAdv(a.loan[i], b.loan[i]);
  for (int i = 0; i < ADV_DOORS; i++) ok &= sameAdv(a.door[i], b.door[i]);
  for (int i = 0; i < ADV_STAGES; i++) ok &= sameAdv(a.caught[i], b.caught[i]);
  for (int i = 0; i < RAID_SEATS; i++) {
    ok &= sameAdv(a.seatMon[i], b.seatMon[i]) && sameMon(a.seat[i], b.seat[i]);
    const RaidSeat &x = a.battle.seat[i], &y = b.battle.seat[i];
    ok &= x.chooser == y.chooser && x.form == y.form && x.formed == y.formed && sameMon(x.mon, y.mon);
  }
  const RaidBattle &x = a.battle, &y = b.battle;
  ok &= x.diff == y.diff && x.state == y.state && x.turn == y.turn && sameMon(x.boss.mon, y.boss.mon) &&
        x.boss.form == y.boss.form && x.boss.final == y.boss.final && x.boss.actions == y.boss.actions &&
        x.boss.shields == y.boss.shields && x.boss.shieldsMax == y.boss.shieldsMax &&
        x.boss.barrier == y.boss.barrier && !memcmp(x.boss.barrierPct, y.boss.barrierPct, RAID_BARRIERS_MAX);
  return ok;
}

// Wins the fight on, through the real turn: the foe is put one hit from the
// end, everything else as the run made it.
static bool winFight() {
  if (gAdv.phase != ADV_FIGHT) return false;
  RaidBoss &k = gAdv.battle.boss;
  k.shields = 0;
  k.barrier = RAID_BARRIERS_MAX;   // past every barrier
  k.mon.hp = 1;
  for (int t = 0; t < 40 && gAdv.phase == ADV_FIGHT; t++) {
    RaidTurn out;
    for (uint8_t s = 0; s < MOVE_SLOTS; s++)
      if (advTurn(s, false, out, zero)) break;
    k.mon.hp = k.mon.hp ? 1 : 0;   // a miss or a heal must not drag it out
  }
  return gAdv.phase != ADV_FIGHT;
}

int main() {
  forkSetFeatures(true);
  nvs().clear();
  gRegionArt = 0xFFFF;
  advBegin();

  // --- the pools -----------------------------------------------------------
  {
    bool stageOk = true;
    uint16_t stages = 0;
    for (int16_t d = 1; d <= DEX_COUNT; d++) {
      if (!advStageOk(d)) continue;
      stages++;
      const DexEntry &e = DEX_TBL[d];
      if (e.evolvesTo || e.rarity == R_LEGENDARIO || forkIsMythical(d) || !speciesHasArt(d)) {
        stageOk = false;
        printf("      %d %s should not be a stage foe\n", d, e.name);
      }
    }
    ck(stageOk && stages > 300, "stage foes and loans: fully evolved, not legendary or mythical, drawable");
    ck(!advStageOk(10) && !advStageOk(150) && !advStageOk(151) && advStageOk(6),
       "CATERPIE, MEWTWO and MEW are not stage foes; CHARIZARD is");

    // every boss each difficulty can roll
    bool formsOk = true, easyOnlyMega = true, normalNoBox = true, hardNoMega = true;
    bool normalGmax = false, normalSub = false, hardBox = false, hardMyth = false, hardFusion = false;
    for (uint8_t diff = 0; diff < RAID_DIFFS; diff++) {
      for (int16_t d = 1; d <= DEX_COUNT; d++) {
        if (advBossOk(diff, d, 0)) {
          if (diff == RAID_EASY) easyOnlyMega = false;
          if (diff == RAID_NORMAL && advIsBoxLegend(d)) normalNoBox = false;
          if (diff == RAID_NORMAL && DEX_TBL[d].rarity == R_LEGENDARIO) normalSub = true;
          if (diff == RAID_HARD && advIsBoxLegend(d)) hardBox = true;
          if (diff == RAID_HARD && forkIsMythical(d)) hardMyth = true;
        }
      }
      for (uint16_t id = 1; id <= FORM_COUNT; id++) {
        const FormEntry &f = FORM_TBL[id];
        if (!advBossOk(diff, f.dex, (uint8_t)id)) continue;
        if (!forkFormUsable((uint8_t)id)) formsOk = false;
        if (diff == RAID_EASY && f.kind != FK_MEGA) easyOnlyMega = false;
        if (diff == RAID_NORMAL && f.kind == FK_GMAX) normalGmax = true;
        if (diff == RAID_HARD && f.kind == FK_MEGA) hardNoMega = false;
        if (diff == RAID_HARD && f.kind == FK_FUSION) hardFusion = true;
      }
    }
    ck(formsOk, "no boss is ever a form without art");
    ck(easyOnlyMega, "an easy boss is always a Mega");
    ck(normalGmax && normalSub && normalNoBox, "normal adds G-Max and sub-legendaries, never a box legendary");
    ck(hardBox && hardMyth && hardFusion && hardNoMega,
       "hard is box legendaries, mythicals and fusions, not plain Megas");
    ck(advBossOk(RAID_NORMAL, 144, 0) && !advBossOk(RAID_NORMAL, 150, 0) && advBossOk(RAID_HARD, 150, 0),
       "ARTICUNO is a normal boss; MEWTWO a hard one, not a normal one");

    // a region without its pack is in no pool
    const uint8_t kantoR = regionOfDex(6);
    gRegionArt = (uint16_t)(1u << kantoR);   // Kanto only
    bool kanto = true;
    for (int16_t d = 1; d <= DEX_COUNT; d++)
      for (uint8_t diff = 0; diff < RAID_DIFFS; diff++)
        if ((advStageOk(d) || advBossOk(diff, d, 0)) && regionOfDex(d) != kantoR) kanto = false;
    ck(kanto, "with only Kanto installed, every stage foe and boss is from Kanto");
    ck(advStageOk(6) && advBossOk(RAID_HARD, 150, 0), "(and Kanto still has its own: CHARIZARD, MEWTWO)");
    gRegionArt = 0xFFFF;
  }

  // --- one free run a day --------------------------------------------------
  {
    ck(advStartCost(DAY) == 0, "the first run of the day is free");
    ck(advStart(RAID_EASY, DAY), "it starts");
    ck(!advStart(RAID_EASY, DAY), "a second cannot start while one is on");
    endRun();
    ck(advStartCost(DAY) == ADV_RUN_PRICE, "the next one that day costs coins");
    ck(advStartCost(DAY + 1) == 0, "and the next day is free again");
    reload();
    ck(advStartCost(DAY) == ADV_RUN_PRICE, "the free run taken survives a restart");
  }

  // --- a run, start to finish ------------------------------------------------
  {
    ck(advStart(RAID_NORMAL, DAY + 1), "a normal run starts");
    const AdvRun &r = gAdv;
    ck(r.phase == ADV_LOAN && r.hearts == RAID_HEARTS && r.stage == 0, "at the loan, four hearts, stage 1");
    ck(advBossOk(RAID_NORMAL, r.boss.dex, r.boss.form), "the boss is rolled at the start, from its pool");
    bool loansOk = true;
    for (uint8_t i = 0; i < ADV_LOANS; i++) {
      loansOk &= advStageOk(r.loan[i].dex) && !r.loan[i].shiny;
      for (uint8_t j = 0; j < i; j++) loansOk &= r.loan[i].dex != r.loan[j].dex;
    }
    ck(loansOk, "three different loans, each from the stage pool, none shiny");
    bool compOk = true;
    for (uint8_t i = 1; i < RAID_SEATS; i++)
      compOk &= advStageOk(r.seat[i].dex) && r.seat[i].level == 50 && r.seat[i].hp == r.seat[i].maxHp;
    ck(compOk, "three companions, at the run's level and full HP");

    ck(!advPickDoor(0) && !advCatch(true), "nothing but a loan can be picked at the loan");
    ck(!advPickLoan(ADV_LOANS), "a loan past the three is refused");
    const int16_t loan = r.loan[1].dex;
    ck(advPickLoan(1) && r.phase == ADV_DOOR, "picking a loan opens the doors");
    ck(r.seat[0].dex == loan && r.seat[0].level == 50 && r.seatMon[0].form == advSeatFormOf(loan),
       "seat 1 is the loan, at level 50, with the form it may become");
    ck(advStageOk(r.door[0].dex) && advStageOk(r.door[1].dex), "both doors lead to stage foes");

    uint8_t diffTypes = 0;
    for (int i = 0; i < 4; i++) {
      // the doors are re-rolled through the real path: a run starts each time
      endRun();
      advStart(RAID_EASY, DAY + 1);
      advPickLoan(0);
      diffTypes += DEX_TBL[gAdv.door[0].dex].type1 != DEX_TBL[gAdv.door[1].dex].type1 ? 1 : 0;
    }
    ck(diffTypes >= 3, "the two doors almost always show different types");
    {
      endRun();
      for (int16_t d = 1; d <= DEX_COUNT; d++) gStagePool += advStageOk(d) ? 1 : 0;
      gSticky = 0;
      advStart(RAID_EASY, DAY + 1, sticky);
      const AdvRun &q = gAdv;
      ck(q.loan[0].dex != q.loan[1].dex && q.loan[1].dex != q.loan[2].dex && q.loan[0].dex != q.loan[2].dex,
         "loans that would repeat are rerolled");
      gSticky = 0;
      advPickLoan(0, sticky);
      ck(DEX_TBL[q.door[0].dex].type1 != DEX_TBL[q.door[1].dex].type1,
         "doors that would share a type are rerolled");
    }

    // through four stages
    bool fightOk = true, catchOk = true, carryOk = true;
    for (uint8_t s = 0; s < ADV_STAGES - 1; s++) {
      const AdvMon foe = gAdv.door[1];
      fightOk &= advPickDoor(1) && gAdv.phase == ADV_FIGHT && gAdv.battle.boss.mon.dex == foe.dex &&
                 !gAdv.battle.boss.final;
      const uint16_t hp0 = 7 + s;   // a battered seat 1
      gAdv.battle.seat[0].mon.hp = hp0;
      fightOk &= winFight() && gAdv.phase == ADV_CATCH;
      catchOk &= gAdv.caughtN == s + 1 && gAdv.caught[s].dex == foe.dex &&
                 gAdv.caught[s].shiny == foe.shiny && !memcmp(gAdv.caught[s].iv, foe.iv, 4);
      carryOk &= gAdv.battle.seat[0].formed || (gAdv.seat[0].hp == gAdv.battle.seat[0].mon.hp &&
                                                 gAdv.seat[0].hp < gAdv.seat[0].maxHp);
      if (s == 1) {
        const int16_t was = gAdv.seat[0].dex;
        advCatch(false);
        carryOk &= gAdv.seat[0].dex == was;
      } else {
        advCatch(true);
        catchOk &= gAdv.seat[0].dex == foe.dex && gAdv.seat[0].hp == gAdv.seat[0].maxHp;
      }
    }
    ck(fightOk, "each door opens a fight against the foe behind it");
    ck(catchOk, "each win catches exactly that foe; swapping it in brings it at full HP");
    ck(carryOk, "HP carries out of a fight; declining the swap keeps your creature");
    ck(gAdv.phase == ADV_FIGHT && gAdv.stage == ADV_STAGES - 1 && gAdv.battle.boss.final &&
       gAdv.battle.boss.mon.dex == gAdv.boss.dex && gAdv.battle.boss.form == gAdv.boss.form,
       "after the fourth catch the boss fight starts at once");
    {
      Combatant k;
      advBuild(k, gAdv.boss, advLevel(), true);
      ck(gAdv.battle.boss.mon.maxHp == (uint16_t)(k.maxHp * raidRules(RAID_EASY).bossHpMult),
         "the boss has stage 5's HP multiplier, in its form");
    }
    const AdvMon boss = gAdv.boss;
    ck(winFight() && gAdv.phase == ADV_KEEP && gAdv.won && gAdv.caughtN == ADV_STAGES &&
       gAdv.caught[4].dex == boss.dex && gAdv.caught[4].form == boss.form,
       "beating the boss catches it, form and all, and the run is won");
    ck(advStagesWon() == ADV_STAGES, "five stages won");

    const AdvReward keep = advReward(4), none = advReward(ADV_KEEP_NONE);
    ck(keep.coins == 20 && keep.item == IT_GREATBALL && keep.itemN == 1, "an easy win pays 20 coins and a GREAT BALL");
    ck(none.coins == 2 * keep.coins && none.item == keep.item, "keeping nothing pays double the coins");
    ck(advFinish() && gAdv.phase == ADV_IDLE && gAdv.freeDay == DAY + 1, "finishing ends the run and keeps the day");
    ck(!advFinish(), "and cannot be repeated");
  }

  // --- a loss --------------------------------------------------------------
  {
    gAdv.freeDay = 0;
    advStart(RAID_HARD, DAY + 2);
    advPickLoan(0);
    advPickDoor(0);
    winFight();
    advCatch(false);
    advPickDoor(0);
    // one heart left and everyone on 1 HP: the next faint loses
    gAdv.hearts = 1;
    for (int t = 0; t < 60 && gAdv.phase == ADV_FIGHT; t++) {
      for (RaidSeat &s : gAdv.battle.seat) if (s.mon.hp) s.mon.hp = 1;
      RaidTurn out;
      for (uint8_t s = 0; s < MOVE_SLOTS; s++)
        if (advTurn(s, false, out, zero)) break;
    }
    ck(gAdv.phase == ADV_KEEP && !gAdv.won && gAdv.hearts == 0, "spending the last heart ends the run as a loss");
    ck(gAdv.caughtN == 1 && advStagesWon() == 1, "what was caught before is still there to keep");
    const AdvReward w = advReward(0);
    ck(w.coins == 80 / ADV_STAGES && !w.item, "a loss pays a share per stage beaten, and no item");
    advFinish();
  }

  // --- forfeiting --------------------------------------------------------------
  {
    gAdv.freeDay = 0;
    advStart(RAID_EASY, DAY + 3);
    ck(advForfeit() && gAdv.phase == ADV_KEEP && gAdv.caughtN == 0, "a run can be given up at the loan");
    ck(advReward(ADV_KEEP_NONE).coins == 0, "and pays nothing");
    advFinish();
    ck(advStartCost(DAY + 3) == ADV_RUN_PRICE, "giving up still spends the day's free run");
  }

  // --- shinies -------------------------------------------------------------
  {
    uint16_t shinyHard = 0, shinyEasy = 0;
    for (int i = 0; i < 2000; i++) {
      gAdv.freeDay = 0;
      advStart(RAID_HARD, DAY);
      shinyHard += gAdv.boss.shiny;
      endRun();
      gAdv.freeDay = 0;
      advStart(RAID_EASY, DAY);
      shinyEasy += gAdv.boss.shiny;
      endRun();
    }
    printf("      shiny bosses in 2000 runs: hard %u, easy %u\n", shinyHard, shinyEasy);
    ck(shinyHard >= 8 && shinyHard <= 40, "hard bosses are shiny about 1 in 100");
    ck(shinyEasy <= 4, "easy ones at the wild rate");
    gAdv.freeDay = 0;
    advStart(RAID_HARD, DAY, top);
    ck(!gAdv.boss.shiny, "a hard boss is not shiny off the top roll...");
    endRun();
    gAdv.freeDay = 0;
    advStart(RAID_HARD, DAY, zero);
    ck(gAdv.boss.shiny, "...and is off the bottom one");
    bool floor = true;
    for (uint8_t v : gAdv.boss.iv) floor &= v >= 20;
    ck(floor, "a shiny's IVs are floored at 20");
    endRun();
  }

  // --- a legendary boss gets three perfect IVs ------------------------------
  {
    bool perfect = true;
    for (int i = 0; i < 50; i++) {
      gAdv.freeDay = 0;
      advStart(RAID_HARD, DAY);
      uint8_t n = 0;
      for (uint8_t v : gAdv.boss.iv) n += v == 31 ? 1 : 0;
      perfect &= n >= 3;
      endRun();
    }
    ck(perfect, "a box legendary, mythical or fusion boss has three perfect IVs");
  }

  // --- the record: resume exactly where it was --------------------------------
  {
    gAdv.freeDay = 0;
    advStart(RAID_NORMAL, DAY + 4);
    AdvRun was = gAdv;
    reload();
    ck(sameRun(gAdv, was) && gAdv.phase == ADV_LOAN, "a run at the loan survives a restart");
    advPickLoan(2);
    was = gAdv;
    reload();
    ck(sameRun(gAdv, was) && gAdv.phase == ADV_DOOR, "...and at the doors");
    advPickDoor(0);
    // a few turns in, damage done on both sides
    for (int t = 0; t < 2 && gAdv.phase == ADV_FIGHT; t++) {
      RaidTurn out;
      advTurn(0, false, out, zero);
    }
    was = gAdv;
    reload();
    ck(sameRun(gAdv, was) && gAdv.phase == ADV_FIGHT, "...and mid-fight, shields, HP and all");
    ck(gAdv.battle.hearts == &gAdv.hearts, "a reloaded fight spends the run's hearts, not a stale pointer");

    // the same next turn from the reloaded run as from the one before it
    if (gAdv.phase == ADV_FIGHT) {
      AdvRun live = gAdv;
      g_seed = 99;
      RaidTurn a;
      advTurn(0, false, a, zero);
      const AdvRun afterLive = gAdv;
      gAdv = live;
      gAdv.battle.hearts = &gAdv.hearts;
      advSave();
      reload();
      g_seed = 99;
      RaidTurn b;
      advTurn(0, false, b, zero);
      ck(sameRun(gAdv, afterLive) && a.n == b.n, "a resumed fight plays the next turn exactly as it would have");
    }
    winFight();
    was = gAdv;
    reload();
    ck(sameRun(gAdv, was) && gAdv.phase == ADV_CATCH, "...and at the catch");
  }

  // --- a damaged or foreign record --------------------------------------------
  {
    static uint8_t buf[ADV_BLOB_CAP];
    const uint16_t n = encoded(buf, sizeof(buf));
    AdvRun out;
    ck(advDecode(buf, n, out) && sameRun(out, gAdv), "the record decodes to the run it encoded");
    ck(out.battle.hearts == &out.hearts, "a decoded fight spends its own run's hearts");

    uint8_t *b = buf + CKPT_HDR;
    const uint8_t phase = b[0];
    b[0] = ADV_PHASES;
    ck(!advDecode(buf, n, out) && out.freeDay == gAdv.freeDay,
       "an unknown phase is refused, and the free day survives it");
    b[0] = phase;
    const uint8_t mon = b[10];
    b[10] = 7;
    ck(!advDecode(buf, n, out), "a creature entry shorter than this build's is refused");
    b[10] = mon;
    const int16_t dex = (int16_t)ckptRd16(b + 12);
    ckptWr16(b + 12, DEX_COUNT + 1);
    ck(!advDecode(buf, n, out), "a dex past the table is refused");
    ckptWr16(b + 12, (uint16_t)dex);
    ck(!advDecode(buf, n - 40, out), "a truncated record is refused");
    ck(advDecode(buf, n, out), "(and the record put back decodes again)");

    // through the store: a record that fails its checks drops the run but not the day
    const uint32_t day = gAdv.freeDay;
    b[0] = ADV_PHASES;
    ckptSeal(buf, n - CKPT_CRC, ADV_MAGIC, ADV_VERSION, 1000);
    nvs().clear();
    Preferences p;
    p.begin("tamapoke", false);
    p.putBytes("advA", buf, n);
    reload();
    ck(gAdv.phase == ADV_IDLE && gAdv.freeDay == day, "an unreadable run is dropped; today's free run stays taken");
  }

  // --- off the switch, nothing ---------------------------------------------------
  {
    nvs().clear();
    forkSetFeatures(false);
    reload();
    gAdv.freeDay = 0;
    advStart(RAID_EASY, DAY);
    ck(!advSave(), "with the fork's features off nothing is written");
    forkSetFeatures(true);
  }

  printf("%s\n", bad ? "SOME CHECKS FAILED" : "all passed");
  return bad ? 1 : 0;
}
