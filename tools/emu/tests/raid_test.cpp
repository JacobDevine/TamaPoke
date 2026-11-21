// Adventures, step 12b: the four-seat raid battle (fork_adv.h). A form's
// typing and stats in a fight, the boss's barriers and shields, the hearts,
// turn order, and the seats' choosers -- each pinned at its boundary rather
// than by an average, which is why the raid takes its rolls from outside.
//
// Most fights here use FIXED-DAMAGE moves (SEISMIC TOSS hits for the user's
// level, never misses and never crits differently) so a hit lands on an exact
// HP; the boundaries could not be pinned through the damage spread otherwise.
#include "Arduino.h"
#include "Preferences.h"
#include "fork.h"
#include "fork_adv.h"
#include "battle.h"
#include "party.h"
#include "pet.h"
#include "types.h"
#include <cstdio>
#include <cstring>
#include <initializer_list>

#ifndef TAMA_NATIVE_TEST
uint32_t g_seed = 7;
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

static int bad = 0;
static void ck(bool ok, const char *what) {
  printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) bad++;
}

static uint8_t mv(const char *name) {
  for (uint8_t i = 1; i < MOVE_COUNT; i++)
    if (!strcmp(MOVE_TBL[i].name, name)) return i;
  printf("      no move %s\n", name);
  return 0;
}

static uint8_t rowOf(int16_t dex, const char *label) {
  for (uint16_t id = 1; id <= FORM_COUNT; id++)
    if (FORM_TBL[id].dex == dex && !strcmp(FORM_TBL[id].label, label)) return (uint8_t)id;
  return 0;
}

// A roll source the test controls: returns the values queued, then 0.
static uint32_t gRolls[64];
static uint8_t gRollN = 0, gRollAt = 0;
static uint32_t queued(uint32_t) { return gRollAt < gRollN ? gRolls[gRollAt++] : 0; }
static uint32_t counting(uint32_t) { return gRollAt++; }
static uint32_t zeroRoll(uint32_t) { return 0; }
static void rolls(std::initializer_list<uint32_t> v) {
  gRollN = 0; gRollAt = 0;
  for (uint32_t r : v) gRolls[gRollN++] = r;
}

static void moves(Combatant &c, uint8_t a, uint8_t b = 0, uint8_t d = 0, uint8_t e = 0) {
  c.moves[0] = a; c.moves[1] = b; c.moves[2] = d; c.moves[3] = e;
}

// A plain stage foe that cannot act (no moves), so only the seats change it.
static void quietBoss(RaidBattle &b, int16_t dex, uint8_t level, bool final = false) {
  Combatant boss;
  raidMon(boss, dex, level, 15, false);
  moves(boss, 0);
  raidSetBoss(b, boss, 0, final);
}

static RaidTurn turn;

