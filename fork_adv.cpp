#include "fork_adv.h"
#include "fork.h"
#include "ckpt.h"
#include "pet.h"
#include "moves.h"
#include <Preferences.h>
#include <string.h>
#include "wild.h"
#include "noart.h"
#include "items.h"

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

uint8_t forkDrawnForm(uint32_t key, int16_t dex) {
  if (!forkFeatures() || !key) return 0;
  const uint8_t f = forkKeptForm(key, dex);
  if (!forkFormUsable(f) || FORM_TBL[f].dex != dex || forkFormIsState(FORM_TBL[f].kind)) return 0;
  return f;
}

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

// --- 12c. the run ---------------------------------------------------------------

AdvRun gAdv;

// The box legendaries: the cover creatures, as against the birds, beasts and
// Regis. A list, because nothing in the data tells them apart (Regigigas has a
// box legendary's stat total). Cosmog and Cosmoem are left out on purpose, and
// so is a lone Calyrex: neither is a boss.
static const int16_t BOX_LEGENDS[] = {
  150, 249, 250, 382, 383, 384, 483, 484, 487, 643, 644, 646,
  716, 717, 718, 791, 792, 800, 888, 889, 890, 1007, 1008,
};

bool advIsBoxLegend(int16_t dex) {
  for (int16_t d : BOX_LEGENDS)
    if (d == dex) return true;
  return false;
}

// Can be drawn and is in an installed region: the rule every pool starts from.
static bool advDrawable(int16_t dex) {
  return dex >= 1 && dex <= DEX_COUNT && speciesHasArt(dex) && regionAvailable(regionOfDex(dex));
}

bool advStageOk(int16_t dex) {
  if (!advDrawable(dex)) return false;
  const DexEntry &d = DEX_TBL[dex];
  return d.evolvesTo == 0 && d.rarity != R_LEGENDARIO && !forkIsMythical(dex);
}

static bool advSubLegend(int16_t dex) {
  return DEX_TBL[dex].rarity == R_LEGENDARIO && DEX_TBL[dex].evolvesTo == 0 &&
         !forkIsMythical(dex) && !advIsBoxLegend(dex);
}

// A Mega goes by WHOSE Mega it is: an easy run on the S24 drew MEGA RAYQUAZA at
// level 30 and lost four hearts to it, which is a hard boss by any reading. So
// easy has ordinary species' Megas, normal adds the sub-legendaries' (LATIAS,
// LATIOS), and the box legendaries' and mythicals' (MEWTWO, RAYQUAZA, DIANCIE)
// are hard's.
bool advBossOk(uint8_t diff, int16_t dex, uint8_t form) {
  if (!advDrawable(dex)) return false;
  if (form && (!forkFormUsable(form) || FORM_TBL[form].dex != dex)) return false;
  const uint8_t kind = form ? FORM_TBL[form].kind : FK_NONE;
  const bool top = advIsBoxLegend(dex) || forkIsMythical(dex);
  const bool legend = top || DEX_TBL[dex].rarity == R_LEGENDARIO;
  switch (diff) {
    case RAID_EASY:
      return kind == FK_MEGA && !legend;
    case RAID_NORMAL:
      return (kind == FK_MEGA && !top) || kind == FK_GMAX || (!form && advSubLegend(dex));
    case RAID_HARD:
      if (form) return kind == FK_FUSION || kind == FK_PRIMAL || kind == FK_ULTRA ||
                       (kind == FK_MEGA && top);
      return top;
    default:
      return false;
  }
}

uint8_t advSeatFormOf(int16_t dex) {
  uint8_t f = 0;
  if (forkFormsOf(dex, FK_MEGA, &f, 1)) return f;
  if (forkFormsOf(dex, FK_GMAX, &f, 1)) return f;
  return 0;
}

static uint32_t advDraw(RaidRoll roll, uint32_t n) {
  if (n <= 1) return 0;
  return roll ? roll(n) % n : (uint32_t)random((long)n);
}

static int16_t advPickStage(RaidRoll roll) {
  uint16_t n = 0;
  for (int16_t d = 1; d <= DEX_COUNT; d++) n += advStageOk(d) ? 1 : 0;
  if (!n) return 0;
  uint32_t k = advDraw(roll, n);
  for (int16_t d = 1; d <= DEX_COUNT; d++)
    if (advStageOk(d) && k-- == 0) return d;
  return 0;
}

