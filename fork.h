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
// (natures) adds an owner stamp beside it, checked against the live pet on load.

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