int main() {
  forkSetFeatures(true);
  const uint8_t TOSS = mv("SEISMIC TOSS"), QUICK = mv("QUICK ATTACK"),
                EQ = mv("EARTHQUAKE"), DCLAW = mv("DRAGON CLAW"), GROWL = mv("GROWL"),
                DEDGE = mv("DOUBLE-EDGE"), TACKLE = mv("TACKLE");
  const uint8_t MX = rowOf(6, "MEGA X"), MY = rowOf(6, "MEGA Y"), KB = rowOf(646, "BLACK");
  ck(TOSS && QUICK && EQ && DCLAW && GROWL && DEDGE && TACKLE && MX && MY && KB,
     "the moves and forms this test uses exist");
  ck(forkFormUsable(MX) && !forkFormUsable(MY), "MEGA X has art and MEGA Y has none (the premise below)");

  // --- the stat formula is additive in the base stat --------------------------
  // forkApplyForm() moves each stat by (form base - species base). That is only
  // exact while the formula is base + something independent of the base, so
  // pin it through the real Party accessors over species, levels, IVs, training.
  {
    bool additive = true;
    const int16_t pairs[][2] = { { 6, 1 }, { 150, 129 }, { 646, 25 }, { 384, 10 } };
    for (auto &p : pairs)
      for (uint16_t lvl : { 1, 30, 73, 100 })
        for (uint8_t iv : { 0, 15, 31 })
          for (uint8_t tr : { 0, 40, 100 }) {
            PartyMon a, b;
            a.dex = p[0]; b.dex = p[1];
            a.level = b.level = lvl;
            a.ivAtk = a.ivDef = a.ivSpe = a.ivHp = b.ivAtk = b.ivDef = b.ivSpe = b.ivHp = iv;
            a.trAtk = a.trDef = a.trSpe = b.trAtk = b.trDef = b.trSpe = tr;
            const DexEntry &A = DEX_TBL[p[0]], &B = DEX_TBL[p[1]];
            additive &= party.atkOf(a) - party.atkOf(b) == A.bAtk - B.bAtk;
            additive &= party.defOf(a) - party.defOf(b) == A.bDef - B.bDef;
            additive &= party.spaOf(a) - party.spaOf(b) == A.bSpA - B.bSpA;
            additive &= party.spdOf(a) - party.spdOf(b) == A.bSpD - B.bSpD;
            additive &= party.speOf(a) - party.speOf(b) == A.bSpe - B.bSpe;
            additive &= party.vitOf(a) - party.vitOf(b) == A.bHp - B.bHp;
          }
    ck(additive, "every stat differs between two species by exactly their base difference");
  }

  // --- a form in a fight ------------------------------------------------------
  {
    Combatant zard, mega;
    raidMon(zard, 6, 50, 20, false);
    mega = zard;
    ck(forkApplyForm(mega, MX), "CHARIZARD can become MEGA X");
    const FormEntry &f = FORM_TBL[MX];
    const DexEntry &d = DEX_TBL[6];
    ck(mega.base[SI_ATK] - zard.base[SI_ATK] == f.bAtk - d.bAtk &&
       mega.base[SI_DEF] - zard.base[SI_DEF] == f.bDef - d.bDef &&
       mega.base[SI_SPA] - zard.base[SI_SPA] == f.bSpA - d.bSpA &&
       mega.base[SI_SPD] - zard.base[SI_SPD] == f.bSpD - d.bSpD &&
       mega.base[SI_SPE] - zard.base[SI_SPE] == f.bSpe - d.bSpe &&
       mega.maxHp - zard.maxHp == f.bHp - d.bHp,
       "MEGA X's stats are the species' moved by the form's base difference");
    ck(mega.typed && mega.type1 == T_FIRE && mega.type2 == T_DRAGON, "MEGA X is FIRE/DRAGON");

    // The typing reaches every rule that reads it.
    ck(battleEffVs(T_GROUND, zard) == 0 && battleEffVs(T_GROUND, mega) == 200,
       "EARTHQUAKE: nothing on CHARIZARD (flying), super effective on MEGA X");
    Combatant digger;
    raidMon(digger, 51, 50, 20, false);   // DUGTRIO
    ck(battleDamage(digger, zard, EQ, false, 236) == 0 &&
       battleDamage(digger, mega, EQ, false, 236) > 0,
       "battleDamage() reads the form's typing, not the species'");
    ck(!battleStab(zard, T_DRAGON) && battleStab(mega, T_DRAGON),
       "DRAGON CLAW gets STAB on MEGA X only");
    // The same stats with and without the typing: only STAB tells them apart.
    Combatant plain = mega;
    plain.typed = false;
    const uint16_t withStab = battleDamage(mega, digger, DCLAW, false, 255),
                   without = battleDamage(plain, digger, DCLAW, false, 255);
    ck(without > 10 && withStab * 10 >= without * 14 && withStab * 10 <= without * 16,
       "battleDamage() gives MEGA X's DRAGON CLAW the STAB bonus");
    TurnLog lg;
    Combatant m2 = mega, dg = digger;
    battleAct(dg, m2, EQ, lg);
    ck(lg.effPct == 200 && !lg.immune, "battleAct() logs MEGA X's effectiveness");
    // An untyped combatant behaves exactly as upstream's species lookup.
    bool same = true;
    for (int16_t dex = 1; dex <= DEX_COUNT; dex += 7) {
      Combatant c; c.dex = dex;
      for (uint8_t t = 0; t < TYPE_COUNT; t++)
        same &= battleEffVs(t, c) == typeEffVsDex(t, dex) && battleStab(c, t) == hasStab(dex, t);
    }
    ck(same, "without a form the typing is the species', for every type");

    // Damage taken is kept across the change, and back again.
    Combatant hurt = zard;
    hurt.hp = hurt.maxHp - 10;
    forkApplyForm(hurt, MX);
    ck(hurt.maxHp - hurt.hp == 10, "transforming keeps the damage already taken");
    ck(forkApplyForm(hurt, 0, MX) && hurt.maxHp == zard.maxHp && hurt.hp == zard.maxHp - 10 &&
       !hurt.typed && !memcmp(hurt.base, zard.base, sizeof(zard.base)),
       "and reverting restores the species exactly");

    Combatant c = zard;
    ck(!forkApplyForm(c, MY) && !memcmp(&c, &zard, sizeof(c)), "an art-less form is refused and changes nothing");
    ck(!forkApplyForm(c, KB) && !memcmp(&c, &zard, sizeof(c)), "another species' form is refused");
  }

  // --- the boss: dynamaxed by the rules -----------------------------------------
  {
    ck(raidRules(RAID_EASY).level == 30 && raidRules(RAID_NORMAL).level == 50 &&
       raidRules(RAID_HARD).level == 70, "the three difficulties are levels 30 / 50 / 70");
    ck(!raidRules(RAID_EASY).smartBoss && !raidRules(RAID_NORMAL).smartBoss && raidRules(RAID_HARD).smartBoss,
       "only hard's boss uses the hard-mode AI");
    ck(!raidRules(RAID_EASY).focus && !raidRules(RAID_NORMAL).focus && !raidRules(RAID_HARD).focus,
       "no difficulty's foes hunt the seat they can knock out (12d: hard won 0 runs of 2000 with it)");
    bool ordered = true;
    for (uint8_t d = 0; d < RAID_DIFFS; d++) {
      const RaidRules &r = raidRules(d);
      ordered &= r.barrierPct[0] > 0 && r.barrierPct[0] < 100;
      for (int i = 1; i < RAID_BARRIERS_MAX; i++)
        ordered &= r.barrierPct[i] == 0 || r.barrierPct[i] < r.barrierPct[i - 1];
      ordered &= r.bossHpMult >= r.hpMult && r.bossActions >= r.actions && r.bossShields >= r.shields;
    }
    ck(ordered, "every difficulty has a barrier, descending, and stage 5 is never softer");

    RaidBattle b;
    raidBegin(b, RAID_NORMAL, nullptr);
    Combatant boss;
    raidMon(boss, 143, 50, 15, false);   // SNORLAX
    raidSetBoss(b, boss, 0, false);
    ck(b.boss.mon.maxHp == boss.maxHp * raidRules(RAID_NORMAL).hpMult && b.boss.mon.hp == b.boss.mon.maxHp,
       "a stage foe has its HP multiplied, at full");
    raidSetBoss(b, boss, 0, true);
    ck(b.boss.mon.maxHp == boss.maxHp * raidRules(RAID_NORMAL).bossHpMult &&
       b.boss.actions == raidRules(RAID_NORMAL).bossActions,
       "the stage 5 boss has its own multiplier and actions");
    boss.maxHp = 60000;
    raidSetBoss(b, boss, 0, true);
    ck(b.boss.mon.maxHp == 65535, "a huge boss clamps rather than wrapping");
    ck(raidBarrierHp(1000, 50) == 500 && raidBarrierHp(1, 25) == 1, "a barrier is never at 0 HP");
  }

  // --- barriers: the exact HP that raises the shields ---------------------------
  {
    const uint8_t L = 30;
    for (int off = -1; off <= 1; off++) {
      RaidBattle b;
      raidBegin(b, RAID_EASY, nullptr);
      quietBoss(b, 143, L);
      Combatant s; raidMon(s, 68, L, 15, false); moves(s, TOSS);   // MACHAMP
      raidSeat(b, 0, SEAT_LOCAL, s);
      const uint16_t line = raidBarrierHp(b.boss.mon.maxHp, b.boss.barrierPct[0]);
      b.boss.mon.hp = line + L + off;    // the toss lands at line + off
      raidSetAction(b, 0, 0);
      raidResolve(b, turn, queued);
      char what[96];
      if (off <= 0) {
        snprintf(what, sizeof what, "a hit to %s the barrier stops ON it and raises %u shields",
                 off ? "past" : "exactly", raidRules(RAID_EASY).shields);
        ck(b.boss.mon.hp == line && b.boss.shields == raidRules(RAID_EASY).shields &&
           b.boss.barrier == 1 && turn.ev[0].barrier, what);
      } else {
        ck(b.boss.mon.hp == line + 1 && b.boss.shields == 0 && b.boss.barrier == 0,
           "a hit one HP short of the barrier raises nothing");
      }
    }
    // Nothing carries THROUGH a barrier, not even a knockout.
    RaidBattle b;
    raidBegin(b, RAID_EASY, nullptr);
    quietBoss(b, 143, 100);
    b.boss.mon.maxHp = b.boss.mon.hp = 100;
    Combatant s; raidMon(s, 68, 100, 15, false); moves(s, TOSS);
    raidSeat(b, 0, SEAT_LOCAL, s);
    raidSetAction(b, 0, 0);
    raidResolve(b, turn, queued);
    ck(b.state == RAID_ON && b.boss.mon.hp == 50 && b.boss.shields > 0,
       "a knockout blow stops at the barrier: the boss cannot be KO'd through it");
  }

  // --- shields ------------------------------------------------------------------
  {
    ck(raidShieldBreak(100) == 1 && raidShieldBreak(50) == 1 && raidShieldBreak(200) == 2 &&
       raidShieldBreak(400) == 2, "a super-effective hit breaks two shields, anything else one");

    auto setup = [&](RaidBattle &b, uint8_t shields, uint8_t move, int16_t bossDex) {
      raidBegin(b, RAID_EASY, nullptr);
      quietBoss(b, bossDex, 30);
      b.boss.shields = shields;
      b.boss.barrier = 1;
      Combatant s; raidMon(s, 68, 30, 15, false); moves(s, move);
      raidSeat(b, 0, SEAT_LOCAL, s);
      raidSetAction(b, 0, 0);
    };
    RaidBattle b;
    setup(b, 3, TOSS, 79);   // SLOWPOKE: water/psychic, fighting is resisted
    uint16_t hp = b.boss.mon.hp;
    raidResolve(b, turn, queued);
    ck(b.boss.shields == 2 && b.boss.mon.hp == hp && turn.ev[0].shielded && turn.ev[0].shieldsBroken == 1,
       "a resisted hit on shields breaks one and does no damage");
    setup(b, 3, TOSS, 143);  // SNORLAX: normal, fighting is super effective
    hp = b.boss.mon.hp;
    raidResolve(b, turn, queued);
    ck(b.boss.shields == 1 && b.boss.mon.hp == hp && turn.ev[0].shieldsBroken == 2,
       "a super-effective hit breaks two");
    ck(b.boss.mon.stage[SI_DEF] == 0, "and the boss is not staggered while one stands");
    setup(b, 1, TOSS, 143);
    raidResolve(b, turn, queued);
    ck(b.boss.shields == 0 && turn.ev[0].shieldsBroken == 1 && turn.ev[0].staggered,
       "a super-effective hit on the LAST shield breaks only what stands");
    ck(b.boss.mon.stage[SI_DEF] == RAID_STAGGER && b.boss.mon.stage[SI_SPD] == RAID_STAGGER &&
       b.boss.mon.stage[SI_ATK] == 0, "breaking the last shield staggers DEF and SP.DEF");
    setup(b, 2, GROWL, 143);
    raidResolve(b, turn, queued);
    ck(b.boss.shields == 2 && b.boss.mon.stage[SI_ATK] == 0,
       "a status move breaks no shield and its stage never lands");
    // Shields keep burn chip off the boss; without them it lands.
    for (uint8_t sh : { 2, 0 }) {
      setup(b, sh, GROWL, 143);
      b.boss.mon.ailment = AIL_BURN;
      hp = b.boss.mon.hp;
      raidResolve(b, turn, queued);
      ck(sh ? b.boss.mon.hp == hp : b.boss.mon.hp == hp - b.boss.mon.maxHp / 16,
         sh ? "shields keep the burn's chip off the boss" : "without shields the chip lands");
    }
  }

  // --- hearts -------------------------------------------------------------------
  {
    // A boss whose one move is SEISMIC TOSS, hitting a seat with 1 HP left.
    auto setup = [&](RaidBattle &b, uint8_t *hearts) {
      raidBegin(b, RAID_EASY, hearts);
      Combatant boss; raidMon(boss, 68, 30, 15, false); moves(boss, TOSS);
      raidSetBoss(b, boss, 0, false);
      Combatant s; raidMon(s, 143, 30, 15, false); moves(s, 0);   // cannot act
      raidSeat(b, 0, SEAT_LOCAL, s);
    };
    uint8_t hearts = RAID_HEARTS;
    RaidBattle b;
    setup(b, &hearts);
    bool on = true;
    for (int i = 1; i <= RAID_HEARTS; i++) {
      b.seat[0].mon.hp = 1;
      b.seat[0].mon.ailment = AIL_POISON;
      b.seat[0].mon.stage[SI_ATK] = 3;
      ck(raidReady(b), "a seat with no move to choose is ready");
      raidResolve(b, turn, queued);
      if (i < RAID_HEARTS) {
        on &= b.state == RAID_ON && hearts == RAID_HEARTS - i;
      } else {
        ck(on, "each of the first three faints costs one heart and the fight goes on");
        ck(b.state == RAID_LOST && hearts == 0, "the faint that spends the last heart loses");
        ck(b.seat[0].mon.hp == 0, "and nobody is revived after a loss");
      }
      if (i == 1) {
        ck(b.seat[0].mon.hp == b.seat[0].mon.maxHp * RAID_REVIVE_PCT / 100 && !b.seat[0].down,
           "a fainted seat comes back at the revive share at the end of the turn");
        ck(b.seat[0].mon.ailment == AIL_NONE && b.seat[0].mon.stage[SI_ATK] == 0,
           "with its ailment and its stages cleared");
        ck(turn.n >= 2 && turn.ev[0].heartsLost == 1 && turn.ev[turn.n - 1].revived,
           "the turn narrates the heart and the revival");
      }
    }
    RaidTurn t2;
    ck(raidResolve(b, t2, queued) == RAID_LOST && t2.n == 0, "a lost battle resolves nothing more");

    setup(b, nullptr);
    b.seat[0].mon.hp = 1;
    raidResolve(b, turn, queued);
    ck(b.state == RAID_LOST, "with no hearts at all, one faint loses");

    // Two seats down to one action each turn still cost a heart apiece.
    hearts = RAID_HEARTS;
    setup(b, &hearts);
    b.boss.actions = 2;
    Combatant s2 = b.seat[0].mon;
    raidSeat(b, 1, SEAT_LOCAL, s2);
    b.seat[0].mon.hp = b.seat[1].mon.hp = 1;
    raidResolve(b, turn, queued);
    ck(hearts == RAID_HEARTS - 2 && b.state == RAID_ON, "two faints in one turn cost two hearts");

    // The boss going down wins, even on the action that spent the last heart:
    // DOUBLE-EDGE's recoil knocks the attacker out as it knocks the boss out.
    hearts = 1;
    raidBegin(b, RAID_EASY, &hearts);
    quietBoss(b, 143, 30);
    for (uint8_t &p : b.boss.barrierPct) p = 0;
    b.boss.mon.hp = 1;
    Combatant s; raidMon(s, 143, 30, 15, false); moves(s, DEDGE);
    s.hp = 1;
    raidSeat(b, 0, SEAT_LOCAL, s);
    raidSetAction(b, 0, 0);
    raidResolve(b, turn, queued);
    ck(turn.ev[0].log.targetFainted && turn.ev[0].heartsLost == 1, "(the recoil fainted the attacker)");
    ck(b.state == RAID_WON && hearts == 0, "a boss KO wins even as the last heart is spent");
  }

  // --- turn order ---------------------------------------------------------------
  {
    // A priority move goes before a faster boss; then speed.
    RaidBattle b;
    raidBegin(b, RAID_EASY, nullptr);
    Combatant boss; raidMon(boss, 101, 30, 31, false); moves(boss, TACKLE);   // ELECTRODE, fast
    raidSetBoss(b, boss, 0, false);
    Combatant slow; raidMon(slow, 143, 30, 0, false);
    moves(slow, QUICK, TACKLE);
    raidSeat(b, 0, SEAT_LOCAL, slow);
    raidSeat(b, 1, SEAT_LOCAL, slow);
    raidSetAction(b, 0, 1);   // TACKLE
    raidSetAction(b, 1, 0);   // QUICK ATTACK
    raidResolve(b, turn, queued);
    ck(turn.n >= 3 && turn.ev[0].actor == 1 && turn.ev[1].actor == RAID_BOSS_ACTOR && turn.ev[2].actor == 0,
       "QUICK ATTACK, then the faster boss, then the slow TACKLE");

    // A speed tie goes to the higher roll, so the roll decides it, not the seat number.
    for (int flip = 0; flip < 2; flip++) {
      raidBegin(b, RAID_EASY, nullptr);
      quietBoss(b, 143, 30);
      b.boss.mon.base[SI_SPE] = 1;
      Combatant a; raidMon(a, 143, 30, 15, false); moves(a, TACKLE);
      raidSeat(b, 0, SEAT_LOCAL, a);
      raidSeat(b, 1, SEAT_LOCAL, a);
      raidSetAction(b, 0, 0);
      raidSetAction(b, 1, 0);
      if (flip) rolls({ 10, 5, 0 }); else rolls({ 5, 10, 0 });
      raidResolve(b, turn, queued);
      ck(turn.ev[0].actor == (flip ? 0 : 1), flip ? "a tie: seat 0 rolled higher and went first"
                                                  : "a tie: seat 1 rolled higher and went first");
    }

    // A final boss's extra actions come after everyone has moved.
    raidBegin(b, RAID_EASY, nullptr);
    Combatant fb; raidMon(fb, 101, 30, 31, false); moves(fb, TACKLE);
    raidSetBoss(b, fb, 0, true);
    Combatant tank; raidMon(tank, 143, 30, 31, false); moves(tank, TACKLE);
    for (uint8_t i = 0; i < RAID_SEATS; i++) { raidSeat(b, i, SEAT_LOCAL, tank); raidSetAction(b, i, 0); }
    raidResolve(b, turn, counting);
    uint8_t bossAt[4], nb = 0;
    for (uint8_t i = 0; i < turn.n; i++)
      if (turn.ev[i].actor == RAID_BOSS_ACTOR && !turn.ev[i].endTurn) bossAt[nb++] = i;
    ck(nb == raidRules(RAID_EASY).bossActions && bossAt[0] == 0 && bossAt[nb - 1] == RAID_SEATS + nb - 1,
       "the boss acts first by speed, and its extra action comes after all four seats");
  }

  // --- who the boss hits ----------------------------------------------------------
  {
    RaidBattle b;
    raidBegin(b, RAID_EASY, nullptr);
    Combatant boss; raidMon(boss, 68, 30, 15, false); moves(boss, TOSS);
    raidSetBoss(b, boss, 0, false);
    Combatant s; raidMon(s, 143, 30, 15, false);
    raidSeat(b, 0, SEAT_LOCAL, s);
    raidSeat(b, 2, SEAT_AI, s);
    raidSeat(b, 3, SEAT_AI, s);
    b.seat[2].down = true;
    ck(raidBossTarget(b, false, 0) == 0 && raidBossTarget(b, false, 1) == 3 &&
       raidBossTarget(b, false, 2) == 0, "the roll picks among LIVE seats: empty and downed ones are skipped");
    b.seat[0].mon.hp = 200; b.seat[3].mon.hp = 20;
    ck(raidBossTarget(b, true, 0) == 3, "smart: the seat a hit knocks out");
    b.seat[0].down = b.seat[3].down = true;
    ck(raidBossTarget(b, false, 0) == RAID_SEATS, "nobody live: no target");
  }

  // --- seats, choosers and transforming ------------------------------------------
  {
    RaidBattle b;
    raidBegin(b, RAID_EASY, nullptr);
    quietBoss(b, 143, 30);
    Combatant zard; raidMon(zard, 6, 30, 15, false); moves(zard, DCLAW, TACKLE);
    Combatant kyu; raidMon(kyu, 646, 30, 15, false);
    raidSeat(b, 0, SEAT_LOCAL, zard, MX);
    raidSeat(b, 1, SEAT_AI, zard, MY);
    raidSeat(b, 2, SEAT_AI, kyu, KB);
    ck(b.seat[0].form == MX, "a loan may carry a Mega it can become");
    ck(b.seat[1].form == 0, "but not one with no art");
    ck(b.seat[2].form == 0, "and a fusion is a creature, never a state a seat becomes");

    ck(!raidSetAction(b, 3, 0), "an empty seat takes no action");
    ck(!raidSetAction(b, 0, MOVE_SLOTS) && !raidSetAction(b, 0, 2), "nor a bad or empty move slot");
    ck(!raidSetAction(b, 1, 0, true), "nor a transform the seat cannot make");
    ck(!raidReady(b), "not ready while seats have not chosen");
    raidFillAI(b);
    ck(b.seat[1].pick && b.seat[2].pick && !b.seat[0].pick, "the AI fills its seats and leaves the player's");
    ck(!raidReady(b), "still not ready: the local seat waits for its player");
    ck(raidSetAction(b, 0, 0, true) && raidReady(b), "the player picks, with a transform, and the turn is ready");
    raidResolve(b, turn, queued);
    ck(turn.ev[0].transformed && turn.ev[0].actor == 0 && b.seat[0].formed && b.seat[0].mon.typed &&
       b.seat[0].mon.type2 == T_DRAGON, "the transform happens before anyone acts");
    ck(!b.seat[0].pick && !b.seat[1].pick && !b.seat[0].transform, "the picks are cleared for the next turn");
    ck(!raidSetAction(b, 0, 0, true), "a seat transforms once a battle");

    // An AI seat with a Mega takes it on its own.
    raidBegin(b, RAID_EASY, nullptr);
    quietBoss(b, 143, 30);
    raidSeat(b, 0, SEAT_AI, zard, MX);
    raidFillAI(b);
    ck(b.seat[0].transform, "an AI seat transforms the first time it can");
  }

  // --- 12d. the companions' team rules ---------------------------------------------
  // Each rule is driven through raidFillAI() on a fixture where it and the
  // plain choice disagree, and the other side of it is checked too, so a rule
  // that never engages cannot pass by the plain AI happening to agree.
  {
    const uint8_t WGUN = mv("WATER GUN"), BSLAM = mv("BODY SLAM"), HYDRO = mv("HYDRO PUMP"),
                  THUNDER = mv("THUNDER"), SOLAR = mv("SOLAR BEAM"), RECOVER = mv("RECOVER");
    ck(WGUN && BSLAM && HYDRO && THUNDER && SOLAR && RECOVER, "the 12d moves exist");
    // SANDSLASH is pure GROUND: WATER and GRASS hit it double, ELECTRIC not at all.
    auto fight = [&](RaidBattle &b, uint8_t shields, bool barrierLeft) {
      raidBegin(b, RAID_EASY, nullptr);
      quietBoss(b, 28, 30);
      b.boss.shields = shields;
      if (!barrierLeft) b.boss.barrier = RAID_BARRIERS_MAX;
    };
    auto picked = [&](const RaidBattle &b, uint8_t i) -> uint8_t {
      const RaidSeat &s = b.seat[i];
      return s.pick ? s.mon.moves[s.pick - 1] : 0;
    };
    Combatant lax; raidMon(lax, 143, 30, 15, false);    // SNORLAX: slow, NORMAL (BODY SLAM is STAB)
    Combatant elec; raidMon(elec, 101, 30, 15, false);  // ELECTRODE: fast
    RaidBattle b;

    // Shields: what a hit breaks, not what it would deal.
    Combatant c = lax; moves(c, BSLAM, WGUN);
    fight(b, 2, true);
    ck(battleDamage(c, b.boss.mon, BSLAM, false, 236) > battleDamage(c, b.boss.mon, WGUN, false, 236),
       "(premise: BODY SLAM deals more than WATER GUN)");
    raidSeat(b, 0, SEAT_AI, c); raidFillAI(b);
    ck(picked(b, 0) == WGUN, "shields up: the super-effective hit, which breaks two");
    fight(b, 0, false);
    raidSeat(b, 0, SEAT_AI, c); raidFillAI(b);
    ck(picked(b, 0) == BSLAM, "shields down: the harder hit");

    // The plan: the slower seat knows what the faster one will break.
    Combatant fast = elec; moves(fast, WGUN, BSLAM);
    Combatant slow = lax;  moves(slow, WGUN, BSLAM);
    ck(battleSpeed(fast) > battleSpeed(slow), "(premise: ELECTRODE outruns SNORLAX)");
    fight(b, 2, false);
    raidSeat(b, 0, SEAT_AI, slow); raidSeat(b, 1, SEAT_AI, fast); raidFillAI(b);
    ck(picked(b, 1) == WGUN && picked(b, 0) == BSLAM,
       "the faster seat breaks both shields, so the slower one hits the staggered boss");
    fight(b, 3, false);
    raidSeat(b, 0, SEAT_AI, slow); raidSeat(b, 1, SEAT_AI, fast); raidFillAI(b);
    ck(picked(b, 1) == WGUN && picked(b, 0) == WGUN, "with one shield left after it, the slower one breaks that");
    // The player's pick is part of the plan.
    fight(b, 2, false);
    raidSeat(b, 0, SEAT_AI, slow); raidSeat(b, 1, SEAT_LOCAL, fast);
    raidSetAction(b, 1, 0); raidFillAI(b);
    ck(picked(b, 0) == BSLAM, "the player's own shield break counts");
    fight(b, 2, false);
    raidSeat(b, 0, SEAT_AI, slow); raidSeat(b, 1, SEAT_LOCAL, fast);
    raidSetAction(b, 1, 1); raidFillAI(b);
    ck(picked(b, 0) == WGUN, "and a player hitting for one leaves the other for the AI");

    // A barrier: the hit stops on it, so a sure one beats a big one.
    c = lax; moves(c, HYDRO, WGUN);
    fight(b, 0, true);
    const uint16_t line = raidBarrierHp(b.boss.mon.maxHp, b.boss.barrierPct[0]);
    b.boss.mon.hp = line + 3;
    ck(battleDamage(c, b.boss.mon, WGUN, false, 217) > 3, "(premise: either hit reaches the barrier)");
    raidSeat(b, 0, SEAT_AI, c); raidFillAI(b);
    ck(picked(b, 0) == WGUN, "near a barrier: the move that cannot miss");
    fight(b, 0, false);
    raidSeat(b, 0, SEAT_AI, c); raidFillAI(b);
    ck(picked(b, 0) == HYDRO, "with no barrier ahead: the harder hit, misses and all");

    // Never a move the boss is immune to; never a recoil that would cost a heart.
    c = elec; moves(c, THUNDER, TACKLE);
    fight(b, 0, false);
    raidSeat(b, 0, SEAT_AI, c); raidFillAI(b);
    ck(picked(b, 0) == TACKLE, "no THUNDER on a GROUND");
    c = lax; moves(c, DEDGE, TACKLE);
    fight(b, 0, false);
    raidSeat(b, 0, SEAT_AI, c); raidFillAI(b);
    ck(picked(b, 0) == DEDGE, "at full HP the recoil move is worth it");
    c.hp = 1;
    fight(b, 0, false);
    raidSeat(b, 0, SEAT_AI, c); raidFillAI(b);
    ck(picked(b, 0) == TACKLE, "at 1 HP it would faint to its own recoil: not chosen");

    // A charge move lands next turn: no use against shields, half value without.
    c = lax; moves(c, SOLAR, TACKLE);
    fight(b, 2, false);
    raidSeat(b, 0, SEAT_AI, c); raidFillAI(b);
    ck(picked(b, 0) == TACKLE, "shields up: no wind-up");
    fight(b, 0, false);
    raidSeat(b, 0, SEAT_AI, c); raidFillAI(b);
    ck(picked(b, 0) == SOLAR, "shields down: SOLAR BEAM is still worth two turns");

    // Healing: only when in KO range, and only when it gets out of it.
    Combatant hitter; raidMon(hitter, 28, 30, 15, false); moves(hitter, EQ);
    c = lax; moves(c, TACKLE, RECOVER);
    raidBegin(b, RAID_EASY, nullptr);
    raidSetBoss(b, hitter, 0, false);
    const uint16_t threat = battleDamage(b.boss.mon, c, EQ, false, 236);
    ck(threat > 1 && threat < c.maxHp / 2, "(premise: the boss's EARTHQUAKE takes under half of SNORLAX)");
    c.hp = threat;
    raidSeat(b, 0, SEAT_AI, c); raidFillAI(b);
    ck(picked(b, 0) == RECOVER, "in KO range: it heals");
    c.hp = threat + 1;
    raidSeat(b, 0, SEAT_AI, c); raidFillAI(b);
    ck(picked(b, 0) == TACKLE, "one HP out of range: it attacks");
    c.hp = 1; c.maxHp = 2 * (threat - 1);   // RECOVER brings it to exactly `threat`
    raidSeat(b, 0, SEAT_AI, c); raidFillAI(b);
    ck(picked(b, 0) == TACKLE, "a heal that would still leave it in range: it attacks instead");
  }

  // --- 12d. the boss's AI: smart only at stage 5, aimed by the roll ------------------
  {
    const uint8_t WGUN = mv("WATER GUN"), THUNDER = mv("THUNDER");
    Combatant boss; raidMon(boss, 135, 70, 31, false);   // JOLTEON
    moves(boss, THUNDER, WGUN);
    Combatant ground; raidMon(ground, 28, 70, 31, false);
    moves(ground, TACKLE);
    // Against a GROUND seat THUNDER does nothing, so only a chooser that reads
    // typing always takes WATER GUN; the random one takes THUNDER half the time.
    int immuneFinal = 0, immuneStage = 0, immuneEasy = 0;
    for (int run = 0; run < 40; run++)
      for (int kind = 0; kind < 3; kind++) {   // hard stage 5, hard stage 1, easy stage 5
        const bool final = kind != 1;
        uint8_t hearts = RAID_HEARTS;
        RaidBattle b;
        raidBegin(b, kind == 2 ? RAID_EASY : RAID_HARD, &hearts);
        raidSetBoss(b, boss, 0, final);
        raidSeat(b, 0, SEAT_LOCAL, ground);
        raidSetAction(b, 0, 0);
        raidResolve(b, turn, nullptr);
        for (uint8_t i = 0; i < turn.n; i++)
          if (turn.ev[i].actor == RAID_BOSS_ACTOR && turn.ev[i].log.move == THUNDER)
            (kind == 0 ? immuneFinal : kind == 1 ? immuneStage : immuneEasy)++;
      }
    ck(immuneFinal == 0, "hard's stage 5 boss chooses its moves (never THUNDER on a GROUND)");
    ck(immuneStage > 0, "hard's stage foes do not");
    ck(immuneEasy > 0, "nor does easy's stage 5 boss");

    // Aimed by the roll: a seat the boss could knock out is not singled out.
    uint8_t hearts = RAID_HEARTS;
    RaidBattle b;
    raidBegin(b, RAID_HARD, &hearts);
    Combatant tosser; raidMon(tosser, 68, 70, 15, false); moves(tosser, TOSS);
    raidSetBoss(b, tosser, 0, true);
    Combatant s; raidMon(s, 143, 70, 15, false); moves(s, TACKLE);
    raidSeat(b, 0, SEAT_LOCAL, s); raidSeat(b, 1, SEAT_LOCAL, s);
    b.seat[1].mon.hp = 20;   // a TOSS for 70 knocks seat 1 out
    ck(raidBossTarget(b, true, 0) == 1, "(premise: a focused boss would aim at seat 1)");
    raidSetAction(b, 0, 0); raidSetAction(b, 1, 0);
    raidResolve(b, turn, zeroRoll);
    bool hitZero = false;
    for (uint8_t i = 0; i < turn.n; i++)
      if (turn.ev[i].actor == RAID_BOSS_ACTOR && !turn.ev[i].endTurn) hitZero |= turn.ev[i].target == 0;
    ck(hitZero && hearts == RAID_HEARTS, "roll 0 aims at seat 0, though seat 1 was the knockout");
  }

  // --- whole fights end -------------------------------------------------------------
  // Not a balance claim (12d measures that): every fight must END, win or lose,
  // and a heart can only be spent by a faint.
  {
    const int16_t pool[] = { 3, 6, 9, 25, 59, 65, 68, 94, 130, 143, 149, 248, 257, 260, 282, 445 };
    const int N = sizeof(pool) / sizeof(pool[0]);
    for (uint8_t d = 0; d < RAID_DIFFS; d++) {
      const RaidRules &r = raidRules(d);
      int won = 0, lost = 0, stuck = 0, spent = 0, turns = 0;
      bool heartsOk = true;
      for (int run = 0; run < 60; run++) {
        uint8_t hearts = RAID_HEARTS;
        RaidBattle b;
        raidBegin(b, d, &hearts);
        Combatant boss; raidMon(boss, pool[(run * 7 + d) % N], r.level, 20, false);
        raidSetBoss(b, boss, 0, run % 2);
        for (uint8_t i = 0; i < RAID_SEATS; i++) {
          Combatant s; raidMon(s, pool[(run * 3 + i * 5 + d) % N], r.level, 20, false);
          raidSeat(b, i, SEAT_AI, s);
        }
        int t = 0;
        while (b.state == RAID_ON && t < 200) {
          raidFillAI(b);
          if (!raidReady(b)) break;
          const uint8_t before = hearts;
          raidResolve(b, turn, nullptr);
          uint8_t faints = 0;
          for (uint8_t i = 0; i < turn.n; i++) faints += turn.ev[i].heartsLost;
          heartsOk &= before - hearts == (faints < before ? faints : before);
          t++;
        }
        if (b.state == RAID_WON) won++; else if (b.state == RAID_LOST) lost++; else stuck++;
        spent += RAID_HEARTS - hearts;
        turns += t;
      }
      printf("      %s: four AI seats won %d / lost %d of 60, %.1f hearts, %.1f turns a fight\n",
             d == 0 ? "easy  " : d == 1 ? "normal" : "hard  ", won, lost, spent / 60.0, turns / 60.0);
      char what[64];
      snprintf(what, sizeof what, "difficulty %u: every fight ends", d);
      ck(stuck == 0, what);
      ck(heartsOk, "hearts fall by exactly the faints, never below zero");
    }
  }

  printf("%s (%d failed)\n", bad ? "FAILED" : "all passed", bad);
  return bad ? 1 : 0;
}
