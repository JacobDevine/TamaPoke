#include "fork.h"
#include "ckpt.h"
#include "fork_i18n.h"
#include "party.h"
#include "pet.h"
#include "evo_rules.h"
#include "battle.h"
#include "inventory.h"
#include "wild.h"
#include <string.h>
#include <math.h>

// See fork.h for why any of this exists.

static bool gForkOn = false;

bool forkFeatures() { return gForkOn; }
void forkSetFeatures(bool on) { gForkOn = on; }

Fork forkData;
ForkHost gForkHost;

uint8_t forkTakeSteps(ForkStepBucket *out, uint8_t max) {
  if (!gForkHost.takeSteps || !out || !max) return 0;
  const uint8_t n = gForkHost.takeSteps(out, max);
  return n < max ? n : max;   // never trust the host past the buffer
}
uint8_t forkStepsStatus() {
  if (!gForkHost.stepsStatus) return FORK_STEPS_NONE;
  const uint8_t s = gForkHost.stepsStatus();
  return s <= FORK_STEPS_ON ? s : FORK_STEPS_NONE;
}
void forkAskSteps(bool firstTimeOnly) {
  if (gForkHost.askSteps) gForkHost.askSteps(firstTimeOnly);
}

// --- the record --------------------------------------------------------------

uint16_t Fork::encode(const ForkState &s, uint8_t *buf, size_t cap,
                      uint32_t generation) {
  const size_t bodyEnd = CKPT_HDR + FORK_BODY_LEN;
  if (cap < bodyEnd + CKPT_CRC) return 0;
  memset(buf, 0, bodyEnd + CKPT_CRC);
  uint8_t *b = buf + CKPT_HDR;
  ckptWr32(b + FORK_OFF_CREATED, s.createdEpoch);
  ckptWr32(b + FORK_OFF_SICK_KEY, s.sickKey);
  b[FORK_OFF_SICK_MIN] = (uint8_t)s.sickMinutes;
  b[FORK_OFF_SICK_MIN + 1] = (uint8_t)(s.sickMinutes >> 8);
  b[FORK_OFF_MEDICINE] = s.medicine;
  b[FORK_OFF_FLAGS] = s.flags;
  ckptWr32(b + FORK_OFF_COINS, s.coins);
  ckptWr32(b + FORK_OFF_STEP_DAY, s.stepDay);
  ckptWr32(b + FORK_OFF_STEP_TODAY, s.stepsToday);
  ckptWr32(b + FORK_OFF_STEP_TOTAL, s.stepsTotal);
  b[FORK_OFF_STEP_MILES] = s.stepMiles;
  b[FORK_OFF_STEP_JOY] = s.stepJoy;
  b[FORK_OFF_STEP_BOND] = s.stepBond;
  b[FORK_OFF_STEP_RANK] = s.stepRank;
  ckptWr32(b + FORK_OFF_EXP_KEY, s.expKey);
  ckptWr32(b + FORK_OFF_EXP_END, s.expEnd);
  ckptWr32(b + FORK_OFF_EXP_STEPS, s.expSteps);
  b[FORK_OFF_EXP_SITE] = s.expSite;
  b[FORK_OFF_EXP_LEN] = s.expLen;
  ckptWr32(b + FORK_OFF_TOUCH_DAY, s.touchDay);
  b[FORK_OFF_TOUCH_JOY] = s.touchJoy;
  return ckptSeal(buf, bodyEnd, FORK_MAGIC, FORK_VERSION, generation);
}

bool Fork::decode(const uint8_t *buf, uint16_t n, ForkState &out) {
  if (n < CKPT_HDR + CKPT_CRC) return false;
  // The prefix rule: copy what the stored body has, up to what this build
  // knows, into a zeroed body. Fields it did not have read back as zero here
  // and are then given their initialisers below rather than trusting zero.
  uint8_t body[FORK_BODY_LEN] = { 0 };
  const size_t stored = n - CKPT_HDR - CKPT_CRC;
  memcpy(body, buf + CKPT_HDR, stored < sizeof(body) ? stored : sizeof(body));
  ForkState s;
  if (stored >= FORK_OFF_CREATED + 4) s.createdEpoch = ckptRd32(body + FORK_OFF_CREATED);
  if (stored >= FORK_OFF_SICK_KEY + 4) s.sickKey = ckptRd32(body + FORK_OFF_SICK_KEY);
  if (stored >= FORK_OFF_SICK_MIN + 2)
    s.sickMinutes = (uint16_t)(body[FORK_OFF_SICK_MIN] | body[FORK_OFF_SICK_MIN + 1] << 8);
  if (stored >= FORK_OFF_MEDICINE + 1) s.medicine = body[FORK_OFF_MEDICINE];
  if (stored >= FORK_OFF_FLAGS + 1) s.flags = body[FORK_OFF_FLAGS];
  if (stored >= FORK_OFF_COINS + 4) s.coins = ckptRd32(body + FORK_OFF_COINS);
  if (stored >= FORK_OFF_STEP_DAY + 4) s.stepDay = ckptRd32(body + FORK_OFF_STEP_DAY);
  if (stored >= FORK_OFF_STEP_TODAY + 4) s.stepsToday = ckptRd32(body + FORK_OFF_STEP_TODAY);
  if (stored >= FORK_OFF_STEP_TOTAL + 4) s.stepsTotal = ckptRd32(body + FORK_OFF_STEP_TOTAL);
  if (stored >= FORK_OFF_STEP_MILES + 1) s.stepMiles = body[FORK_OFF_STEP_MILES];
  if (stored >= FORK_OFF_STEP_JOY + 1) s.stepJoy = body[FORK_OFF_STEP_JOY];
  if (stored >= FORK_OFF_STEP_BOND + 1) s.stepBond = body[FORK_OFF_STEP_BOND];
  if (stored >= FORK_OFF_STEP_RANK + 1) s.stepRank = body[FORK_OFF_STEP_RANK];
  // An expedition is whole or absent: a body cut inside it reads as no trip
  // rather than a trip with no end time.
  if (stored >= FORK_OFF_EXP_LEN + 1) {
    s.expKey = ckptRd32(body + FORK_OFF_EXP_KEY);
    s.expEnd = ckptRd32(body + FORK_OFF_EXP_END);
    s.expSteps = ckptRd32(body + FORK_OFF_EXP_STEPS);
    s.expSite = body[FORK_OFF_EXP_SITE];
    s.expLen = body[FORK_OFF_EXP_LEN];
    // a later build's site or length this one does not know: no trip
    if (s.expSite >= FORK_EXP_SITES || s.expLen >= FORK_EXP_LENS) s.expKey = 0;
  }
  // The touch cap is whole or absent too: a day with no count would be a day
  // whose joy is already spent, or one that never started.
  if (stored >= FORK_OFF_TOUCH_JOY + 1) {
    s.touchDay = ckptRd32(body + FORK_OFF_TOUCH_DAY);
    s.touchJoy = body[FORK_OFF_TOUCH_JOY];
  }
  out = s;
  return true;
}

