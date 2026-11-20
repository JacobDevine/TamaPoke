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
// Phase 6 (touch). See section 9.
#define FORK_OFF_TOUCH_DAY  46     // u32: the day (lastSeenEpoch / 86400) touchJoy counts
#define FORK_OFF_TOUCH_JOY  50     // u8:  joy the gestures have given that day
// Phase 7 (the pet acting on its own). See section 10.
#define FORK_OFF_CHARGE_FROM 51    // u32: epoch the app went to the background on charge, 0 = not
#define FORK_OFF_STEP_PREV_DAY 55  // u32: the day before stepDay's record moved on...
#define FORK_OFF_STEP_PREV   59    // u32: ...and its steps, for "yesterday you walked"
// Phase 8a (the world from the clock). See section 11.
#define FORK_OFF_GIFT_YEAR   63    // u16: the year giftMask counts for
#define FORK_OFF_GIFT_MASK   65    // u8:  holidays whose gift that year has given, bit h-1
#define FORK_OFF_BDAY_MONTH  66    // u8:  the player's birthday, 1..12; 0 = not set
#define FORK_OFF_BDAY_DAY    67    // u8:  ...1..31
#define FORK_OFF_HATCH_KEY   68    // u32: forkCreatureKey() of the creature hatchDay belongs to
#define FORK_OFF_HATCH_DAY   72    // u32: the local day (epoch / 86400) it hatched
// Phase 8b (the weather). See section 12.
#define FORK_OFF_WX_HOUR     76    // u32: the local hour (epoch / 3600) the weather last nudged joy
#define FORK_OFF_WX_DAY      80    // u32: the day the two counts below are for
#define FORK_OFF_WX_UP       84    // u8:  joy the weather has given that day
#define FORK_OFF_WX_DOWN     85    // u8:  joy the weather has taken that day
#define FORK_BODY_LEN     86

// FORK_OFF_FLAGS bits. A save that predates a bit reads it as 0, so a bit
// that turns something OFF is how a setting defaults to on.
#define FORK_FLAG_MED_GIVEN 0x01   // the starting medicine has been handed out
#define FORK_FLAG_NO_SELFCARE 0x02 // the player turned self-care while charging off
#define FORK_FLAG_NO_HAPTICS 0x04  // the player turned the phone's buzz off (settings)
#define FORK_FLAG_NO_STANDBY 0x08  // the player turned the standby clock off (settings)
// The one bit that turns something ON, so it defaults to off: the weather is
// the app's only use of the network, and nothing is sent until it is asked for.
#define FORK_FLAG_WEATHER   0x10   // the player turned the weather on (settings)

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
  uint32_t touchDay = 0;
  uint8_t touchJoy = 0;
  uint32_t chargeFrom = 0;
  uint32_t stepPrevDay = 0;
  uint32_t stepPrev = 0;
  uint16_t giftYear = 0;
  uint8_t giftMask = 0;
  uint8_t bdayMonth = 0;
  uint8_t bdayDay = 0;
  uint32_t hatchKey = 0;
  uint32_t hatchDay = 0;
  uint32_t wxHour = 0;
  uint32_t wxDay = 0;
  uint8_t wxUp = 0;
  uint8_t wxDown = 0;
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
  // The album's own record (section 13), on the same handle. Written at once by
  // forkAlbumRecord(); begin() loads it.
  bool albumSave();
  uint32_t albumGeneration() const { return albGen; }

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
  uint32_t albGen = 0;
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
// The same key from any creature's raw fields (a PartyMon, a catch). It is the
// ONE place the bits are laid out: forkCreatureKey() calls it, and adv_test
// pins the two equal over every value (CLAUDE.md trap 1).
uint32_t forkKeyOf(uint8_t ivAtk, uint8_t ivDef, uint8_t ivSpe, uint8_t ivHp, bool shiny);
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
  // Phase 7. Is the phone plugged in (charging or full)?
  bool (*charging)() = nullptr;
  // Phase 8a. Where the phone is, roughly, in degrees (north and east positive),
  // and its UTC offset in seconds. False when it does not know. The Android app
  // answers from the time zone's own coordinates (IANA zone.tab), so it needs
  // no location permission and sends nothing anywhere.
  bool (*place)(float *lat, float *lon, int32_t *zoneSec) = nullptr;
  // Phase 8a. Ask the player for their birthday (the phone's own date picker),
  // starting from month/day (0 = not set). The answer comes back later through
  // forkSetBirthday(), on the game loop.
  void (*askBirthday)(uint8_t month, uint8_t day) = nullptr;
  // Phase 8b. The latest weather report, when the player has turned the
  // weather on and the phone has one fresh enough to believe: the WMO weather
  // code, the temperature in whole degrees, and its unit ('C' or 'F', the
  // phone's choice). False when there is none. The phone fetches it itself,
  // and only after weatherFetch() asked it to.
  bool (*weather)(uint8_t *wmo, int16_t *temp, char *unit) = nullptr;
  // Phase 8b. Start (on, from these coordinates) or stop fetching. The host
  // forgets its report when stopped. Called by forkWeatherSync() when the
  // answer changes (the setting, the place), never each frame.
  void (*weatherFetch)(bool on, float lat, float lon) = nullptr;
};
extern ForkHost gForkHost;

