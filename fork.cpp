#include "fork.h"
#include "ckpt.h"
#include "fork_i18n.h"
#include "party.h"
#include "pet.h"
#include "evo_rules.h"
#include "battle.h"
#include "inventory.h"
#include "wild.h"
#include "link.h"
#include <string.h>
#include <math.h>
#include <initializer_list>

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
bool forkCharging() { return gForkHost.charging && gForkHost.charging(); }
bool forkPlace(float *lat, float *lon, int32_t *zoneSec) {
  float la = 0, lo = 0;
  int32_t z = 0;
  if (!gForkHost.place || !gForkHost.place(&la, &lo, &z)) return false;
  // Never trust the host past what a place can be.
  if (!(la >= -90.0f && la <= 90.0f) || !(lo >= -180.0f && lo <= 180.0f)) return false;
  if (z < -14 * 3600 || z > 14 * 3600) return false;
  if (lat) *lat = la;
  if (lon) *lon = lo;
  if (zoneSec) *zoneSec = z;
  return true;
}
bool forkCanAskBirthday() { return forkFeatures() && gForkHost.askBirthday; }
// --- 14. phone to phone (fork.h) ---------------------------------------------
bool forkNetOn() { return gForkHost.netRole != nullptr; }
void forkNetRole(bool host, const char *name) {
  if (gForkHost.netRole) gForkHost.netRole(host, name, FORK_NET_LINK);
}
bool forkNetChoosing(const Link &l) {
  return forkNetOn() && !l.isHost && l.state == LINK_LISTENING &&
         !(gForkHost.netTargeted && gForkHost.netTargeted());
}
bool forkNetAwaitingGuest(const Link &l) {
  return forkNetOn() && l.isHost && l.live() && l.state != LINK_READY &&
         l.state != LINK_WAITING && l.state != LINK_DONE &&
         !(gForkHost.netLocked && gForkHost.netLocked());
}
uint8_t forkNetHosts(ForkNetHost *out, uint8_t max) {
  if (!gForkHost.netHosts) return 0;
  const uint8_t n = gForkHost.netHosts(out, max);
  for (uint8_t i = 0; i < n && i < max; i++) {   // never trust the host's strings
    out[i].name[sizeof(out[i].name) - 1] = 0;
    out[i].addr[sizeof(out[i].addr) - 1] = 0;
  }
  return n < max ? n : max;
}
bool forkNetJoin(const char *addr) { return gForkHost.netJoin && gForkHost.netJoin(addr); }
void forkNetAskAddress() {
  if (gForkHost.netAskAddress) gForkHost.netAskAddress(FT(FS_NET_TYPE));
}
bool forkNetMyAddress(char *out, uint8_t n) {
  return n && gForkHost.netMyAddress && gForkHost.netMyAddress(out, n);
}
void forkNetPoll(Link &l) {
  if (forkNetOn() && !l.isHost && l.state == LINK_LISTENING &&
      gForkHost.netTargeted && gForkHost.netTargeted())
    l.start();
}

