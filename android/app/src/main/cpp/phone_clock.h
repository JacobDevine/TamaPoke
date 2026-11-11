#pragma once
// The phone's clock across time zones.
//
// The firmware's epoch is LOCAL wall-clock seconds, not UTC (see the
// TAMA_HOST_WALLCLOCK block in tools/emu/host_impl.cpp): the board's RTC holds
// the time on the player's wall, and the night window and the clock read the
// hour as epoch % 86400. Time away is a difference of two such readings. So
// when the phone's zone changes between them, the change reads as time passing:
// fly 10 hours east and the creature ages 10 hours, fly west and the absence is
// refused as the clock going backwards. Daylight saving does the same by an
// hour, twice a year.
//
// So the phone does not read its zone fresh each time. It keeps the zone it
// last saved in (in its own NVS namespace, in the same file as the save, so the
// two are written together), measures every absence in that zone, and only then
// adopts the new one -- moving the last-seen time by the same amount, so the
// jump is never counted. The hour shown, and the night window, follow the new
// zone from that moment.
//
// No Android dependency: tools/emu/tests/phone_test.cpp links it as is.
#include <stdint.h>

class Pet;

// The phone's own UTC offset right now, in seconds east of UTC.
int32_t phoneSystemZone();

// The offset the firmware's clock is read in. Until phoneClockBegin(), the
// system's.
int32_t phoneClockZone();

// After the store is loaded and BEFORE setup() measures the absence: take the
// zone the save was last written in. A store with none (a first run, or one
// from before this existed) takes the system's, which is how it always behaved.
void phoneClockBegin();

// After an absence has been measured, and whenever the zone may have changed:
// switch to the system's zone, moving the creature's last-seen time by the
// difference without aging it. True if the zone changed.
bool phoneClockAdopt(Pet &p);
