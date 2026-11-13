#pragma once
// "Your creature needs you", while the app is closed.
//
// The phone treats going to the background as switching the board off, and the
// absence is applied on return by Pet::syncClock(): awake, food falls 2 a minute
// and hygiene, energy and joy 1, all to a floor of 15; asleep they fall much more
// slowly to floors of 30 (food), 35 (joy) and 45 (hygiene) while energy rises.
// So the moment the creature WILL first need something is known when the app
// leaves, and one reminder is scheduled for it then and cancelled on return.
// It says what the creature would say: statusMsg()'s line for that need.
//
// The drain below restates syncClock()'s. phone_test holds it to the real
// thing: for every starting state it checks, the forecast minute is the first
// at which syncClock() itself leaves the creature in need, the minute before is
// not, and the need named is the one statusMsg() names. Change the drain in
// pet.cpp and that test fails here.
//
// No Android dependency, so tools/emu/tests/phone_test.cpp links it as is.
#include <stdint.h>
#include "i18n.h"

class Pet;

// statusMsg() names a need below this, for each of the four. phone_test pins
// it to statusMsg().
#define PHONE_NEED_BELOW 25
// Do not tell somebody who has only just put the phone down. A build with
// -Ptamapoke.remindNow=true skips the wait altogether, for testing (see
// android_main.cpp); this is the default everyone else gets.
#define REMIND_MIN_MINUTES 5

// Minutes of absence after which the creature first needs something, as
// syncClock() would apply them, and in *which the line statusMsg() would show
// for it (S_HUNGRY, S_NEEDS_BATH, S_EXHAUSTED or S_SAD). -1 if it never does
// within the catch-up limit: asleep with everything over the line, an egg, a
// farewell, the starter not yet chosen.
int32_t needAfterMinutes(const Pet &p, StrId *which = nullptr);

// Whether a local wall-clock time falls in the night window, Pet::isNightHour()'s
// rule for an arbitrary time (it reads only the pet's last-seen time).
bool remindIsNight(uint32_t localEpoch);

// When to post the reminder, in minutes from now: the forecast, not sooner than
// REMIND_MIN_MINUTES, and moved out of the night window to its end -- nobody
// wants a creature's needs at three in the morning. -1 for no reminder.
int32_t remindDelayMinutes(const Pet &p, uint32_t localNowEpoch, StrId *which = nullptr);

// --- the sick reminder (fork.h section 6) ---
//
// Its own notification, beside the care one. Sickness does not move while the
// app is closed (nothing in syncClock() touches it), so there is no forecast to
// make: a creature that leaves sick is still sick, and one reminder is posted
// SICK_REMIND_MINUTES later, moved out of the night like the care reminder.
#define SICK_REMIND_MINUTES 30

// Minutes from now, or -1 when the creature is not sick (or the fork's
// features are off).
int32_t sickRemindDelayMinutes(const Pet &p, uint32_t localNowEpoch);

// --- the expedition reminder (fork.h section 8) ---
//
// Its own notification, posted when the creature is due back from an
// expedition, moved out of the night like the others. The trip finishes
// whether or not the app is open (it is in the fork record), so this is only
// the news of it.
// Minutes from now, at least 1, or -1 when it is not away.
int32_t expRemindDelayMinutes(const Pet &p, uint32_t localNowEpoch);
