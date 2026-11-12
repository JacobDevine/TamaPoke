// The Android fork's groundwork (fork.h): the switch, its own save record, its
// own string table and the host hooks. See android/FEATURES_PLAN.md, Phase 0.
//
// Every check here guards a way the fork's state could leak into, or be lost
// from, upstream's -- which is the one thing this layer exists to prevent.
#include "Arduino.h"
#include "Preferences.h"
#include "fork.h"
#include "fork_i18n.h"
#include "ckpt.h"
#include "save.h"
#include <cstdio>
#include <cstring>
#include <vector>

#ifndef TAMA_NATIVE_TEST
uint32_t g_seed = 1;
FakeSerial Serial;
FakeESP ESP;
FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0;
volatile bool g_touchDown = false;
bool wasPressed = false;
uint32_t millis() { return 0; }
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }
void sfxPlay(uint8_t) {}
#endif

static int bad = 0;
static void ck(bool ok, const char *what) {
  printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) bad++;
}

// Each helper opens its own handle. Not a shared `Preferences store()`
// returned by value: the stub's handle is shared between copies, so the
// temporary's destructor would close the one being used.
#define OPEN Preferences p; p.begin("tamapoke", false)
static bool has(const char *k) { OPEN; return p.isKey(k); }
static void wipe() { OPEN; p.clear(); }
static std::vector<uint8_t> get(const char *k) {
  OPEN;
  std::vector<uint8_t> v(p.getBytesLength(k));
  if (!v.empty()) p.getBytes(k, v.data(), v.size());
  return v;
}
static void put(const char *k, const std::vector<uint8_t> &v) {
  OPEN;
  p.putBytes(k, v.data(), v.size());
}

// A record exactly as a build with a different body length would write it.
static std::vector<uint8_t> recordWithBody(size_t bodyLen, uint32_t created,
                                           uint32_t generation, uint32_t magic = FORK_MAGIC) {
  std::vector<uint8_t> b(CKPT_HDR + bodyLen + CKPT_CRC, 0xEE);   // junk past our fields
  if (bodyLen >= 4) ckptWr32(b.data() + CKPT_HDR + FORK_OFF_CREATED, created);
  ckptSeal(b.data(), CKPT_HDR + bodyLen, magic, FORK_VERSION, generation);
  return b;
}

static void testSwitchOff() {
  wipe();
  forkSetFeatures(false);
  ck(!forkFeatures(), "the switch is OFF by default (the board, the emulator, --shot)");
  Fork f;
  f.begin();
  f.st.createdEpoch = 123;
  ck(!f.save(1000), "save() refuses with the switch off");
  ck(!has("frkA") && !has("frkB"), "...and a board store never gains the fork's keys");

  // Off must also mean begin() reads nothing: a record that IS in the store
  // (a phone backup restored onto a board) is not picked up and acted on.
  put("frkA", recordWithBody(FORK_BODY_LEN, 777, 1));
  Fork g;
  g.begin();
  ck(!g.loaded() && g.st.createdEpoch == 0, "begin() with the switch off does not load a record that is there");
  // ...the companion that proves that record WAS loadable, so the line above
  // cannot pass by the record being broken.
  forkSetFeatures(true);
  Fork h;
  h.begin();
  ck(h.loaded() && h.st.createdEpoch == 777, "...while the same store loads it with the switch on");
  forkSetFeatures(false);
  wipe();
}

static void testRecord() {
  wipe();
  forkSetFeatures(true);
  Fork f;
  f.begin();
  ck(!f.loaded(), "a save with no fork record reads as not loaded (the set-up trigger)");
  ck(f.save(5000), "the first save writes");
  ck(f.st.createdEpoch == 5000, "the first save stamps when the record was created");
  ck(has("frkA") && !has("frkB"), "generation 1 goes to slot A");
  ck(f.save(6000), "the second save writes");
  ck(has("frkA") && has("frkB"), "generation 2 goes to slot B, keeping A as the fallback");
  ck(f.st.createdEpoch == 5000, "a later save keeps the creation stamp");

  Fork r;
  r.begin();
  ck(r.loaded() && r.generation() == 2 && r.st.createdEpoch == 5000,
     "a reload takes the newest slot");

  // A cut while slot A (generation 3) was being written: it lands torn. The
  // reader must fall back to B, whole, rather than to nothing.
  ck(r.save(7000) && r.generation() == 3, "generation 3 written to A");
  std::vector<uint8_t> a = get("frkA");
  a[CKPT_HDR] ^= 0x5A;   // the body changes under its crc
  put("frkA", a);
  Fork t;
  t.begin();
  ck(t.loaded() && t.generation() == 2, "a torn newest slot falls back to the previous whole one");
  // Saving from there must overwrite the torn slot, not the good one.
  ck(t.save(8000) && t.generation() == 3, "saving after the fallback continues the count");
  Fork u;
  u.begin();
  ck(u.loaded() && u.generation() == 3, "...into the torn slot, so the whole copy survived the write");

  // A record from somebody else's layout: same keys, different magic.
  wipe();
  put("frkA", recordWithBody(FORK_BODY_LEN, 999, 1, 0x12345678u));
  Fork m;
  m.begin();
  ck(!m.loaded(), "a record with another magic is refused, not misread");
  forkSetFeatures(false);
  wipe();
}

