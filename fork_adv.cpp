#include "fork_adv.h"
#include "fork.h"
#include "ckpt.h"
#include "pet.h"
#include "moves.h"
#include <Preferences.h>
#include <string.h>

// Adventures (android/FEATURES_PLAN.md, Phase 12). See fork_adv.h.

// A form id is a uint8_t wherever it is stored. Outgrowing it must stop the
// build rather than wrap, which would quietly hand a kept creature someone
// else's form (CLAUDE.md trap 2, "uint8_t with a dex of 386").
static_assert(FORM_COUNT <= 255, "form ids are uint8_t: widen them before adding rows");

const FormEntry *forkForm(uint8_t id) {
  if (id == 0 || id > FORM_COUNT) return nullptr;
  return &FORM_TBL[id];
}

bool forkFormUsable(uint8_t id) {
  const FormEntry *f = forkForm(id);
  return f && f->art;
}

uint8_t forkFormsOf(int16_t dex, uint8_t kind, uint8_t *out, uint8_t max) {
  uint8_t n = 0;
  for (uint16_t id = 1; id <= FORM_COUNT; id++) {
    const FormEntry &f = FORM_TBL[id];
    if (f.dex != dex || (kind != FK_NONE && f.kind != kind)) continue;
    if (!forkFormUsable((uint8_t)id)) continue;
    if (out && n < max) out[n] = (uint8_t)id;
    n++;
  }
  return n;
}

bool forkFormIsState(uint8_t kind) {
  return kind == FK_MEGA || kind == FK_PRIMAL || kind == FK_GMAX || kind == FK_ULTRA;
}

bool forkIsMythical(int16_t dex) {
  for (uint8_t i = 0; i < MYTHICAL_COUNT; i++)
    if (MYTHICAL_DEX[i] == dex) return true;
  return false;
}

// --- 12a. kept forms --------------------------------------------------------

static ForkKept gKept[FORK_KEPT_MAX];   // oldest first
static uint8_t gKeptN = 0;
static Preferences gKeptPrefs;          // its own handle, never copied
static bool gKeptOpen = false;
static uint32_t gKeptGen = 0;

uint32_t forkKeyOf(uint8_t ivAtk, uint8_t ivDef, uint8_t ivSpe, uint8_t ivHp, bool shiny) {
  return (uint32_t)(ivAtk & 31) | (uint32_t)(ivDef & 31) << 5 |
         (uint32_t)(ivSpe & 31) << 10 | (uint32_t)(ivHp & 31) << 15 |
         (uint32_t)(shiny ? 1 : 0) << 20 | 0x80000000u;
}

void forkKeptClear() {
  for (auto &k : gKept) k = ForkKept();
  gKeptN = 0;
}

uint16_t forkKeptEncode(uint8_t *buf, size_t cap, uint32_t generation) {
  const size_t bodyEnd = CKPT_HDR + FORK_KEPT_HDR + (size_t)gKeptN * FORK_KEPT_ENTRY;
  if (cap < bodyEnd + CKPT_CRC) return 0;
  memset(buf, 0, bodyEnd + CKPT_CRC);
  uint8_t *b = buf + CKPT_HDR;
  ckptWr16(b, gKeptN);
  b[2] = FORK_KEPT_ENTRY;
  uint8_t *e = b + FORK_KEPT_HDR;
  for (uint8_t i = 0; i < gKeptN; i++, e += FORK_KEPT_ENTRY) {
    ckptWr32(e, gKept[i].key);
    ckptWr16(e + 4, (uint16_t)gKept[i].dex);
    e[6] = gKept[i].form;
  }
  return ckptSeal(buf, bodyEnd, FORK_KEPT_MAGIC, FORK_KEPT_VERSION, generation);
}