// The hooks, read safely: each returns the "nothing" answer when unset.
uint8_t forkTakeSteps(ForkStepBucket *out, uint8_t max);
uint8_t forkStepsStatus();
void forkAskSteps(bool firstTimeOnly);
bool forkCharging();   // false when unset: no charger is no self-care
bool forkPlace(float *lat, float *lon, int32_t *zoneSec);   // false when unset
bool forkCanAskBirthday();
void forkAskBirthday();
// Phase 8b, read safely: false when unset or the report is out of range.
bool forkWeatherReport(uint8_t *wmo, int16_t *temp, char *unit);

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

// --- 9. more ways to touch the pet (android/FEATURES_PLAN.md, Phase 6) --------
//
// On the pet screen, inside inPetZone, with the creature awake and present:
//  - STROKE: a finger moved back and forth across it (three legs or more, each
//    at least FORK_STROKE_LEG px). Joy and bond, like a caress.
//  - TICKLE: a quick wiggle in place (four legs or more, each shorter than
//    that). Joy -- until FORK_TICKLE_ANNOY of them inside a minute, after which
//    the next one ANNOYS it and costs joy instead.
//  - DOUBLE TAP: the second tap inside FORK_DOUBLE_TAP_MS makes it hop. The
//    first is upstream's caress, untouched, so a single tap is not delayed.
//  - POKE: where a tap lands picks a reaction, head or belly. Cosmetic: the tap
//    is still upstream's caress and pays exactly what it always did.
// And with the phone's accelerometer:
//  - SHAKE: three hard jolts inside FORK_SHAKE_WINDOW_MS make it dizzy, which
//    is rough handling and costs joy, at most once per FORK_SHAKE_COOLDOWN_MS.
//  - FACE DOWN: held face down for FORK_FACEDOWN_MS is the board's PWR short
//    press -- the screen goes off and Pet::setScreenOff() decides the rest, so
//    it sleeps only in upstream's night window, exactly as the board does.
//    Face up again turns it back on.
//
// THE GESTURES NEVER TAKE ANYTHING UPSTREAM OWNS. A stroke has reversed at least
// twice, which no swipe on the tile axis does, and it is classified on release
// BEFORE the swipe checks; a finger that has reversed is not the 3 s hold either
// (forkStrokeBusy()). The pet zone and SCR_MAIN are asked by the sketch, the same
// "state what is allowed" gate as the hold.
//
// CAPPED. The joy strokes, tickles and hops give is at most FORK_TOUCH_JOY_MAX a
// day, kept in the record so a restart does not reset it; their bond goes
// through addBond() and its own daily cap. Losses are never capped.

#define FORK_TOUCH_JOY_MAX     20   // joy a day from strokes, tickles and hops
#define FORK_STROKE_JOY         3
#define FORK_TICKLE_JOY         2
#define FORK_HOP_JOY            2
#define FORK_STROKE_BOND        1   // inside addBond()'s daily cap
#define FORK_TICKLE_ANNOY       3   // tickles a minute before the next one annoys it
#define FORK_TICKLE_WINDOW_MS 60000
#define FORK_ANNOY_JOY          3   // what an annoying tickle costs
#define FORK_SHAKE_JOY          3   // what a shake costs
#define FORK_SHAKE_COOLDOWN_MS 30000
#define FORK_DOUBLE_TAP_MS    350
#define FORK_DOUBLE_TAP_PX     40
#define FORK_STROKE_LEG        30   // px: a stroke's legs are at least this long
#define FORK_LEG_MIN           10   // px: a movement shorter than this is not a leg
#define FORK_REACT_MS        2500   // how long a hop, poke, stroke or tickle shows
#define FORK_DIZZY_MS        4000
// Motion, in m/s^2 with gravity included (what SDL's accelerometer reports).
#define FORK_SHAKE_JOLT      12.0f  // |a| further than this from 1 g is a jolt
#define FORK_SHAKE_JOLTS        3
#define FORK_SHAKE_WINDOW_MS  900
#define FORK_FACEDOWN_Z     -8.0f   // z below this is face down...
#define FORK_FACEUP_Z       -3.0f   // ...and above this is not, any more
#define FORK_FACEDOWN_MS     1500
#define FORK_FACEUP_MS        300

