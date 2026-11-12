#include "fork.h"
#include "ckpt.h"
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