bool forkWeatherReport(uint8_t *wmo, int16_t *temp, char *unit) {
  uint8_t w = 0;
  int16_t t = 0;
  char u = 'C';
  if (!gForkHost.weather || !gForkHost.weather(&w, &t, &u)) return false;
  // Never trust the host past what a report can be.
  if (u != 'C' && u != 'F') return false;
  if (t < -100 || t > 150) return false;
  if (wmo) *wmo = w;
  if (temp) *temp = t;
  if (unit) *unit = u;
  return true;
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
  ckptWr32(b + FORK_OFF_CHARGE_FROM, s.chargeFrom);
  ckptWr32(b + FORK_OFF_STEP_PREV_DAY, s.stepPrevDay);
  ckptWr32(b + FORK_OFF_STEP_PREV, s.stepPrev);
  b[FORK_OFF_GIFT_YEAR] = (uint8_t)s.giftYear;
  b[FORK_OFF_GIFT_YEAR + 1] = (uint8_t)(s.giftYear >> 8);
  b[FORK_OFF_GIFT_MASK] = s.giftMask;
  b[FORK_OFF_BDAY_MONTH] = s.bdayMonth;
  b[FORK_OFF_BDAY_DAY] = s.bdayDay;
  ckptWr32(b + FORK_OFF_HATCH_KEY, s.hatchKey);
  ckptWr32(b + FORK_OFF_HATCH_DAY, s.hatchDay);
  ckptWr32(b + FORK_OFF_WX_HOUR, s.wxHour);
  ckptWr32(b + FORK_OFF_WX_DAY, s.wxDay);
  b[FORK_OFF_WX_UP] = s.wxUp;
  b[FORK_OFF_WX_DOWN] = s.wxDown;
  b[FORK_OFF_ACC_OWNED] = (uint8_t)s.accOwned;
  b[FORK_OFF_ACC_OWNED + 1] = (uint8_t)(s.accOwned >> 8);
  b[FORK_OFF_ACC_WORN] = s.accWorn;
  ckptWr32(b + FORK_OFF_ACC_KEY, s.accKey);
  b[FORK_OFF_FRAME] = s.frame;
  b[FORK_OFF_FRAME_SEEN] = s.frameSeen;
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
  if (stored >= FORK_OFF_CHARGE_FROM + 4) s.chargeFrom = ckptRd32(body + FORK_OFF_CHARGE_FROM);
  // Yesterday's walk is whole or absent: a count with no day is nobody's.
  if (stored >= FORK_OFF_STEP_PREV + 4) {
    s.stepPrevDay = ckptRd32(body + FORK_OFF_STEP_PREV_DAY);
    s.stepPrev = ckptRd32(body + FORK_OFF_STEP_PREV);
  }
  // A year's gifts are whole or absent: a mask with no year is nobody's.
  if (stored >= FORK_OFF_GIFT_MASK + 1) {
    s.giftYear = (uint16_t)(body[FORK_OFF_GIFT_YEAR] | body[FORK_OFF_GIFT_YEAR + 1] << 8);
    s.giftMask = body[FORK_OFF_GIFT_MASK];
  }
  // The birthday likewise, and a date that cannot be one reads as not set.
  if (stored >= FORK_OFF_BDAY_DAY + 1) {
    const uint8_t m = body[FORK_OFF_BDAY_MONTH], d = body[FORK_OFF_BDAY_DAY];
    if (m >= 1 && m <= 12 && d >= 1 && d <= 31) { s.bdayMonth = m; s.bdayDay = d; }
  }
  if (stored >= FORK_OFF_HATCH_DAY + 4) {
    s.hatchKey = ckptRd32(body + FORK_OFF_HATCH_KEY);
    s.hatchDay = ckptRd32(body + FORK_OFF_HATCH_DAY);
  }
  // The weather's day is whole or absent, like the touch cap: counts with no
  // day would be a day already spent, or one that never started.
  if (stored >= FORK_OFF_WX_DOWN + 1) {
    s.wxHour = ckptRd32(body + FORK_OFF_WX_HOUR);
    s.wxDay = ckptRd32(body + FORK_OFF_WX_DAY);
    s.wxUp = body[FORK_OFF_WX_UP];
    s.wxDown = body[FORK_OFF_WX_DOWN];
  }
  if (stored >= FORK_OFF_ACC_OWNED + 2)
    s.accOwned = (uint16_t)(body[FORK_OFF_ACC_OWNED] | body[FORK_OFF_ACC_OWNED + 1] << 8);
  // What is worn is whole or absent: a worn accessory with no wearer is
  // nobody's, and one this build does not know (or does not own) is not worn.
  if (stored >= FORK_OFF_ACC_KEY + 4) {
    s.accWorn = body[FORK_OFF_ACC_WORN];
    s.accKey = ckptRd32(body + FORK_OFF_ACC_KEY);
    if (s.accWorn == 0 || s.accWorn >= FORK_ACC_COUNT || !((s.accOwned >> (s.accWorn - 1)) & 1)) {
      s.accWorn = 0;
      s.accKey = 0;
    }
  }
  // A frame a later build added reads as "the best open"; one this build
  // knows but is not open is caught where it is drawn (forkFrameShown()).
  if (stored >= FORK_OFF_FRAME + 1) {
    s.frame = body[FORK_OFF_FRAME];
    if (s.frame > FORK_FRAME_COUNT) s.frame = 0;
  }
  if (stored >= FORK_OFF_FRAME_SEEN + 1) {
    s.frameSeen = body[FORK_OFF_FRAME_SEEN];
    if (s.frameSeen >= FORK_FRAME_COUNT) s.frameSeen = FORK_FRAME_COUNT - 1;
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
  // The album, its own record (section 13).
  forkAlbumClear();
  albGen = 0;
  {
    static uint8_t abuf[FORK_ALBUM_BLOB_CAP];
    const uint16_t an = ckptReadNewest(prefs, "albA", "albB", FORK_ALBUM_MAGIC, abuf, sizeof(abuf));
    if (an && forkAlbumDecode(abuf, an)) albGen = ckptRd32(abuf + 8);
    else forkAlbumClear();
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

uint32_t forkKeyOf(uint8_t ivAtk, uint8_t ivDef, uint8_t ivSpe, uint8_t ivHp, bool shiny) {
  // The nature's 21 bits, unhashed, with a top bit so no creature is key 0.
  return (uint32_t)(ivAtk & 31) | (uint32_t)(ivDef & 31) << 5 |
         (uint32_t)(ivSpe & 31) << 10 | (uint32_t)(ivHp & 31) << 15 |
         (uint32_t)(shiny ? 1 : 0) << 20 | 0x80000000u;
}

uint32_t forkCreatureKey(const Pet &p) {
  if (p.isEgg()) return 0;
  return forkKeyOf(p.ivAtk, p.ivDef, p.ivSpe, p.ivHp, p.shiny);
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

static void bannerPush(const char *l1, const char *l2);   // the banner queue, below

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
  // Self-medication (section 10): the player's own medicine, after a while.
  if (s.sickMinutes >= FORK_SELFMED_MIN && s.medicine) {
    forkCure(p);
    bannerPush(FT(FS_SELFMED), FT(FS_CURED));
    return;
  }
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
    s.stepPrevDay = s.stepDay;   // kept for "yesterday you walked" (section 10)
    s.stepPrev = s.stepsToday;
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
  // Its site's accessory, until it is owned (section 14), announced after the
  // finds. Rolled only while it is still to find, so an owned one costs the
  // random sequence nothing.
  const uint8_t acc = forkAccForSite(site);
  if (acc && !forkAccOwned(acc) && (uint8_t)random(100) < FORK_ACC_EXP_PCT[len]) forkAccGive(acc);
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
static void react(uint8_t r, uint32_t ms) {
  static const char *const NAME[] = { "none", "hop", "head", "belly", "stroke", "tickle", "annoyed", "dizzy" };
  gReact = r; gReactAt = ms;
  // one line a gesture, and only ever on the phone: how a real finger is checked
  Serial.printf("fork: touch %s\n", r < 8 ? NAME[r] : "?");
}

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
    Serial.printf("fork: face %s\n", want ? "down" : "up");
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

// --- 10. the pet acting on its own ---------------------------------------------

// The bubbles' timing and the one-shot reasons waiting to be said.
static struct {
  uint32_t mainSince = 0;   // ms the pet screen came up; 0 = not on it
  uint32_t lastAt = 0;      // ms the last bubble started; 0 = none yet
  uint32_t saidAt[FORK_SAY_COUNT] = {};   // ms each reason was last said; 0 = never
  uint8_t showing = FORK_SAY_NONE;
  uint32_t showAt = 0;
  bool wasAsleep = false;
  bool morning = false;     // woke up, not yet greeted
  uint32_t morningAt = 0;
  bool selfcare = false;    // looked after itself, not yet said
  uint32_t walkedDay = 0;   // the day "yesterday you walked" was said on
  uint32_t holidayDay = 0;  // the day today's greeting was said on
  uint8_t wxSaid = FORK_WX_NONE;   // the weather last remarked on (section 12)
  char text[40] = "";
} gSay;

// Not an egg, not a ceremony, not out: a creature that is here to be cared for.
static bool present(const Pet &p) {
  return !p.isEgg() && p.ceremony == CER_NONE && !forkAway(p);
}

bool forkSelfCareOn() {
  return !(forkData.st.flags & FORK_FLAG_NO_SELFCARE);
}

void forkSetSelfCare(bool on) {
  if (!forkFeatures() || forkSelfCareOn() == on) return;
  if (on) forkData.st.flags &= (uint8_t)~FORK_FLAG_NO_SELFCARE;
  else forkData.st.flags |= FORK_FLAG_NO_SELFCARE;
  forkData.markDirty();
  forkFlush();
}

bool forkHapticsOn() {
  return !(forkData.st.flags & FORK_FLAG_NO_HAPTICS);
}

void forkSetHaptics(bool on) {
  if (!forkFeatures() || forkHapticsOn() == on) return;
  if (on) forkData.st.flags &= (uint8_t)~FORK_FLAG_NO_HAPTICS;
  else forkData.st.flags |= FORK_FLAG_NO_HAPTICS;
  forkData.markDirty();
  forkFlush();
}

bool Pet::forkSelfCareApply() {
  if (!forkFeatures()) return false;
  uint8_t *const need[] = { &fullness, &joy, &energy, &hygiene };
  bool any = false;
  for (uint8_t *n : need) {
    if (*n < FORK_SELFCARE_BELOW) { *n = FORK_SELFCARE_TO; any = true; }
  }
  // Not care: nothing here touches bond, the streak, the heart or the mistakes.
  if (any) pendingSave = true;
  return any;
}

bool forkSelfCareLive(Pet &p, uint32_t idleMs) {
  if (!forkFeatures() || !forkSelfCareOn() || !forkCharging()) return false;
  if (idleMs < (uint32_t)FORK_SELFCARE_IDLE_MIN * 60000u || !present(p)) return false;
  if (!p.forkSelfCareApply()) return false;
  gSay.selfcare = true;
  return true;
}

void forkChargeLeave(uint32_t nowEpoch) {
  if (!forkFeatures()) return;
  const uint32_t from = (nowEpoch && forkCharging()) ? nowEpoch : 0;
  if (forkData.st.chargeFrom == from) return;
  forkData.st.chargeFrom = from;
  gForkEpoch = nowEpoch;
  forkData.markDirty();
  forkFlush();
}

bool forkChargeReturn(Pet &p, uint32_t nowEpoch) {
  if (!forkFeatures()) return false;
  const uint32_t from = forkData.st.chargeFrom;
  if (from) {   // spent either way: the next absence is measured afresh
    forkData.st.chargeFrom = 0;
    gForkEpoch = nowEpoch;
    forkData.markDirty();
    forkFlush();
  }
  if (!from || !nowEpoch || nowEpoch < from) return false;
  if (nowEpoch - from < (uint32_t)FORK_SELFCARE_IDLE_MIN * 60u) return false;
  if (!forkSelfCareOn() || !forkCharging() || !present(p)) return false;
  if (!p.forkSelfCareApply()) return false;
  gSay.selfcare = true;
  return true;
}

uint32_t forkStepsYesterday(uint32_t nowEpoch) {
  if (!nowEpoch) return 0;
  const uint32_t today = nowEpoch / 86400;
  const ForkState &s = forkData.st;
  if (s.stepDay + 1 == today) return s.stepsToday;          // today's walk not counted yet
  if (s.stepDay == today && s.stepPrevDay + 1 == today) return s.stepPrev;
  return 0;
}

uint8_t forkBehPick(const Pet &p, uint8_t roll) {
  if (!forkFeatures() || !present(p) || p.sleeping) return FORK_BEH_NONE;
  const uint8_t nat = p.nature();
  uint8_t nap = p.energy < FORK_NAP_ENERGY ? 35 : p.energy < 60 ? 8 : 0;
  if (nat == NATURE_LAZY) nap += nap ? 15 : 8;
  uint8_t hop = p.joy >= FORK_HOP_JOY_AT ? 15 : 0;
  if (nat == NATURE_PLAYFUL && p.joy >= 50) hop += 10;
  uint8_t mess = p.poops ? 15 : 0;
  if (nat == NATURE_TIDY && p.poops) mess += 20;
  // Upstream's own choice always keeps a share: the creature still wanders.
  if (roll < nap) return FORK_BEH_NAP;
  if (roll < nap + hop) return FORK_BEH_HOP;
  if (roll < nap + hop + mess && nap + hop + mess <= 80) return FORK_BEH_MESS;
  return FORK_BEH_NONE;
}

uint8_t forkBehNext(const Pet &p) {
  if (!forkFeatures()) return FORK_BEH_NONE;   // no random(): upstream's sequence stands
  return forkBehPick(p, (uint8_t)random(100));
}

static bool wxLit(uint32_t nowEpoch) { return forkSkyPhase(nowEpoch) != FORK_SKY_NIGHT; }

// The weather has changed since it was last remarked on, and this one is worth
// a word: a clear NIGHT is not (its line is about the day).
static bool wxWorthSaying(uint32_t nowEpoch) {
  const uint8_t k = forkWeather();
  if (!k || k == gSay.wxSaid) return false;
  return !(k == FORK_WX_CLEAR && !wxLit(nowEpoch));
}

// What the creature says about it: whether it likes it, or else what it is.
static const char *wxLine(const Pet &p, uint32_t nowEpoch) {
  const uint8_t k = forkWeather();
  if (!k) return "";
  const DexEntry &d = DEX_TBL[p.speciesId];
  const int8_t m = forkWeatherMood(k, d.type1, d.type2, wxLit(nowEpoch));
  // A clear day pleases everybody, so its own line says so better.
  if (m > 0 && k != FORK_WX_CLEAR) return FT(FS_WXSAY_LIKE);
  if (m < 0) return FT(FS_WXSAY_DISLIKE);
  return FT((FStrId)(FS_WXSAY_CLEAR + k - FORK_WX_CLEAR));
}

// The reason, by priority. `ms`: when set, a reason said inside
// FORK_SAY_REPEAT_MS is skipped for the next one.
static uint8_t sayPick(const Pet &p, uint32_t nowEpoch, uint32_t ms, bool timed) {
  const uint32_t today = nowEpoch ? nowEpoch / 86400 : 0;
  const bool want[FORK_SAY_COUNT] = {
    false,
    forkSick(p),
    p.fullness < FORK_SAY_LOW,
    p.energy < FORK_SAY_LOW,
    p.hygiene < FORK_SAY_LOW,
    p.joy < FORK_SAY_LOW,
    p.poops >= 2,
    gSay.morning,
    gSay.selfcare,
    today && gSay.walkedDay != today && forkStepsYesterday(nowEpoch) >= FORK_SAY_STEPS,
    today && gSay.holidayDay != today && forkHoliday(p, nowEpoch) != FORK_HOL_NONE,
    wxWorthSaying(nowEpoch),
  };
  // The greeting goes straight after SICK: a holiday comes once a day and a
  // hungry creature can say so in three minutes' time.
  static const uint8_t ORDER[FORK_SAY_COUNT - 1] = {
    FORK_SAY_SICK, FORK_SAY_HOLIDAY, FORK_SAY_HUNGRY, FORK_SAY_TIRED, FORK_SAY_DIRTY,
    FORK_SAY_BORED, FORK_SAY_MESS, FORK_SAY_MORNING, FORK_SAY_WEATHER, FORK_SAY_SELFCARE,
    FORK_SAY_WALKED,
  };
  static_assert(sizeof(ORDER) == FORK_SAY_COUNT - 1, "every bubble reason, once");
  for (uint8_t r : ORDER) {
    if (!want[r]) continue;
    if (timed && gSay.saidAt[r] && ms - gSay.saidAt[r] < FORK_SAY_REPEAT_MS) continue;
    return r;
  }
  return FORK_SAY_NONE;
}

uint8_t forkSayReason(const Pet &p, uint32_t nowEpoch) {
  if (!forkFeatures() || !present(p) || p.sleeping) return FORK_SAY_NONE;
  return sayPick(p, nowEpoch, 0, false);
}

void forkSayPoll(const Pet &p, bool onMain, uint32_t ms, uint32_t nowEpoch) {
  if (!forkFeatures()) return;
  // Waking up is seen here, whatever screen is up, so the greeting waits for it.
  if (p.sleeping) gSay.wasAsleep = true;
  else if (gSay.wasAsleep) { gSay.wasAsleep = false; gSay.morning = true; gSay.morningAt = ms; }
  if (gSay.morning && ms - gSay.morningAt > 10u * 60000u) gSay.morning = false;   // too late to say

  if (!onMain || !present(p) || p.sleeping) { gSay.mainSince = 0; gSay.showing = FORK_SAY_NONE; return; }
  if (!gSay.mainSince) gSay.mainSince = ms ? ms : 1;
  if (gSay.showing && ms - gSay.showAt < FORK_SAY_SHOW_MS) return;
  gSay.showing = FORK_SAY_NONE;
  if (ms - gSay.mainSince < FORK_SAY_FIRST_MS) return;
  if (gSay.lastAt && ms - gSay.lastAt < FORK_SAY_GAP_MS) return;
  if (forkReact(ms) != FORK_REACT_NONE) return;   // never over a reaction's marks

  const uint8_t r = sayPick(p, nowEpoch, ms, true);
  if (!r) return;
  if (r == FORK_SAY_WALKED) {
    snprintf(gSay.text, sizeof(gSay.text), FT(FS_SAY_WALKED_FMT),
             (unsigned long)forkStepsYesterday(nowEpoch));
    gSay.walkedDay = nowEpoch / 86400;
  } else if (r == FORK_SAY_HOLIDAY) {
    snprintf(gSay.text, sizeof(gSay.text), "%s", forkHolidayGreeting(forkHoliday(p, nowEpoch)));
    gSay.holidayDay = nowEpoch / 86400;
  } else if (r == FORK_SAY_WEATHER) {
    snprintf(gSay.text, sizeof(gSay.text), "%s", wxLine(p, nowEpoch));
    gSay.wxSaid = forkWeather();
  } else {
    snprintf(gSay.text, sizeof(gSay.text), "%s", FT((FStrId)(FS_SAY_SICK + r - FORK_SAY_SICK)));
  }
  if (r == FORK_SAY_MORNING) gSay.morning = false;
  if (r == FORK_SAY_SELFCARE) gSay.selfcare = false;
  gSay.showing = r;
  gSay.showAt = ms;
  gSay.lastAt = ms ? ms : 1;
  gSay.saidAt[r] = ms ? ms : 1;
  Serial.printf("fork: say %u\n", (unsigned)r);
}

const char *forkSaying(uint32_t ms) {
  if (!forkFeatures() || !gSay.showing || ms - gSay.showAt >= FORK_SAY_SHOW_MS) return nullptr;
  return gSay.text;
}

void forkSayReset() { gSay = {}; }

// --- the world from the clock -------------------------------------------------

// Howard Hinnant's days_from_civil / civil_from_days, for the proleptic
// Gregorian calendar. Exact for every date a uint32_t epoch can hold.
uint32_t forkDayNumber(uint16_t year, uint8_t month, uint8_t day) {
  int32_t y = (int32_t)year - (month <= 2 ? 1 : 0);
  const int32_t era = (y >= 0 ? y : y - 399) / 400;
  const uint32_t yoe = (uint32_t)(y - era * 400);
  const uint32_t mp = (month + 9) % 12;   // March = 0
  const uint32_t doy = (153 * mp + 2) / 5 + day - 1;
  const uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  const int32_t days = era * 146097 + (int32_t)doe - 719468;
  return days > 0 ? (uint32_t)days : 0;
}

ForkDate forkDate(uint32_t localEpoch) {
  const int32_t z = (int32_t)(localEpoch / 86400) + 719468;
  const int32_t era = z / 146097;
  const uint32_t doe = (uint32_t)(z - era * 146097);
  const uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const uint32_t mp = (5 * doy + 2) / 153;
  ForkDate d;
  d.day = (uint8_t)(doy - (153 * mp + 2) / 5 + 1);
  d.month = (uint8_t)(mp < 10 ? mp + 3 : mp - 9);
  d.year = (uint16_t)(yoe + era * 400 + (d.month <= 2 ? 1 : 0));
  d.wday = (uint8_t)((localEpoch / 86400 + 4) % 7);   // 1970-01-01 was a Thursday
  return d;
}

static bool leapYear(uint16_t y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

void forkEaster(uint16_t year, uint8_t *month, uint8_t *day) {
  // The anonymous Gregorian algorithm (Meeus/Jones/Butcher).
  const int a = year % 19, b = year / 100, c = year % 100, d = b / 4, e = b % 4;
  const int f = (b + 8) / 25, g = (b - f + 1) / 3, h = (19 * a + b - d - g + 15) % 30;
  const int i = c / 4, k = c % 4, l = (32 + 2 * e + 2 * i - h - k) % 7;
  const int m = (a + 11 * h + 22 * l) / 451;
  *month = (uint8_t)((h + l - 7 * m + 114) / 31);
  *day = (uint8_t)((h + l - 7 * m + 114) % 31 + 1);
}

// The NOAA solar calculator's low-precision equations, which are within a
// minute or two of the almanac -- far finer than a sky drawn in four phases.
uint8_t forkSunCalc(uint16_t year, uint8_t month, uint8_t day, float lat, float lon,
                    int32_t zoneSec, int16_t *rise, int16_t *set) {
  *rise = 0;
  *set = 1440;
  const float PI_F = 3.14159265f, RAD = PI_F / 180.0f;
  const int doy = (int)(forkDayNumber(year, month, day) - forkDayNumber(year, 1, 1)) + 1;
  const float g = 2.0f * PI_F / (leapYear(year) ? 366.0f : 365.0f) * (float)(doy - 1);
  const float eqt = 229.18f * (0.000075f + 0.001868f * cosf(g) - 0.032077f * sinf(g) -
                               0.014615f * cosf(2 * g) - 0.040849f * sinf(2 * g));
  const float decl = 0.006918f - 0.399912f * cosf(g) + 0.070257f * sinf(g) -
                     0.006758f * cosf(2 * g) + 0.000907f * sinf(2 * g) -
                     0.002697f * cosf(3 * g) + 0.00148f * sinf(3 * g);
  const float la = lat * RAD;
  const float cosH = (cosf(90.833f * RAD) - sinf(la) * sinf(decl)) / (cosf(la) * cosf(decl));
  if (cosH > 1.0f) return FORK_SUN_DOWN;
  if (cosH < -1.0f) return FORK_SUN_UP;
  const float H = acosf(cosH) / RAD;
  const float zoneMin = (float)zoneSec / 60.0f;
  float r = 720.0f - 4.0f * (lon + H) - eqt + zoneMin;
  float s = 720.0f - 4.0f * (lon - H) - eqt + zoneMin;
  r = fmodf(r + 2880.0f, 1440.0f);
  s = fmodf(s + 2880.0f, 1440.0f);
  *rise = (int16_t)lroundf(r);
  *set = (int16_t)lroundf(s);
  return FORK_SUN_NORMAL;
}

bool forkSunToday(uint32_t localEpoch, int16_t *rise, int16_t *set) {
  float lat, lon;
  int32_t zone;
  if (!localEpoch || !forkPlace(&lat, &lon, &zone)) return false;
  const ForkDate d = forkDate(localEpoch);
  const uint8_t k = forkSunCalc(d.year, d.month, d.day, lat, lon, zone, rise, set);
  if (k == FORK_SUN_UP) { *rise = 0; *set = 1440; }
  else if (k == FORK_SUN_DOWN) { *rise = -1; *set = -1; }
  return true;
}

// Upstream's fixed hours, as drawScene() has always drawn them.
static uint8_t skyByHour(int h) {
  if (h < 6 || h >= 20) return FORK_SKY_NIGHT;
  if (h < 8) return FORK_SKY_DAWN;
  if (h < 18) return FORK_SKY_DAY;
  return FORK_SKY_DUSK;
}

uint8_t forkSkyPhaseAt(int m, uint8_t sun, int16_t rise, int16_t set) {
  if (sun == FORK_SUN_UP) return FORK_SKY_DAY;
  if (sun == FORK_SUN_DOWN) return FORK_SKY_NIGHT;
  // A zone so far from its sun that the day wraps midnight: the fixed hours.
  if (rise >= set) return skyByHour(m / 60);
  int dayFrom = rise + FORK_TWILIGHT_POST, duskFrom = set - FORK_TWILIGHT_POST;
  if (dayFrom > duskFrom) dayFrom = duskFrom = (rise + set) / 2;   // a day too short for both
  if (m < rise - FORK_TWILIGHT_PRE || m >= set + FORK_TWILIGHT_PRE) return FORK_SKY_NIGHT;
  if (m < dayFrom) return FORK_SKY_DAWN;
  if (m < duskFrom) return FORK_SKY_DAY;
  return FORK_SKY_DUSK;
}

uint8_t forkSkyPhase(uint32_t localEpoch) {
  const int m = localEpoch ? (int)(localEpoch % 86400 / 60) : 13 * 60;
  float lat, lon;
  int32_t zone;
  if (!forkFeatures() || !localEpoch || !forkPlace(&lat, &lon, &zone)) return skyByHour(m / 60);
  const ForkDate d = forkDate(localEpoch);
  int16_t rise, set;
  const uint8_t k = forkSunCalc(d.year, d.month, d.day, lat, lon, zone, &rise, &set);
  return forkSkyPhaseAt(m, k, rise, set);
}

float forkMoonAge(uint32_t localEpoch, int32_t zoneSec) {
  const double SYNODIC = 29.530588853;
  const double NEW_2000 = 947182440.0;   // a new moon: 2000-01-06 18:14 UTC
  const double utc = (double)localEpoch - (double)zoneSec;
  double age = fmod((utc - NEW_2000) / 86400.0, SYNODIC);
  if (age < 0) age += SYNODIC;
  return (float)age;
}

uint8_t forkSeasonAt(uint8_t month, float lat) {
  if (month < 1 || month > 12 || (lat > -FORK_TROPICS_LAT && lat < FORK_TROPICS_LAT))
    return FORK_SEASON_NONE;
  // Meteorological seasons, whole months: Dec-Feb is the north's winter.
  static const uint8_t NORTH[12] = {
    FORK_WINTER, FORK_WINTER, FORK_SPRING, FORK_SPRING, FORK_SPRING, FORK_SUMMER,
    FORK_SUMMER, FORK_SUMMER, FORK_AUTUMN, FORK_AUTUMN, FORK_AUTUMN, FORK_WINTER,
  };
  return NORTH[(lat >= 0 ? month - 1 : (month + 5) % 12)];
}

uint8_t forkSeason(uint32_t localEpoch) {
  float lat;
  if (!forkFeatures() || !localEpoch || !forkPlace(&lat, nullptr, nullptr)) return FORK_SEASON_NONE;
  return forkSeasonAt(forkDate(localEpoch).month, lat);
}

uint8_t forkHolidayOn(uint16_t year, uint8_t month, uint8_t day) {
  if (month == 1 && day == 1) return FORK_HOL_NEWYEAR;
  if (month == 2 && day == 14) return FORK_HOL_VALENTINE;
  if (month == 10 && day == 31) return FORK_HOL_HALLOWEEN;
  if (month == 12 && (day == 24 || day == 25)) return FORK_HOL_CHRISTMAS;
  uint8_t em, ed;
  forkEaster(year, &em, &ed);
  if (month == em && day == ed) return FORK_HOL_EASTER;
  return FORK_HOL_NONE;
}

// Does an anniversary of month/day fall on this date? 29 February is kept on
// the 28th in the years that have no 29th.
static bool anniversary(const ForkDate &d, uint8_t month, uint8_t day) {
  if (!month || !day) return false;
  if (month == 2 && day == 29 && !leapYear(d.year)) return d.month == 2 && d.day == 28;
  return d.month == month && d.day == day;
}

// Before this the clock is not a real date (an unset board reads months out),
// and nothing is celebrated on it.
#define FORK_WORLD_EPOCH_MIN 1577836800u   // 2020-01-01

uint32_t forkHatchDay(const Pet &p) {
  const uint32_t key = forkCreatureKey(p);
  return key && key == forkData.st.hatchKey ? forkData.st.hatchDay : 0;
}

static bool isHatchDay(const Pet &p, const ForkDate &d, uint32_t today) {
  const uint32_t hd = forkHatchDay(p);
  if (!hd || hd >= today) return false;   // the day itself is not an anniversary
  const ForkDate h = forkDate(hd * 86400u);
  return anniversary(d, h.month, h.day);
}

// Every holiday that is today, birthdays first. Returns how many.
static uint8_t holidaysToday(const Pet &p, uint32_t localEpoch, uint8_t *out) {
  if (!forkFeatures() || localEpoch < FORK_WORLD_EPOCH_MIN) return 0;
  const ForkDate d = forkDate(localEpoch);
  uint8_t n = 0;
  if (anniversary(d, forkData.st.bdayMonth, forkData.st.bdayDay)) out[n++] = FORK_HOL_BIRTHDAY;
  if (isHatchDay(p, d, localEpoch / 86400)) out[n++] = FORK_HOL_HATCHDAY;
  const uint8_t cal = forkHolidayOn(d.year, d.month, d.day);
  if (cal) out[n++] = cal;
  return n;
}

uint8_t forkHoliday(const Pet &p, uint32_t localEpoch) {
  uint8_t h[3];
  return holidaysToday(p, localEpoch, h) ? h[0] : FORK_HOL_NONE;
}

const char *forkHolidayTitle(uint8_t h) {
  if (h == FORK_HOL_NONE || h >= FORK_HOL_COUNT) return "";
  return FT((FStrId)(FS_HOL_NEWYEAR + h - FORK_HOL_NEWYEAR));
}
const char *forkHolidayGreeting(uint8_t h) {
  if (h == FORK_HOL_NONE || h >= FORK_HOL_COUNT) return "";
  return FT((FStrId)(FS_HOLSAY_NEWYEAR + h - FORK_HOL_NEWYEAR));
}

// One holiday's gift: banners say what came, one per item.
static void giveItem(const char *title, ItemKey k, uint8_t n) {
  char l2[32];
  const uint8_t got = bag.add(k, n);
  if (!got) { bannerPush(title, FT(FS_BAG_FULL)); return; }
  if (got > 1) {
    char what[24];
    snprintf(what, sizeof(what), "%s x%u", itemEntry(k).name, (unsigned)got);
    snprintf(l2, sizeof(l2), FT(FS_GIFT_FMT), what);
  } else {
    snprintf(l2, sizeof(l2), FT(FS_GIFT_FMT), itemEntry(k).name);
  }
  bannerPush(title, l2);
}
static void giveCoins(const char *title, uint32_t n) {
  char l2[32];
  snprintf(l2, sizeof(l2), FT(FS_GOT_COINS), (unsigned long)n);
  forkAddCoins(n);
  bannerPush(title, l2);
}

static void giveGift(uint8_t h) {
  const char *t = forkHolidayTitle(h);
  switch (h) {
    case FORK_HOL_NEWYEAR:
      giveCoins(t, FORK_GIFT_COINS_NEWYEAR);
      forkAccGive(FORK_ACC_TOPHAT);   // section 14, once ever
      break;
    case FORK_HOL_VALENTINE: giveItem(t, IT_HYPERPOTION, 1); break;
    case FORK_HOL_EASTER:    giveItem(t, IT_GREATBALL, 3); break;
    case FORK_HOL_HALLOWEEN:
      giveItem(t, IT_ULTRABALL, 2);
      forkAccGive(FORK_ACC_WITCH);
      break;
    case FORK_HOL_CHRISTMAS:
      giveItem(t, IT_PROTEIN, 1);
      giveItem(t, IT_IRON, 1);
      giveItem(t, IT_CARBOS, 1);
      forkAccGive(FORK_ACC_SANTA);
      break;
    case FORK_HOL_BIRTHDAY:  giveItem(t, IT_MASTERBALL, 1); break;
    case FORK_HOL_HATCHDAY:  giveCoins(t, FORK_GIFT_COINS_HATCHDAY); break;
    default: break;
  }
}

bool forkHolidayPoll(Pet &p, uint32_t localEpoch) {
  if (!forkFeatures() || localEpoch < FORK_WORLD_EPOCH_MIN) return false;
  gForkEpoch = localEpoch;
  ForkState &s = forkData.st;
  // The hatch day, stamped the first time this creature is seen.
  const uint32_t key = forkCreatureKey(p);
  if (key && key != s.hatchKey) {
    const uint32_t today = localEpoch / 86400, ageDays = p.ageMinutes / 1440;
    s.hatchKey = key;
    s.hatchDay = ageDays < today ? today - ageDays : today;
    forkData.markDirty();
  }
  uint8_t hs[3];
  const uint8_t n = holidaysToday(p, localEpoch, hs);
  const uint16_t year = forkDate(localEpoch).year;
  bool gave = false;
  for (uint8_t i = 0; i < n; i++) {
    if (s.giftYear != year) { s.giftYear = year; s.giftMask = 0; }
    const uint8_t bit = (uint8_t)(1u << (hs[i] - 1));
    if (s.giftMask & bit) continue;
    s.giftMask |= bit;
    forkData.markDirty();
    giveGift(hs[i]);
    gave = true;
    Serial.printf("fork: holiday %u gift\n", (unsigned)hs[i]);
  }
  forkFlush();
  return gave;
}

void forkSetBirthday(uint8_t month, uint8_t day) {
  static const uint8_t DAYS[12] = { 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  ForkState &s = forkData.st;
  if (month < 1 || month > 12 || day < 1 || day > DAYS[month - 1]) { month = 0; day = 0; }
  if (s.bdayMonth == month && s.bdayDay == day) return;
  s.bdayMonth = month;
  s.bdayDay = day;
  forkData.markDirty();
  forkFlush();
}

bool forkBirthday(uint8_t *month, uint8_t *day) {
  if (month) *month = forkData.st.bdayMonth;
  if (day) *day = forkData.st.bdayDay;
  return forkData.st.bdayMonth != 0;
}

void forkAskBirthday() {
  if (forkCanAskBirthday()) gForkHost.askBirthday(forkData.st.bdayMonth, forkData.st.bdayDay);
}

const char *forkWeekdayName(uint8_t wday) { return FT((FStrId)(FS_WD_SUN + (wday % 7))); }
const char *forkMonthName(uint8_t month) {
  return month >= 1 && month <= 12 ? FT((FStrId)(FS_MON_JAN + month - 1)) : "";
}

bool forkStandbyOn() { return !(forkData.st.flags & FORK_FLAG_NO_STANDBY); }
void forkSetStandby(bool on) {
  const uint8_t f = on ? (uint8_t)(forkData.st.flags & ~FORK_FLAG_NO_STANDBY)
                       : (uint8_t)(forkData.st.flags | FORK_FLAG_NO_STANDBY);
  if (f == forkData.st.flags) return;
  forkData.st.flags = f;
  forkData.markDirty();
  forkFlush();
}
bool forkKeepScreenOn() { return forkFeatures() && forkStandbyOn() && forkCharging(); }

// --- 12. the weather -------------------------------------------------------------

uint8_t forkWeatherKind(uint8_t wmo) {
  // WMO 4677, as Open-Meteo reports it.
  if (wmo <= 1) return FORK_WX_CLEAR;                          // clear, mainly clear
  if (wmo <= 3) return FORK_WX_CLOUDY;                         // partly cloudy, overcast
  if (wmo == 45 || wmo == 48) return FORK_WX_FOG;              // fog, rime fog
  if ((wmo >= 51 && wmo <= 67) || (wmo >= 80 && wmo <= 82)) return FORK_WX_RAIN;
  if ((wmo >= 71 && wmo <= 77) || wmo == 85 || wmo == 86) return FORK_WX_SNOW;
  if (wmo >= 95 && wmo <= 99) return FORK_WX_STORM;
  return FORK_WX_NONE;
}

bool forkWeatherOn() { return (forkData.st.flags & FORK_FLAG_WEATHER) != 0; }
void forkSetWeather(bool on) {
  if (!forkFeatures() || forkWeatherOn() == on) return;
  if (on) forkData.st.flags |= FORK_FLAG_WEATHER;
  else forkData.st.flags &= (uint8_t)~FORK_FLAG_WEATHER;
  forkData.markDirty();
  forkFlush();
  if (!on) gSay.wxSaid = FORK_WX_NONE;   // turned back on, it may say it again
  forkWeatherSync();
}

static struct {
  bool told = false;    // the host has been told anything at all
  bool on = false;
  float lat = 0, lon = 0;
} gWxTold;

void forkWeatherSync() {
  if (!gForkHost.weatherFetch) return;
  float lat = 0, lon = 0;
  const bool on = forkWeatherWanted(&lat, &lon);
  if (gWxTold.told && gWxTold.on == on && (!on || (gWxTold.lat == lat && gWxTold.lon == lon))) return;
  // Off is said even to a host never told on: a phone that fetched under a
  // previous run must stop and forget, whatever it remembers.
  gWxTold.told = true;
  gWxTold.on = on;
  gWxTold.lat = lat;
  gWxTold.lon = lon;
  gForkHost.weatherFetch(on, lat, lon);
}

bool forkWeatherWanted(float *lat, float *lon) {
  float la, lo;
  if (!forkFeatures() || !forkWeatherOn() || !forkPlace(&la, &lo, nullptr)) return false;
  // A tenth of a degree, whatever the host's place was: the zone's city is
  // already that coarse, and nothing finer may leave the phone.
  if (lat) *lat = roundf(la * 10.0f) / 10.0f;
  if (lon) *lon = roundf(lo * 10.0f) / 10.0f;
  return true;
}

uint8_t forkWeather() {
  uint8_t wmo;
  if (!forkWeatherWanted(nullptr, nullptr) || !forkWeatherReport(&wmo, nullptr, nullptr))
    return FORK_WX_NONE;
  return forkWeatherKind(wmo);
}

bool forkWeatherTemp(int16_t *temp, char *unit) {
  if (!forkWeather()) return false;
  return forkWeatherReport(nullptr, temp, unit);
}

const char *forkWeatherName(uint8_t kind) {
  if (kind < FORK_WX_CLEAR || kind >= FORK_WX_COUNT) return "";
  return FT((FStrId)(FS_WX_CLEAR + kind - FORK_WX_CLEAR));
}

static bool typeIn(uint8_t t1, uint8_t t2, std::initializer_list<uint8_t> set) {
  for (uint8_t t : set) if (t == t1 || t == t2) return true;
  return false;
}

int8_t forkWeatherMood(uint8_t kind, uint8_t t1, uint8_t t2, bool day) {
  bool likes = false, dislikes = false;
  int8_t otherwise = 0;
  switch (kind) {
    case FORK_WX_CLEAR:   // a sunny day cheers everybody; a clear night is just night
      otherwise = day ? 1 : 0;
      break;
    case FORK_WX_FOG:
      likes = typeIn(t1, t2, { T_GHOST, T_DARK });
      break;
    case FORK_WX_RAIN:
      likes = typeIn(t1, t2, { T_WATER, T_GRASS });
      dislikes = typeIn(t1, t2, { T_FIRE, T_ROCK, T_GROUND });
      break;
    case FORK_WX_SNOW:
      likes = typeIn(t1, t2, { T_ICE });
      dislikes = typeIn(t1, t2, { T_FIRE });
      break;
    case FORK_WX_STORM:   // thunder frightens most creatures
      likes = typeIn(t1, t2, { T_ELECTRIC, T_WATER });
      dislikes = typeIn(t1, t2, { T_FIRE });
      otherwise = -1;
      break;
    default:              // cloudy, or nothing known
      break;
  }
  if (likes && dislikes) return 0;   // one half of it likes it, the other does not
  if (likes) return 1;
  if (dislikes) return -1;
  return otherwise;
}

static void wxDayRoll(uint32_t day) {
  ForkState &s = forkData.st;
  if (s.wxDay == day) return;
  s.wxDay = day;
  s.wxUp = s.wxDown = 0;
  forkData.markDirty();
}

uint8_t forkWeatherJoyUp(uint32_t e) {
  return e && forkData.st.wxDay == e / 86400 ? forkData.st.wxUp : 0;
}
uint8_t forkWeatherJoyDown(uint32_t e) {
  return e && forkData.st.wxDay == e / 86400 ? forkData.st.wxDown : 0;
}

bool forkWeatherPoll(Pet &p, uint32_t localEpoch) {
  if (!forkFeatures()) return false;
  forkWeatherSync();
  if (localEpoch < FORK_WORLD_EPOCH_MIN) return false;
  const uint8_t k = forkWeather();
  if (!k || !present(p) || p.sleeping) return false;
  ForkState &s = forkData.st;
  const uint32_t hour = localEpoch / 3600;
  if (s.wxHour == hour) return false;
  // The hour is spent whatever the creature thinks, so a mood of 0 does not
  // leave it to be asked again every 30 s.
  s.wxHour = hour;
  forkData.markDirty();
  wxDayRoll(localEpoch / 86400);
  gForkEpoch = localEpoch;
  const DexEntry &d = DEX_TBL[p.speciesId];
  const int8_t m = forkWeatherMood(k, d.type1, d.type2, wxLit(localEpoch));
  bool moved = false;
  if (m > 0 && s.wxUp < FORK_WX_JOY_DAY && p.joy < 100) {
    p.joy++;
    s.wxUp++;
    moved = true;
  } else if (m < 0 && s.wxDown < FORK_WX_JOY_DAY && p.joy > 0) {
    p.joy--;
    s.wxDown++;
    moved = true;
  }
  forkFlush();
  if (moved) Serial.printf("fork: weather %u joy %+d\n", (unsigned)k, (int)m);
  return moved;
}

bool forkWeatherKnown() { return forkWeather() != FORK_WX_NONE; }
bool forkWeatherHidesSky() {
  const uint8_t k = forkWeather();
  return k != FORK_WX_NONE && k != FORK_WX_CLEAR;
}
bool forkWeatherFlash(uint32_t now) {
  return forkWeather() == FORK_WX_STORM && now % FORK_WX_FLASH_EVERY_MS < FORK_WX_FLASH_MS;
}

// --- 13. the farm and the album -------------------------------------------------

static ForkAlbumEntry gAlbum[FORK_ALBUM_MAX];   // oldest first
static uint16_t gAlbumN = 0;
static uint32_t gAlbumEver = 0;

void forkAlbumClear() {
  for (auto &e : gAlbum) e = ForkAlbumEntry();
  gAlbumN = 0;
  gAlbumEver = 0;
}

uint16_t forkAlbumEncode(uint8_t *buf, size_t cap, uint32_t generation) {
  const size_t bodyEnd = CKPT_HDR + FORK_ALBUM_HDR + (size_t)gAlbumN * FORK_ALBUM_ENTRY;
  if (cap < bodyEnd + CKPT_CRC) return 0;
  memset(buf, 0, bodyEnd + CKPT_CRC);
  uint8_t *b = buf + CKPT_HDR;
  ckptWr16(b + 0, FORK_ALBUM_ENTRY);
  ckptWr16(b + 2, gAlbumN);
  ckptWr32(b + 4, gAlbumEver);
  for (uint16_t i = 0; i < gAlbumN; i++) {
    const ForkAlbumEntry &e = gAlbum[i];
    uint8_t *o = b + FORK_ALBUM_HDR + (size_t)i * FORK_ALBUM_ENTRY;
    ckptWr32(o + 0, e.key);
    ckptWr16(o + 4, (uint16_t)e.dex);
    ckptWr16(o + 6, e.level);
    ckptWr16(o + 8, e.medals);
    o[10] = e.ending;
    o[11] = e.shiny ? 1 : 0;
    memcpy(o + 12, e.nick, sizeof(e.nick));
    o[12 + sizeof(e.nick) - 1] = 0;
    ckptWr32(o + 24, e.hatchDay);
    ckptWr32(o + 28, e.leftDay);
  }
  return ckptSeal(buf, bodyEnd, FORK_ALBUM_MAGIC, FORK_ALBUM_VERSION, generation);
}

bool forkAlbumDecode(const uint8_t *buf, uint16_t n) {
  forkAlbumClear();
  if (n < CKPT_HDR + CKPT_CRC + FORK_ALBUM_HDR) return false;
  const uint8_t *b = buf + CKPT_HDR;
  const size_t stored = n - CKPT_HDR - CKPT_CRC;
  // The dimensions travel in the body: a later build's bigger entry is read by
  // its prefix, and one too short to hold this build's fields is refused rather
  // than half read.
  const uint16_t esz = ckptRd16(b + 0);
  uint16_t count = ckptRd16(b + 2);
  if (esz < FORK_ALBUM_ENTRY) return false;
  if ((size_t)FORK_ALBUM_HDR + (size_t)count * esz > stored) return false;
  gAlbumEver = ckptRd32(b + 4);
  // More than this build keeps (a later build's FORK_ALBUM_MAX): the newest.
  const uint16_t skip = count > FORK_ALBUM_MAX ? count - FORK_ALBUM_MAX : 0;
  for (uint16_t i = skip; i < count; i++) {
    const uint8_t *o = b + FORK_ALBUM_HDR + (size_t)i * esz;
    ForkAlbumEntry e;
    e.key = ckptRd32(o + 0);
    e.dex = (int16_t)ckptRd16(o + 4);
    e.level = ckptRd16(o + 6);
    e.medals = ckptRd16(o + 8);
    e.ending = o[10];
    e.shiny = o[11] & 1;
    memcpy(e.nick, o + 12, sizeof(e.nick));
    e.nick[sizeof(e.nick) - 1] = 0;
    e.hatchDay = ckptRd32(o + 24);
    e.leftDay = ckptRd32(o + 28);
    // Nothing read from a save indexes a table unchecked: a dex past this
    // build's DEX_COUNT (a downgrade) or an ending it does not know is skipped.
    if (e.dex < 1 || e.dex > DEX_COUNT || e.ending == FORK_END_NONE || e.ending >= FORK_END_COUNT)
      continue;
    gAlbum[gAlbumN++] = e;
  }
  if (gAlbumEver < gAlbumN) gAlbumEver = gAlbumN;
  return true;
}

bool Fork::albumSave() {
  if (!forkFeatures() || !opened) return false;
  static uint8_t buf[FORK_ALBUM_BLOB_CAP];
  const uint32_t next = albGen + 1;
  const uint16_t n = forkAlbumEncode(buf, sizeof(buf), next);
  if (!n) return false;
  const char *key = ckptSlot(next, "albA", "albB");
  if (prefs.putBytes(key, buf, n) != n) {
    Serial.printf("save: the album (%s) did not write\n", key);
    return false;
  }
  albGen = next;
  return true;
}

uint8_t forkEndingOf(const Pet &p) {
  switch (p.ceremony) {
    // startRetire() runs the farewell's ceremony but marks lastEnd a release
    // when the farewell was not earned (early, or banked again): no blessing,
    // and not a farewell in the album either.
    case CER_FAREWELL: return p.lastEnd == CER_RELEASE ? FORK_END_RETIRED : FORK_END_FAREWELL;
    case CER_RUNAWAY:  return FORK_END_RUNAWAY;
    case CER_RELEASE:  return FORK_END_RELEASED;
    default:           return FORK_END_NONE;
  }
}

const char *forkEndingName(uint8_t ending) {
  switch (ending) {
    case FORK_END_FAREWELL: return FT(FS_END_FAREWELL);
    case FORK_END_RETIRED:  return FT(FS_END_RETIRED);
    case FORK_END_RUNAWAY:  return FT(FS_END_RUNAWAY);
    case FORK_END_RELEASED: return FT(FS_END_RELEASED);
    default:                return "";
  }
}

bool forkAlbumRecord(const Pet &p, uint32_t localEpoch) {
  if (!forkFeatures() || p.isEgg()) return false;
  const uint8_t ending = forkEndingOf(p);
  if (ending == FORK_END_NONE) return false;
  ForkAlbumEntry e;
  e.key = forkCreatureKey(p);
  e.dex = p.speciesId;
  e.level = (uint16_t)p.level();
  e.medals = p.medals;
  e.ending = ending;
  e.shiny = p.shiny ? 1 : 0;
  strncpy(e.nick, p.nick, sizeof(e.nick) - 1);
  e.nick[sizeof(e.nick) - 1] = 0;
  e.hatchDay = forkHatchDay(p);
  e.leftDay = localEpoch >= FORK_WORLD_EPOCH_MIN ? localEpoch / 86400 : 0;
  // A ceremony replayed after a cut between this write and the pet's: the
  // same creature leaving the same way at the same level is the same entry.
  if (gAlbumN) {
    const ForkAlbumEntry &last = gAlbum[gAlbumN - 1];
    if (last.key == e.key && last.dex == e.dex && last.ending == e.ending && last.level == e.level)
      return false;
  }
  if (gAlbumN == FORK_ALBUM_MAX) {
    memmove(&gAlbum[0], &gAlbum[1], sizeof(gAlbum[0]) * (FORK_ALBUM_MAX - 1));
    gAlbumN--;
  }
  gAlbum[gAlbumN++] = e;
  gAlbumEver++;
  forkData.albumSave();
  Serial.printf("fork: album #%lu dex %d ending %u\n", (unsigned long)gAlbumEver, e.dex, ending);
  return true;
}

uint16_t forkAlbumCount() { return forkFeatures() ? gAlbumN : 0; }
uint32_t forkAlbumEver() { return forkFeatures() ? gAlbumEver : 0; }

bool forkAlbumGet(uint16_t i, ForkAlbumEntry *out) {
  if (!forkFeatures() || i >= gAlbumN) return false;
  if (out) *out = gAlbum[gAlbumN - 1 - i];
  return true;
}

uint8_t forkAlbumPages() {
  if (!forkFeatures()) return 0;
  const uint16_t pages = (gAlbumN + FORK_ALBUM_PER_PAGE - 1) / FORK_ALBUM_PER_PAGE;
  return (uint8_t)(pages ? pages : 1);
}

void forkDayLabel(uint32_t day, char *out, size_t n) {
  if (!day) { snprintf(out, n, "?"); return; }
  const ForkDate d = forkDate(day * 86400u);
  snprintf(out, n, "%u %s %02u", (unsigned)d.day, forkMonthName(d.month), (unsigned)(d.year % 100));
}

// --- the farm ---

const PartyMon *forkFarmMon(const Party &pt, const ForkFarmMon &f) {
  if (!f.inBox) return f.slot < PARTY_SLOTS ? &pt.slots[f.slot] : nullptr;
  if (!pt.box.data() || f.slot >= BOX_SLOTS) return nullptr;
  return &pt.box[f.slot];
}

// The i-th banked creature, party first: false past the end.
static bool farmNth(const Party &pt, uint16_t n, ForkFarmMon *out) {
  uint16_t k = 0;
  for (uint16_t i = 0; i < PARTY_SLOTS; i++)
    if (!pt.slots[i].empty() && k++ == n) { *out = { false, i }; return true; }
  if (!pt.box.data()) return false;
  for (uint16_t i = 0; i < BOX_SLOTS; i++)
    if (!pt.box[i].empty() && k++ == n) { *out = { true, i }; return true; }
  return false;
}

static uint16_t farmTotal(const Party &pt) {
  uint16_t n = pt.count();
  if (pt.box.data()) n += pt.boxCount();
  return n;
}

uint8_t forkFarmPick(const Party &pt, uint32_t day, ForkFarmMon *out, uint16_t *total) {
  if (total) *total = 0;
  if (!forkFeatures()) return 0;
  const uint16_t n = farmTotal(pt);
  if (total) *total = n;
  if (!n) return 0;
  const uint8_t shown = n < FORK_FARM_SHOWN ? (uint8_t)n : FORK_FARM_SHOWN;
  // The next run of them each day, wrapping, so everybody is out at least once
  // every ceil(n / FORK_FARM_SHOWN) days -- a hash of the day would leave some
  // waiting for weeks.
  const uint16_t start = n > FORK_FARM_SHOWN ? (uint16_t)(((uint64_t)day * FORK_FARM_SHOWN) % n) : 0;
  uint8_t got = 0;
  for (uint8_t i = 0; i < shown; i++)
    if (farmNth(pt, (uint16_t)((start + i) % n), &out[got])) got++;
  return got;
}

// --- the visitor ---

static bool gVisitOn = false;        // a visitor is crossing
static uint32_t gVisitFrom = 0;      // ms it set off
static uint32_t gVisitNext = 0;      // ms the next one may set off; 0 = not on the pet screen
static ForkVisitor gVisit;

void forkVisitReset() {
  gVisitOn = false;
  gVisitFrom = 0;
  gVisitNext = 0;
}

void forkVisitPoll(const Pet &p, const Party &pt, bool onMain, uint32_t ms, int32_t rollIn) {
  const bool can = forkFeatures() && onMain && !p.isEgg() && p.ceremony == CER_NONE &&
                   !p.sleeping && !p.awaitingStarter();
  if (!can) { forkVisitReset(); return; }
  if (!gVisitNext) { gVisitNext = ms + FORK_VISIT_FIRST_MS; if (!gVisitNext) gVisitNext = 1; }
  if (gVisitOn) {
    if (ms - gVisitFrom < FORK_VISIT_MS) return;
    gVisitOn = false;
    gVisitNext = ms + FORK_VISIT_GAP_MS;
    if (!gVisitNext) gVisitNext = 1;
  }
  if ((int32_t)(ms - gVisitNext) < 0) return;
  const uint16_t n = farmTotal(pt);
  const uint32_t roll = rollIn < 0 ? (uint32_t)random(0x7FFFFFFF) : (uint32_t)rollIn;
  ForkFarmMon f;
  if (!n || !farmNth(pt, (uint16_t)(roll % n), &f)) {
    gVisitNext = ms + FORK_VISIT_GAP_MS;   // nobody banked: look again later
    if (!gVisitNext) gVisitNext = 1;
    return;
  }
  const PartyMon *m = forkFarmMon(pt, f);
  if (!m || m->empty() || m->dex < 1 || m->dex > DEX_COUNT) return;
  gVisit.dex = m->dex;
  gVisit.shiny = m->shiny != 0;
  gVisit.key = forkKeyOf(m->ivAtk, m->ivDef, m->ivSpe, m->ivHp, m->shiny);
  gVisit.dir = (roll >> 16) & 1 ? 1 : -1;
  snprintf(gVisit.name, sizeof(gVisit.name), "%s", m->nick[0] ? m->nick : DEX_TBL[m->dex].name);
  gVisitOn = true;
  gVisitFrom = ms;
  Serial.printf("fork: visitor dex %d\n", m->dex);
}

bool forkVisitor(uint32_t ms, ForkVisitor *out, int *x) {
  if (!forkFeatures() || !gVisitOn) return false;
  const uint32_t t = ms - gVisitFrom;
  if (t >= FORK_VISIT_MS) return false;
  // From just off one edge to just off the other.
  const int span = 466 + 2 * 50;
  const int along = (int)((uint64_t)t * span / FORK_VISIT_MS);
  if (x) *x = gVisit.dir > 0 ? -50 + along : 516 - along;
  if (out) *out = gVisit;
  return true;
}

// --- 14. accessories -----------------------------------------------------------

const uint8_t FORK_ACC_EXP_PCT[FORK_EXP_LENS] = { 5, 10, 20 };

uint8_t forkAccForSite(uint8_t site) {
  switch (site) {
    case FORK_EXP_COAST:   return FORK_ACC_BOW;
    case FORK_EXP_VOLCANO: return FORK_ACC_GOGGLES;
    case FORK_EXP_FOREST:  return FORK_ACC_FLOWER;
    default:               return FORK_ACC_NONE;
  }
}

const char *forkAccName(uint8_t a) {
  if (a == FORK_ACC_NONE || a >= FORK_ACC_COUNT) return "";
  return FT((FStrId)(FS_ACC_BOW + a - FORK_ACC_BOW));
}

const char *forkAccHint(uint8_t a) {
  if (a == FORK_ACC_NONE || a >= FORK_ACC_COUNT) return "";
  return FT((FStrId)(FS_ACCHINT_BOW + a - FORK_ACC_BOW));
}

bool forkAccOwned(uint8_t a) {
  if (a == FORK_ACC_NONE || a >= FORK_ACC_COUNT) return false;
  return (forkData.st.accOwned >> (a - 1)) & 1;
}

bool forkAccGive(uint8_t a) {
  if (!forkFeatures() || a == FORK_ACC_NONE || a >= FORK_ACC_COUNT || forkAccOwned(a)) return false;
  forkData.st.accOwned |= (uint16_t)(1u << (a - 1));
  forkData.markDirty();
  forkFlush();
  bannerPush(FT(FS_ACC_NEW), forkAccName(a));
  Serial.printf("fork: accessory %u\n", (unsigned)a);
  return true;
}

uint8_t forkAccWorn(const Pet &p) {
  if (!forkFeatures() || p.isEgg() || p.ceremony != CER_NONE) return FORK_ACC_NONE;
  const ForkState &s = forkData.st;
  if (!s.accWorn || !s.accKey || s.accKey != forkCreatureKey(p)) return FORK_ACC_NONE;
  return forkAccOwned(s.accWorn) ? s.accWorn : FORK_ACC_NONE;
}

bool forkAccWear(const Pet &p, uint8_t a) {
  if (!forkFeatures() || p.isEgg() || p.ceremony != CER_NONE) return false;
  if (a != FORK_ACC_NONE && !forkAccOwned(a)) return false;
  ForkState &s = forkData.st;
  const uint32_t key = a ? forkCreatureKey(p) : 0;
  if (s.accWorn == a && s.accKey == key) return true;
  s.accWorn = a;
  s.accKey = key;
  forkData.markDirty();
  forkFlush();
  return true;
}

void forkAccPoll(const Pet &p) {
  if (!forkFeatures()) return;
  if (forkData.st.stepRank >= 1 || forkStepRankFor(forkData.st.stepsTotal) >= 1) forkAccGive(FORK_ACC_CAP);
  if (p.registeredCount() >= FORK_ACC_CROWN_DEX) forkAccGive(FORK_ACC_CROWN);
}

static ForkHead gHead = { false, 0, 0, 0, 0, 0 };

void forkHeadNote(const uint8_t *frame, uint8_t w, uint8_t h, int x0, int y0, uint8_t s) {
  gHead.ok = false;
  if (!frame || !w || !h || !s) return;
  // The head starts at the first row that is a real part of the creature: at
  // least a quarter as many opaque pixels as its widest row. A thin crest, an
  // antenna or two ear tips poke above that, and a hat sitting on them floats
  // (a sleeping Blaziken's crest on the S24, Magnemite's antenna, Eevee's ears).
  uint8_t widest = 0;
  for (uint8_t r = 0; r < h; r++) {
    const uint8_t *row = frame + (uint32_t)r * w;
    uint8_t n = 0;
    for (uint8_t c = 0; c < w; c++) n += row[c] != 0xFF;
    if (n > widest) widest = n;
  }
  if (!widest) return;   // an empty frame has no head
  uint8_t need = widest / 4;
  if (need < 2) need = 2;
  if (need > widest) need = widest;
  uint8_t top = 0;
  for (; top < h; top++) {
    const uint8_t *row = frame + (uint32_t)top * w;
    uint8_t n = 0;
    for (uint8_t c = 0; c < w; c++) n += row[c] != 0xFF;
    if (n >= need) break;
  }
  // The band: a tenth of the frame from there, at least two rows, gives the
  // head's span.
  uint8_t band = h / 10;
  if (band < 2) band = 2;
  int lo = w, hi = -1;
  for (uint8_t r = top; r < h && r < top + band; r++) {
    const uint8_t *row = frame + (uint32_t)r * w;
    for (int c = 0; c < w; c++) {
      if (row[c] == 0xFF) continue;
      if (c < lo) lo = c;
      if (c > hi) hi = c;
    }
  }
  gHead.ok = true;
  gHead.s = s;
  gHead.y = (int16_t)(y0 + top * s);
  gHead.left = (int16_t)(x0 + lo * s);
  gHead.right = (int16_t)(x0 + (hi + 1) * s);
  gHead.x = (int16_t)((gHead.left + gHead.right) / 2);
}

ForkHead forkHeadLast() { return gHead; }
void forkHeadClear() { gHead.ok = false; }

// --- 16. card frames -------------------------------------------------------------

uint16_t forkFrameNeed(uint8_t f) {
  switch (f) {
    case FORK_FRAME_BRONZE:  return 25;
    case FORK_FRAME_SILVER:  return 100;
    case FORK_FRAME_GOLD:    return 250;
    case FORK_FRAME_CRYSTAL: return 500;
    case FORK_FRAME_MASTER:  return DEX_COUNT;   // every species, whatever the dex grows to
    default:                 return 0;
  }
}

bool forkFrameOpen(uint8_t f, uint16_t registered) {
  return f < FORK_FRAME_COUNT && registered >= forkFrameNeed(f);
}

uint8_t forkFrameBest(uint16_t registered) {
  uint8_t best = FORK_FRAME_NONE;
  for (uint8_t f = 1; f < FORK_FRAME_COUNT; f++)
    if (forkFrameOpen(f, registered)) best = f;
  return best;
}

const char *forkFrameName(uint8_t f) {
  if (f >= FORK_FRAME_COUNT) return "";
  return FT((FStrId)(FS_FRAME_NONE + f));
}

uint8_t forkFrameShown(uint16_t registered) {
  if (!forkFeatures()) return FORK_FRAME_NONE;
  const uint8_t c = forkData.st.frame;
  if (c && forkFrameOpen((uint8_t)(c - 1), registered)) return (uint8_t)(c - 1);
  return forkFrameBest(registered);
}

uint8_t forkFrameCycle(uint16_t registered) {
  if (!forkFeatures()) return FORK_FRAME_NONE;
  uint8_t f = forkFrameShown(registered);
  do { f = (uint8_t)((f + 1) % FORK_FRAME_COUNT); } while (!forkFrameOpen(f, registered));
  forkData.st.frame = (uint8_t)(f + 1);
  forkData.markDirty();
  forkFlush();
  return f;
}

void forkFramePoll(const Pet &p) {
  if (!forkFeatures()) return;
  const uint8_t best = forkFrameBest(p.registeredCount());
  if (best <= forkData.st.frameSeen) return;
  forkData.st.frameSeen = best;
  forkData.markDirty();
  forkFlush();
  bannerPush(FT(FS_FRAME_NEW), forkFrameName(best));
  Serial.printf("fork: frame %u\n", (unsigned)best);
}

// --- 17. the scrub -----------------------------------------------------------------

static struct {
  bool active = false;
  uint8_t n = 0;
  int16_t x[FORK_SCRUB_MAX], y[FORK_SCRUB_MAX];
  uint8_t dirt[FORK_SCRUB_MAX];
  int16_t fx = 0, fy = 0;   // the finger's last point
  bool finger = false;
  uint8_t hyg0 = 0;         // hygiene when it began
  uint32_t until = 0;
} gScrub;

uint8_t forkScrubSpotsFor(uint8_t hygiene) {
  if (hygiene > 100) hygiene = 100;
  // 3 at 100, one more for every 100/7 it lacks, 9 at 0 or near it
  const uint16_t extra = (uint16_t)(100 - hygiene) * (FORK_SCRUB_MAX - FORK_SCRUB_MIN + 1) / 101;
  return (uint8_t)(FORK_SCRUB_MIN + extra);
}

bool forkScrubWanted(const Pet &p) {
  return forkFeatures() && forkTouchable(p);
}

bool forkScrubBegin(const Pet &p, const int16_t *dx, const int16_t *dy, uint8_t n, uint32_t ms) {
  if (!forkScrubWanted(p) || gScrub.active || !n || !dx || !dy) return false;
  if (n > FORK_SCRUB_MAX) n = FORK_SCRUB_MAX;
  gScrub = {};
  gScrub.active = true;
  gScrub.n = n;
  for (uint8_t i = 0; i < n; i++) {
    gScrub.x[i] = dx[i];
    gScrub.y[i] = dy[i];
    gScrub.dirt[i] = FORK_SCRUB_DIRT;
  }
  gScrub.hyg0 = p.hygiene;
  gScrub.until = ms + FORK_SCRUB_MS;
  Serial.printf("fork: scrub %u spots\n", (unsigned)n);
  return true;
}

bool forkScrubActive() { return forkFeatures() && gScrub.active; }

// The squared distance from (px, py) to the segment a-b.
static int32_t segDist2(int32_t px, int32_t py, int32_t ax, int32_t ay, int32_t bx, int32_t by) {
  const int32_t vx = bx - ax, vy = by - ay, wx = px - ax, wy = py - ay;
  const int32_t vv = vx * vx + vy * vy;
  float t = vv ? (float)(wx * vx + wy * vy) / (float)vv : 0.0f;
  if (t < 0) t = 0;
  if (t > 1) t = 1;
  const float cx = ax + t * vx - px, cy = ay + t * vy - py;
  return (int32_t)(cx * cx + cy * cy);
}

void forkScrubRub(int16_t dx, int16_t dy, bool down) {
  if (!forkScrubActive()) return;
  if (down || !gScrub.finger) {
    gScrub.finger = true;
    gScrub.fx = dx; gScrub.fy = dy;
    return;
  }
  const int32_t mx = dx - gScrub.fx, my = dy - gScrub.fy;
  const int len = (int)sqrtf((float)(mx * mx + my * my));
  if (len < 1) return;
  const int32_t r2 = (int32_t)FORK_SCRUB_RADIUS * FORK_SCRUB_RADIUS;
  for (uint8_t i = 0; i < gScrub.n; i++) {
    if (!gScrub.dirt[i]) continue;
    if (segDist2(gScrub.x[i], gScrub.y[i], gScrub.fx, gScrub.fy, dx, dy) > r2) continue;
    const int w = len * FORK_SCRUB_PER_PX;
    gScrub.dirt[i] = (uint8_t)(w >= gScrub.dirt[i] ? 0 : gScrub.dirt[i] - w);
  }
  gScrub.fx = dx; gScrub.fy = dy;
}

bool forkScrubSpot(uint8_t i, int16_t *dx, int16_t *dy, uint8_t *dirt) {
  if (!forkScrubActive() || i >= gScrub.n) return false;
  if (dx) *dx = gScrub.x[i];
  if (dy) *dy = gScrub.y[i];
  if (dirt) *dirt = gScrub.dirt[i];
  return true;
}
uint8_t forkScrubCount() { return forkScrubActive() ? gScrub.n : 0; }
uint8_t forkScrubLeft() {
  if (!forkScrubActive()) return 0;
  uint8_t k = 0;
  for (uint8_t i = 0; i < gScrub.n; i++) k += gScrub.dirt[i] ? 1 : 0;
  return k;
}
uint32_t forkScrubMsLeft(uint32_t ms) {
  if (!forkScrubActive()) return 0;
  return (int32_t)(gScrub.until - ms) > 0 ? gScrub.until - ms : 0;
}

uint8_t forkScrubPartial(uint8_t hygiene0, uint16_t wiped, uint16_t total) {
  if (hygiene0 >= 100 || !total) return hygiene0 > 100 ? 100 : hygiene0;
  if (wiped > total) wiped = total;
  return (uint8_t)(hygiene0 + (uint32_t)(100 - hygiene0) * wiped / total);
}

void Pet::forkScrubCare(uint8_t to) {
  if (!forkFeatures() || to <= hygiene) return;
  hygiene = to > 100 ? 100 : to;
  save();
}

static void scrubPayPartial(Pet &p) {
  uint16_t wiped = 0;
  for (uint8_t i = 0; i < gScrub.n; i++) wiped += FORK_SCRUB_DIRT - gScrub.dirt[i];
  const uint16_t total = (uint16_t)gScrub.n * FORK_SCRUB_DIRT;
  gScrub.active = false;
  // From the hygiene it began with, never below what it has now (it could
  // have been fed or washed some other way meanwhile).
  p.forkScrubCare(forkScrubPartial(gScrub.hyg0, wiped, total));
  Serial.printf("fork: scrub stopped, %u of %u wiped\n", (unsigned)wiped, (unsigned)total);
}

void forkScrubStop(Pet &p) {
  if (!forkScrubActive()) return;
  scrubPayPartial(p);
}

uint8_t forkScrubPoll(Pet &p, bool onMain, uint32_t ms) {
  if (!forkScrubActive()) return FORK_SCRUB_GOING;
  if (!forkScrubLeft()) {
    gScrub.active = false;
    p.forkTouchCare(touchJoy(p, FORK_SCRUB_JOY), 0);
    forkFlush();
    Serial.println("fork: scrub done");
    return FORK_SCRUB_DONE;
  }
  if (!onMain || !forkTouchable(p) || (int32_t)(gScrub.until - ms) <= 0) {
    scrubPayPartial(p);
    return FORK_SCRUB_STOPPED;
  }
  return FORK_SCRUB_GOING;
}

// --- 18. the help ----------------------------------------------------------------

bool forkHelpSeen() { return (forkData.st.flags & FORK_FLAG_HELP_SEEN) != 0; }

void forkHelpMarkSeen() {
  if (!forkFeatures() || forkHelpSeen()) return;
  forkData.st.flags |= FORK_FLAG_HELP_SEEN;
  forkData.markDirty();
  forkFlush();
}

bool forkHelpFirstRun() {
  if (!forkFeatures() || forkHelpSeen()) return false;
  forkHelpMarkSeen();
  return true;
}

const char *forkHelpTitle(uint8_t page) {
  // The tiles' own names, so the help can never call one something the tab
  // bar does not.
  switch (page) {
    case 0: return FT(FS_HELP_GESTURES);
    case 1: return T(S_TAB_PLAYER);
    case 2: return T(S_GOALS);
    case 3: return T(S_PARTY);
    case 4: return T(S_TAB_HOME);
    case 5: return T(S_EXPLORE);
    case 6: return T(S_GYMS);
    case 7: return T(S_TAB_DEX);
    default: return "";
  }
}

const char *forkHelpLine(uint8_t page, uint8_t line) {
  if (page >= FORK_HELP_PAGES || line >= FORK_HELP_LINES) return "";
  return FT((FStrId)(FS_HELP_0_0 + page * FORK_HELP_LINES + line));
}