// Every (species, form) this difficulty's boss may be: counted when `count` is
// given, otherwise the k-th.
static bool advBossAt(uint8_t diff, uint32_t k, uint32_t *count, int16_t *dex, uint8_t *form) {
  uint32_t n = 0;
  for (int16_t d = 1; d <= DEX_COUNT; d++) {
    if (!advBossOk(diff, d, 0)) continue;
    if (!count && n == k) { *dex = d; *form = 0; return true; }
    n++;
  }
  for (uint16_t id = 1; id <= FORM_COUNT; id++) {
    if (!advBossOk(diff, FORM_TBL[id].dex, (uint8_t)id)) continue;
    if (!count && n == k) { *dex = FORM_TBL[id].dex; *form = (uint8_t)id; return true; }
    n++;
  }
  if (count) *count = n;
  return false;
}

// IVs and shininess for a foe. Hard boosts the shiny odds; a shiny's IVs are
// floored as everywhere else, and a legendary or mythical gets three perfect
// IVs, as one hatched from an egg does.
static void advRollMon(AdvMon &m, uint8_t diff, RaidRoll roll) {
  for (uint8_t &v : m.iv) v = (uint8_t)advDraw(roll, 32);
  m.shiny = diff == RAID_HARD ? advDraw(roll, ADV_SHINY_HARD) == 0
                              : wildShinyForRoll(advDraw(roll, WILD_SHINY_SCALE), 0);
  wildApplyShiny(m.shiny, m.iv[0], m.iv[1], m.iv[2], m.iv[3]);
  // (SOLGALEO and LUNALA evolve from COSMOEM, so the tier alone misses them.)
  if (DEX_TBL[m.dex].rarity == R_LEGENDARIO || forkIsMythical(m.dex) || advIsBoxLegend(m.dex)) {
    uint8_t perfect = 0;
    for (uint8_t i = 0; i < 4; i++) perfect += m.iv[i] == 31 ? 1 : 0;
    for (uint8_t tries = 0; perfect < 3 && tries < 32; tries++) {
      uint8_t &v = m.iv[advDraw(roll, 4)];
      if (v != 31) { v = 31; perfect++; }
    }
  }
}

void advBuild(Combatant &c, const AdvMon &m, uint8_t level, bool applyForm) {
  Pet mon;   // a scratch creature, as raidMon() builds one
  mon.dbgHatchAs(m.dex, m.shiny);
  mon.ivAtk = m.iv[0]; mon.ivDef = m.iv[1]; mon.ivSpe = m.iv[2]; mon.ivHp = m.iv[3];
  mon.ageMinutes = (uint32_t)(level ? level - 1 : 0) * MINUTES_PER_LEVEL;
  mon.relearnFromLevel();
  combatantFromPet(c, mon);
  c.shiny = m.shiny;
  if (applyForm && m.form) forkApplyForm(c, m.form);
}

uint8_t advLevel() { return raidRules(gAdv.diff).level; }

// A loan: a stage-pool species, never shiny, with middling IVs, and the form
// it may become.
static void advRollLoan(AdvMon &m, RaidRoll roll) {
  m = AdvMon();
  m.dex = advPickStage(roll);
  for (uint8_t &v : m.iv) v = (uint8_t)(10 + advDraw(roll, 16));
  m.form = advSeatFormOf(m.dex);
}

static void advSetSeat(AdvRun &r, uint8_t i, const AdvMon &m) {
  r.seatMon[i] = m;
  advBuild(r.seat[i], m, raidRules(r.diff).level, false);
}

// Two doors, preferably of different types.
static void advRollDoors(AdvRun &r, RaidRoll roll) {
  for (uint8_t d = 0; d < ADV_DOORS; d++) {
    AdvMon &m = r.door[d];
    for (uint8_t tries = 0; tries < 16; tries++) {
      m = AdvMon();
      m.dex = advPickStage(roll);
      if (d == 0 || DEX_TBL[m.dex].type1 != DEX_TBL[r.door[0].dex].type1) break;
    }
    advRollMon(m, r.diff, roll);
  }
}