void Fork::begin() {
  st = ForkState();
  loadedOk = false;
  dirty = false;
  gen = 0;
  opened = false;
  // The board does not even open a handle: with the switch off this file must
  // cost it nothing beyond the code size.
  if (!forkFeatures()) return;
  opened = prefs.begin("tamapoke", false);
  if (!opened) {
    Serial.println("save: NVS would not open for the fork record");
    return;
  }
  static uint8_t buf[FORK_BLOB_CAP];
  uint16_t n = ckptReadNewest(prefs, "frkA", "frkB", FORK_MAGIC, buf, sizeof(buf));
  if (n && decode(buf, n, st)) {
    gen = ckptRd32(buf + 8);
    loadedOk = true;
  } else {
    st = ForkState();
  }
  // The starting medicine, once per player: a flag rather than "the record was
  // missing", so a phone save from before Phase 3 gets it too, and never twice.
  if (!(st.flags & FORK_FLAG_MED_GIVEN)) {
    st.medicine = (uint8_t)min(255, st.medicine + FORK_MED_START);
    st.flags |= FORK_FLAG_MED_GIVEN;
    dirty = true;
  }
}

bool Fork::save(uint32_t nowEpoch) {
  if (!forkFeatures() || !opened) return false;
  if (!st.createdEpoch) st.createdEpoch = nowEpoch ? nowEpoch : 1;
  static uint8_t buf[FORK_BLOB_CAP];
  const uint32_t next = gen + 1;
  uint16_t n = encode(st, buf, sizeof(buf), next);
  if (!n) return false;
  // Into the slot the loaded record did NOT come from, so the copy we would
  // fall back to is never the one being overwritten (ckpt.h, ckptSlot()).
  const char *key = ckptSlot(next, "frkA", "frkB");
  if (prefs.putBytes(key, buf, n) != n) {
    Serial.printf("save: the fork record (%s) did not write\n", key);
    return false;
  }
  gen = next;
  loadedOk = true;
  dirty = false;
  return true;
}

// --- natures -------------------------------------------------------------------

uint8_t natureFromIVs(uint8_t ivAtk, uint8_t ivDef, uint8_t ivSpe, uint8_t ivHp, bool shiny) {
  // The 21 bits that are fixed at hatch, through a full-avalanche mixer
  // (murmur3's finaliser), so one IV point apart is as likely to differ as any
  // two creatures. Never change this: see fork.h.
  uint32_t h = (uint32_t)(ivAtk & 31) | (uint32_t)(ivDef & 31) << 5 |
               (uint32_t)(ivSpe & 31) << 10 | (uint32_t)(ivHp & 31) << 15 |
               (uint32_t)(shiny ? 1 : 0) << 20;
  h ^= 0x9E3779B9u;   // so all-zero IVs do not hash to zero
  h ^= h >> 16; h *= 0x85EBCA6Bu;
  h ^= h >> 13; h *= 0xC2B2AE35u;
  h ^= h >> 16;
  return (uint8_t)(h % NATURE_COUNT);
}

uint8_t natureNeed(uint8_t nature) {
  // NEED_* after NONE is in NATURE_* order after EASYGOING: one correspondence.
  return nature >= 1 && nature < NATURE_COUNT ? nature : NEED_NONE;
}

const char *natureName(uint8_t nature) {
  if (nature >= NATURE_COUNT) return "";
  return FT((FStrId)(FS_NAT_EASYGOING + nature));
}

uint8_t monNature(const PartyMon &m) {
  if (m.empty()) return NATURE_NONE;
  return natureFromIVs(m.ivAtk, m.ivDef, m.ivSpe, m.ivHp, m.shiny != 0);
}

uint8_t Pet::nature() const {
  if (isEgg()) return NATURE_NONE;
  return natureFromIVs(ivAtk, ivDef, ivSpe, ivHp, shiny);
}

// Counts awake minutes for the extra drain. Deliberately NOT ageMinutes: a
// frozen companion does not age, and a nature would then drain on every minute
// or on none. Not saved either; losing a part-count of three minutes to a
// restart costs the creature at most one point.
static uint32_t gNatureMinutes = 0;

static uint8_t natureLose(uint8_t v, bool offline) {
  // Offline keeps syncClock()'s floor of 15: a creature left alone comes back
  // hungry, never at zero. Live drain has no floor, the same as upstream's.
  const uint8_t floor = offline ? 15 : 0;
  return v > floor ? v - 1 : v;
}

void Pet::forkNatureTick(bool offline) {
  if (!forkFeatures() || isEgg() || sleeping) return;
  const uint8_t need = natureNeed(nature());
  if (need == NEED_NONE) return;
  gNatureMinutes++;
  const uint32_t every = need == NEED_FOOD ? NATURE_FOOD_EVERY : NATURE_OTHER_EVERY;
  if (gNatureMinutes % every) return;
  switch (need) {
    case NEED_FOOD:    fullness = natureLose(fullness, offline); break;
    case NEED_JOY:     joy      = natureLose(joy, offline); break;
    case NEED_HYGIENE: hygiene  = natureLose(hygiene, offline); break;
    case NEED_ENERGY:  energy   = natureLose(energy, offline); break;
  }
}

void Pet::forkNatureCare(uint8_t need) {
  if (!forkFeatures() || isEgg() || ceremony != CER_NONE) return;
  if (need == NEED_NONE || natureNeed(nature()) != need) return;
  addBond(NATURE_CARE_BOND);   // inside addBond()'s daily cap, so not farmable
  heartUntil = millis() + HEART_MS;
}

// --- evolution rules -------------------------------------------------------------