bool forkKeptDecode(const uint8_t *buf, uint16_t n) {
  forkKeptClear();
  if (n < CKPT_HDR + FORK_KEPT_HDR + CKPT_CRC) return false;
  const uint8_t *b = buf + CKPT_HDR;
  const size_t body = n - CKPT_HDR - CKPT_CRC;
  const uint16_t count = ckptRd16(b);
  const uint8_t size = b[2];
  // The prefix rule, per entry: a later build may write bigger entries, and
  // this one reads the 7 bytes it knows out of each. A smaller one is not a
  // record any build wrote.
  if (size < 7 || FORK_KEPT_HDR + (size_t)count * size > body) return false;
  // Keep the NEWEST if a later build kept more than this one can hold.
  const uint16_t skip = count > FORK_KEPT_MAX ? count - FORK_KEPT_MAX : 0;
  for (uint16_t i = skip; i < count; i++) {
    const uint8_t *e = b + FORK_KEPT_HDR + (size_t)i * size;
    ForkKept k;
    k.key = ckptRd32(e);
    k.dex = (int16_t)ckptRd16(e + 4);
    k.form = e[6];
    // Nothing off a record is trusted to index a table (as link.cpp does).
    if (!k.key || k.dex < 1 || k.dex > DEX_COUNT || !forkForm(k.form) ||
        FORM_TBL[k.form].dex != k.dex)
      continue;
    gKept[gKeptN++] = k;
  }
  return true;
}

void forkKeptBegin() {
  forkKeptClear();
  gKeptGen = 0;
  if (!forkFeatures()) return;
  if (!gKeptOpen) gKeptOpen = gKeptPrefs.begin("tamapoke", false);
  if (!gKeptOpen) {
    Serial.println("save: NVS would not open for the kept-forms record");
    return;
  }
  static uint8_t buf[FORK_KEPT_BLOB_CAP];
  const uint16_t n = ckptReadNewest(gKeptPrefs, "frmA", "frmB", FORK_KEPT_MAGIC, buf, sizeof(buf));
  if (n && forkKeptDecode(buf, n)) gKeptGen = ckptRd32(buf + 8);
  else forkKeptClear();
}

static bool keptSave() {
  if (!forkFeatures() || !gKeptOpen) return false;
  static uint8_t buf[FORK_KEPT_BLOB_CAP];
  const uint32_t next = gKeptGen + 1;
  const uint16_t n = forkKeptEncode(buf, sizeof(buf), next);
  if (!n) return false;
  // The slot the loaded record did NOT come from (ckpt.h, ckptSlot()).
  const char *key = ckptSlot(next, "frmA", "frmB");
  if (gKeptPrefs.putBytes(key, buf, n) != n) {
    Serial.printf("save: the kept-forms record (%s) did not write\n", key);
    return false;
  }
  gKeptGen = next;
  return true;
}

static int16_t keptFind(uint32_t key, int16_t dex) {
  for (uint8_t i = 0; i < gKeptN; i++)
    if (gKept[i].key == key && gKept[i].dex == dex) return i;
  return -1;
}

bool forkKeepForm(uint32_t key, int16_t dex, uint8_t form) {
  if (!key || !forkFormUsable(form) || FORM_TBL[form].dex != dex) return false;
  ForkKept was[FORK_KEPT_MAX];
  memcpy(was, gKept, sizeof(was));
  const uint8_t wasN = gKeptN;
  const int16_t at = keptFind(key, dex);
  if (at >= 0) {
    gKept[at].form = form;
  } else {
    if (gKeptN == FORK_KEPT_MAX) {   // full: the oldest drops off
      memmove(gKept, gKept + 1, sizeof(ForkKept) * (FORK_KEPT_MAX - 1));
      gKeptN--;
    }
    gKept[gKeptN].key = key;
    gKept[gKeptN].dex = dex;
    gKept[gKeptN].form = form;
    gKeptN++;
  }
  if (keptSave()) return true;
  // Not written: put it back, so memory never claims what the store does not.
  memcpy(gKept, was, sizeof(was));
  gKeptN = wasN;
  return false;
}

uint8_t forkKeptForm(uint32_t key, int16_t dex) {
  const int16_t at = keptFind(key, dex);
  return at < 0 ? 0 : gKept[at].form;
}

uint8_t forkKeptCount() { return gKeptN; }

// --- 12a/12b. a form in a fight ----------------------------------------------

// The six base stats of a species, or of a form when `form` is nonzero, in
// Combatant order (SI_ATK, SI_DEF, SI_SPA, SI_SPD, SI_SPE) with HP last.
static void baseStatsOf(int16_t dex, uint8_t form, int16_t out[SI_COUNT + 1]) {
  if (form) {
    const FormEntry &f = FORM_TBL[form];
    out[SI_ATK] = f.bAtk; out[SI_DEF] = f.bDef; out[SI_SPA] = f.bSpA;
    out[SI_SPD] = f.bSpD; out[SI_SPE] = f.bSpe; out[SI_COUNT] = f.bHp;
  } else {
    const DexEntry &d = DEX_TBL[dex];
    out[SI_ATK] = d.bAtk; out[SI_DEF] = d.bDef; out[SI_SPA] = d.bSpA;
    out[SI_SPD] = d.bSpD; out[SI_SPE] = d.bSpe; out[SI_COUNT] = d.bHp;
  }
}

