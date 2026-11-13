// The Android app's save backup and hunger reminder (android/app/src/main/cpp/
// phone_save.cpp, phone_remind.cpp), held to the firmware rather than to
// themselves:
//
//   - the .tpsave the phone writes is byte for byte what the console's EXPORT
//     prints, so a phone backup restores on a board through the installer and
//     the other way round;
//   - the verifier accepts what the firmware writes (its own export, and the
//     golden file the real firmware produced) and refuses every damaged copy,
//     and saveImport() accepts what it accepts;
//   - the care forecast is the first minute at which Pet::syncClock() ITSELF
//     leaves the creature in need of anything, the need it names is the one
//     statusMsg() names, and the threshold is statusMsg()'s for all four;
//   - the night rule is Pet::isNightHour()'s.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "save.h"
#include "i18n.h"
#include "phone_save.h"
#include "phone_remind.h"
#include "phone_clock.h"
#include "fork.h"
#include <cstdlib>
#include <ctime>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <unistd.h>

uint32_t g_seed = 7; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0; volatile bool g_touchDown = false;

static std::vector<std::string> gIn;
static size_t gAt = 0;
int FakeSerial::available() { return gAt < gIn.size() ? 1 : 0; }
String FakeSerial::readStringUntil(char) {
  if (gAt >= gIn.size()) return String("");
  return String(gIn[gAt++].c_str());
}
void FakeESP::restart() {}

void setup(); void render(); void handleSerial();
const char *statusMsg();
extern Pet pet;

static int bad = 0;
static void ck(bool ok, const char *w) { printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) bad++; }

// console_test's capture: the queued lines through handleSerial(), stdout kept
static std::string runConsole(const std::vector<std::string> &lines) {
  gIn = lines; gAt = 0;
  char path[] = "/tmp/tamapoke_phone_XXXXXX";
  int fd = mkstemp(path);
  if (fd < 0) return "";
  fflush(stdout);
  int saved = dup(fileno(stdout));
  dup2(fd, fileno(stdout));
  while (gAt < gIn.size()) handleSerial();
  fflush(stdout);
  dup2(saved, fileno(stdout));
  close(saved);
  lseek(fd, 0, SEEK_SET);
  std::string out;
  char b[4096]; ssize_t n;
  while ((n = read(fd, b, sizeof(b))) > 0) out.append(b, (size_t)n);
  close(fd);
  remove(path);
  return out;
}

static bool verifies(const std::string &t, std::string *why = nullptr) {
  std::string w;
  bool ok = tpsaveVerify(t, nullptr, &w);
  if (why) *why = w;
  return ok;
}

// lines of a text, without their newlines
static std::vector<std::string> linesOf(const std::string &t) {
  std::vector<std::string> v;
  std::istringstream in(t);
  std::string l;
  while (std::getline(in, l)) v.push_back(l);
  return v;
}
static std::string join(const std::vector<std::string> &v) {
  std::string s;
  for (auto &l : v) s += l + "\n";
  return s;
}

// --- the forecast against the real syncClock() ---
static const uint32_t T0 = 1790000000UL;   // past CLOCK_EPOCH_FLOOR
static void stage(uint8_t food, bool asleep, uint32_t age) {
  pet.ceremony = CER_NONE;
  pet.fullness = food; pet.joy = pet.energy = pet.hygiene = 90;
  pet.sleeping = asleep;
  pet.ageMinutes = age;
  pet.lastSeenEpoch = T0;
}
// Food, hygiene, energy, joy.
static void stage4(const uint8_t v[4], bool asleep, uint32_t age) {
  stage(v[0], asleep, age);
  pet.hygiene = v[1]; pet.energy = v[2]; pet.joy = v[3];
  pet.weight = 30;   // not CHUBBY, which statusMsg() would say over HAPPY
}
static void quietSync(uint32_t e) {
  fflush(stdout);
  const int saved = dup(fileno(stdout));
  FILE *null = fopen("/dev/null", "w");
  dup2(fileno(null), fileno(stdout));
  pet.syncClock(e);
  fflush(stdout);
  dup2(saved, fileno(stdout));
  close(saved);
  fclose(null);
}

