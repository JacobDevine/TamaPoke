#pragma once
// "Your creature is hungry", while the app is closed.
//
// The phone treats going to the background as switching the board off, and the
// absence is applied on return by Pet::syncClock(): food falls 2 a minute while
// awake, to a floor of 15, and much more slowly with a floor of 30 while asleep.
// So the moment the creature WILL read as hungry is known when the app leaves,
// and one reminder is scheduled for it then and cancelled on return.
//
// The drain below restates syncClock()'s food path. phone_test holds it to the
// real thing: for every starting state it checks, the forecast minute is the
// first at which syncClock() itself leaves the creature hungry, and the minute
// before is not. Change the drain in pet.cpp and that test fails here.
//
// No Android dependency, so tools/emu/tests/phone_test.cpp links it as is.
#include <stdint.h>

class Pet;

// statusMsg() says S_HUNGRY below this. phone_test pins it to statusMsg().
#define PHONE_HUNGRY_BELOW 25
// Do not tell somebody who has only just put the phone down. A build with
// -Ptamapoke.remindNow=true skips the wait altogether, for testing (see
// android_main.cpp); this is the default everyone else gets.
#define REMIND_MIN_MINUTES 5

// Minutes of absence after which the creature first reads as hungry, as
// syncClock() would apply them. -1 if it never does within the catch-up limit:
// asleep (food's floor is 30), an egg, a farewell, the starter not yet chosen.
int32_t hungryAfterMinutes(const Pet &p);

// Whether a local wall-clock time falls in the night window, Pet::isNightHour()'s
// rule for an arbitrary time (it reads only the pet's last-seen time).
bool remindIsNight(uint32_t localEpoch);

// When to post the reminder, in minutes from now: the forecast, not sooner than
// REMIND_MIN_MINUTES, and moved out of the night window to its end -- nobody
// wants a creature's hunger at three in the morning. -1 for no reminder.
int32_t remindDelayMinutes(const Pet &p, uint32_t localNowEpoch);