static uint16_t shifted(uint16_t v, int16_t delta) {
  const int32_t r = (int32_t)v + delta;
  return r < 1 ? 1 : (r > 65535 ? 65535 : (uint16_t)r);
}

bool forkApplyForm(Combatant &c, uint8_t form, uint8_t fromForm) {
  if (c.dex < 1 || c.dex > DEX_COUNT) return false;
  if (form && (!forkFormUsable(form) || FORM_TBL[form].dex != c.dex)) return false;
  if (fromForm && (!forkForm(fromForm) || FORM_TBL[fromForm].dex != c.dex)) return false;
  int16_t was[SI_COUNT + 1], now[SI_COUNT + 1];
  baseStatsOf(c.dex, fromForm, was);
  baseStatsOf(c.dex, form, now);
  for (int i = 0; i < SI_COUNT; i++) c.base[i] = shifted(c.base[i], now[i] - was[i]);
  // HP: the same delta on the maximum, and the damage already taken kept.
  const uint16_t lost = c.maxHp - c.hp;
  const bool out = c.hp == 0;
  c.maxHp = shifted(c.maxHp, now[SI_COUNT] - was[SI_COUNT]);
  c.hp = out ? 0 : (c.maxHp > lost ? c.maxHp - lost : 1);
  c.typed = form != 0;
  c.type1 = form ? FORM_TBL[form].type1 : 0;
  c.type2 = form ? FORM_TBL[form].type2 : 0;
  return true;
}

// --- 12b. the raid battle -----------------------------------------------------

static const RaidRules RAID_RULES[RAID_DIFFS] = {
  //  lvl hp boss act bAct sh bSh  barriers      smart
  {   30,  3,  5,  1,  2,  2,  3, { 50,  0,  0 }, false },   // easy
  {   50,  4,  6,  1,  2,  3,  4, { 66, 33,  0 }, false },   // normal
  {   70,  4,  7,  1,  2,  3,  5, { 75, 50, 25 }, true  },   // hard
};

const RaidRules &raidRules(uint8_t diff) {
  return RAID_RULES[diff < RAID_DIFFS ? diff : RAID_EASY];
}

void raidMon(Combatant &c, int16_t dex, uint8_t level, uint8_t iv, bool shiny,
             uint8_t form) {
  Pet mon;   // a scratch creature, as the .ino's foeFromSpecies() builds one
  mon.dbgHatchAs(dex, shiny);
  mon.ivAtk = mon.ivDef = mon.ivSpe = mon.ivHp = iv;
  mon.ageMinutes = (uint32_t)(level ? level - 1 : 0) * MINUTES_PER_LEVEL;
  mon.relearnFromLevel();
  combatantFromPet(c, mon);
  c.shiny = shiny;
  if (form) forkApplyForm(c, form);
}

void raidBegin(RaidBattle &b, uint8_t diff, uint8_t *hearts) {
  b = RaidBattle();
  b.diff = diff < RAID_DIFFS ? diff : RAID_EASY;
  b.hearts = hearts;
}

// A seat may only BECOME a state form (Mega, G-Max...): a fusion is a creature
// of its own and arrives already in it.
static bool seatFormOk(const Combatant &c, uint8_t form) {
  return form && forkFormUsable(form) && FORM_TBL[form].dex == c.dex &&
         forkFormIsState(FORM_TBL[form].kind);
}

void raidSeat(RaidBattle &b, uint8_t i, uint8_t chooser, const Combatant &mon,
              uint8_t form) {
  if (i >= RAID_SEATS) return;
  RaidSeat &s = b.seat[i];
  s = RaidSeat();
  s.chooser = chooser;
  s.mon = mon;
  s.form = seatFormOk(mon, form) ? form : 0;
}