static void testPrefixRule() {
  forkSetFeatures(true);
  // A LATER build appended fields: the body is longer than this one knows.
  wipe();
  put("frkA", recordWithBody(FORK_BODY_LEN + 40, 4242, 1));
  Fork f;
  f.begin();
  ck(f.loaded() && f.st.createdEpoch == 4242,
     "a longer record from a later build loads the fields this build knows");
  // ...and one too long for the buffer would be refused, so the cap must leave
  // real room above today's body.
  ck(FORK_BLOB_CAP >= 4 * (CKPT_HDR + FORK_BODY_LEN + CKPT_CRC),
     "FORK_BLOB_CAP leaves room for records written by later builds");

  // An EARLIER build: a body shorter than today's. Its missing fields must come
  // back at their defaults rather than as whatever followed in the buffer.
  wipe();
  put("frkA", recordWithBody(0, 0, 1));
  Fork g;
  g.begin();
  ck(g.loaded() && g.st.createdEpoch == 0, "a shorter record leaves the newer fields at their defaults");

  // encode/decode agree with each other on every field...
  ForkState s;
  s.createdEpoch = 0xA1B2C3D4u;
  uint8_t buf[FORK_BLOB_CAP];
  uint16_t n = Fork::encode(s, buf, sizeof(buf), 9);
  ForkState back;
  ck(n == CKPT_HDR + FORK_BODY_LEN + CKPT_CRC && Fork::decode(buf, n, back) &&
         back.createdEpoch == s.createdEpoch, "encode and decode round-trip");
  // ...and against the documented OFFSET, not merely each other: two copies of
  // the same mistake would round-trip perfectly.
  ck(ckptRd32(buf + CKPT_HDR + FORK_OFF_CREATED) == 0xA1B2C3D4u,
     "createdEpoch sits at its documented body offset");
  forkSetFeatures(false);
  wipe();
}

static void testBackup() {
  wipe();
  forkSetFeatures(true);
  Fork f;
  f.begin();
  f.save(31337);
  f.save(31338);

  bool listed[2] = { false, false };
  for (uint16_t i = 0; i < SAVE_FIELD_COUNT; i++) {
    if (!strcmp(SAVE_FIELDS[i].key, "frkA")) listed[0] = SAVE_FIELDS[i].kind == SK_BYTES;
    if (!strcmp(SAVE_FIELDS[i].key, "frkB")) listed[1] = SAVE_FIELDS[i].kind == SK_BYTES;
  }
  ck(listed[0] && listed[1], "both fork slots are in SAVE_FIELDS");

  static uint8_t blob[SAVE_TRANSFER_MAX];
  size_t n = saveExport(blob, sizeof(blob));
  ck(n > 0, "EXPORT succeeds with a fork record present");
  wipe();
  ck(!has("frkA") && !has("frkB"), "(the store really is empty before the restore)");
  ck(saveImport(blob, n), "IMPORT accepts it");
  Fork r;
  r.begin();
  ck(r.loaded() && r.generation() == 2 && r.st.createdEpoch == 31337,
     "a backup restores the fork record, not only upstream's half of the save");
  forkSetFeatures(false);
  wipe();
}

static void testStrings() {
  static const char *LANG[] = { "ES", "EN", "FR", "DE", "IT", "PT" };
  const Lang was = gLang;
  int problems = 0;
  for (int l = 0; l < LANG_COUNT; l++) {
    gLang = (Lang)l;
    for (int s = 0; s < FS_COUNT; s++) {
      const char *v = FT((FStrId)s);
      if (!v || !*v) { printf("  EMPTY  %s index %d\n", LANG[l], s); problems++; continue; }
      for (const unsigned char *p = (const unsigned char *)v; *p; p++)
        if (*p > 0x7F) { printf("  NON-ASCII  %s index %d: \"%s\"\n", LANG[l], s, v); problems++; break; }
    }
  }
  ck(problems == 0, "every fork string is present and ASCII in all six languages");
  gLang = LANG_EN;
  ck(!strcmp(FT(FS_NEEDS_BOND), "NEEDS MORE BOND"), "the English row is the EN row (the table is in Lang order)");
  gLang = LANG_ES;
  ck(strcmp(FT(FS_NEEDS_BOND), "NEEDS MORE BOND") != 0, "...and Spanish did not silently fall back to it");
  ck(FT((FStrId)FS_COUNT) && !*FT((FStrId)FS_COUNT), "an out-of-range id gives an empty string, not a crash");
  gLang = was;
}

static uint32_t fakeSteps() { return 1234; }

static void testHooks() {
  ck(forkTakeSteps() == 0, "with no host hook, steps read as none (the board's answer)");
  gForkHost.takeSteps = fakeSteps;
  ck(forkTakeSteps() == 1234, "a host hook is what forkTakeSteps() reports");
  gForkHost.takeSteps = nullptr;
}

int main() {
  testSwitchOff();
  testRecord();
  testPrefixRule();
  testBackup();
  testStrings();
  testHooks();
  printf("%s fork_test: %d failure(s)\n", bad ? "FAIL" : "PASS", bad);
  return bad ? 1 : 0;
}