enum : uint8_t {
  FORK_GESTURE_NONE = 0,
  FORK_GESTURE_STROKE,
  FORK_GESTURE_TICKLE,
};
// What the creature is showing. FORK_REACT_NONE always on the board.
enum : uint8_t {
  FORK_REACT_NONE = 0,
  FORK_REACT_HOP,
  FORK_REACT_HEAD,
  FORK_REACT_BELLY,
  FORK_REACT_STROKE,
  FORK_REACT_TICKLE,
  FORK_REACT_ANNOYED,
  FORK_REACT_DIZZY,
};

// The finger, from the sketch's handleTouch(). Pure tracking: no switch, no pet.
void forkStrokeBegin(int16_t x, int16_t y);
void forkStrokeMove(int16_t x, int16_t y);
// What the finger has drawn so far; FORK_GESTURE_NONE for a tap or a swipe.
uint8_t forkStrokeKind();
// Has it reversed twice? Then it is a stroke or a tickle under way, never the hold.
bool forkStrokeBusy();

// Can the creature be touched like this right now: the switch, awake, hatched,
// present, no ceremony. The sketch adds the screen and the pet zone.
bool forkTouchable(const Pet &p);
// A stroke or a tickle the finger just finished. True if it was one (the
// gesture is spent), whether or not it paid anything.
bool forkPetStroke(Pet &p, uint8_t kind, uint32_t ms);
// A tap on the creature. `head`: above its middle. True when it was the second
// of a double tap and became a hop -- the caller then skips the caress.
bool forkPetTap(Pet &p, int16_t x, int16_t y, bool head, uint32_t ms);

// One accelerometer sample, from the phone. Nothing with the switch off.
void forkMotionSample(float ax, float ay, float az, uint32_t ms);
// A shake becomes dizzy here, if the creature is on screen to feel it.
void forkMotionPoll(Pet &p, bool onMain, uint32_t ms);
// The face-down state changed: 1 face down, 0 face up, -1 no change. Spent once read.
int8_t forkFaceDownChange();

// The reaction showing at `ms`, and how far into it.
uint8_t forkReact(uint32_t ms);
uint32_t forkReactAge(uint32_t ms);
// Where it moves the creature: up for a hop, side to side while dizzy. 0 when none.
int forkReactDy(uint32_t ms);
int forkReactDx(uint32_t ms);
// Tests only: forget the finger, the reaction, the tickle count and the motion.
void forkTouchReset();

// --- 10. the pet acting on its own (android/FEATURES_PLAN.md, Phase 7) --------
//
// Three things the creature does without being asked, all settled with the
// owner (2026-11-18):
//
// SELF-MEDICATION. After FORK_SELFMED_MIN awake sick minutes it takes one of the
// player's medicines by itself, with a banner. No medicine, no cure: it is the
// player's medicine, bought with the player's coins. Inside forkSickTick(), so
// it counts the same awake minutes sickness does and nothing offline.
//
// SELF-CARE WHILE CHARGING, on by default (FORK_FLAG_NO_SELFCARE turns it off,
// from settings). Plugged in and untouched for FORK_SELFCARE_IDLE_MIN, it raises
// every need below FORK_SELFCARE_BELOW to FORK_SELFCARE_TO. It is NOT the
// player's care: no bond, no streak, no heart, no care-mistake forgiveness.
// The phone's screen timing out pauses the app, so it runs in two places:
//  - live, from the sketch's loop, with the app open;
//  - on coming back, if the phone was on charge when the app left (kept in the
//    record, so a process Android killed meanwhile still counts) AND is on
//    charge now, at least FORK_SELFCARE_IDLE_MIN later. Unplugged in between
//    cannot be seen and is forgiven; plugged in only after leaving is not
//    counted. Applied after syncClock()'s catch-up, so its floors still hold.
// Not for an egg, a ceremony, or a creature out on an expedition (whose needs
// stand still anyway).
//
// THE PLANNER adds to upstream's behNext() when the creature is content: a nap
// when tired, a hop when happy, a visit to the mess when there are poops --
// each likelier for the nature that cares about it. Otherwise upstream's own
// choice runs, so the creature still wanders and gestures exactly as before.
//
// SPEECH BUBBLES, "sometimes": only with a reason, at most one per
// FORK_SAY_GAP_MS on the pet screen, and the same reason not again inside
// FORK_SAY_REPEAT_MS. Never asleep, away, as an egg, or over a reaction.