static bool hasRule(int16_t from) {
  for (uint16_t i = 0; i < EVO_RULE_COUNT; i++)
    if (EVO_RULES[i].from == from) return true;
  return false;
}

bool forkEvoRuled(int16_t from) {
  return forkFeatures() && hasRule(from);
}

bool forkEggBarred(int16_t dex) {
  if (!forkFeatures()) return false;
  for (uint16_t i = 0; i < EVO_RULE_COUNT; i++) {
    if (EVO_RULES[i].to != dex) continue;
    // a branch: the same source leads somewhere else as well
    for (uint16_t j = 0; j < EVO_RULE_COUNT; j++)
      if (EVO_RULES[j].from == EVO_RULES[i].from && EVO_RULES[j].to != dex) return true;
  }
  return false;
}

bool forkEvoIsDay(uint32_t epoch) {
  if (!epoch) return true;   // no clock: never night, the same as isNightHour()
  const uint32_t h = (epoch / 3600) % 24;
  return h >= FORK_EVO_DAY_START && h < FORK_EVO_DAY_END;
}

uint8_t Pet::forkEvoBranches(ForkEvo *out) const {
  if (isEgg() || !forkEvoRuled(speciesId)) return 0;
  const bool day = forkEvoIsDay(lastSeenEpoch);
  const uint8_t nat = nature();
  uint8_t n = 0;
  int8_t firstNature = -1;
  bool natureHit = false;
  for (uint16_t i = 0; i < EVO_RULE_COUNT && n < FORK_EVO_MAX; i++) {
    const EvoRule &r = EVO_RULES[i];
    if (r.from != speciesId) continue;
    // Only forms this player can be shown: evolution is one-way, and a target
    // with no art or no pack would draw as a dex number forever. The same
    // filter upstream's eeveeOptions() applies to its branch.
    if (!speciesShowable(r.to)) continue;
    bool met = false;
    switch (r.cond) {
      case EVC_BOND:       met = bond >= FORK_EVO_BOND; break;
      case EVC_BOND_DAY:   met = bond >= FORK_EVO_BOND && day; break;
      case EVC_BOND_NIGHT: met = bond >= FORK_EVO_BOND && !day; break;
      case EVC_BOND_HIGH:  met = bond >= FORK_EVO_BOND_HIGH; break;
      case EVC_ATK_GT_DEF: met = trAtk > trDef; break;
      case EVC_DEF_GT_ATK: met = trDef > trAtk; break;
      case EVC_ATK_EQ_DEF: met = trAtk == trDef; break;
      case EVC_NATURE:
        met = nat < NATURE_COUNT && (r.arg >> nat) & 1;
        if (firstNature < 0) firstNature = (int8_t)n;
        if (met) natureHit = true;
        break;
    }
    out[n++] = ForkEvo{ (int16_t)r.to, r.cond, r.arg, met, false };
  }
  // The nature is every branching creature's floor: exactly one nature form is
  // always met. If the pack for THIS nature's form is missing, the first nature
  // form that can be shown stands in, so the creature still has somewhere to go.
  if (!natureHit && firstNature >= 0) out[firstNature].met = true;
  for (uint8_t i = 0; i < n; i++)
    if (out[i].met) { out[i].picked = true; break; }
  return n;
}

int16_t Pet::forkEvoPick() const {
  ForkEvo b[FORK_EVO_MAX];
  const uint8_t n = forkEvoBranches(b);
  for (uint8_t i = 0; i < n; i++)
    if (b[i].picked) return b[i].to;
  return 0;
}

const char *Pet::forkEvoWaitText() const {
  ForkEvo b[FORK_EVO_MAX];
  const uint8_t n = forkEvoBranches(b);
  if (!n || forkEvoPick()) return nullptr;
  // Nothing holds. Only bond rules can leave a creature with nowhere to go (a
  // branch always has its nature or its stats), so say which part of the bond
  // rule is missing: the bond itself first, then the time of day.
  for (uint8_t i = 0; i < n; i++) {
    const uint8_t need = b[i].cond == EVC_BOND_HIGH ? FORK_EVO_BOND_HIGH : FORK_EVO_BOND;
    if (bond < need) return FT(FS_NEEDS_BOND);
  }
  for (uint8_t i = 0; i < n; i++) {
    if (b[i].cond == EVC_BOND_DAY) return FT(FS_EVO_WAIT_DAY);
    if (b[i].cond == EVC_BOND_NIGHT) return FT(FS_EVO_WAIT_NIGHT);
  }
  return nullptr;
}

static bool lineUnreg(const Pet &p, int16_t cur, int depth) {
  if (cur < 1 || cur > DEX_COUNT || depth > 6) return false;
  if (!p.isRegistered(cur)) return true;
  if (hasRule(cur)) {
    for (uint16_t i = 0; i < EVO_RULE_COUNT; i++) {
      const EvoRule &r = EVO_RULES[i];
      if (r.from == cur && speciesShowable(r.to) && lineUnreg(p, r.to, depth + 1)) return true;
    }
    return false;
  }
  return lineUnreg(p, DEX_TBL[cur].evolvesTo, depth + 1);
}

bool Pet::forkLineHasUnregistered(int16_t base) const {
  return lineUnreg(*this, base, 0);
}

uint16_t Pet::forkEvoLevel() const {
  // canEvolveNow()'s sum, word for word: the source's level, plus a level per
  // care mistake, plus the day owed for retiring the previous creature early.
  return (uint16_t)(DEX_TBL[speciesId].evolveLevel + careMistakes + evoPenalty());
}

// --- sickness, medicine and coins -------------------------------------------------

uint32_t forkCreatureKey(const Pet &p) {
  if (p.isEgg()) return 0;
  // The nature's 21 bits, unhashed, with a top bit so no creature is key 0.
  return (uint32_t)(p.ivAtk & 31) | (uint32_t)(p.ivDef & 31) << 5 |
         (uint32_t)(p.ivSpe & 31) << 10 | (uint32_t)(p.ivHp & 31) << 15 |
         (uint32_t)(p.shiny ? 1 : 0) << 20 | 0x80000000u;
}

bool forkSick(const Pet &p) {
  if (!forkFeatures() || p.isEgg() || p.ceremony != CER_NONE) return false;
  return forkData.st.sickKey && forkData.st.sickKey == forkCreatureKey(p);
}