void raidSetBoss(RaidBattle &b, const Combatant &mon, uint8_t form, bool final) {
  const RaidRules &r = raidRules(b.diff);
  RaidBoss &k = b.boss;
  k = RaidBoss();
  k.mon = mon;
  k.form = form;
  k.final = final;
  const uint32_t hp = (uint32_t)mon.maxHp * (final ? r.bossHpMult : r.hpMult);
  k.mon.maxHp = hp > 65535 ? 65535 : (hp ? (uint16_t)hp : 1);
  k.mon.hp = k.mon.maxHp;
  k.actions = final ? r.bossActions : r.actions;
  k.shieldsMax = final ? r.bossShields : r.shields;
  for (int i = 0; i < RAID_BARRIERS_MAX; i++) k.barrierPct[i] = r.barrierPct[i];
}

uint16_t raidBarrierHp(uint16_t maxHp, uint8_t pct) {
  const uint32_t v = (uint32_t)maxHp * pct / 100;
  return v ? (uint16_t)v : 1;
}

uint8_t raidShieldBreak(uint16_t effPct) { return effPct > 100 ? 2 : 1; }

static bool seatLive(const RaidSeat &s) {
  return s.occupied() && !s.down && !s.mon.fainted();
}

uint8_t raidBossTarget(const RaidBattle &b, bool smart, uint32_t roll) {
  uint8_t live[RAID_SEATS], n = 0;
  for (uint8_t i = 0; i < RAID_SEATS; i++)
    if (seatLive(b.seat[i])) live[n++] = i;
  if (!n) return RAID_SEATS;
  if (!smart) return live[roll % n];
  uint8_t best = live[0];
  uint32_t bestScore = 0;
  for (uint8_t k = 0; k < n; k++) {
    const Combatant &t = b.seat[live[k]].mon;
    const uint8_t mv = aiChooseMove(b.boss.mon, t, true);
    const uint16_t dmg = battleDamage(b.boss.mon, t, mv, false, 236);
    // A share of what it has left, so a KO (>= 1000) beats any chip.
    const uint32_t score = (uint32_t)dmg * 1000 / (t.hp ? t.hp : 1);
    if (score > bestScore) { bestScore = score; best = live[k]; }
  }
  return best;
}

static bool hasLegalMove(const Combatant &c) {
  for (int i = 0; i < MOVE_SLOTS; i++)
    if (c.moves[i] && c.moves[i] < MOVE_COUNT) return true;
  return false;
}

bool raidSetAction(RaidBattle &b, uint8_t i, uint8_t slot, bool transform) {
  if (b.state != RAID_ON || i >= RAID_SEATS || slot >= MOVE_SLOTS) return false;
  RaidSeat &s = b.seat[i];
  if (!seatLive(s)) return false;
  const uint8_t mv = s.mon.moves[slot];
  if (!mv || mv >= MOVE_COUNT) return false;
  if (transform && (s.formed || !seatFormOk(s.mon, s.form))) return false;
  s.pick = slot + 1;
  s.transform = transform;
  return true;
}

void raidFillAI(RaidBattle &b) {
  for (uint8_t i = 0; i < RAID_SEATS; i++) {
    RaidSeat &s = b.seat[i];
    if (s.chooser != SEAT_AI || !seatLive(s) || s.pick) continue;
    const uint8_t mv = aiChooseMove(s.mon, b.boss.mon, true);
    for (uint8_t k = 0; k < MOVE_SLOTS; k++)
      if (mv && s.mon.moves[k] == mv) {
        raidSetAction(b, i, k, !s.formed && seatFormOk(s.mon, s.form));
        break;
      }
  }
}

// A seat whose turn is already decided needs no pick: no move to choose from,
// recharging, or a charge move winding up.
static bool seatDecided(const RaidSeat &s) {
  return s.pick || !hasLegalMove(s.mon) || s.mon.recharge || s.mon.charging;
}

bool raidReady(const RaidBattle &b) {
  if (b.state != RAID_ON) return false;
  for (uint8_t i = 0; i < RAID_SEATS; i++)
    if (seatLive(b.seat[i]) && !seatDecided(b.seat[i])) return false;
  return true;
}

// --- one turn ---

namespace {
struct RaidCtx {
  RaidBattle &b;
  RaidTurn &out;
  RaidRoll roll;
  uint32_t draw(uint32_t n) {
    if (n <= 1) return 0;
    return roll ? roll(n) % n : (uint32_t)random((long)n);
  }
  RaidEvent &next(uint8_t actor, uint8_t target) {
    static RaidEvent spill;   // past the cap: still applied, just not narrated
    RaidEvent &e = out.n < RAID_EVENTS_MAX ? out.ev[out.n++] : spill;
    e = RaidEvent();
    e.actor = actor;
    e.target = target;
    return e;
  }
};
}  // namespace