#define FORK_SELFMED_MIN         10   // awake sick minutes before it takes medicine
#define FORK_SELFCARE_IDLE_MIN   10   // untouched minutes on charge before self-care
#define FORK_SELFCARE_BELOW      40   // a need under this...
#define FORK_SELFCARE_TO         50   // ...is raised to this
#define FORK_SAY_GAP_MS      180000   // between two bubbles
#define FORK_SAY_FIRST_MS     15000   // after arriving on the pet screen
#define FORK_SAY_REPEAT_MS   900000   // before the same reason again
#define FORK_SAY_SHOW_MS       4500   // how long one shows
#define FORK_SAY_LOW             25   // a need under this is worth saying
#define FORK_SAY_STEPS         1000   // yesterday's walk worth mentioning
#define FORK_NAP_ENERGY          35   // energy under this makes a nap likely
#define FORK_HOP_JOY_AT          70   // joy at or above this makes a hop likely
#define FORK_NAP_MS            9000   // how long a nap lasts

// The self-care setting. On by default; the record keeps it.
bool forkSelfCareOn();
// The phone's haptics, on by default; FORK_FLAG_NO_HAPTICS turns them off. A
// player-wide setting kept in this record rather than a key of its own. Off
// the phone nothing buzzes whatever this says (gHostHaptic is null).
bool forkHapticsOn();
void forkSetHaptics(bool on);
void forkSetSelfCare(bool on);
// The live check, from the sketch's loop: charging, the setting, untouched for
// `idleMs`. True if it raised anything.
bool forkSelfCareLive(Pet &p, uint32_t idleMs);
// The app is going to the background: remember whether it is on charge.
void forkChargeLeave(uint32_t nowEpoch);
// The app is back (after syncClock()). True if it raised anything.
bool forkChargeReturn(Pet &p, uint32_t nowEpoch);

// Yesterday's steps, if the record still knows them; 0 otherwise.
uint32_t forkStepsYesterday(uint32_t nowEpoch);

// The planner's choice. FORK_BEH_NONE leaves it to upstream.
enum : uint8_t { FORK_BEH_NONE = 0, FORK_BEH_NAP, FORK_BEH_HOP, FORK_BEH_MESS };
// `roll` is 0..99; pure apart from the switch, so tests can sweep it.
uint8_t forkBehPick(const Pet &p, uint8_t roll);
// The same with its own random(), and ONLY with the switch on: off, it spends
// no random() and upstream's sequence is untouched.
uint8_t forkBehNext(const Pet &p);

// What a bubble says, by priority.
enum : uint8_t {
  FORK_SAY_NONE = 0,
  FORK_SAY_SICK,
  FORK_SAY_HUNGRY,
  FORK_SAY_TIRED,
  FORK_SAY_DIRTY,
  FORK_SAY_BORED,
  FORK_SAY_MESS,
  FORK_SAY_MORNING,    // just woke up
  FORK_SAY_SELFCARE,   // looked after itself while on charge
  FORK_SAY_WALKED,     // yesterday's walk
  FORK_SAY_HOLIDAY,    // today's holiday or birthday, once a day (section 11); said
                       // straight after SICK, whatever this order says
  FORK_SAY_WEATHER,    // the weather changed (section 12): liking it, or what it is
  FORK_SAY_COUNT,
};
// The reason it would speak for right now, ignoring the timing.
uint8_t forkSayReason(const Pet &p, uint32_t nowEpoch);
// Once a frame from the pet screen's draw: decides whether a bubble starts.
// `onMain`: the pet screen is up and the screen is on.
void forkSayPoll(const Pet &p, bool onMain, uint32_t ms, uint32_t nowEpoch);
// The bubble showing at `ms`, or null.
const char *forkSaying(uint32_t ms);
// Tests only: forget the bubbles' timing and what has been said.
void forkSayReset();

