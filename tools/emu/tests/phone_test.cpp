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
//   - the hunger forecast is the first minute at which Pet::syncClock() ITSELF
//     leaves the creature hungry, and the threshold is statusMsg()'s;
//   - the night rule is Pet::isNightHour()'s.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "save.h"
#include "i18n.h"
#include "phone_save.h"
#include "phone_remind.h"
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
static uint8_t foodAfter(uint8_t food, bool asleep, uint32_t age, uint32_t mins) {
  stage(food, asleep, age);
  fflush(stdout);   // syncClock() reports every absence; 150 of them say nothing here
  const int saved = dup(fileno(stdout));
  FILE *null = fopen("/dev/null", "w");
  dup2(fileno(null), fileno(stdout));
  pet.syncClock(T0 + mins * 60);
  fflush(stdout);
  dup2(saved, fileno(stdout));
  close(saved);
  fclose(null);
  return pet.fullness;
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

  // ---- the hunger threshold is statusMsg()'s ----
  if (pet.isEgg()) pet.dbgHatchAs(6, false);
  stage(PHONE_HUNGRY_BELOW - 1, false, 600);
  pet.bond = 0;
  ck(!strcmp(statusMsg(), T(S_HUNGRY)), "statusMsg() is hungry just under PHONE_HUNGRY_BELOW");
  stage(PHONE_HUNGRY_BELOW, false, 600);
  ck(strcmp(statusMsg(), T(S_HUNGRY)) != 0, "...and not at it");

  // ---- the forecast is syncClock()'s first hungry minute ----
  {
    const uint8_t foods[] = { 100, 99, 80, 61, 40, 31, 30, 27, 26, 25, 24, 20, 16, 15, 10, 0 };
    int some = 0, none = 0, wrong = 0;
    for (uint8_t f : foods)
      for (int asleep = 0; asleep < 2; asleep++)
        for (uint32_t age = 600; age < 602; age++) {
          stage(f, asleep, age);
          const int32_t m = hungryAfterMinutes(pet);
          if (m > 0) {
            some++;
            const uint8_t at = foodAfter(f, asleep, age, (uint32_t)m);
            const uint8_t before = m > 2 ? foodAfter(f, asleep, age, (uint32_t)m - 1) : 255;
            if (at >= PHONE_HUNGRY_BELOW || before < PHONE_HUNGRY_BELOW) {
              wrong++;
              printf("      food %u asleep %d age %u: forecast %d, syncClock gives %u then %u\n",
                     f, asleep, age, m, before, at);
            }
          } else {
            none++;
            const uint32_t spans[] = { 2, 60, 600, 6000, (uint32_t)CLOCK_MAX_OFFLINE_MINS };
            for (uint32_t s : spans)
              if (foodAfter(f, asleep, age, s) < PHONE_HUNGRY_BELOW) {
                wrong++;
                printf("      food %u asleep %d: no forecast, but hungry after %u min\n", f, asleep, s);
              }
          }
        }
    printf("      %d states forecast hungry, %d never\n", some, none);
    ck(some > 10 && none > 10, "both outcomes are exercised");
    ck(wrong == 0, "every forecast matches syncClock(), awake and asleep");
    stage(100, false, 600);
    ck(hungryAfterMinutes(pet) == 38, "awake from full: hungry after 38 minutes (2 a minute)");
    stage(100, true, 600);
    ck(hungryAfterMinutes(pet) == -1, "asleep: never (food's floor is 30)");
    stage(100, false, 600);
    pet.ceremony = CER_FAREWELL;
    ck(hungryAfterMinutes(pet) == -1, "during a farewell: never");
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
    stage(PHONE_HUNGRY_BELOW - 1, false, 600);
    ck(remindDelayMinutes(pet, day + 12 * 3600) == REMIND_MIN_MINUTES,
       "already hungry: not before REMIND_MIN_MINUTES");
    stage(100, true, 600);
    ck(remindDelayMinutes(pet, day + 12 * 3600) == -1, "asleep: no reminder");
  }

  printf(bad ? "phone_test: %d FAILED\n" : "phone_test: all passed\n", bad);
  return bad ? 1 : 0;
}