uint32_t advStartCost(uint32_t day) { return gAdv.freeDay == day ? ADV_RUN_PRICE : 0; }

bool advStart(uint8_t diff, uint32_t day, RaidRoll roll) {
  if (gAdv.phase != ADV_IDLE || diff >= RAID_DIFFS) return false;
  uint32_t bosses = 0;
  advBossAt(diff, 0, &bosses, nullptr, nullptr);
  if (!bosses || !advPickStage(nullptr)) return false;
  AdvRun &r = gAdv;
  r = AdvRun();
  r.freeDay = day;   // free or paid, today's free run is now taken
  r.diff = diff;
  r.hearts = RAID_HEARTS;
  int16_t bd = 0;
  uint8_t bf = 0;
  advBossAt(diff, advDraw(roll, bosses), nullptr, &bd, &bf);
  r.boss.dex = bd;
  r.boss.form = bf;
  advRollMon(r.boss, diff, roll);
  for (uint8_t i = 0; i < ADV_LOANS; i++) {
    for (uint8_t tries = 0; tries < 16; tries++) {
      advRollLoan(r.loan[i], roll);
      bool dup = false;
      for (uint8_t j = 0; j < i; j++) dup |= r.loan[j].dex == r.loan[i].dex;
      if (!dup) break;
    }
  }
  for (uint8_t i = 1; i < RAID_SEATS; i++) {   // the companions
    AdvMon m;
    advRollLoan(m, roll);
    advSetSeat(r, i, m);
  }
  r.phase = ADV_LOAN;
  advSave();
  return true;
}

bool advPickLoan(uint8_t i, RaidRoll roll) {
  AdvRun &r = gAdv;
  if (r.phase != ADV_LOAN || i >= ADV_LOANS || !r.loan[i].dex) return false;
  advSetSeat(r, 0, r.loan[i]);
  advRollDoors(r, roll);
  r.phase = ADV_DOOR;
  advSave();
  return true;
}

// Puts every seat and the foe into a fresh battle. A seat that ended the last
// fight fainted comes back at the revive share, as it would have mid-fight.
static void advBeginFight(AdvRun &r, const AdvMon &foe, bool final) {
  raidBegin(r.battle, r.diff, &r.hearts);
  for (uint8_t i = 0; i < RAID_SEATS; i++) {
    Combatant c = r.seat[i];
    if (!c.hp) {
      const uint32_t hp = (uint32_t)c.maxHp * RAID_REVIVE_PCT / 100;
      c.hp = hp ? (uint16_t)hp : 1;
    }
    raidSeat(r.battle, i, i == 0 ? SEAT_LOCAL : SEAT_AI, c, r.seatMon[i].form);
  }
  Combatant k;
  advBuild(k, foe, raidRules(r.diff).level, true);
  raidSetBoss(r.battle, k, foe.form, final);
  r.phase = ADV_FIGHT;
}

bool advPickDoor(uint8_t i) {
  AdvRun &r = gAdv;
  if (r.phase != ADV_DOOR || i >= ADV_DOORS || !r.door[i].dex) return false;
  r.doorPicked = i;
  advBeginFight(r, r.door[i], false);
  advSave();
  return true;
}

// What carries out of a fight: HP and nothing else. A transformed seat keeps
// its HP SHARE, back on its species' own maximum; the transformation itself is
// for one battle, so the next may transform again.
static Combatant advCarry(const RaidSeat &s, const Combatant &before) {
  Combatant c = before;
  const uint16_t mx = s.mon.maxHp ? s.mon.maxHp : 1;
  const uint32_t hp = s.formed ? (uint32_t)before.maxHp * s.mon.hp / mx : s.mon.hp;
  c.hp = hp > before.maxHp ? before.maxHp : (uint16_t)hp;
  if (s.mon.hp && !c.hp) c.hp = 1;
  return c;
}

uint8_t advStagesWon() {
  const AdvRun &r = gAdv;
  if (r.phase == ADV_CATCH) return r.stage + 1;
  if (r.phase == ADV_KEEP) return r.won ? ADV_STAGES : r.caughtN;
  return r.stage;
}