bool forkSickRisk(const Pet &p) {
  if (!forkFeatures() || p.isEgg() || p.ceremony != CER_NONE) return false;
  return p.poops >= FORK_SICK_POOPS || p.weight > FORK_SICK_WEIGHT ||
         p.lowestStat() <= FORK_SICK_NEED;
}

// The last clock reading a Pet-taking call saw, for createdEpoch on a first
// write that comes from the bag rather than from a tick.
static uint32_t gForkEpoch = 0;

void forkFlush() {
  if (forkData.isDirty()) forkData.save(gForkEpoch);
}

static uint8_t sickLose(uint8_t v) { return v ? v - 1 : 0; }

static bool gSicknessOn = true;
void forkTestSetSickness(bool on) { gSicknessOn = on; }

void forkSickTick(Pet &p, int roll) {
  if (!forkFeatures() || !gSicknessOn) return;
  if (p.isEgg() || p.sleeping || p.ceremony != CER_NONE) return;
  gForkEpoch = p.lastSeenEpoch;
  ForkState &s = forkData.st;
  if (!forkSick(p)) {
    if (forkSickRisk(p) && (roll < 0 ? random(100) : roll) < FORK_SICK_CHANCE) {
      s.sickKey = forkCreatureKey(p);
      s.sickMinutes = 0;
      forkData.markDirty();
    }
    forkFlush();
    return;
  }
  if (s.sickMinutes < 0xFFFF) s.sickMinutes++;
  // Live drain has no floor, the same as upstream's awake drain.
  if (s.sickMinutes % FORK_SICK_DRAIN_EVERY == 0) {
    p.fullness = sickLose(p.fullness);
    p.joy = sickLose(p.joy);
    p.hygiene = sickLose(p.hygiene);
    p.energy = sickLose(p.energy);
  }
  if (s.sickMinutes % FORK_SICK_MISTAKE_MIN == 0) {
    if (p.careMistakes < 255) p.careMistakes++;
    forkData.markDirty();
  }
  // Progress is written every ten minutes, not every one: a cut loses at most
  // ten minutes toward the next mistake, which is in the player's favour.
  if (s.sickMinutes % 10 == 0) forkData.markDirty();
  forkFlush();
}

bool forkCure(Pet &p) {
  if (!forkSick(p) || !forkData.st.medicine) return false;
  gForkEpoch = p.lastSeenEpoch;
  forkData.st.medicine--;
  forkData.st.sickKey = 0;
  forkData.st.sickMinutes = 0;
  forkData.markDirty();
  forkFlush();
  return true;
}

void forkSickBattle(const Pet &p, Combatant &c) {
  if (!forkSick(p)) return;
  for (int i = 0; i < SI_COUNT; i++) {
    const uint32_t v = (uint32_t)c.base[i] * FORK_SICK_BATTLE_PCT / 100;
    c.base[i] = (uint16_t)(v ? v : 1);
  }
}

uint32_t forkAddCoins(uint32_t n) {
  if (!forkFeatures() || !n) return 0;
  uint32_t &c = forkData.st.coins;
  const uint32_t room = c < FORK_COINS_MAX ? FORK_COINS_MAX - c : 0;
  if (n > room) n = room;
  if (!n) return 0;
  c += n;
  forkData.markDirty();
  forkFlush();
  return n;
}

bool forkBuyMedicine() {
  if (!forkFeatures()) return false;
  ForkState &s = forkData.st;
  if (s.coins < FORK_MED_PRICE || s.medicine >= 99) return false;
  s.coins -= FORK_MED_PRICE;
  s.medicine++;
  forkData.markDirty();
  forkFlush();
  return true;
}

uint32_t forkWinCoins(bool wild, bool hard, int8_t trainer) {
  if (!forkFeatures()) return 0;
  uint32_t n = wild ? FORK_COINS_WILD
                    : FORK_COINS_GYM + 2u * (uint32_t)(trainer > 0 ? trainer : 0);
  return hard ? n * 2 : n;
}

// --- the step counter ----------------------------------------------------------

const uint32_t FORK_STEP_MILE[FORK_STEP_MILES] = { 500, 2000, 5000 };
const uint32_t FORK_STEP_RANK[FORK_STEP_RANKS] = { 10000, 50000, 100000, 250000 };

// The banner queue. Text is copied in: an item's name is static, but the step
// count is formatted.
#define FORK_BANNERS 4
struct ForkBannerMsg { char l1[24]; char l2[28]; };
static ForkBannerMsg gBanner[FORK_BANNERS];
static uint8_t gBannerN = 0;
static uint32_t gBannerShownAt = 0;   // millis() the head was first drawn; 0 = not yet

static void bannerPush(const char *l1, const char *l2) {
  if (gBannerN == FORK_BANNERS) {   // full: the oldest goes
    memmove(&gBanner[0], &gBanner[1], sizeof(ForkBannerMsg) * (FORK_BANNERS - 1));
    gBannerN--;
    gBannerShownAt = 0;
  }
  ForkBannerMsg &m = gBanner[gBannerN++];
  snprintf(m.l1, sizeof(m.l1), "%s", l1);
  snprintf(m.l2, sizeof(m.l2), "%s", l2);
}

bool forkBanner(const char **line1, const char **line2) {
  if (!forkFeatures()) return false;
  const uint32_t now = millis();
  while (gBannerN) {
    if (!gBannerShownAt) gBannerShownAt = now ? now : 1;
    if (now - gBannerShownAt < FORK_BANNER_MS) {
      *line1 = gBanner[0].l1;
      *line2 = gBanner[0].l2;
      return true;
    }
    memmove(&gBanner[0], &gBanner[1], sizeof(ForkBannerMsg) * (FORK_BANNERS - 1));
    gBannerN--;
    gBannerShownAt = 0;
  }
  return false;
}

void forkBannerClear() { gBannerN = 0; gBannerShownAt = 0; }

uint8_t forkStepRankFor(uint32_t total) {
  uint8_t r = 0;
  while (r < FORK_STEP_RANKS && total >= FORK_STEP_RANK[r]) r++;
  return r;
}

const char *forkStepRankName(uint8_t rank) {
  if (rank > FORK_STEP_RANKS) rank = FORK_STEP_RANKS;
  return FT((FStrId)(FS_RANK_0 + rank));
}

