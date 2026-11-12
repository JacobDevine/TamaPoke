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
#include "pet.h"
#include "party.h"
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
static uint32_t gNowMs = 0;
uint32_t millis() { return gNowMs; }
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

// --- natures -------------------------------------------------------------------

// A hatched, awake creature with every need full, nothing else going on.
static void freshPet(Pet &p, uint8_t a, uint8_t d, uint8_t sp, uint8_t h, bool shiny = false) {
  p.speciesId = 1;
  p.ivAtk = a; p.ivDef = d; p.ivSpe = sp; p.ivHp = h;
  p.shiny = shiny;
  p.fullness = p.joy = p.energy = p.hygiene = 100;
  p.poops = 0;
  p.weight = 0;
  p.sleeping = false;
  p.bond = 0;
  p.lastSeenEpoch = 0;
}

// The first IV set (in a fixed order) whose nature is `want`.
static bool ivsFor(uint8_t want, uint8_t out[4]) {
  for (int a = 8; a < 32; a++) for (int d = 8; d < 32; d++)
    for (int sp = 8; sp < 32; sp++) for (int h = 8; h < 32; h++)
      if (natureFromIVs(a, d, sp, h, false) == want) {
        out[0] = a; out[1] = d; out[2] = sp; out[3] = h;
        return true;
      }
  return false;
}

static void testNatureHash() {
  // Every IV a hatch can roll (8..31, rollIV()), both shiny states.
  uint32_t n[NATURE_COUNT] = { 0 }, total = 0;
  uint32_t byPers[Pet::PERSONALITY_COUNT][NATURE_COUNT] = { { 0 } };
  uint32_t persTotal[Pet::PERSONALITY_COUNT] = { 0 };
  bool inRange = true;
  Pet p;
  p.speciesId = 1;
  for (int sh = 0; sh < 2; sh++)
    for (int a = 8; a < 32; a++) for (int d = 8; d < 32; d++)
      for (int sp = 8; sp < 32; sp++) for (int h = 8; h < 32; h++) {
        uint8_t v = natureFromIVs(a, d, sp, h, sh);
        if (v >= NATURE_COUNT) { inRange = false; continue; }
        n[v]++; total++;
        p.ivAtk = a; p.ivDef = d; p.ivSpe = sp; p.ivHp = h;
        uint8_t pers = p.personality();
        byPers[pers][v]++; persTotal[pers]++;
      }
  ck(inRange, "a nature is always one of the five");
  bool even = true;
  for (int i = 0; i < NATURE_COUNT; i++) {
    double f = (double)n[i] / total;
    printf("  nature %d: %.3f\n", i, f);
    if (f < 0.19 || f > 0.21) even = false;
  }
  ck(even, "each nature comes up 19-21% of the time over every IV a hatch can roll");
  // Independent of upstream's trait: a hash of the IVs, not a reading of them.
  // Were it the same reading, a trait would pin its nature.
  bool indep = true;
  for (int t = 0; t < Pet::PERSONALITY_COUNT; t++) {
    if (persTotal[t] < 500) continue;
    for (int i = 0; i < NATURE_COUNT; i++) {
      double f = (double)byPers[t][i] / persTotal[t];
      if (f < 0.15 || f > 0.25) {
        printf("  trait %d -> nature %d at %.3f\n", t, i, f);
        indep = false;
      }
    }
  }
  ck(indep, "every trait still spreads over all five natures (15-25% each)");

  // PINNED. The hash is effectively part of every save: change it and every
  // creature already raised changes nature. These values are what this build
  // gives; a failure here means the mapping moved, not that the test is stale.
  ck(natureFromIVs(16, 16, 16, 16, false) == NATURE_EASYGOING &&
     natureFromIVs(31, 31, 31, 31, false) == NATURE_GLUTTON &&
     natureFromIVs(8, 20, 31, 12, false) == NATURE_TIDY &&
     natureFromIVs(8, 20, 31, 12, true) == NATURE_LAZY,
     "the IV -> nature mapping is pinned");

  // The live pet and the banked copy of it agree: toPartyMon() is the route
  // into the party, the box and back.
  bool agree = true;
  for (int i = 0; i < 200; i++) {
    Pet q;
    freshPet(q, 8 + (i * 7) % 24, 8 + (i * 11) % 24, 8 + (i * 13) % 24, 8 + (i * 5) % 24, i & 1);
    PartyMon m = q.toPartyMon();
    if (monNature(m) != q.nature()) agree = false;
  }
  ck(agree, "a creature's nature is the same live and banked");
  Pet egg;
  ck(egg.isEgg() && egg.nature() == NATURE_NONE, "an egg has no nature yet");
  PartyMon empty;
  ck(monNature(empty) == NATURE_NONE, "an empty slot has no nature");
  ck(natureNeed(NATURE_EASYGOING) == NEED_NONE && natureNeed(NATURE_GLUTTON) == NEED_FOOD &&
     natureNeed(NATURE_PLAYFUL) == NEED_JOY && natureNeed(NATURE_TIDY) == NEED_HYGIENE &&
     natureNeed(NATURE_LAZY) == NEED_ENERGY, "each nature names its need");
  ck(!*natureName(NATURE_NONE), "no creature, no name");
  const Lang was = gLang;
  bool distinct = true;
  for (int l = 0; l < LANG_COUNT; l++) {
    gLang = (Lang)l;
    for (int i = 0; i < NATURE_COUNT; i++)
      for (int j = i + 1; j < NATURE_COUNT; j++)
        if (!strcmp(natureName(i), natureName(j))) distinct = false;
  }
  gLang = was;
  ck(distinct, "the five names differ in every language");
}