// --- 11. the world from the clock (android/FEATURES_PLAN.md, Phase 8a) --------
//
// The sky follows the real sun, the scene the season, and the calendar's
// holidays and birthdays decorate it, greet the player and leave a gift.
//
// LOOKS ONLY, apart from the gifts (settled with the owner, 2026-11-18). Sleep
// keeps upstream's fixed 00:00-06:00 window (Pet::isNightHour()), and the
// evolutions that ask for day or night keep FORK_EVO_DAY_START/END: a creature's
// rules do not move with the seasons or the time zone. What changes is only
// what the scene draws: gNight and the sky's phase.
//
// THE CLOCK IS LOCAL. The firmware's epoch is wall time (phone_clock.cpp), so a
// day is epoch / 86400 and the date comes straight from it. The sun is worked
// out in UTC, which is what the place's zone offset is for.
//
// Nowhere known (no host, or a zone with no coordinates, like "UTC"), the sky
// keeps upstream's fixed hours and there are no seasons. Holidays need only the
// date, so they still happen.

// The sky, in drawing order through a day.
enum : uint8_t { FORK_SKY_NIGHT = 0, FORK_SKY_DAWN, FORK_SKY_DAY, FORK_SKY_DUSK };
// How long dawn and dusk last around sunrise and sunset, in minutes: from
// FORK_TWILIGHT_PRE before the sun is up to FORK_TWILIGHT_POST after, and the
// mirror at sunset. Upstream's fixed hours make each two hours long.
#define FORK_TWILIGHT_PRE   30
#define FORK_TWILIGHT_POST  60

// The civil date of a local epoch (pure). wday: 0 = Sunday.
struct ForkDate { uint16_t year; uint8_t month, day, wday; };
ForkDate forkDate(uint32_t localEpoch);
// The local day number of a date (pure): epoch / 86400 of its midnight.
uint32_t forkDayNumber(uint16_t year, uint8_t month, uint8_t day);
// Western Easter Sunday (pure). Months are 1-based.
void forkEaster(uint16_t year, uint8_t *month, uint8_t *day);

// Sunrise and sunset, in minutes after local midnight (pure). Returns
// FORK_SUN_NORMAL with both filled; FORK_SUN_UP for a day the sun never sets,
// FORK_SUN_DOWN for one it never rises (both left at 0 and 1440).
enum : uint8_t { FORK_SUN_NORMAL = 0, FORK_SUN_UP, FORK_SUN_DOWN };
uint8_t forkSunCalc(uint16_t year, uint8_t month, uint8_t day, float lat, float lon,
                    int32_t zoneSec, int16_t *rise, int16_t *set);
// The same for today, from the host's place. False when it has none.
bool forkSunToday(uint32_t localEpoch, int16_t *rise, int16_t *set);
// The sky at a local time: from the sun with the switch on and a place known,
// otherwise upstream's hours (night 20-6, dawn 6-8, day 8-18, dusk 18-20).
uint8_t forkSkyPhase(uint32_t localEpoch);
// The pure rule underneath, for tests: `sun` is a FORK_SUN_* answer.
uint8_t forkSkyPhaseAt(int minuteOfDay, uint8_t sun, int16_t rise, int16_t set);

// The moon's age in days, 0 (new) to 29.53 (pure). From the local epoch and
// the zone; a day's error in the zone is a few percent of a phase.
float forkMoonAge(uint32_t localEpoch, int32_t zoneSec);

// Seasons, by the place's hemisphere. NONE without a place, or in the tropics,
// where the year has no winter worth drawing.
enum : uint8_t { FORK_SEASON_NONE = 0, FORK_SPRING, FORK_SUMMER, FORK_AUTUMN, FORK_WINTER };
#define FORK_TROPICS_LAT 23.5f
uint8_t forkSeasonAt(uint8_t month, float lat);   // pure: meteorological seasons
uint8_t forkSeason(uint32_t localEpoch);          // from the host; NONE with the switch off

// Holidays, append-only: each one's gift is remembered by bit (h - 1) in the
// record. FORK_HOL_BIRTHDAY is the player's, FORK_HOL_HATCHDAY the live
// creature's (the anniversary of its hatching, not the day itself).
enum : uint8_t {
  FORK_HOL_NONE = 0,
  FORK_HOL_NEWYEAR,     // 1 January
  FORK_HOL_VALENTINE,   // 14 February
  FORK_HOL_EASTER,      // Easter Sunday
  FORK_HOL_HALLOWEEN,   // 31 October
  FORK_HOL_CHRISTMAS,   // 24 and 25 December
  FORK_HOL_BIRTHDAY,    // the player's, once it is set (28 Feb for 29 Feb outside leap years)
  FORK_HOL_HATCHDAY,    // the live creature's
  FORK_HOL_COUNT,
};
// The calendar's holiday on a date (pure), ignoring both birthdays.
uint8_t forkHolidayOn(uint16_t year, uint8_t month, uint8_t day);
// Today's, birthdays first. NONE with the switch off or no clock.
uint8_t forkHoliday(const Pet &p, uint32_t localEpoch);
// Its banner title (at most 17 characters) and greeting bubble (at most 24).
const char *forkHolidayTitle(uint8_t h);
const char *forkHolidayGreeting(uint8_t h);