uint32_t forkStepsToday(uint32_t nowEpoch) {
  if (!forkFeatures() || !nowEpoch) return 0;
  const ForkState &s = forkData.st;
  return s.stepDay == nowEpoch / 86400 ? s.stepsToday : 0;
}

// What each milestone pays: one upstream item, drawn from its own short list.
static ItemKey milestoneItem(uint8_t i) {
  static const ItemKey M0[] = { IT_POKEBALL };
  static const ItemKey M1[] = { IT_POTION, IT_GREATBALL };
  static const ItemKey M2[] = { IT_PROTEIN, IT_IRON, IT_CARBOS, IT_ULTRABALL };
  switch (i) {
    case 0: return M0[0];
    case 1: return M1[random(2)];
    default: return M2[random(4)];
  }
}

static uint32_t satAdd(uint32_t a, uint32_t b) { return a > 0xFFFFFFFFu - b ? 0xFFFFFFFFu : a + b; }

void forkStepsApply(Pet &p, const ForkStepBucket &b, uint32_t nowEpoch) {
  if (!forkFeatures() || !b.steps) return;
  ForkState &s = forkData.st;
  gForkEpoch = nowEpoch;
  s.stepsTotal = satAdd(s.stepsTotal, b.steps);
  forkData.markDirty();

  // A newer day's steps start that day's record; an older day's (a clock moved
  // back, a zone change) count only towards the trail.
  if (b.day > s.stepDay) {
    s.stepDay = b.day;
    s.stepsToday = 0;
    s.stepMiles = 0;
    s.stepJoy = 0;
    s.stepBond = 0;
  }
  if (b.day == s.stepDay) {
    s.stepsToday = satAdd(s.stepsToday, b.steps);
    char l1[24];
    for (uint8_t i = 0; i < FORK_STEP_MILES; i++) {
      if ((s.stepMiles >> i) & 1 || s.stepsToday < FORK_STEP_MILE[i]) continue;
      s.stepMiles |= (uint8_t)(1u << i);
      const ItemKey k = milestoneItem(i);
      snprintf(l1, sizeof(l1), FT(FS_STEPS_BANNER), (unsigned long)FORK_STEP_MILE[i]);
      // A full stack keeps nothing; say so rather than name an item that is not there.
      bannerPush(l1, bag.add(k) ? itemEntry(k).name : FT(FS_BAG_FULL));
    }
    // Joy and bond: today's walk only, and only for a creature to give them to.
    const uint32_t today = nowEpoch ? nowEpoch / 86400 : b.day;
    if (s.stepDay == today && !p.isEgg() && p.ceremony == CER_NONE) {
      uint32_t joyDue = s.stepsToday / FORK_STEP_JOY_EVERY;
      if (joyDue > FORK_STEP_JOY_MAX) joyDue = FORK_STEP_JOY_MAX;
      if (joyDue > s.stepJoy) {
        const uint32_t j = p.joy + (joyDue - s.stepJoy);
        p.joy = (uint8_t)(j > 100 ? 100 : j);
        s.stepJoy = (uint8_t)joyDue;
      }
      uint32_t bondDue = s.stepsToday / FORK_STEP_BOND_EVERY;
      if (bondDue > FORK_STEP_BOND_MAX) bondDue = FORK_STEP_BOND_MAX;
      if (bondDue > s.stepBond) {
        p.forkStepBond((uint8_t)(bondDue - s.stepBond));   // upstream's daily cap applies
        s.stepBond = (uint8_t)bondDue;
      }
    }
  }
  const uint8_t rank = forkStepRankFor(s.stepsTotal);
  if (rank > s.stepRank) {
    s.stepRank = rank;
    bannerPush(FT(FS_RANK_BANNER), forkStepRankName(rank));
  }
}

void Pet::forkStepBond(uint8_t amt) {
  // Not care: walking must not keep the daily streak alive on its own, which
  // addBond() would do by registering the day's care. Found on the S24.
  if (forkFeatures()) addBond(amt, false);
}

void forkStepsPoll(Pet &p, uint32_t nowEpoch) {
  if (!forkFeatures()) return;
  ForkStepBucket b[FORK_STEP_BUCKETS];
  // More than one buffer's worth only after a long absence; keep reading.
  for (int round = 0; round < 8; round++) {
    const uint8_t n = forkTakeSteps(b, FORK_STEP_BUCKETS);
    for (uint8_t i = 0; i < n; i++) forkStepsApply(p, b[i], nowEpoch);
    if (n < FORK_STEP_BUCKETS) break;
  }
  forkFlush();
}

uint8_t forkStepWildPct(uint32_t nowEpoch) {
  const uint32_t t = forkStepsToday(nowEpoch);
  return (uint8_t)(t >= FORK_STEP_FULL ? 100 : t * 100 / FORK_STEP_FULL);
}

bool forkStepShiny(uint32_t roll, uint32_t nowEpoch) {
  const uint8_t pct = forkStepWildPct(nowEpoch);
  if (!pct) return false;
  // Up to the base odds again, in the roll's own units: 1/4096 -> 2/4096.
  return (roll % WILD_SHINY_SCALE) < WILD_SHINY_BASE + WILD_SHINY_BASE * pct / 100;
}

uint8_t forkStepCatch(uint8_t chance, uint32_t nowEpoch) {
  if (!chance || chance >= 100) return chance;
  const uint8_t pct = forkStepWildPct(nowEpoch);
  if (!pct) return chance;
  const uint32_t c = chance + (uint32_t)chance * FORK_STEP_CATCH_PCT * pct / 10000;
  // upstream never goes above 95 short of a sure catch; neither does walking
  return (uint8_t)(c > 95 ? 95 : c);
}

// --- expeditions ------------------------------------------------------------------

const uint8_t FORK_EXP_MINUTES[FORK_EXP_LENS] = { 15, 30, 60 };
const uint8_t FORK_EXP_ENERGY[FORK_EXP_LENS] = { 10, 20, 35 };
const uint8_t FORK_EXP_FINDS[FORK_EXP_LENS] = { 1, 2, 3 };
const uint16_t FORK_EXP_WALK[FORK_EXP_LENS] = { 300, 600, 1200 };

uint8_t forkExpBiome(uint8_t site) {
  // upstream's biomes: 1 the beach, 3 the volcano, 2 the forest
  static const uint8_t B[FORK_EXP_SITES] = { 1, 3, 2 };
  return site < FORK_EXP_SITES ? B[site] : 0;
}