// After anything that may have lowered the boss's HP: a crossed barrier stops
// it there and raises the shields. One per hit, since the hit stops at it.
static void settleBoss(RaidBoss &k, uint16_t hpBefore, RaidEvent &e) {
  if (k.barrier >= RAID_BARRIERS_MAX || !k.barrierPct[k.barrier]) return;
  const uint16_t line = raidBarrierHp(k.mon.maxHp, k.barrierPct[k.barrier]);
  if (hpBefore <= line || k.mon.hp > line) return;
  k.mon.hp = line;
  k.shields = k.shieldsMax;
  k.barrier++;
  e.barrier = true;
  e.log.targetFainted = false;
}

// Every seat that has just gone down costs a heart. Then the result: the boss
// out wins, even on the action that spent the last heart.
static void settleSeats(RaidCtx &x, RaidEvent &e) {
  RaidBattle &b = x.b;
  for (uint8_t i = 0; i < RAID_SEATS; i++) {
    RaidSeat &s = b.seat[i];
    if (!s.occupied() || s.down || !s.mon.fainted()) continue;
    s.down = true;
    e.heartsLost++;
    if (b.hearts && *b.hearts) (*b.hearts)--;
  }
  if (b.boss.mon.fainted()) b.state = RAID_WON;
  else if (e.heartsLost && (!b.hearts || !*b.hearts)) b.state = RAID_LOST;
}

static void seatAttacks(RaidCtx &x, uint8_t i, uint8_t mv) {
  RaidBattle &b = x.b;
  RaidBoss &k = b.boss;
  RaidEvent &e = x.next(i, RAID_BOSS_ACTOR);
  const Combatant before = k.mon;
  battleAct(b.seat[i].mon, k.mon, mv, e.log);
  const TurnLog &l = e.log;
  const bool landed = !l.missed && !l.immune && !l.skipped && !l.hurtSelf &&
                      !l.charged && l.move && l.move < MOVE_COUNT &&
                      MOVE_TBL[l.move].cat != MC_STATUS;
  if (k.shields) {
    // Nothing reaches the boss through a shield: not damage, an ailment or a
    // stage. Its own state (a recharge, a wind-up) is its, so that stays.
    k.mon.hp = before.hp;
    k.mon.ailment = before.ailment;
    k.mon.ailTurns = before.ailTurns;
    k.mon.confuseTurns = before.confuseTurns;
    for (int s = 0; s < SI_COUNT; s++) k.mon.stage[s] = before.stage[s];
    e.log.targetFainted = false;
    e.log.inflicted = AIL_NONE;
    if (landed) {
      uint8_t n = raidShieldBreak(l.effPct);
      if (n > k.shields) n = k.shields;
      k.shields -= n;
      e.shielded = true;
      e.shieldsBroken = n;
      if (!k.shields) {
        e.staggered = true;
        static const uint8_t DROP[2] = { SI_DEF, SI_SPD };
        for (uint8_t s : DROP) {
          const int v = k.mon.stage[s] + RAID_STAGGER;
          k.mon.stage[s] = v < -6 ? -6 : (int8_t)v;
        }
      }
    }
  } else {
    settleBoss(k, before.hp, e);
  }
  settleSeats(x, e);
}

static void bossActs(RaidCtx &x) {
  RaidBattle &b = x.b;
  RaidBoss &k = b.boss;
  if (k.mon.fainted()) return;
  uint8_t live = 0;
  for (const RaidSeat &s : b.seat) live += seatLive(s) ? 1 : 0;
  if (!live) return;   // everyone down this turn: nobody to hit
  const bool smart = raidRules(b.diff).smart;
  const uint8_t t = raidBossTarget(b, smart, x.draw(live));
  RaidEvent &e = x.next(RAID_BOSS_ACTOR, t);
  const uint8_t mv = aiChooseMove(k.mon, b.seat[t].mon, smart);
  const uint16_t hpBefore = k.mon.hp;
  battleAct(k.mon, b.seat[t].mon, mv, e.log);
  settleBoss(k, hpBefore, e);   // recoil or confusion may cross a barrier
  settleSeats(x, e);
}

