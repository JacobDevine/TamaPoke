#pragma once
#include <Arduino.h>
#include <Preferences.h>

// THE ANDROID FORK'S OWN STATE, KEPT APART FROM UPSTREAM'S.
//
// This fork ships the Android app and keeps merging upstream (CLAUDE.md, top).
// Every feature it adds -- natures, sickness, steps, expeditions, the farm --
// needs saved state, strings and drawing, and upstream keeps changing the code
// those would naturally live in. This file is what stops the two from colliding.
// See android/FEATURES_PLAN.md, Phase 0, for the reasoning in full; the short
// version is three rules.
//
// 1. ASK forkFeatures() BEFORE DOING ANYTHING A FORK FEATURE DOES. It is false
//    on the board and under --shot, so the board's screenshots stay
//    byte-identical and a board save never gains fork state. It names what is
//    ALLOWED, in one place -- never a list of places to exclude.
//
// 2. FORK STATE LIVES IN ITS OWN RECORD, `frkA` / `frkB`, never appended to
//    upstream's pet or player checkpoints. Those are append-only, and upstream
//    appends to them too: the first upstream release that adds a field would
//    land it on the same byte offset as ours, and every phone save would be
//    silently reread as something else. Our own magic and our own generation
//    counter make that impossible.
//
// 3. FORK STRINGS LIVE IN fork_i18n.h, never appended to STRINGS, for the same
//    reason one level up: that table is positional and upstream appends to it.

// --- 1. the switch ----------------------------------------------------------

bool forkFeatures();
// Called by the Android app before setup(), and by tests. Nothing else.
void forkSetFeatures(bool on);

// --- 2. the record ----------------------------------------------------------
//
// Laid out by ckpt.h's rules: magic, version, size, generation, body, crc, two
// slots used alternately so a cut mid-write leaves the previous copy whole.
//
// THE BODY IS APPEND-ONLY, BY BYTE OFFSET. Each field has a fixed offset below.
// A reader takes the prefix of the stored body it understands and leaves the
// rest at the initialisers in ForkState, so a record written before a field
// existed loads with that field at its default. A new field therefore goes at
// FORK_BODY_LEN and moves FORK_BODY_LEN past it; it is never inserted. An array
// that grows with the game (anything sized by DEX_COUNT, REGION_COUNT or a slot
// count) carries its dimension in the body beside it, as the player record
// does, so a later build can tell a short array from a moved field.
//
// If a change ever cannot be expressed that way, bump FORK_MAGIC rather than
// FORK_VERSION -- see ckpt.h on why the version is informational only.
//
// PER-CREATURE FIELDS NEED AN OWNER. This record and the pet checkpoint are two
// separate writes, so a cut between them can leave one describing a creature the
// other has already replaced. Whichever phase adds the first per-creature field
// adds an owner stamp beside it, checked against the live pet on load. (Natures
// need none: they are derived from the creature, not stored -- see section 3.)

#define FORK_MAGIC   0x4B465054u   // "TPFK", little-endian
#define FORK_VERSION 1

// Body offsets. Append only.
#define FORK_OFF_CREATED   0       // u32: rtc epoch the record was first written
#define FORK_BODY_LEN      4

// Large enough for records written by LATER builds: ckptRead() copies nothing
// at all when the stored blob is bigger than the buffer, so a tight one would
// make every downgrade read as "no record".
#define FORK_BLOB_CAP 1024

struct ForkState {
  uint32_t createdEpoch = 0;
};

class Fork {
public:
  // Opens the namespace and loads the newest whole slot. With forkFeatures()
  // off it does nothing at all -- the board does not even open a handle -- so
  // the switch must be set before this runs.
  void begin();
  // Writes the next slot. Does NOTHING unless forkFeatures() is on, so the
  // board can never gain the keys, whatever calls this.
  bool save(uint32_t nowEpoch);

  // Did begin() find a record? False on a save that predates the fork's
  // features, which is the trigger a feature uses to set itself up for a
  // creature that already exists.
  bool loaded() const { return loadedOk; }
  uint32_t generation() const { return gen; }

  ForkState st;

  // For tests: encode/decode without touching the store.
  static uint16_t encode(const ForkState &s, uint8_t *buf, size_t cap, uint32_t generation);
  static bool decode(const uint8_t *buf, uint16_t n, ForkState &out);

private:
  Preferences prefs;
  bool opened = false;
  bool loadedOk = false;
  uint32_t gen = 0;
};

extern Fork forkData;   // not `fork`: that is POSIX fork() on the phone

// --- 3. natures (android/FEATURES_PLAN.md, Phase 1) ------------------------
//
// One need drains a little faster and caring for that need bonds a little more.
// Care only: a nature never touches a battle stat, so the gym ladder's measured
// win rates and a LAN fight against a board are exactly upstream's.
//
// DERIVED, NOT STORED. A creature has no id of its own, and upstream moves
// creatures between the live pet, the party, the box and wild catches in more
// places than this fork could hook without each hook becoming a merge conflict.
// The four IVs and the shiny flag are fixed at hatch and travel with the
// creature everywhere, so the nature is a scrambled hash of those: every banked,
// boxed or caught creature has one, and it can never disagree between copies.
//
// A HASH ON PURPOSE, unlike upstream's personality(), which READS the IVs (the
// best one names the trait). Hashing keeps a nature independent of that trait,
// so a BOLD creature is not always a GLUTTON. fork_test measures both the spread
// and the independence over every IV a creature can roll.
//
// THE MAPPING IS PART OF EVERY PLAYER'S SAVE, in effect. Changing the hash, the
// order below or NATURE_COUNT changes the nature of every creature already
// raised. Append-only, like the record.