const char *forkExpSiteName(uint8_t site) {
  if (site >= FORK_EXP_SITES) return "";
  return FT((FStrId)(FS_EXP_COAST + site));
}

uint8_t forkExpFind(uint8_t site, uint8_t roll) {
  // Ten entries a site, so a find's odds read straight off the table: each
  // entry is ten percent. The coast heals, the volcano trains, the forest
  // gives medicine and balls.
  static const uint8_t T[FORK_EXP_SITES][10] = {
    { IT_POTION, IT_POTION, IT_POTION, IT_SUPERPOTION, IT_SUPERPOTION,
      IT_FULLHEAL, IT_FULLHEAL, IT_GREATBALL, IT_POKEBALL, IT_HYPERPOTION },
    { IT_PROTEIN, IT_PROTEIN, IT_IRON, IT_IRON, IT_CARBOS,
      IT_CARBOS, IT_SUPERPOTION, IT_POTION, IT_GREATBALL, IT_ULTRABALL },
    { FORK_EXP_MEDICINE, FORK_EXP_MEDICINE, FORK_EXP_MEDICINE, IT_POKEBALL, IT_POKEBALL,
      IT_POKEBALL, IT_POTION, IT_POTION, IT_FULLHEAL, IT_GREATBALL },
  };
  if (site >= FORK_EXP_SITES) site = FORK_EXP_FOREST;
  return T[site][(roll % 100) / 10];
}

static bool expActive() { return forkData.st.expKey != 0; }

bool forkAway(const Pet &p) {
  if (!forkFeatures() || !expActive()) return false;
  if (p.isEgg() || p.ceremony != CER_NONE) return false;
  return forkData.st.expKey == forkCreatureKey(p);
}

uint8_t forkExpCanSend(const Pet &p, uint8_t len, uint32_t nowEpoch) {
  if (!forkFeatures() || p.isEgg() || p.ceremony != CER_NONE || p.awaitingStarter())
    return FORK_EXP_OFF;
  if (forkAway(p)) return FORK_EXP_AWAY;
  if (!nowEpoch) return FORK_EXP_NO_CLOCK;
  if (p.sleeping) return FORK_EXP_ASLEEP;
  if (forkSick(p)) return FORK_EXP_SICK;
  if (len >= FORK_EXP_LENS) len = FORK_EXP_LENS - 1;
  if (p.energy < FORK_EXP_ENERGY[len]) return FORK_EXP_TIRED;
  return FORK_EXP_OK;
}

bool forkExpSend(Pet &p, uint8_t site, uint8_t len, uint32_t nowEpoch) {
  if (site >= FORK_EXP_SITES || len >= FORK_EXP_LENS) return false;
  if (forkExpCanSend(p, len, nowEpoch) != FORK_EXP_OK) return false;
  gForkEpoch = nowEpoch;
  ForkState &s = forkData.st;
  // A trip left over from a creature that is no longer here is replaced.
  s.expKey = forkCreatureKey(p);
  s.expEnd = nowEpoch + (uint32_t)FORK_EXP_MINUTES[len] * 60;
  s.expSteps = s.stepsTotal;
  s.expSite = site;
  s.expLen = len;
  forkData.markDirty();
  forkFlush();
  // The trip first, then its cost: a cut between the two writes gives a free
  // trip, never a paid one that did not happen.
  p.energy = (uint8_t)(p.energy - FORK_EXP_ENERGY[len]);
  p.saveNow();
  return true;
}

uint32_t forkExpSecondsLeft(uint32_t nowEpoch) {
  if (!forkFeatures() || !expActive()) return 0;
  const uint32_t end = forkData.st.expEnd;
  return nowEpoch < end ? end - nowEpoch : 0;
}

uint32_t forkExpMinutesLeft(const Pet &p, uint32_t fromEpoch) {
  if (!forkAway(p) || !fromEpoch) return 0;
  const uint32_t end = forkData.st.expEnd;
  if (fromEpoch >= end) return 0;
  const uint32_t m = (end - fromEpoch + 59) / 60;   // a part minute out is a minute out
  const uint32_t most = FORK_EXP_MINUTES[FORK_EXP_LENS - 1] + FORK_EXP_SLACK_MIN;
  return m > most ? 0 : m;
}

static void expClear() {
  ForkState &s = forkData.st;
  s.expKey = 0;
  s.expEnd = 0;
  s.expSteps = 0;
  s.expSite = 0;
  s.expLen = 0;
  forkData.markDirty();
}

void forkExpPoll(Pet &p, uint32_t nowEpoch) {
  if (!forkFeatures() || !expActive()) return;
  ForkState &s = forkData.st;
  gForkEpoch = nowEpoch;
  if (!forkAway(p)) {
    // swapped out, released or replaced while it was out: called back
    expClear();
    bannerPush(FT(FS_EXP_EARLY), FT(FS_EXP_NOTHING));
    forkFlush();
    return;
  }
  if (!nowEpoch) return;
  const uint32_t most = (uint32_t)(FORK_EXP_MINUTES[FORK_EXP_LENS - 1] + FORK_EXP_SLACK_MIN) * 60;
  const bool due = nowEpoch >= s.expEnd;
  const bool clockBack = s.expEnd > nowEpoch && s.expEnd - nowEpoch > most;
  if (!due && !clockBack) return;
  const uint8_t site = s.expSite < FORK_EXP_SITES ? s.expSite : FORK_EXP_FOREST;
  const uint8_t len = s.expLen < FORK_EXP_LENS ? s.expLen : 0;
  const bool walked = s.stepsTotal >= s.expSteps &&
                      s.stepsTotal - s.expSteps >= FORK_EXP_WALK[len];
  // The finds are drawn first and the trip is cleared, with any medicine,
  // in ONE write of the record; upstream's items go into the bag after it. A
  // cut between the two loses those items, and never pays a trip twice.
  uint8_t finds[FORK_EXP_FINDS[FORK_EXP_LENS - 1] + 1];
  uint8_t n = 0;
  for (uint8_t i = 0; i < FORK_EXP_FINDS[len]; i++) finds[n++] = forkExpFind(site, (uint8_t)random(100));
  if (walked) finds[n++] = forkExpFind(site, (uint8_t)random(100));
  bool kept[sizeof(finds)];
  for (uint8_t i = 0; i < n; i++) {
    kept[i] = true;
    if (finds[i] != FORK_EXP_MEDICINE) continue;
    if (s.medicine < 99) s.medicine++;
    else kept[i] = false;
  }
  expClear();
  forkFlush();
  for (uint8_t i = 0; i < n; i++) {
    const char *l1 = (walked && i == n - 1) ? FT(FS_EXP_BONUS) : FT(FS_EXP_BACK);
    const char *l2;
    if (finds[i] == FORK_EXP_MEDICINE) l2 = kept[i] ? FT(FS_MEDICINE) : FT(FS_BAG_FULL);
    else l2 = bag.add((ItemKey)finds[i]) ? itemEntry((ItemKey)finds[i]).name : FT(FS_BAG_FULL);
    bannerPush(l1, l2);
  }
}