uint8_t raidResolve(RaidBattle &b, RaidTurn &out, RaidRoll roll) {
  out.n = 0;
  if (b.state != RAID_ON) return b.state;
  RaidCtx x{ b, out, roll };
  b.turn++;

  // 1. Transformations, before anyone acts (as a Mega does in the games).
  for (uint8_t i = 0; i < RAID_SEATS; i++) {
    RaidSeat &s = b.seat[i];
    if (!seatLive(s) || !s.transform) continue;
    if (!s.formed && forkApplyForm(s.mon, s.form)) {
      s.formed = true;
      x.next(i, i).transformed = true;
    }
  }

  // 2. Order: every live seat and the boss's first action, by move priority
  // then staged speed, ties by roll. The boss's move is chosen when it acts
  // (its target may be down by then), so it orders at priority 0.
  struct Act { uint8_t who, mv; int8_t pri; uint16_t spe; uint32_t tie; };
  Act acts[RAID_SEATS + 1];
  uint8_t n = 0;
  for (uint8_t i = 0; i < RAID_SEATS; i++) {
    const RaidSeat &s = b.seat[i];
    if (!seatLive(s)) continue;
    const uint8_t mv = s.pick ? s.mon.moves[s.pick - 1] : 0;
    acts[n++] = { i, mv, battlePriority(mv), battleSpeed(s.mon), x.draw(65536) };
  }
  acts[n++] = { (uint8_t)RAID_BOSS_ACTOR, 0, 0, battleSpeed(b.boss.mon), x.draw(65536) };
  for (uint8_t i = 1; i < n; i++)   // insertion sort, n <= 5
    for (uint8_t j = i; j > 0; j--) {
      const Act &a = acts[j - 1], &c = acts[j];
      const bool swap = c.pri != a.pri ? c.pri > a.pri
                      : c.spe != a.spe ? c.spe > a.spe : c.tie > a.tie;
      if (!swap) break;
      const Act t = acts[j]; acts[j] = acts[j - 1]; acts[j - 1] = t;
    }

  // 3. Everyone acts; the boss's extra actions come after.
  for (uint8_t i = 0; i < n && b.state == RAID_ON; i++) {
    if (acts[i].who == RAID_BOSS_ACTOR) bossActs(x);
    else if (seatLive(b.seat[acts[i].who])) seatAttacks(x, acts[i].who, acts[i].mv);
  }
  for (uint8_t a = 1; a < b.boss.actions && b.state == RAID_ON; a++) bossActs(x);

  // 4. Burn and poison. Shields keep the chip off the boss too.
  for (uint8_t i = 0; i < RAID_SEATS && b.state == RAID_ON; i++) {
    RaidSeat &s = b.seat[i];
    if (!seatLive(s) || (s.mon.ailment != AIL_BURN && s.mon.ailment != AIL_POISON)) continue;
    RaidEvent &e = x.next(i, i);
    e.endTurn = true;
    battleEndTurn(s.mon, e.log);
    settleSeats(x, e);
  }
  RaidBoss &k = b.boss;
  if (b.state == RAID_ON && !k.shields &&
      (k.mon.ailment == AIL_BURN || k.mon.ailment == AIL_POISON)) {
    RaidEvent &e = x.next(RAID_BOSS_ACTOR, RAID_BOSS_ACTOR);
    e.endTurn = true;
    const uint16_t hpBefore = k.mon.hp;
    battleEndTurn(k.mon, e.log);
    settleBoss(k, hpBefore, e);
    settleSeats(x, e);
  }

  // 5. The fallen come back, for the heart they cost.
  for (uint8_t i = 0; i < RAID_SEATS; i++) {
    RaidSeat &s = b.seat[i];
    s.pick = 0;
    s.transform = false;
    if (b.state != RAID_ON || !s.down) continue;
    Combatant &m = s.mon;
    const uint32_t hp = (uint32_t)m.maxHp * RAID_REVIVE_PCT / 100;
    m.hp = hp ? (uint16_t)hp : 1;
    m.ailment = AIL_NONE;
    m.ailTurns = m.confuseTurns = m.charging = 0;
    m.recharge = false;
    for (int st = 0; st < SI_COUNT; st++) m.stage[st] = 0;
    s.down = false;
    x.next(i, i).revived = true;
  }
  return b.state;
}