// THE GIFT, once a year per holiday, given the first time the app is running on
// the day. Into upstream's bag (Inventory::add()) or the fork's purse; never
// training directly. The table is in fork.cpp; android/README.md quotes it.
#define FORK_GIFT_COINS_NEWYEAR  100
#define FORK_GIFT_COINS_HATCHDAY  50
// Looks for today's holiday and pays its gift if it is still owed. Called with
// the 30 s clock and on coming back to the app. Stamps the live creature's
// hatch day as well (forkHatchDay()). True if a gift was given.
bool forkHolidayPoll(Pet &p, uint32_t localEpoch);
// The live creature's hatch day (a local day number), stamped by the poll the
// first time it sees the creature: today, less its age. 0 for an egg or before
// the first stamp. A creature brought back from the party (Pet::reviveFrom())
// carries an age that matches its level rather than its life, so its date is
// an estimate.
uint32_t forkHatchDay(const Pet &p);

// The player's birthday. 0/0 when not set.
void forkSetBirthday(uint8_t month, uint8_t day);
bool forkBirthday(uint8_t *month, uint8_t *day);

// Localised names for the standby clock and the birthday chip (ASCII, short).
const char *forkWeekdayName(uint8_t wday);   // 0 = Sunday
const char *forkMonthName(uint8_t month);    // 1..12

// THE STANDBY CLOCK. On the phone, plugged in and left on the pet screen for
// FORK_STANDBY_IDLE_MS, the screen becomes a large dim clock with the date,
// today's sunrise and sunset and any holiday. A touch brings the game back and
// is spent doing so. While it shows, the creature is treated as if the screen
// were OFF (Pet::setScreenOff()) -- the app keeps the screen awake on charge so
// the clock can show, and without this a night on the charger would keep the
// creature awake and draining (CLAUDE.md § "The three drain paths"). At night
// it therefore sleeps, as it would with the board's PWR press.
#define FORK_STANDBY_IDLE_MS   90000u
#define FORK_STANDBY_FRAME_MS   1000u   // one frame a second is plenty for a clock
#define FORK_STANDBY_DRIFT_MIN     1u   // the layout moves this often (OLED burn-in)
// The setting, on by default; the record keeps it.
bool forkStandbyOn();
void forkSetStandby(bool on);
// Should the app keep the screen awake: the switch, the setting, on charge.
bool forkKeepScreenOn();

// --- 12. the weather (android/FEATURES_PLAN.md, Phase 8b) ----------------------
//
// The real weather where the phone is, drawn over the scene, nudging the
// creature's joy a little by its type, and shown on the standby clock.
//
// OFF UNTIL THE PLAYER TURNS IT ON (settings, the language row's right half).
// It is the app's only use of the network. What is sent is the time zone's
// city (section 11's place, already rounded to 0.1 degree) to Open-Meteo, and
// nothing else: no account, no key, no identifier. The phone fetches; the
// firmware only ever sees the report.
//
// LOOKS AND JOY ONLY (settled with the owner, 2026-11-18). No spawns, no
// battles, no drain: an hourly nudge of one joy, by type, capped per day in
// both directions so neither a week of rain nor of sun can stand in for care.

// What the scene draws, from the report's WMO code.
enum : uint8_t {
  FORK_WX_NONE = 0,   // no report: the switch, the setting, no host, or stale
  FORK_WX_CLEAR,
  FORK_WX_CLOUDY,
  FORK_WX_FOG,
  FORK_WX_RAIN,       // drizzle, rain, freezing rain, showers
  FORK_WX_SNOW,
  FORK_WX_STORM,      // thunderstorms
  FORK_WX_COUNT,
};
// The WMO code to a kind (pure). NONE for a code it does not know.
uint8_t forkWeatherKind(uint8_t wmo);