bool Pet::forkIsAway() const { return forkAway(*this); }

// --- 9. more ways to touch the pet -------------------------------------------

// The finger. A leg is a run in one horizontal direction; it ends when the
// finger comes back FORK_LEG_MIN px from the furthest point it reached, so
// jitter never splits one leg into two.
static struct {
  bool active = false;
  int16_t legFrom = 0, ext = 0, minY = 0, maxY = 0;
  int8_t dir = 0;
  uint8_t legs = 0, longLegs = 0;   // finished legs
} gStroke;

static void strokeEndLeg(int len) {
  if (len < 0) len = -len;
  if (gStroke.legs < 255) gStroke.legs++;
  if (len >= FORK_STROKE_LEG && gStroke.longLegs < 255) gStroke.longLegs++;
}

void forkStrokeBegin(int16_t x, int16_t y) {
  gStroke = {};
  gStroke.active = true;
  gStroke.legFrom = gStroke.ext = x;
  gStroke.minY = gStroke.maxY = y;
}

void forkStrokeMove(int16_t x, int16_t y) {
  if (!gStroke.active) return;
  if (y < gStroke.minY) gStroke.minY = y;
  if (y > gStroke.maxY) gStroke.maxY = y;
  if (gStroke.dir == 0) {
    if (abs(x - gStroke.legFrom) >= FORK_LEG_MIN) { gStroke.dir = x > gStroke.legFrom ? 1 : -1; gStroke.ext = x; }
    return;
  }
  const bool further = gStroke.dir > 0 ? x > gStroke.ext : x < gStroke.ext;
  if (further) { gStroke.ext = x; return; }
  if (abs(x - gStroke.ext) < FORK_LEG_MIN) return;
  strokeEndLeg(gStroke.ext - gStroke.legFrom);   // it turned back
  gStroke.legFrom = gStroke.ext;
  gStroke.ext = x;
  gStroke.dir = (int8_t)-gStroke.dir;
}

uint8_t forkStrokeKind() {
  if (!gStroke.active) return FORK_GESTURE_NONE;
  // a scribble that wanders up and down is neither
  if (gStroke.maxY - gStroke.minY > 100) return FORK_GESTURE_NONE;
  uint8_t legs = gStroke.legs, longLegs = gStroke.longLegs;
  if (gStroke.dir != 0) {   // the leg under way counts too
    const int len = abs(gStroke.ext - gStroke.legFrom);
    legs++;
    if (len >= FORK_STROKE_LEG) longLegs++;
  }
  if (legs >= 3 && longLegs >= 2) return FORK_GESTURE_STROKE;
  if (legs >= 4 && longLegs <= 1) return FORK_GESTURE_TICKLE;
  return FORK_GESTURE_NONE;
}

bool forkStrokeBusy() {
  return forkFeatures() && gStroke.active && gStroke.legs >= 2;
}

// What it is showing.
static uint8_t gReact = FORK_REACT_NONE;
static uint32_t gReactAt = 0;
static void react(uint8_t r, uint32_t ms) { gReact = r; gReactAt = ms; }

uint8_t forkReact(uint32_t ms) {
  if (!forkFeatures() || gReact == FORK_REACT_NONE) return FORK_REACT_NONE;
  const uint32_t len = gReact == FORK_REACT_DIZZY ? FORK_DIZZY_MS : FORK_REACT_MS;
  return ms - gReactAt < len ? gReact : FORK_REACT_NONE;
}
uint32_t forkReactAge(uint32_t ms) { return forkReact(ms) ? ms - gReactAt : 0; }

int forkReactDy(uint32_t ms) {
  const uint8_t r = forkReact(ms);
  const uint32_t t = ms - gReactAt;
  if (r == FORK_REACT_HOP) {   // two hops, the second smaller
    if (t < 500) return (int)(4 * 48 * t * (500 - t) / (500 * 500));
    if (t < 850) { const uint32_t u = t - 500; return (int)(4 * 20 * u * (350 - u) / (350 * 350)); }
  }
  if (r == FORK_REACT_TICKLE && t < 1200) return ((t / 90) % 2) ? 6 : 0;   // a giggle
  if (r == FORK_REACT_BELLY && t < 400) return ((t / 100) % 2) ? 4 : 0;
  return 0;
}

int forkReactDx(uint32_t ms) {
  const uint8_t r = forkReact(ms);
  const uint32_t t = ms - gReactAt;
  if (r == FORK_REACT_DIZZY) {   // a sway that dies away
    const float k = 1.0f - (float)t / FORK_DIZZY_MS;
    return (int)(14.0f * k * sinf((float)t * 0.012f));
  }
  if (r == FORK_REACT_ANNOYED && t < 450) return ((t / 60) % 2) ? 5 : -5;   // a huff
  return 0;
}

bool forkTouchable(const Pet &p) {
  return forkFeatures() && !p.isEgg() && !p.sleeping && p.ceremony == CER_NONE &&
         !forkAway(p);
}

// The day's joy, inside the cap. Writes the record when it grants any.
static int8_t touchJoy(const Pet &p, uint8_t want) {
  ForkState &s = forkData.st;
  const uint32_t day = p.lastSeenEpoch / 86400;   // Pet::today(), which is private
  if (day != s.touchDay) { s.touchDay = day; s.touchJoy = 0; forkData.markDirty(); }
  const uint8_t left = s.touchJoy < FORK_TOUCH_JOY_MAX ? FORK_TOUCH_JOY_MAX - s.touchJoy : 0;
  const uint8_t g = want < left ? want : left;
  if (g) { s.touchJoy = (uint8_t)(s.touchJoy + g); forkData.markDirty(); }
  return (int8_t)g;
}

