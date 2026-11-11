#include "phone_remind.h"
#include "pet.h"

// pet.cpp's dropTo(), which is file-static there.
static uint8_t dropTo(uint8_t v, uint8_t d, uint8_t fl) {
  if (v <= fl) return v;
  return (v - fl > d) ? v - d : fl;
}

int32_t hungryAfterMinutes(const Pet &p) {
  if (p.isEgg() || p.ceremony != CER_NONE || p.awaitingStarter()) return -1;
  // syncClock() applies nothing to an absence under 2 minutes
  uint8_t food = p.fullness;
  uint32_t age = p.ageMinutes;
  for (uint32_t m = 1; m <= CLOCK_MAX_OFFLINE_MINS; m++) {
    age++;
    if (p.sleeping) {
      if (age % 2 == 0) food = dropTo(food, 1, 30);
    } else {
      food = dropTo(food, 2, 15);
    }
    if (m >= 2 && food < PHONE_HUNGRY_BELOW) return (int32_t)m;
    // asleep or awake, food stops at its floor: once there, nothing changes
    if (m >= 2 && food <= (p.sleeping ? 30 : 15)) return -1;
  }
  return -1;
}

bool remindIsNight(uint32_t localEpoch) {
  const int h = (int)((localEpoch / 3600) % 24);
  if (NIGHT_START < NIGHT_END) return h >= NIGHT_START && h < NIGHT_END;
  return h >= NIGHT_START || h < NIGHT_END;
}

int32_t remindDelayMinutes(const Pet &p, uint32_t localNowEpoch) {
  int32_t m = hungryAfterMinutes(p);
  if (m < 0) return -1;
  if (m < REMIND_MIN_MINUTES) m = REMIND_MIN_MINUTES;
  // step to the end of the night a minute at a time; at most a day's worth
  for (int i = 0; i < 24 * 60 && remindIsNight(localNowEpoch + (uint32_t)m * 60); i++) m++;
  return m;
}