bool advTurn(uint8_t slot, bool transform, RaidTurn &out, RaidRoll roll) {
  AdvRun &r = gAdv;
  out.n = 0;
  if (r.phase != ADV_FIGHT) return false;
  RaidBattle &b = r.battle;
  b.hearts = &r.hearts;   // the record may have been reloaded since it was set
  // A seat whose turn is already decided (recharging, winding up) needs no
  // pick, so an illegal one is only refused when a pick is actually wanted.
  if (!raidSetAction(b, 0, slot, transform)) {
    raidFillAI(b);
    if (!raidReady(b)) return false;
  }
  raidFillAI(b);
  if (!raidReady(b)) return false;
  const uint8_t st = raidResolve(b, out, roll);
  if (st == RAID_WON) {
    for (uint8_t i = 0; i < RAID_SEATS; i++) r.seat[i] = advCarry(b.seat[i], r.seat[i]);
    const bool final = r.stage == ADV_STAGES - 1;
    if (r.caughtN < ADV_STAGES) r.caught[r.caughtN++] = final ? r.boss : r.door[r.doorPicked];
    r.won = final;
    r.phase = final ? ADV_KEEP : ADV_CATCH;
  } else if (st == RAID_LOST) {
    r.won = false;
    r.phase = ADV_KEEP;
  }
  advSave();
  return true;
}

bool advCatch(bool swap, RaidRoll roll) {
  AdvRun &r = gAdv;
  if (r.phase != ADV_CATCH || !r.caughtN) return false;
  if (swap) {
    AdvMon m = r.caught[r.caughtN - 1];
    m.form = advSeatFormOf(m.dex);
    advSetSeat(r, 0, m);   // a fresh creature: full HP
  }
  r.stage++;
  if (r.stage == ADV_STAGES - 1) {
    advBeginFight(r, r.boss, true);
  } else {
    advRollDoors(r, roll);
    r.phase = ADV_DOOR;
  }
  advSave();
  return true;
}

bool advForfeit() {
  AdvRun &r = gAdv;
  if (r.phase == ADV_IDLE || r.phase == ADV_KEEP) return false;
  // Giving up at the LOAN still spends the day's run, or a new one could be
  // rolled until the boss suited.
  r.won = false;
  r.phase = ADV_KEEP;
  advSave();
  return true;
}

static const uint16_t ADV_COINS_WIN[RAID_DIFFS] = { 20, 40, 80 };
static const uint8_t ADV_ITEM_WIN[RAID_DIFFS] = { IT_GREATBALL, IT_ULTRABALL, IT_ULTRABALL };
static const uint8_t ADV_ITEMN_WIN[RAID_DIFFS] = { 1, 1, 2 };

AdvReward advReward(uint8_t keep) {
  const AdvRun &r = gAdv;
  AdvReward w;
  if (r.phase != ADV_KEEP) return w;
  // A win pays it all and an item; a loss a share for each stage beaten.
  w.coins = r.won ? ADV_COINS_WIN[r.diff]
                  : (uint32_t)ADV_COINS_WIN[r.diff] * advStagesWon() / ADV_STAGES;
  if (r.won) { w.item = ADV_ITEM_WIN[r.diff]; w.itemN = ADV_ITEMN_WIN[r.diff]; }
  // Keeping nothing pays double, so declining is a choice rather than a loss.
  if (keep >= r.caughtN) w.coins *= 2;
  return w;
}

bool advFinish() {
  AdvRun &r = gAdv;
  if (r.phase != ADV_KEEP) return false;
  const uint32_t day = r.freeDay;
  r = AdvRun();
  r.freeDay = day;
  advSave();
  return true;
}

// --- the run's record ---
//
// Body, by offset. APPEND-ONLY like every checkpoint (ckpt.h). The two entry
// sizes that may grow (one creature, one combatant) are written in the body, so
// a later build's longer entries are read by their prefix. The counts (3 loans,
// 4 seats, 2 doors, 5 catches) are the game's shape: changing one is a MAGIC
// bump, never a quiet reinterpretation.

#define ADV_MON_BYTES 8
#define ADV_CMB_BYTES 47
#define ADV_HDR 12   // phase diff stage hearts | u32 freeDay | won caughtN monSize cmbSize