// The setting. Off by default; the record keeps it.
bool forkWeatherOn();
void forkSetWeather(bool on);
// Should the phone be fetching, and from where: the switch, the setting, and a
// place to ask about.
bool forkWeatherWanted(float *lat, float *lon);
// Tells the host, through weatherFetch(), when that answer has changed since it
// was last told. Called by forkSetWeather(), by forkWeatherPoll() every 30 s
// (the place follows the time zone), and by the host after setup().
void forkWeatherSync();
// Today's weather, NONE unless all of the above and a report.
uint8_t forkWeather();
// ...and the report's temperature. False when forkWeather() is NONE.
bool forkWeatherTemp(int16_t *temp, char *unit);
// The kind's name for the standby clock (at most 7 characters).
const char *forkWeatherName(uint8_t kind);

// How a creature takes the weather (pure): +1 likes it, -1 dislikes it, 0 does
// not mind. `day`: the sky is lit (a clear night is nothing to cheer). A
// creature with one type that likes it and one that does not, does not mind.
int8_t forkWeatherMood(uint8_t kind, uint8_t type1, uint8_t type2, bool day);

// THE NUDGE: once per local hour on the clock, awake and here (not an egg, not
// away, not asleep), one joy up or down by forkWeatherMood(), at most
// FORK_WX_JOY_DAY each way per day. Called with the 30 s clock. True if joy moved.
#define FORK_WX_JOY_DAY 4
bool forkWeatherPoll(Pet &p, uint32_t localEpoch);
// What the day has given and taken so far, for tests.
uint8_t forkWeatherJoyUp(uint32_t localEpoch);
uint8_t forkWeatherJoyDown(uint32_t localEpoch);

// For the drawing (the sketch's forkDrawWeather() and drawScene()). Each
// answers "nothing" with forkWeather() NONE -- so with the switch off, and on
// the board. forkWeatherHidesSky(): the sun, the moon and the stars are behind
// cloud. forkWeatherKnown(): there is a report, so the season's light winter
// snowfall yields to it (it snows when the report says so, and only then).
bool forkWeatherHidesSky();
bool forkWeatherKnown();
// A lightning flash is showing at `now` (ms), in a storm: FORK_WX_FLASH_MS
// once every FORK_WX_FLASH_EVERY_MS. Pure on the time, so tests can find one.
#define FORK_WX_FLASH_EVERY_MS 7000u
#define FORK_WX_FLASH_MS        180u
bool forkWeatherFlash(uint32_t now);

// --- 13. the farm and the album (android/FEATURES_PLAN.md, Phase 9) -----------
//
// THE FARM IS THE BOX MADE VISIBLE (Option A, settled with the owner,
// 2026-11-18). Banked creatures -- the party and the box -- stay FROZEN exactly
// as upstream keeps them (CLAUDE.md § "B3"): nothing here ages, levels, trains
// or feeds one, so the gym ladder's balance is untouched. What changes is that
// they can be SEEN: a FARM tab beside the party shows a field of them, and now
// and then one wanders across the pet screen behind the live creature.
//
// THE ALBUM remembers every creature whose life ended -- a farewell, an early
// retire, a runaway, a release -- with its dates, level, medals and how it
// ended. A runaway and a release leave nothing else behind, so it is the only
// trace of them. It is the medals screen's pages after the steps page.
//
// THE ALBUM IS ITS OWN RECORD, `albA` / `albB`, not part of the fork record:
// it is about 2 KB and changes only when a life ends, while the fork record is
// rewritten every few minutes. Same checkpoint rules (ckpt.h), its own magic and
// its own generation counter. Its DIMENSIONS TRAVEL IN THE BODY -- the entry
// size and the count -- so a later build can grow an entry (append-only, by
// byte offset, like the record) or FORK_ALBUM_MAX without misreading this one.
//
//   body  u16 entry size  u16 count  u32 lives ended ever
//         then `count` entries, oldest first:
//   +0  u32 forkCreatureKey()     +4  i16 dex        +6  u16 level
//   +8  u16 medals               +10  u8 FORK_END_*  +11 u8 flags (bit 0 shiny)
//   +12 char nick[12]            +24  u32 hatch day  +28 u32 day it left
//   (days are local, epoch / 86400; 0 = not known)
//
// ONE CALLER: Pet::update(), at the end of the ceremony and before the party
// handover, which is the single place every ending passes through. Written at
// once, since the creature is about to be gone. A cut between that write and
// the pet's own would replay the ceremony, so an entry identical to the newest
// one is not added twice.

