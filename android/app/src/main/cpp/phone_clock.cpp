#include "phone_clock.h"
#include "pet.h"
#include "Preferences.h"
#include <ctime>

// Its own namespace: WIPE clears the game's, and a factory reset has no reason
// to forget which zone the phone is in.
#define PHONE_NS "phone"
#define ZONE_KEY "zone"
// No real offset is this far from UTC (they span -12 h to +14 h).
#define ZONE_UNSET 0x7FFFFFFFu

static bool g_begun = false;
static int32_t g_zone = 0;

int32_t phoneSystemZone() {
  time_t now = time(nullptr);
  struct tm lt;
  localtime_r(&now, &lt);
  return (int32_t)lt.tm_gmtoff;
}

int32_t phoneClockZone() { return g_begun ? g_zone : phoneSystemZone(); }

static void storeZone(int32_t z) {
  Preferences p;
  if (!p.begin(PHONE_NS, false)) return;
  p.putUInt(ZONE_KEY, (uint32_t)z);
  p.end();
}

void phoneClockBegin() {
  uint32_t stored = ZONE_UNSET;
  Preferences p;
  if (p.begin(PHONE_NS, true)) {
    stored = p.getUInt(ZONE_KEY, ZONE_UNSET);
    p.end();
  }
  g_begun = true;
  if (stored == ZONE_UNSET) {
    g_zone = phoneSystemZone();
    storeZone(g_zone);
  } else {
    g_zone = (int32_t)stored;
  }
}

bool phoneClockAdopt(Pet &pet) {
  const int32_t now = phoneSystemZone();
  if (g_begun && now == g_zone) return false;
  const int32_t delta = now - phoneClockZone();
  g_begun = true;
  g_zone = now;
  // The creature's reference moves with the wall, so the next measured absence
  // starts from the same instant it did before. setClock() saves at once, and
  // the zone below lands in the same store: both or neither reach the disk.
  if (pet.lastSeenEpoch) pet.setClock((uint32_t)((int64_t)pet.lastSeenEpoch + delta));
  storeZone(now);
  return delta != 0;
}
