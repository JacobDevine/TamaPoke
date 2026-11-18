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
// Phase 3 (sickness, medicine, coins). See section 6.
#define FORK_OFF_SICK_KEY  4       // u32: forkCreatureKey() of the sick creature, 0 = none
#define FORK_OFF_SICK_MIN  8       // u16: awake minutes it has been sick
#define FORK_OFF_MEDICINE 10       // u8:  medicine in the bag
#define FORK_OFF_FLAGS    11       // u8:  FORK_FLAG_*
#define FORK_OFF_COINS    12       // u32: the coin purse
// Phase 4 (the step counter). See section 7.
#define FORK_OFF_STEP_DAY   16     // u32: the day (epoch / 86400) the counts below are for
#define FORK_OFF_STEP_TODAY 20     // u32: steps on that day
#define FORK_OFF_STEP_TOTAL 24     // u32: steps ever, the trail rank's count
#define FORK_OFF_STEP_MILES 28     // u8:  that day's milestones already paid, one bit each
#define FORK_OFF_STEP_JOY   29     // u8:  joy that day's steps have already given
#define FORK_OFF_STEP_BOND  30     // u8:  bond that day's steps have already given
#define FORK_OFF_STEP_RANK  31     // u8:  the highest trail rank already announced
// Phase 5 (expeditions). See section 8. EXP_KEY is the owner stamp the
// per-creature rule above asks for.
#define FORK_OFF_EXP_KEY    32     // u32: forkCreatureKey() of the creature away, 0 = none
#define FORK_OFF_EXP_END    36     // u32: the epoch it comes back
#define FORK_OFF_EXP_STEPS  40     // u32: stepsTotal when it left, for the walking bonus
#define FORK_OFF_EXP_SITE   44     // u8:  FORK_EXP_* site
#define FORK_OFF_EXP_LEN    45     // u8:  index into FORK_EXP_MINUTES
#define FORK_BODY_LEN     46

// FORK_OFF_FLAGS bits. A save that predates a bit reads it as 0.
#define FORK_FLAG_MED_GIVEN 0x01   // the starting medicine has been handed out

// Large enough for records written by LATER builds: ckptRead() copies nothing
// at all when the stored blob is bigger than the buffer, so a tight one would
// make every downgrade read as "no record".
#define FORK_BLOB_CAP 1024