static void wrMon(uint8_t *p, const AdvMon &m) {
  ckptWr16(p, (uint16_t)m.dex);
  p[2] = m.form;
  p[3] = m.shiny ? 1 : 0;
  memcpy(p + 4, m.iv, 4);
}
static bool rdMon(const uint8_t *p, AdvMon &m) {
  m.dex = (int16_t)ckptRd16(p);
  m.form = p[2];
  m.shiny = p[3] & 1;
  memcpy(m.iv, p + 4, 4);
  if (m.dex == 0) return m.form == 0;   // an unused slot
  if (m.dex < 1 || m.dex > DEX_COUNT) return false;
  for (uint8_t v : m.iv)
    if (v > 31) return false;
  return !m.form || (forkForm(m.form) && FORM_TBL[m.form].dex == m.dex);
}

static void wrCmb(uint8_t *p, const Combatant &c) {
  ckptWr16(p, (uint16_t)c.dex);
  p[2] = c.level;
  ckptWr16(p + 3, c.maxHp);
  ckptWr16(p + 5, c.hp);
  for (int i = 0; i < SI_COUNT; i++) ckptWr16(p + 7 + 2 * i, c.base[i]);
  memcpy(p + 17, c.moves, MOVE_SLOTS);
  for (int i = 0; i < SI_COUNT; i++) p[21 + i] = (uint8_t)c.stage[i];
  p[26] = c.ailment; p[27] = c.ailTurns; p[28] = c.confuseTurns;
  p[29] = c.recharge ? 1 : 0; p[30] = c.charging; p[31] = c.shiny ? 1 : 0;
  memcpy(p + 32, c.name, 12);
  p[44] = c.typed ? 1 : 0; p[45] = c.type1; p[46] = c.type2;
}

static bool typeOk(uint8_t t) { return t <= T_FAIRY || t == T_NONE; }

// Nothing off a record is trusted to index a table, as link.cpp does.
static bool rdCmb(const uint8_t *p, Combatant &c) {
  c = Combatant();
  c.dex = (int16_t)ckptRd16(p);
  c.level = p[2];
  c.maxHp = ckptRd16(p + 3);
  c.hp = ckptRd16(p + 5);
  for (int i = 0; i < SI_COUNT; i++) c.base[i] = ckptRd16(p + 7 + 2 * i);
  memcpy(c.moves, p + 17, MOVE_SLOTS);
  for (int i = 0; i < SI_COUNT; i++) c.stage[i] = (int8_t)p[21 + i];
  c.ailment = p[26]; c.ailTurns = p[27]; c.confuseTurns = p[28];
  c.recharge = p[29] & 1; c.charging = p[30]; c.shiny = p[31] & 1;
  memcpy(c.name, p + 32, 12);
  c.name[11] = 0;
  c.typed = p[44] & 1; c.type1 = p[45]; c.type2 = p[46];
  if (c.dex == 0) return true;   // an empty seat
  if (c.dex < 1 || c.dex > DEX_COUNT || !c.maxHp || c.hp > c.maxHp) return false;
  if (c.level < 1 || c.level > MAX_LEVEL || c.ailment > AIL_CONFUSE) return false;
  if (c.charging >= MOVE_COUNT) return false;
  for (uint8_t m : c.moves)
    if (m >= MOVE_COUNT) return false;
  for (int8_t s : c.stage)
    if (s < -6 || s > 6) return false;
  return !c.typed || (typeOk(c.type1) && typeOk(c.type2));
}

// Where everything after the header sits, from the two entry sizes.
struct AdvLayout {
  size_t loan, seatMon, seat, boss, door, caught, bat, batBoss, batSeats, seatStride, end;
};
#define ADV_BAT_HDR 14   // doorPicked diff state turn(2) | boss form final actions
                         // shields shieldsMax barrier barrierPct[3]
static AdvLayout advLayout(size_t mon, size_t cmb) {
  AdvLayout L;
  L.loan = ADV_HDR;
  L.seatMon = L.loan + ADV_LOANS * mon;
  L.seat = L.seatMon + RAID_SEATS * mon;
  L.boss = L.seat + RAID_SEATS * cmb;
  L.door = L.boss + mon;
  L.caught = L.door + ADV_DOORS * mon;
  L.bat = L.caught + ADV_STAGES * mon;
  L.batBoss = L.bat + ADV_BAT_HDR;
  L.batSeats = L.batBoss + cmb;
  L.seatStride = 3 + cmb;   // chooser form formed, then the combatant
  L.end = L.batSeats + RAID_SEATS * L.seatStride;
  return L;
}