// Runs `mins` offline minutes through syncClock(), the catch-up path.
static void offline(Pet &p, uint32_t mins) {
  const uint32_t base = 1800000000u;   // a real time, past CLOCK_EPOCH_FLOOR
  p.lastSeenEpoch = base;
  p.syncClock(base + mins * 60);
}

// Runs `mins` live minutes through update() -> tick(), the awake path.
static void live(Pet &p, uint32_t mins) {
  g_seed = 7;   // tick() rolls poops; both runs of a comparison roll the same
  gNowMs = 0;
  p.update(0);
  gNowMs = mins * PET_TICK_MS;
  p.update(gNowMs);
}

static void testNatureDrain() {
  uint8_t g[4], l[4], e[4];
  ck(ivsFor(NATURE_GLUTTON, g) && ivsFor(NATURE_LAZY, l) && ivsFor(NATURE_EASYGOING, e),
     "(IVs found for a glutton, a lazy one and an easygoing one)");

  // Offline, 20 minutes, the glutton: 2 extra food, nothing else.
  Pet off, on;
  forkSetFeatures(false);
  freshPet(off, g[0], g[1], g[2], g[3]);
  offline(off, 20);
  forkSetFeatures(true);
  freshPet(on, g[0], g[1], g[2], g[3]);
  offline(on, 20);
  printf("  offline 20 min, glutton: food %u off / %u on\n", off.fullness, on.fullness);
  ck(off.fullness - on.fullness == 20 / NATURE_FOOD_EVERY,
     "offline, a glutton loses one more food every NATURE_FOOD_EVERY minutes");
  ck(off.joy == on.joy && off.energy == on.energy && off.hygiene == on.hygiene,
     "...and only food");

  // Offline keeps its floor of 15 however long it was.
  Pet longOn;
  freshPet(longOn, g[0], g[1], g[2], g[3]);
  offline(longOn, 600);
  ck(longOn.fullness == 15, "offline, the extra drain stops at the catch-up's floor of 15");

  // The switch off is upstream's game: the nature does nothing on a board.
  // (The run above already compared off against on; this pins off as
  // upstream's numbers rather than merely "different from on".)
  ck(off.fullness == 100 - 2 * 20, "with the switch off, food drains at upstream's 2 a minute");

  // Live, 40 minutes, the lazy one: 10 extra energy, food untouched. Energy,
  // because nothing random touches it awake -- hygiene follows the poops.
  Pet loff, lon;
  forkSetFeatures(false);
  freshPet(loff, l[0], l[1], l[2], l[3]);
  live(loff, 40);
  forkSetFeatures(true);
  freshPet(lon, l[0], l[1], l[2], l[3]);
  live(lon, 40);
  printf("  live 40 min, lazy: energy %u off / %u on\n", loff.energy, lon.energy);
  ck(loff.energy == 100 - 40, "(with the switch off, energy drains upstream's 1 a minute)");
  ck(loff.energy - lon.energy == 40 / NATURE_OTHER_EVERY,
     "awake, a lazy one loses one more energy every NATURE_OTHER_EVERY minutes");
  ck(loff.fullness == lon.fullness && loff.joy == lon.joy && loff.hygiene == lon.hygiene,
     "...and nothing else");

  // Asleep, nothing extra: the sleep floors are upstream's and stay so.
  Pet soff, son;
  forkSetFeatures(false);
  freshPet(soff, g[0], g[1], g[2], g[3]);
  soff.sleeping = true;
  offline(soff, 60);
  forkSetFeatures(true);
  freshPet(son, g[0], g[1], g[2], g[3]);
  son.sleeping = true;
  offline(son, 60);
  ck(soff.fullness == son.fullness, "asleep, a nature drains nothing extra");

  // EASYGOING has no favourite and drains nothing extra.
  Pet eoff, eon;
  forkSetFeatures(false);
  freshPet(eoff, e[0], e[1], e[2], e[3]);
  offline(eoff, 40);
  forkSetFeatures(true);
  freshPet(eon, e[0], e[1], e[2], e[3]);
  offline(eon, 40);
  ck(eoff.fullness == eon.fullness && eoff.joy == eon.joy && eoff.energy == eon.energy &&
     eoff.hygiene == eon.hygiene, "an easygoing creature drains exactly as upstream's");
  forkSetFeatures(false);
}