static uint8_t gTickles = 0;
static uint32_t gTickleFrom = 0;

bool forkPetStroke(Pet &p, uint8_t kind, uint32_t ms) {
  if (!forkFeatures() || kind == FORK_GESTURE_NONE) return false;
  gStroke.active = false;
  if (!forkTouchable(p)) return true;   // spent all the same: it was no swipe
  if (kind == FORK_GESTURE_STROKE) {
    react(FORK_REACT_STROKE, ms);
    p.forkTouchCare(touchJoy(p, FORK_STROKE_JOY), FORK_STROKE_BOND);
  } else {
    if (gTickles == 0 || ms - gTickleFrom > FORK_TICKLE_WINDOW_MS) { gTickles = 0; gTickleFrom = ms; }
    if (gTickles < 255) gTickles++;
    if (gTickles > FORK_TICKLE_ANNOY) {
      react(FORK_REACT_ANNOYED, ms);
      p.forkTouchCare(-(int8_t)FORK_ANNOY_JOY, 0);
    } else {
      react(FORK_REACT_TICKLE, ms);
      p.forkTouchCare(touchJoy(p, FORK_TICKLE_JOY), 0);
    }
  }
  forkFlush();
  return true;
}

static bool gTapValid = false;
static uint32_t gTapAt = 0;
static int16_t gTapX = 0, gTapY = 0;

bool forkPetTap(Pet &p, int16_t x, int16_t y, bool head, uint32_t ms) {
  if (!forkTouchable(p)) { gTapValid = false; return false; }
  if (gTapValid && ms - gTapAt <= FORK_DOUBLE_TAP_MS &&
      abs(x - gTapX) <= FORK_DOUBLE_TAP_PX && abs(y - gTapY) <= FORK_DOUBLE_TAP_PX) {
    gTapValid = false;   // a third tap starts over rather than hopping again
    react(FORK_REACT_HOP, ms);
    p.forkTouchCare(touchJoy(p, FORK_HOP_JOY), 0);
    forkFlush();
    return true;
  }
  gTapValid = true; gTapAt = ms; gTapX = x; gTapY = y;
  react(head ? FORK_REACT_HEAD : FORK_REACT_BELLY, ms);
  return false;   // upstream's caress goes ahead
}

void Pet::forkTouchCare(int8_t joyDelta, uint8_t bondAmt) {
  if (!forkFeatures()) return;
  const int j = (int)joy + joyDelta;
  joy = (uint8_t)(j < 0 ? 0 : j > 100 ? 100 : j);
  if (joyDelta > 0 || bondAmt) heartUntil = millis() + HEART_MS;
  if (!bondAmt) return;
  const uint8_t oldBond = bond, oldCount = bondToday;
  const uint32_t oldDay = bondDay;
  addBond(bondAmt);   // care, like the caress it stands beside
  if (bond != oldBond || bondToday != oldCount || bondDay != oldDay) save();
}

// The phone's motion.
static struct {
  uint32_t jolts[FORK_SHAKE_JOLTS] = {};
  uint8_t joltN = 0;
  bool inJolt = false, shake = false;
  bool down = false, pending = false;
  uint32_t pendingFrom = 0;
  int8_t change = -1;
  bool shook = false;
  uint32_t lastShake = 0;
} gMotion;

void forkMotionSample(float ax, float ay, float az, uint32_t ms) {
  if (!forkFeatures()) return;
  const float dev = fabsf(sqrtf(ax * ax + ay * ay + az * az) - 9.81f);
  if (!gMotion.inJolt && dev > FORK_SHAKE_JOLT) {
    gMotion.inJolt = true;
    // keep the last few jolts; a shake is that many inside the window
    if (gMotion.joltN == FORK_SHAKE_JOLTS) {
      memmove(gMotion.jolts, gMotion.jolts + 1, sizeof(uint32_t) * (FORK_SHAKE_JOLTS - 1));
      gMotion.joltN--;
    }
    gMotion.jolts[gMotion.joltN++] = ms;
    if (gMotion.joltN == FORK_SHAKE_JOLTS && ms - gMotion.jolts[0] <= FORK_SHAKE_WINDOW_MS) {
      gMotion.shake = true;
      gMotion.joltN = 0;
    }
  } else if (gMotion.inJolt && dev < FORK_SHAKE_JOLT / 2) {
    gMotion.inJolt = false;
  }
  // Face down, with hysteresis and a hold, so a phone tipped past flat while
  // being read does not turn the screen off.
  const bool want = gMotion.down ? !(az > FORK_FACEUP_Z) : az < FORK_FACEDOWN_Z;
  if (want == gMotion.down) { gMotion.pending = false; return; }
  if (!gMotion.pending) { gMotion.pending = true; gMotion.pendingFrom = ms; }
  if (ms - gMotion.pendingFrom >= (want ? FORK_FACEDOWN_MS : FORK_FACEUP_MS)) {
    gMotion.down = want;
    gMotion.change = want ? 1 : 0;
    gMotion.pending = false;
  }
}

int8_t forkFaceDownChange() {
  if (!forkFeatures()) return -1;
  const int8_t c = gMotion.change;
  gMotion.change = -1;
  return c;
}

void forkMotionPoll(Pet &p, bool onMain, uint32_t ms) {
  if (!forkFeatures() || !gMotion.shake) return;
  gMotion.shake = false;
  if (!onMain || !forkTouchable(p)) return;
  react(FORK_REACT_DIZZY, ms);
  // A dizzy creature shaken again sways again, but pays for it once a cooldown.
  if (gMotion.shook && ms - gMotion.lastShake < FORK_SHAKE_COOLDOWN_MS) return;
  gMotion.shook = true;
  gMotion.lastShake = ms;
  p.forkTouchCare(-(int8_t)FORK_SHAKE_JOY, 0);
}

void forkTouchReset() {
  gStroke = {};
  gReact = FORK_REACT_NONE; gReactAt = 0;
  gTickles = 0; gTickleFrom = 0;
  gTapValid = false;
  gMotion = {};
  gMotion.change = -1;
}