#define FORK_ALBUM_MAGIC   0x4C415054u   // "TPAL"
#define FORK_ALBUM_VERSION 1
#define FORK_ALBUM_ENTRY   32            // bytes per entry this build writes
#define FORK_ALBUM_HDR     8             // the body's own header, before the entries
#define FORK_ALBUM_MAX     64            // the newest are kept; the oldest drop off
#define FORK_ALBUM_PER_PAGE 3            // entries on one medals-screen page
// Room for a LATER build's album (bigger entries, more of them): ckptRead()
// copies nothing at all when the stored blob is bigger than the buffer.
#define FORK_ALBUM_BLOB_CAP 8192

enum : uint8_t {
  FORK_END_NONE = 0,
  FORK_END_FAREWELL,   // the earned farewell: banked, and the next egg blessed
  FORK_END_RETIRED,    // banked early, or banked again (no blessing)
  FORK_END_RUNAWAY,
  FORK_END_RELEASED,
  FORK_END_COUNT,
};

struct ForkAlbumEntry {
  uint32_t key = 0;
  int16_t dex = 0;
  uint16_t level = 0;
  uint16_t medals = 0;
  uint8_t ending = FORK_END_NONE;
  uint8_t shiny = 0;
  char nick[12] = "";
  uint32_t hatchDay = 0;
  uint32_t leftDay = 0;
};

// Which ending a ceremony is (pure on the Pet): NONE outside one.
uint8_t forkEndingOf(const Pet &p);
const char *forkEndingName(uint8_t ending);
// Records the live creature as it leaves. `localEpoch` dates it. False when
// there is nothing to record (the switch, no ceremony) or it was the newest
// entry already.
bool forkAlbumRecord(const Pet &p, uint32_t localEpoch);
uint16_t forkAlbumCount();
uint32_t forkAlbumEver();   // lives ended, including those dropped off the end
// The i-th entry, 0 = the most recent. False past the end.
bool forkAlbumGet(uint16_t i, ForkAlbumEntry *out);
// Pages the medals screen gives the album: at least one, so an empty album can
// say so. 0 with the switch off.
uint8_t forkAlbumPages();
// "12 MAR 26" for a local day, "?" for 0.
void forkDayLabel(uint32_t day, char *out, size_t n);
// For tests, and for Fork::begin()/save: the album's body codec. encode()
// writes the whole checkpoint blob (header and crc); decode() reads one.
uint16_t forkAlbumEncode(uint8_t *buf, size_t cap, uint32_t generation);
bool forkAlbumDecode(const uint8_t *buf, uint16_t n);
void forkAlbumClear();   // tests

// THE FARM: who is out on it. Banked creatures (party first, then the box, in
// slot order); when there are more than FORK_FARM_SHOWN, the next run of them
// each day, so a big box takes turns -- everybody is out at least once every
// ceil(n / FORK_FARM_SHOWN) days -- rather than showing the same few.
#define FORK_FARM_SHOWN 8
struct ForkFarmMon {
  bool inBox;        // false: party slot
  uint16_t slot;     // index into party.slots or party.box
};
class Party;
// Fills out[] (at most FORK_FARM_SHOWN) and returns how many; *total is how
// many banked creatures there are in all. 0 with the switch off.
uint8_t forkFarmPick(const Party &pt, uint32_t day, ForkFarmMon *out, uint16_t *total);
const PartyMon *forkFarmMon(const Party &pt, const ForkFarmMon &f);

// THE VISITOR: a banked creature crossing the pet screen behind the live one.
// The first comes FORK_VISIT_FIRST_MS after arriving on the pet screen, then
// one every FORK_VISIT_GAP_MS, each taking FORK_VISIT_MS to cross. Only on the
// pet screen, with a hatched creature awake and no ceremony; leaving the
// screen sends the visitor home. Purely a look: it changes no state.
#define FORK_VISIT_FIRST_MS  40000u
#define FORK_VISIT_GAP_MS   240000u
#define FORK_VISIT_MS        16000u
#define FORK_VISIT_SCALE         2   // thumbnail scale: smaller than the pet, further off
struct ForkVisitor {
  int16_t dex;
  bool shiny;
  uint32_t key;      // forkKeyOf(), so a kept fusion visits as itself (12f)
  int8_t dir;        // +1 walking right, -1 left
  char name[12];     // nickname, or the species
};
// Called every loop. `roll` picks who and which way; -1 draws one from
// random(), and only when a visitor is due, so the loop does not consume the
// random sequence every frame (tests pass their own).
void forkVisitPoll(const Pet &p, const Party &pt, bool onMain, uint32_t ms, int32_t roll = -1);
// The visitor crossing at `ms`, and its x (the centre, in the 466-wide scene).
bool forkVisitor(uint32_t ms, ForkVisitor *out, int *x);
void forkVisitReset();