uint16_t advEncode(const AdvRun &r, uint8_t *buf, size_t cap, uint32_t generation) {
  const AdvLayout L = advLayout(ADV_MON_BYTES, ADV_CMB_BYTES);
  const size_t bodyEnd = CKPT_HDR + L.end;
  if (cap < bodyEnd + CKPT_CRC) return 0;
  memset(buf, 0, bodyEnd + CKPT_CRC);
  uint8_t *b = buf + CKPT_HDR;
  b[0] = r.phase; b[1] = r.diff; b[2] = r.stage; b[3] = r.hearts;
  ckptWr32(b + 4, r.freeDay);
  b[8] = r.won ? 1 : 0; b[9] = r.caughtN; b[10] = ADV_MON_BYTES; b[11] = ADV_CMB_BYTES;
  for (int i = 0; i < ADV_LOANS; i++) wrMon(b + L.loan + i * ADV_MON_BYTES, r.loan[i]);
  for (int i = 0; i < RAID_SEATS; i++) wrMon(b + L.seatMon + i * ADV_MON_BYTES, r.seatMon[i]);
  for (int i = 0; i < RAID_SEATS; i++) wrCmb(b + L.seat + i * ADV_CMB_BYTES, r.seat[i]);
  wrMon(b + L.boss, r.boss);
  for (int i = 0; i < ADV_DOORS; i++) wrMon(b + L.door + i * ADV_MON_BYTES, r.door[i]);
  for (int i = 0; i < ADV_STAGES; i++) wrMon(b + L.caught + i * ADV_MON_BYTES, r.caught[i]);
  const RaidBattle &t = r.battle;
  uint8_t *p = b + L.bat;
  p[0] = r.doorPicked; p[1] = t.diff; p[2] = t.state; ckptWr16(p + 3, t.turn);
  p[5] = t.boss.form; p[6] = t.boss.final ? 1 : 0; p[7] = t.boss.actions;
  p[8] = t.boss.shields; p[9] = t.boss.shieldsMax; p[10] = t.boss.barrier;
  memcpy(p + 11, t.boss.barrierPct, RAID_BARRIERS_MAX);
  wrCmb(b + L.batBoss, t.boss.mon);
  for (int i = 0; i < RAID_SEATS; i++) {
    uint8_t *s = b + L.batSeats + i * L.seatStride;
    s[0] = t.seat[i].chooser; s[1] = t.seat[i].form; s[2] = t.seat[i].formed ? 1 : 0;
    wrCmb(s + 3, t.seat[i].mon);
  }
  return ckptSeal(buf, bodyEnd, ADV_MAGIC, ADV_VERSION, generation);
}

