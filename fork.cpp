#include "fork.h"
#include "ckpt.h"
#include "fork_i18n.h"
#include "party.h"
#include "pet.h"
#include <string.h>

// See fork.h for why any of this exists.

static bool gForkOn = false;

bool forkFeatures() { return gForkOn; }
void forkSetFeatures(bool on) { gForkOn = on; }

Fork forkData;
ForkHost gForkHost;

uint32_t forkTakeSteps() { return gForkHost.takeSteps ? gForkHost.takeSteps() : 0; }

// --- the record --------------------------------------------------------------

uint16_t Fork::encode(const ForkState &s, uint8_t *buf, size_t cap,
                      uint32_t generation) {
  const size_t bodyEnd = CKPT_HDR + FORK_BODY_LEN;
  if (cap < bodyEnd + CKPT_CRC) return 0;
  memset(buf, 0, bodyEnd + CKPT_CRC);
  uint8_t *b = buf + CKPT_HDR;
  ckptWr32(b + FORK_OFF_CREATED, s.createdEpoch);
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
  out = s;
  return true;
}

void Fork::begin() {
  st = ForkState();
  loadedOk = false;
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
  if (!n) return;
  if (!decode(buf, n, st)) { st = ForkState(); return; }
  gen = ckptRd32(buf + 8);
  loadedOk = true;
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