static void testNatureCare() {
  uint8_t t[4], g[4], l[4];
  ivsFor(NATURE_TIDY, t);
  ivsFor(NATURE_GLUTTON, g);
  ivsFor(NATURE_LAZY, l);

  Pet off, on, other;
  forkSetFeatures(false);
  freshPet(off, t[0], t[1], t[2], t[3]);
  off.clean();
  forkSetFeatures(true);
  freshPet(on, t[0], t[1], t[2], t[3]);
  on.clean();
  freshPet(other, g[0], g[1], g[2], g[3]);
  other.clean();
  printf("  clean: bond %u off / %u tidy / %u glutton\n", off.bond, on.bond, other.bond);
  ck(on.bond == off.bond + NATURE_CARE_BOND, "the favourite care bonds a little more");
  ck(other.bond == off.bond, "...and anyone else's favourite gives nothing extra");

  // Food for the glutton, through both ways of feeding.
  Pet fb, fc;
  freshPet(fb, g[0], g[1], g[2], g[3]);
  fb.feedBerry(0);
  freshPet(fc, g[0], g[1], g[2], g[3]);
  fc.feedCandy();
  forkSetFeatures(false);
  Pet fb0, fc0;
  freshPet(fb0, g[0], g[1], g[2], g[3]);
  fb0.feedBerry(0);
  freshPet(fc0, g[0], g[1], g[2], g[3]);
  fc0.feedCandy();
  ck(fb.bond == fb0.bond + NATURE_CARE_BOND && fc.bond == fc0.bond + NATURE_CARE_BOND,
     "a glutton bonds more from a berry and from a candy");

  // Rest for the lazy one: going to bed, not being woken.
  forkSetFeatures(true);
  Pet lz;
  freshPet(lz, l[0], l[1], l[2], l[3]);
  lz.toggleLight();
  ck(lz.sleeping && lz.bond == NATURE_CARE_BOND, "a lazy one bonds from being put to bed");
  lz.toggleLight();
  ck(!lz.sleeping && lz.bond == NATURE_CARE_BOND, "...and not from being woken");

  // The daily cap holds: the extra rides inside addBond(), so it cannot farm.
  Pet cap;
  freshPet(cap, t[0], t[1], t[2], t[3]);
  for (int i = 0; i < 50; i++) cap.clean();
  printf("  50 cleans: bond %u\n", cap.bond);
  ck(cap.bond == 20, "fifty cleans still stop at the daily bond cap of 20");
  forkSetFeatures(false);
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
  testNatureHash();
  testNatureDrain();
  testNatureCare();
  printf("%s fork_test: %d failure(s)\n", bad ? "FAIL" : "PASS", bad);
  return bad ? 1 : 0;
}