struct ForkState {
  uint32_t createdEpoch = 0;
  uint32_t sickKey = 0;
  uint16_t sickMinutes = 0;
  uint8_t medicine = 0;
  uint8_t flags = 0;
  uint32_t coins = 0;
  uint32_t stepDay = 0;
  uint32_t stepsToday = 0;
  uint32_t stepsTotal = 0;
  uint8_t stepMiles = 0;
  uint8_t stepJoy = 0;
  uint8_t stepBond = 0;
  uint8_t stepRank = 0;
  uint32_t expKey = 0;
  uint32_t expEnd = 0;
  uint32_t expSteps = 0;
  uint8_t expSite = 0;
  uint8_t expLen = 0;
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
  // Something changed that a later flush should write (forkFlush()).
  void markDirty() { dirty = true; }
  bool isDirty() const { return dirty; }

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
  bool dirty = false;
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

// --- 6. sickness, medicine and coins (android/FEATURES_PLAN.md, Phase 3) ----
//
// Two or more poops, being overweight, or any need at FORK_SICK_NEED or below
// gives an awake creature a FORK_SICK_CHANCE in 100 chance a minute of falling
// sick. While sick it loses an extra point of every need each
// FORK_SICK_DRAIN_EVERY awake minutes, every FORK_SICK_MISTAKE_MIN awake minutes
// count as a care mistake, and its battle stats are FORK_SICK_BATTLE_PCT of
// normal. Medicine cures it.
//
// ONLY AWAKE MINUTES IN THE APP COUNT. Asleep, nothing happens: CLAUDE.md
// § "The three drain paths" is how this game once lost a creature overnight.
// Offline (the app closed, which is the board switched off) nothing happens
// either: syncClock() has no hook, so a two-week deltaMinutes can neither make
// a creature sick nor stack fourteen days of mistakes. That also keeps
// phone_remind.cpp's forecast of the offline drain exact.
//
// OWNED BY THE CREATURE, through forkCreatureKey(): the same 21 bits the nature
// hashes, fixed at hatch and carried into the party and box. Sick means "the
// record's key is the live pet's key", so nothing needs a hook when upstream
// swaps, revives, hatches or catches: a new creature simply is not the sick
// one, and a sick creature banked and brought back is still sick. Only one
// creature is tracked, so a second one falling sick forgets the first.
//
// MEDICINE AND COINS ARE THE PLAYER'S, not the creature's. They are not in
// items.h for the same reason fork state is not in upstream's checkpoints:
// ItemKey is append-only and upstream appends to it. The bag lists them after
// upstream's items (forkBagRows()).

#define FORK_SICK_NEED          20   // a need at or below this can make it sick
#define FORK_SICK_POOPS          2   // ...as can this many poops
#define FORK_SICK_WEIGHT        50   // ...or weight above this (upstream's "overweight")
#define FORK_SICK_CHANCE         2   // per awake minute, in 100, while one holds
#define FORK_SICK_DRAIN_EVERY    3   // awake minutes per extra point off every need
#define FORK_SICK_MISTAKE_MIN  120   // awake sick minutes per care mistake
#define FORK_SICK_BATTLE_PCT    90   // battle stats while sick, percent
#define FORK_MED_START           3   // medicine a player starts with
#define FORK_MED_PRICE          30   // coins per medicine
#define FORK_COINS_MAX        9999
#define FORK_COINS_WILD          5   // a wild win (hard: double)
#define FORK_COINS_GYM          10   // a gym win, +2 per leader deeper (hard: double)

class Pet;
struct Combatant;
// The creature's identity bits; 0 only for an egg (never a real key).
uint32_t forkCreatureKey(const Pet &p);
bool forkSick(const Pet &p);
// Could it fall sick right now: the conditions, without the dice.
bool forkSickRisk(const Pet &p);
// One awake minute: the dice, the drain and the mistake clock. `roll` < 0
// draws random(100), and ONLY when the dice are actually rolled -- a cared-for
// creature spends no random(), so upstream's own rolls (poops) keep their
// sequence. Tests pass 0..99 to force it. Flushes the record if dirty.
void forkSickTick(Pet &p, int roll = -1);
// Uses one medicine. False, and nothing used, when it is not sick or there is
// no medicine.
bool forkCure(Pet &p);
// The live pet's combatant, weakened while sick. Nothing else is touched.
void forkSickBattle(const Pet &p, Combatant &c);
// Coins: add (capped), and buy one medicine. Both write the record.
uint32_t forkAddCoins(uint32_t n);
bool forkBuyMedicine();
// What a battle win pays. 0 with the switch off.
uint32_t forkWinCoins(bool wild, bool hard, int8_t trainer);
// Writes the record if anything marked it dirty.
void forkFlush();
// Tests only: a suite measuring ANOTHER feature's drain (fork_test's natures)
// turns sickness off, or a creature it neglects falls sick and drains more.
void forkTestSetSickness(bool on);

// --- 4. what the phone can tell the game --------------------------------------
//
// Function pointers set by the host before setup(), the same arrangement as
// gHostHaptic and link.cpp's transport: the board leaves them null and simply
// never sees the feature, the tests point them at fakes, and the firmware calls
// across no platform boundary. Each phase adds the hook it needs here, with
// what "nothing" means for it -- so the board's answer is always defined.

// One day's steps as the phone counted them. `day` is the local day the
// firmware's clock uses (epoch / 86400 -- see Pet::today()).
struct ForkStepBucket {
  uint32_t day;
  uint32_t steps;
};

// What the phone says about counting steps.
enum : uint8_t {
  FORK_STEPS_NONE = 0,   // no sensor (and the board, and the tests by default)
  FORK_STEPS_OFF,        // a sensor, but the player has not allowed it
  FORK_STEPS_ON,
};

struct ForkHost {
  // Phase 4. Hands over the steps counted since the previous call, one bucket
  // per day, OLDEST FIRST, already corrected for a reboot of the phone, and
  // forgets them. Returns how many buckets were written (at most `max`); 0 when
  // there is no sensor, no permission, or nothing new.
  uint8_t (*takeSteps)(ForkStepBucket *out, uint8_t max) = nullptr;
  uint8_t (*stepsStatus)() = nullptr;
  // Ask the player for the permission. `firstTimeOnly`: only if it has never
  // been asked (opening the steps page); otherwise always (a tap on it).
  void (*askSteps)(bool firstTimeOnly) = nullptr;
};
extern ForkHost gForkHost;

// The hooks, read safely: each returns the "nothing" answer when unset.
uint8_t forkTakeSteps(ForkStepBucket *out, uint8_t max);
uint8_t forkStepsStatus();
void forkAskSteps(bool firstTimeOnly);

// --- 7. the step counter (android/FEATURES_PLAN.md, Phase 4) ------------------
//
// The phone counts steps whether or not the app is open (its sensor hub keeps
// the count), splits them by day with a midnight alarm, and hands them over in
// day buckets through gForkHost.takeSteps. forkStepsPoll() is the ONE place
// they enter the game: the sketch's loop calls it every 30 s and the app calls
// it on coming back.
//
// A DAY'S RECORD MOVES ONLY WHEN A NEWER DAY'S STEPS ARRIVE, never with the
// clock. Yesterday's last steps can arrive this morning (the midnight read is
// best effort), and they must still count towards yesterday's milestones. So
// "today's steps" is stepsToday only while stepDay is today, and 0 otherwise:
// forkStepsToday() asks that, and everything shown or paid reads it.
//
// What walking gives, every amount capped per day so walking cannot stand in
// for care:
//  - joy and bond, for TODAY's steps only (a late bucket for yesterday pays its
//    milestones but no joy: the creature was not walked today). Bond goes
//    through addBond(), so upstream's own daily cap holds as well.
//  - an upstream item at each milestone, once per day each. Into upstream's
//    bag through Inventory::add(); items.h is not touched.
//  - a trail rank for steps ever, announced once each.
//  - a small wild bonus from today's steps: up to double the base shiny odds,
//    and up to a tenth more catch chance. NOT upstream's shiny bonus points:
//    one of those is a whole percentage point, 40 times the 1/4096 base.
//
// No training: the gym ladder's measured win rates stay upstream's.

#define FORK_STEP_JOY_EVERY   250   // steps per point of joy
#define FORK_STEP_JOY_MAX      20   // joy a day
#define FORK_STEP_BOND_EVERY 1000   // steps per point of bond
#define FORK_STEP_BOND_MAX      3   // bond a day, inside addBond()'s own cap
#define FORK_STEP_MILES         3
#define FORK_STEP_FULL       5000   // today's steps for the whole wild bonus
#define FORK_STEP_CATCH_PCT    10   // the wild catch bonus at full, percent of the chance
#define FORK_STEP_RANKS         4
#define FORK_STEP_BUCKETS       8   // read from the phone per call

// The milestones and the trail ranks, in steps.
extern const uint32_t FORK_STEP_MILE[FORK_STEP_MILES];   // 500, 2000, 5000
extern const uint32_t FORK_STEP_RANK[FORK_STEP_RANKS];   // 10k, 50k, 100k, 250k

// Pulls every waiting bucket from the phone and pays for it. Nothing with the
// switch off. `nowEpoch` is the firmware's clock (rtcEpoch()).
void forkStepsPoll(Pet &p, uint32_t nowEpoch);
// The same, for one bucket: the poll's body, and what the tests drive.
void forkStepsApply(Pet &p, const ForkStepBucket &b, uint32_t nowEpoch);
// Today's steps: the record's count while it is today's, else 0.
uint32_t forkStepsToday(uint32_t nowEpoch);
// The trail rank for a lifetime count, 0..FORK_STEP_RANKS.
uint8_t forkStepRankFor(uint32_t total);
const char *forkStepRankName(uint8_t rank);
// The wild bonus, 0..100: how much of it today's steps have earned.
uint8_t forkStepWildPct(uint32_t nowEpoch);
// The shiny roll's extra: true if `roll` (0..WILD_SHINY_SCALE) is shiny only
// because of the walking. Callers OR it with upstream's own roll.
bool forkStepShiny(uint32_t roll, uint32_t nowEpoch);
// A capture chance with the walking bonus; unchanged at 0, at 100 (a sure
// catch) and with the switch off, and never above 95 otherwise (upstream's cap).
uint8_t forkStepCatch(uint8_t chance, uint32_t nowEpoch);

// A short queue of two-line banners for the main screen (a milestone, a rank).
// Each shows for FORK_BANNER_MS from when it is first drawn, so one earned
// while another screen is open is not missed. False when there is none.
#define FORK_BANNER_MS 3500
bool forkBanner(const char **line1, const char **line2);
void forkBannerClear();


// --- 8. expeditions (android/FEATURES_PLAN.md, Phase 5) -----------------------
//
// The creature goes out for 15, 30 or 60 minutes, which costs energy, and comes
// back with finds from the site it went to. Walking enough steps while it is
// out (section 7) earns one more. This is where the fork's medicine comes from
// besides the shop, and upstream's potions, balls and training items besides
// battles and milestones.
//
// WHILE IT IS AWAY ITS NEEDS STAND STILL. Nothing drains, nothing is dirtied,
// it cannot fall sick, sleep, or make a care mistake: Pet::tick() returns
// early (forkAway()), and syncClock() skips the absence's first
// forkExpMinutesLeft() minutes, so a trip that ends while the app is closed
// pauses exactly the minutes it was out. phone_remind.cpp's forecast skips the
// same minutes; phone_test holds the two together.
//
// While away, the live pet is not there to use: the care buttons are greyed
// (uiButtonDisabled()), the pet zone does not answer, it cannot join a squad
// (pickExists(0), buildSquad()), take a training item, or be offered an
// evolution or a farewell. The tile axis, the menu and the banked party all
// still work.
//
// OWNED BY THE CREATURE, like sickness: the record's key against the live
// pet's. A creature swapped out, released or replaced while away is CALLED
// BACK at the next poll, empty-handed, so no hook is needed in upstream's
// swap, revive or hatch paths.
//
// It comes back only through forkExpPoll(), which the sketch's loop runs every
// 30 s and the app every 5 s and on coming back to the foreground. Rules
// settled with the owner (2026-11-14): never sent asleep or sick, never comes
// back hurt, three fixed sites.

enum : uint8_t { FORK_EXP_COAST = 0, FORK_EXP_VOLCANO, FORK_EXP_FOREST, FORK_EXP_SITES };
#define FORK_EXP_LENS 3
extern const uint8_t FORK_EXP_MINUTES[FORK_EXP_LENS];   // 15, 30, 60
extern const uint8_t FORK_EXP_ENERGY[FORK_EXP_LENS];    // 10, 20, 35: what it costs
extern const uint8_t FORK_EXP_FINDS[FORK_EXP_LENS];     // 1, 2, 3
extern const uint16_t FORK_EXP_WALK[FORK_EXP_LENS];     // 300, 600, 1200: steps for one more
// A clock moved back further than this past the longest trip is not a trip
// still running: it comes back at once rather than staying out for hours.
#define FORK_EXP_SLACK_MIN 60
// A find that is the fork's medicine rather than one of upstream's ItemKeys.
#define FORK_EXP_MEDICINE 0xFE
// What each site's scene looks like: an upstream biome (BIOME_SOIL's index).
uint8_t forkExpBiome(uint8_t site);
const char *forkExpSiteName(uint8_t site);

// Why it cannot go now, or FORK_EXP_OK.
enum : uint8_t {
  FORK_EXP_OK = 0,
  FORK_EXP_OFF,        // the switch is off, or there is no creature (an egg, a ceremony)
  FORK_EXP_AWAY,       // it is already out
  FORK_EXP_ASLEEP,
  FORK_EXP_SICK,
  FORK_EXP_TIRED,      // less energy than the trip costs
  FORK_EXP_NO_CLOCK,   // no time to measure the trip by
};
uint8_t forkExpCanSend(const Pet &p, uint8_t len, uint32_t nowEpoch);
// Sends it: the energy now, the trip into the record. False (and nothing done)
// unless forkExpCanSend() is FORK_EXP_OK.
bool forkExpSend(Pet &p, uint8_t site, uint8_t len, uint32_t nowEpoch);
// Is the live pet out? Until forkExpPoll() brings it back, whatever the clock says.
bool forkAway(const Pet &p);
// Seconds until it is due back, 0 when it is due (or not away).
uint32_t forkExpSecondsLeft(uint32_t nowEpoch);
// Minutes of an absence starting at `fromEpoch` that the creature spends out:
// what syncClock() and the reminder's forecast leave undrained. 0 if not away.
uint32_t forkExpMinutesLeft(const Pet &p, uint32_t fromEpoch);
// Brings it back if it is due (its finds, a banner each), or calls it back
// empty-handed if it is no longer the live pet. Writes the record.
void forkExpPoll(Pet &p, uint32_t nowEpoch);
// A site's find for a roll 0..99 (pure, for tests): an ItemKey or FORK_EXP_MEDICINE.
uint8_t forkExpFind(uint8_t site, uint8_t roll);