int main() {
  setup();
  for (int i = 0; i < 4; i++) render();
  if (pet.awaitingStarter()) pet.chooseStarter(4);
  if (pet.isEgg()) pet.dbgHatchAs(6, false);
  pet.renameTrainer("ASH");   // persists

  // ---- the checksum, against its published check value ----
  ck(tpsaveCrc16((const uint8_t *)"123456789", 9) == 0x29B1, "CRC-16/CCITT-FALSE check value is 0x29B1");

  // ---- the phone's file IS the console's EXPORT ----
  std::string text, why;
  ck(tpsaveExport(&text, &why), "the phone exports the live save");
  if (!why.empty()) printf("      why: %s\n", why.c_str());
  const std::string console = runConsole({ "EXPORT" });
  ck(!text.empty() && text == console, "the phone's .tpsave is byte for byte the console's EXPORT");
  if (text != console) printf("      phone %zu bytes, console %zu bytes\n", text.size(), console.size());

  std::vector<uint8_t> blob;
  ck(tpsaveVerify(text, &blob, &why), "the export verifies");
  static uint8_t direct[SAVE_TRANSFER_MAX];
  const size_t dn = saveExport(direct, sizeof(direct));
  ck(blob.size() == dn && !memcmp(blob.data(), direct, dn), "...and decodes to saveExport()'s bytes");

  // ---- every damaged copy is refused ----
  {
    auto L = linesOf(text);
    ck(L.size() >= 4, "the export spans several data lines (so dropping one means something)");
    if (L.size() < 4) { printf("phone_test: no export to damage\n"); return 1; }
    auto drop = L; drop.erase(drop.begin() + 2);
    ck(!verifies(join(drop), &why) && why.find("declares") != std::string::npos,
       "a capture missing a data line is refused by its declared length");
    printf("      why: %s\n", why.c_str());
    auto headless = drop; headless.erase(headless.begin());
    ck(!verifies(join(headless), &why), "...even with no header to count against");
    printf("      why: %s\n", why.c_str());
    auto flip = L; flip[2][20] = flip[2][20] == '0' ? '1' : '0';   // past the header
    ck(!verifies(join(flip), &why) && why.find("checksum") != std::string::npos,
       "one changed digit fails the checksum");
    auto nocommit = L; nocommit.pop_back();
    ck(!verifies(join(nocommit)), "no commit line is refused");
    auto after = L; after.push_back("IMPORT 00");
    ck(!verifies(join(after)), "content after the commit is refused");
    auto junk = L; junk[1] = "IMPORT 0G";
    ck(!verifies(join(junk)), "a non-hex digit is refused");
    auto odd = L; odd[1] += "0";
    ck(!verifies(join(odd)), "an odd number of digits is refused");
    std::vector<uint8_t> b = blob;
    b[0] = 'X';
    const uint16_t c = tpsaveCrc16(b.data(), b.size() - 2);
    b[b.size() - 2] = (uint8_t)c; b[b.size() - 1] = (uint8_t)(c >> 8);
    ck(!verifies(tpsaveFormat(b.data(), b.size()), &why) && why.find("not a TamaPoke") != std::string::npos,
       "a well-formed blob that is not ours is refused by its magic");
    ck(!verifies(""), "an empty file is refused");
    ck(verifies("\r\n" + std::string(text).replace(text.find('\n'), 1, "\r\n")),
       "CRLF line ends and blank lines are fine");
  }

  // ---- what the firmware accepts ----
  ck(saveImport(blob.data(), blob.size()), "saveImport() accepts the phone's export");
  {
    std::ifstream f(std::string(TAMA_ROOT) + "/tools/fixtures/golden.tpsave");
    std::stringstream ss; ss << f.rdbuf();
    std::vector<uint8_t> g;
    ck(tpsaveVerify(ss.str(), &g, &why), "the golden file the real firmware wrote verifies");
    ck(!g.empty() && saveImport(g.data(), g.size()), "...and the firmware takes it back");
    // put the test's own save back for the rest
    ck(saveImport(blob.data(), blob.size()), "the phone's export restores over it");
    pet.begin();
  }

  ck(tpsaveFileName("A sh!", 1794402309UL) == "tamapoke-Ash-2026-11-11-13-05-09.tpsave",
     "the file name is the installer's, in local time");
  ck(tpsaveFileName("", 0) == "tamapoke-save-1970-01-01-00-00-00.tpsave", "no trainer name is 'save'");

  // ---- the threshold is statusMsg()'s, for each of the four needs ----
  if (pet.isEgg()) pet.dbgHatchAs(6, false);
  {
    struct { uint8_t Pet::*stat; StrId msg; const char *what; } needs[] = {
      { &Pet::fullness, S_HUNGRY, "food" }, { &Pet::hygiene, S_NEEDS_BATH, "hygiene" },
      { &Pet::energy, S_EXHAUSTED, "energy" }, { &Pet::joy, S_SAD, "joy" },
    };
    for (auto &n : needs) {
      char w[96];
      stage(100, false, 600);
      pet.bond = 0;
      pet.*n.stat = PHONE_NEED_BELOW - 1;
      snprintf(w, sizeof w, "statusMsg() names the need just under PHONE_NEED_BELOW (%s)", n.what);
      ck(!strcmp(statusMsg(), T(n.msg)), w);
      pet.*n.stat = PHONE_NEED_BELOW;
      snprintf(w, sizeof w, "...and not at it (%s)", n.what);
      ck(strcmp(statusMsg(), T(n.msg)) != 0, w);
    }
  }

  // ---- the forecast is syncClock()'s first minute of need ----
  {
    const uint8_t vals[] = { 100, 61, 30, 26, 25, 24, 15 };
    int some = 0, none = 0, wrong = 0, named[4] = { 0, 0, 0, 0 };
    const StrId msgs[] = { S_HUNGRY, S_NEEDS_BATH, S_EXHAUSTED, S_SAD };
    // What the creature says after `mins` away, as if awake: statusMsg() itself.
    auto sayAfter = [&](const uint8_t v[4], bool asleep, uint32_t age, uint32_t mins) {
      stage4(v, asleep, age);
      quietSync(T0 + mins * 60);
      pet.sleeping = false;
      pet.bond = 0;
      return statusMsg();
    };
    for (uint8_t f : vals) for (uint8_t h : vals) for (uint8_t e : vals) for (uint8_t j : vals)
      for (int asleep = 0; asleep < 2; asleep++)
        for (uint32_t age = 600; age < 602; age++) {
          const uint8_t v[4] = { f, h, e, j };
          stage4(v, asleep, age);
          StrId which = STR_COUNT;
          const int32_t m = needAfterMinutes(pet, &which);
          if (m > 0) {
            some++;
            for (int i = 0; i < 4; i++) named[i] += which == msgs[i];
            const char *at = sayAfter(v, asleep, age, (uint32_t)m);
            const char *before = m > 2 ? sayAfter(v, asleep, age, (uint32_t)m - 1) : T(S_HAPPY);
            if (which == STR_COUNT || strcmp(at, T(which)) || strcmp(before, T(S_HAPPY))) {
              if (wrong++ < 5)
                printf("      %u/%u/%u/%u asleep %d age %u: forecast %d '%s', syncClock says '%s' then '%s'\n",
                       f, h, e, j, asleep, age, m, which == STR_COUNT ? "?" : T(which), before, at);
            }
          } else {
            none++;
            const uint32_t spans[] = { 2, 60, 600, (uint32_t)CLOCK_MAX_OFFLINE_MINS };
            for (uint32_t sp : spans)
              if (strcmp(sayAfter(v, asleep, age, sp), T(S_HAPPY))) {
                if (wrong++ < 5)
                  printf("      %u/%u/%u/%u asleep %d: no forecast, but '%s' after %u min\n",
                         f, h, e, j, asleep, sayAfter(v, asleep, age, sp), sp);
                break;
              }
          }
        }
    printf("      %d states forecast a need (food %d, bath %d, energy %d, joy %d), %d never\n",
           some, named[0], named[1], named[2], named[3], none);
    ck(some > 100 && none > 100, "both outcomes are exercised");
    ck(named[0] && named[1] && named[2] && named[3], "every one of the four needs is forecast somewhere");
    ck(wrong == 0, "every forecast matches syncClock() and statusMsg(), awake and asleep");

    StrId which = STR_COUNT;
    stage(100, false, 600);
    ck(needAfterMinutes(pet, &which) == 38 && which == S_HUNGRY,
       "awake from full food, 90 elsewhere: hungry after 38 minutes (2 a minute)");
    const uint8_t dirty[4] = { 100, 40, 90, 90 };
    stage4(dirty, false, 600);
    ck(needAfterMinutes(pet, &which) == 16 && which == S_NEEDS_BATH,
       "hygiene 40: a bath after 16 minutes, before food runs out");
    const uint8_t tired[4] = { 100, 100, 30, 90 };
    stage4(tired, false, 600);
    ck(needAfterMinutes(pet, &which) == 6 && which == S_EXHAUSTED, "energy 30: exhausted after 6 minutes");
    const uint8_t bored[4] = { 100, 100, 100, 28 };
    stage4(bored, false, 600);
    ck(needAfterMinutes(pet, &which) == 4 && which == S_SAD, "joy 28: sad after 4 minutes");
    const uint8_t tie[4] = { 100, 50, 100, 50 };
    stage4(tie, false, 600);
    ck(needAfterMinutes(pet, &which) == 26 && which == S_NEEDS_BATH,
       "hygiene and joy run out together: the bath is named, statusMsg()'s order");
    stage(100, true, 600);
    ck(needAfterMinutes(pet) == -1, "asleep: never (the floors are 30, 35 and 45)");
    stage(100, false, 600);
    pet.ceremony = CER_FAREWELL;
    ck(needAfterMinutes(pet) == -1, "during a farewell: never");
    pet.ceremony = CER_NONE;
  }

  // ---- the night is isNightHour()'s ----
  {
    int disagree = 0, nights = 0;
    for (uint32_t t = T0; t < T0 + 2 * 86400; t += 1800) {
      pet.lastSeenEpoch = t;
      if (remindIsNight(t) != pet.isNightHour()) disagree++;
      nights += remindIsNight(t);
    }
    ck(disagree == 0 && nights > 0, "remindIsNight() agrees with Pet::isNightHour() over two days");
    const uint32_t day = (T0 / 86400) * 86400;
    stage(100, false, 600);
    ck(remindDelayMinutes(pet, day + 12 * 3600) == 38, "at noon the reminder comes when it is due");
    ck(remindDelayMinutes(pet, day + 23 * 3600 + 50 * 60) == 6 * 60 + 10,
       "due at 00:28, it waits for the end of the night (06:00)");
    stage(PHONE_NEED_BELOW - 1, false, 600);
    ck(remindDelayMinutes(pet, day + 12 * 3600) == REMIND_MIN_MINUTES,
       "already hungry: not before REMIND_MIN_MINUTES");
    StrId which = STR_COUNT;
    const uint8_t dirty[4] = { 100, 40, 90, 90 };
    stage4(dirty, false, 600);
    ck(remindDelayMinutes(pet, day + 12 * 3600, &which) == 16 && which == S_NEEDS_BATH,
       "the delay carries the need it is for");
    stage(100, true, 600);
    ck(remindDelayMinutes(pet, day + 12 * 3600) == -1, "asleep: no reminder");
  }

  // ---- an expedition: the forecast skips the minutes it is out ----
  // syncClock() leaves those minutes undrained (fork.h section 8), and the
  // forecast has to skip the same ones, or the reminder fires while the
  // creature is still out and comes back fine. The same sweep as above, awake,
  // with a trip of several lengths running when the app leaves.
  {
    forkSetFeatures(true);
    const uint8_t vals[] = { 100, 61, 26, 25, 15 };
    const uint32_t trips[] = { 1, 15, 30, 60 };
    int wrong = 0, some = 0;
    auto out = [&](uint32_t tripMin) {
      forkData.st.expKey = forkCreatureKey(pet);
      forkData.st.expEnd = T0 + tripMin * 60;
      forkData.st.expSite = 0; forkData.st.expLen = 0;
    };
    auto sayAfter = [&](const uint8_t v[4], uint32_t tripMin, uint32_t mins) {
      stage4(v, false, 600);
      out(tripMin);
      quietSync(T0 + mins * 60);
      forkData.st.expKey = 0;   // back: what it says now, not "out exploring"
      pet.bond = 0;
      return statusMsg();
    };
    for (uint32_t trip : trips)
      for (uint8_t f : vals) for (uint8_t h : vals) for (uint8_t e : vals) for (uint8_t j : vals) {
        const uint8_t v[4] = { f, h, e, j };
        stage4(v, false, 600);
        out(trip);
        StrId which = STR_COUNT;
        const int32_t m = needAfterMinutes(pet, &which);
        if (m <= 0) { wrong++; continue; }   // awake, every need reaches 15: always some minute
        some++;
        const char *at = sayAfter(v, trip, (uint32_t)m);
        const char *before = m > 2 ? sayAfter(v, trip, (uint32_t)m - 1) : T(S_HAPPY);
        if (which == STR_COUNT || strcmp(at, T(which)) || strcmp(before, T(S_HAPPY))) {
          if (wrong++ < 5)
            printf("      %u/%u/%u/%u trip %u: forecast %d '%s', syncClock says '%s' then '%s'\n",
                   f, h, e, j, trip, m, which == STR_COUNT ? "?" : T(which), before, at);
        }
      }
    ck(some > 1000 && wrong == 0, "with a trip running, every forecast still matches syncClock()");
    StrId which = STR_COUNT;
    stage(100, false, 600);
    out(30);
    ck(needAfterMinutes(pet, &which) == 38 + 30 && which == S_HUNGRY,
       "full food and a 30 minute trip: hungry 30 minutes later than without (68)");
    // and the trip's own reminder: when it is due, moved out of the night
    pet.lastSeenEpoch = T0;
    const uint32_t day = T0 - T0 % 86400;
    forkData.st.expEnd = day + 12 * 3600 + 25 * 60;
    ck(expRemindDelayMinutes(pet, day + 12 * 3600) == 25, "back at 12:25, left at noon: reminded in 25 minutes");
    forkData.st.expEnd = day + 12 * 3600 + 30;
    ck(expRemindDelayMinutes(pet, day + 12 * 3600) == 1, "a part minute is a minute, never zero");
    forkData.st.expEnd = day + 23 * 3600 + 59 * 60 + 60 * 60;
    ck(expRemindDelayMinutes(pet, day + 23 * 3600 + 59 * 60) == 6 * 60 + 1,
       "back at 00:59: not until the night ends (06:00)");
    forkData.st.expKey = 0;
    ck(expRemindDelayMinutes(pet, day + 12 * 3600) == -1, "not away: no reminder");
    forkSetFeatures(false);
    out(30);
    ck(expRemindDelayMinutes(pet, day + 12 * 3600) == -1, "the switch off: never");
    stage(100, false, 600);
    ck(needAfterMinutes(pet, &which) == 38, "the switch off: a trip in the record changes no forecast");
    forkData.st.expKey = 0;
  }

  // ---- the sick reminder (fork.h section 6) ----
  {
    const uint32_t day = (T0 / 86400) * 86400;
    stage(100, false, 600);
    forkSetFeatures(true);
    forkData.st.sickKey = 0;
    ck(sickRemindDelayMinutes(pet, day + 12 * 3600) == -1, "not sick: no sick reminder");
    forkData.st.sickKey = forkCreatureKey(pet);
    ck(sickRemindDelayMinutes(pet, day + 12 * 3600) == SICK_REMIND_MINUTES,
       "sick at noon: reminded SICK_REMIND_MINUTES later");
    ck(sickRemindDelayMinutes(pet, day + 23 * 3600 + 50 * 60) == 6 * 60 + 10,
       "sick at 23:50: not until the night ends (06:00)");
    forkSetFeatures(false);
    ck(sickRemindDelayMinutes(pet, day + 12 * 3600) == -1, "the switch off: never");
    forkData.st.sickKey = 0;
  }

  // ---- a change of time zone is not time passing ----
  // The phone's clock is UTC + phoneClockZone(), as host_impl.cpp reads it under
  // TAMA_HOST_WALLCLOCK; here the UTC side is a plain number, the zone the real
  // one, moved with TZ.
  {
    auto zoneTo = [](const char *tz) { setenv("TZ", tz, 1); tzset(); };
    auto quiet = [](auto fn) {
      fflush(stdout);
      const int saved = dup(fileno(stdout));
      FILE *null = fopen("/dev/null", "w");
      dup2(fileno(null), fileno(stdout));
      fn();
      fflush(stdout);
      dup2(saved, fileno(stdout));
      close(saved);
      fclose(null);
    };
    const uint32_t U = T0;   // UTC seconds
    { Preferences p; p.begin("phone", false); p.clear(); p.end(); }

    // At home in London, in winter; the store has never held a zone.
    zoneTo("UTC0");
    phoneClockBegin();
    ck(phoneClockZone() == 0, "a store with no zone takes the system's");
    stage(100, false, 600);
    pet.lastSeenEpoch = U + phoneClockZone();

    // Away three hours, landing in Brisbane (UTC+10).
    zoneTo("AEST-10");
    ck(phoneSystemZone() == 36000, "the system zone follows TZ (+10 h)");
    phoneClockBegin();       // a cold start: the zone comes from the store
    ck(phoneClockZone() == 0, "...the clock keeps the zone the save was written in");
    const uint32_t age0 = pet.ageMinutes;
    quiet([&] { pet.syncClock(U + 3 * 3600 + phoneClockZone()); });
    ck(pet.ageMinutes - age0 == 180, "three hours away, flying 10 h east: three hours of age");
    ck(phoneClockAdopt(pet), "...then the new zone is adopted");
    ck(pet.lastSeenEpoch == U + 3 * 3600 + 36000, "...and the last-seen time is Brisbane's wall clock");
    ck(pet.lastSeenEpoch % 86400 / 3600 == (T0 / 3600 + 3 + 10) % 24, "...so the hour is local");
    ck(!phoneClockAdopt(pet), "the same zone again changes nothing");

    // The zone persists with the save: a cold start in Brisbane stays there.
    phoneClockBegin();
    ck(phoneClockZone() == 36000, "the adopted zone is stored");

    // An hour later, flying 18 h west to Los Angeles (UTC-8).
    zoneTo("PST8");
    const uint32_t age1 = pet.ageMinutes;
    quiet([&] { pet.syncClock(U + 4 * 3600 + phoneClockZone()); });
    ck(pet.ageMinutes - age1 == 60, "an hour away, flying 18 h west: an hour of age, not a refusal");
    phoneClockAdopt(pet);
    ck(pet.lastSeenEpoch == U + 4 * 3600 - 8 * 3600, "...and the wall is Los Angeles'");

    // The old behaviour, measured with the system's zone read fresh, ages the
    // flight east: the check above can tell the two apart.
    zoneTo("UTC0");
    phoneClockAdopt(pet);
    stage(100, false, 600);
    pet.lastSeenEpoch = U;
    zoneTo("AEST-10");
    const uint32_t age2 = pet.ageMinutes;
    quiet([&] { pet.syncClock(U + 3 * 3600 + phoneSystemZone()); });
    ck(pet.ageMinutes - age2 == 13 * 60, "(control: reading the zone fresh ages 13 h)");
    zoneTo("UTC0");
    phoneClockAdopt(pet);
  }

  printf(bad ? "phone_test: %d FAILED\n" : "phone_test: all passed\n", bad);
  return bad ? 1 : 0;
}