enum : uint8_t {
  NATURE_EASYGOING = 0,   // no favourite: nothing drains faster
  NATURE_GLUTTON,         // food
  NATURE_PLAYFUL,         // joy
  NATURE_TIDY,            // hygiene
  NATURE_LAZY,            // energy
  NATURE_COUNT,
  NATURE_NONE = 0xFF,     // an egg, or no creature
};

// The need a nature cares about. Same order as the natures after EASYGOING.
enum : uint8_t { NEED_NONE = 0, NEED_FOOD, NEED_JOY, NEED_HYGIENE, NEED_ENERGY };

// Pure: no switch, no state. Never NATURE_NONE.
uint8_t natureFromIVs(uint8_t ivAtk, uint8_t ivDef, uint8_t ivSpe, uint8_t ivHp, bool shiny);
uint8_t natureNeed(uint8_t nature);
// A banked, boxed or caught creature's nature: the same hash, so it is what it
// was while it was the live pet. NATURE_NONE for an empty slot.
struct PartyMon;
uint8_t monNature(const PartyMon &m);
// The nature's name in the current language; "" for NATURE_NONE.
const char *natureName(uint8_t nature);

// Fork balance, in one place. android/README.md § "Fork rules" quotes these.
#define NATURE_FOOD_EVERY  2   // awake minutes per extra point of food lost (2/min -> 2.5)
#define NATURE_OTHER_EVERY 4   // ...of joy, hygiene or energy (1/min -> 1.25)
#define NATURE_CARE_BOND   1   // extra bond for the favourite care, inside the daily cap

// --- 5. evolution rules (android/FEATURES_PLAN.md, Phase 2) -------------------
//
// dex.h holds one evolution per species. The generated evo_rules.h adds the
// branches and bond evolutions it cannot express, and these functions are the
// ONLY way anything reads it: Pet::canEvolveNow(), evolve(), lineHasUnregistered(),
// the egg pool and the card all ask here. Two of those having their own copy of
// the rule is CLAUDE.md trap 1, the evolution-threshold row, which already
// shipped once.
//
// A RULE DECIDES, NEVER THE PLAYER. The level is unchanged -- the source's own
// evolveLevel plus care mistakes plus the retire debt, the same sum as upstream
// -- and these add what picks the branch: bond, bond and the time of day,
// Attack vs Defence training, or the nature where the games use chance, trade,
// gender or version. Care mistakes deliberately pick nothing; they already
// delay every evolution.
//
// Off the switch, forkEvoRuled() is false for every species and nothing below
// changes upstream's evolutions at all.

// Fork balance. android/README.md § "Fork rules" quotes these.
#define FORK_EVO_BOND       30   // bond for a friendship evolution (about 2 days of care)
#define FORK_EVO_BOND_HIGH  60   // Sylveon: the games ask affection, more than friendship
#define FORK_EVO_DAY_START   6   // day for an evolution is 06:00..19:59, the scene's
#define FORK_EVO_DAY_END    20   // own day (gNight), not the 00-06 auto-sleep window

// One possible next form, as the card shows it.
struct ForkEvo {
  int16_t to;       // the species
  uint8_t cond;     // EVC_* (evo_rules.h)
  uint8_t arg;      // EVC_NATURE: the natures' mask
  bool met;         // its condition holds right now (the level is separate)
  bool picked;      // the one this creature is heading for
};
#define FORK_EVO_MAX 8   // the most branches any species has (Eevee)

// Does the fork decide this species' evolution? False whenever the switch is off.
bool forkEvoRuled(int16_t from);
// Is this species a target of a BRANCH (a source with two or more forms)? Those
// are barred from eggs, or Hitmontop, Gallade and Lunala would hatch straight
// out of one -- exactly how Espeon hatched for six generations. Single-target
// bond evolutions (Pichu -> Pikachu) leave upstream's rarity alone. False with
// the switch off.
bool forkEggBarred(int16_t dex);
// Is the time of day "day" for EVC_BOND_DAY? From the RTC epoch; no clock is day.
bool forkEvoIsDay(uint32_t epoch);

// --- 4. what the phone can tell the game --------------------------------------
//
// Function pointers set by the host before setup(), the same arrangement as
// gHostHaptic and link.cpp's transport: the board leaves them null and simply
// never sees the feature, the tests point them at fakes, and the firmware calls
// across no platform boundary. Each phase adds the hook it needs here, with
// what "nothing" means for it -- so the board's answer is always defined.

struct ForkHost {
  // Steps the phone has counted since the previous call, already corrected for
  // a reboot of the phone. 0 when there is no sensor, no permission, or nothing
  // new. Phase 4.
  uint32_t (*takeSteps)() = nullptr;
};
extern ForkHost gForkHost;

// The hooks, read safely: each returns the "nothing" answer when unset.
uint32_t forkTakeSteps();
