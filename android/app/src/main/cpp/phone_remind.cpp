#include "phone_remind.h"
#include "pet.h"
#include "fork.h"

// pet.cpp's dropTo(), which is file-static there.
static uint8_t dropTo(uint8_t v, uint8_t d, uint8_t fl) {
  if (v <= fl) return v;
  return (v - fl > d) ? v - d : fl;
}

// statusMsg()'s order among the four needs: the first one under the line is
// what the creature says, so it is what the reminder says.
static StrId needMsg(uint8_t food, uint8_t hyg, uint8_t ene, uint8_t joy) {
  if (food < PHONE_NEED_BELOW) return S_HUNGRY;
  if (hyg < PHONE_NEED_BELOW) return S_NEEDS_BATH;
  if (ene < PHONE_NEED_BELOW) return S_EXHAUSTED;
  if (joy < PHONE_NEED_BELOW) return S_SAD;
  return STR_COUNT;
}

int32_t needAfterMinutes(const Pet &p, StrId *which) {
  if (which) *which = STR_COUNT;
  if (p.isEgg() || p.ceremony != CER_NONE || p.awaitingStarter()) return -1;
  uint8_t food = p.fullness, hyg = p.hygiene, ene = p.energy, joy = p.joy;
  uint32_t age = p.ageMinutes;
  // The minutes spent out on an expedition drain nothing (fork.h section 8):
  // syncClock() skips the same ones, measured from the same lastSeenEpoch.
  const uint32_t away = forkExpMinutesLeft(p, p.lastSeenEpoch);
  // syncClock() applies nothing to an absence under 2 minutes
  for (uint32_t m = 1; m <= CLOCK_MAX_OFFLINE_MINS; m++) {
    age++;
    if (m <= away) {
      // nothing moves
    } else if (p.sleeping) {
      ene = ene > 94 ? 100 : ene + 6;
      if (age % 2 == 0) {
        food = dropTo(food, 1, 30);
        joy = dropTo(joy, 1, 35);
      }
      if (age % 3 == 0) hyg = dropTo(hyg, 1, 45);
    } else {
      food = dropTo(food, 2, 15);
      ene = dropTo(ene, 1, 15);
      hyg = dropTo(hyg, 1, 15);
      joy = dropTo(joy, 1, 15);
    }
    if (m < 2) continue;
    const StrId s = needMsg(food, hyg, ene, joy);
    if (s != STR_COUNT) {
      if (which) *which = s;
      return (int32_t)m;
    }
    // Asleep, the floors (30, 35, 45) are all over the line and energy only
    // rises, so a creature not in need by now never will be. Awake, every stat
    // reaches 15, under the line, so the loop always ends above.
    if (p.sleeping) return -1;
  }
  return -1;
}

bool remindIsNight(uint32_t localEpoch) {
  const int h = (int)((localEpoch / 3600) % 24);
  if (NIGHT_START < NIGHT_END) return h >= NIGHT_START && h < NIGHT_END;
  return h >= NIGHT_START || h < NIGHT_END;
}

// Moves a delay out of the night window, to its end. A minute at a time; at
// most a day's worth.
static int32_t pastNight(int32_t m, uint32_t localNowEpoch) {
  for (int i = 0; i < 24 * 60 && remindIsNight(localNowEpoch + (uint32_t)m * 60); i++) m++;
  return m;
}

int32_t remindDelayMinutes(const Pet &p, uint32_t localNowEpoch, StrId *which) {
  int32_t m = needAfterMinutes(p, which);
  if (m < 0) return -1;
  if (m < REMIND_MIN_MINUTES) m = REMIND_MIN_MINUTES;
  return pastNight(m, localNowEpoch);
}

int32_t sickRemindDelayMinutes(const Pet &p, uint32_t localNowEpoch) {
  if (!forkSick(p)) return -1;
  return pastNight(SICK_REMIND_MINUTES, localNowEpoch);
}

int32_t expRemindDelayMinutes(const Pet &p, uint32_t localNowEpoch) {
  if (!forkAway(p)) return -1;
  const uint32_t secs = forkExpSecondsLeft(localNowEpoch);
  int32_t m = (int32_t)((secs + 59) / 60);
  if (m < 1) m = 1;
  return pastNight(m, localNowEpoch);
}