bool advDecode(const uint8_t *buf, uint16_t n, AdvRun &out) {
  out = AdvRun();
  if (n < CKPT_HDR + ADV_HDR + CKPT_CRC) return false;
  const uint8_t *b = buf + CKPT_HDR;
  const size_t body = n - CKPT_HDR - CKPT_CRC;
  // The day comes out first and survives a run this build cannot read, so a
  // bad record never hands out a second free run.
  out.freeDay = ckptRd32(b + 4);
  const size_t mon = b[10], cmb = b[11];
  if (mon < ADV_MON_BYTES || cmb < ADV_CMB_BYTES) return false;
  const AdvLayout L = advLayout(mon, cmb);
  if (L.end > body) return false;
  AdvRun r;
  r.phase = b[0]; r.diff = b[1]; r.stage = b[2]; r.hearts = b[3];
  r.freeDay = out.freeDay;
  r.won = b[8] & 1; r.caughtN = b[9];
  if (r.phase >= ADV_PHASES || r.diff >= RAID_DIFFS || r.stage >= ADV_STAGES ||
      r.hearts > RAID_HEARTS || r.caughtN > ADV_STAGES)
    return false;
  bool ok = true;
  for (int i = 0; i < ADV_LOANS; i++) ok &= rdMon(b + L.loan + i * mon, r.loan[i]);
  for (int i = 0; i < RAID_SEATS; i++) ok &= rdMon(b + L.seatMon + i * mon, r.seatMon[i]);
  for (int i = 0; i < RAID_SEATS; i++) ok &= rdCmb(b + L.seat + i * cmb, r.seat[i]);
  ok &= rdMon(b + L.boss, r.boss);
  for (int i = 0; i < ADV_DOORS; i++) ok &= rdMon(b + L.door + i * mon, r.door[i]);
  for (int i = 0; i < ADV_STAGES; i++) ok &= rdMon(b + L.caught + i * mon, r.caught[i]);
  const uint8_t *p = b + L.bat;
  RaidBattle &t = r.battle;
  r.doorPicked = p[0];
  t.diff = p[1]; t.state = p[2]; t.turn = ckptRd16(p + 3);
  t.boss.form = p[5]; t.boss.final = p[6] & 1; t.boss.actions = p[7];
  t.boss.shields = p[8]; t.boss.shieldsMax = p[9]; t.boss.barrier = p[10];
  memcpy(t.boss.barrierPct, p + 11, RAID_BARRIERS_MAX);
  ok &= r.doorPicked < ADV_DOORS && t.diff < RAID_DIFFS && t.state <= RAID_LOST;
  ok &= t.boss.barrier <= RAID_BARRIERS_MAX && rdCmb(b + L.batBoss, t.boss.mon);
  ok &= !t.boss.form || (forkForm(t.boss.form) && FORM_TBL[t.boss.form].dex == t.boss.mon.dex);
  for (int i = 0; i < RAID_SEATS; i++) {
    const uint8_t *s = b + L.batSeats + i * L.seatStride;
    RaidSeat &st = t.seat[i];
    st.chooser = s[0]; st.form = s[1]; st.formed = s[2] & 1;
    ok &= st.chooser <= SEAT_REMOTE && rdCmb(s + 3, st.mon);
    ok &= !st.form || (forkForm(st.form) && FORM_TBL[st.form].dex == st.mon.dex);
  }
  // A run that needs a creature it does not have is not a run.
  if (r.phase >= ADV_DOOR && r.phase <= ADV_CATCH) ok &= r.seat[0].dex != 0 && r.boss.dex != 0;
  if (r.phase == ADV_FIGHT) ok &= t.boss.mon.dex != 0 && t.state == RAID_ON;
  if (r.phase == ADV_CATCH) ok &= r.caughtN > 0;
  if (!ok) return false;
  out = r;
  out.battle.hearts = &out.hearts;
  return true;
}

static Preferences gAdvPrefs;   // its own handle, never copied
static bool gAdvOpen = false;
static uint32_t gAdvGen = 0;

void advBegin() {
  gAdv = AdvRun();
  gAdvGen = 0;
  if (!forkFeatures()) return;
  if (!gAdvOpen) gAdvOpen = gAdvPrefs.begin("tamapoke", false);
  if (!gAdvOpen) {
    Serial.println("save: NVS would not open for the adventure record");
    return;
  }
  static uint8_t buf[ADV_BLOB_CAP];
  const uint16_t n = ckptReadNewest(gAdvPrefs, "advA", "advB", ADV_MAGIC, buf, sizeof(buf));
  if (!n) return;
  gAdvGen = ckptRd32(buf + 8);
  // A record this build cannot read loses the run, never the day's free run
  // already taken: advDecode() leaves freeDay set either way.
  if (!advDecode(buf, n, gAdv))
    Serial.println("save: the adventure record did not decode; the run is dropped");
  gAdv.battle.hearts = &gAdv.hearts;
}

bool advSave() {
  if (!forkFeatures() || !gAdvOpen) return false;
  static uint8_t buf[ADV_BLOB_CAP];
  const uint32_t next = gAdvGen + 1;
  const uint16_t n = advEncode(gAdv, buf, sizeof(buf), next);
  if (!n) return false;
  // The slot the loaded record did NOT come from (ckpt.h, ckptSlot()).
  const char *key = ckptSlot(next, "advA", "advB");
  if (gAdvPrefs.putBytes(key, buf, n) != n) {
    Serial.printf("save: the adventure record (%s) did not write\n", key);
    return false;
  }
  gAdvGen = next;
  return true;
}
