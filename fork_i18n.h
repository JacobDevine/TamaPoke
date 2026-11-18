#pragma once
#include "i18n.h"

// The fork's own UI strings, in the same six languages as STRINGS.
//
// A SEPARATE TABLE ON PURPOSE (fork.h, rule 3). STRINGS is positional and
// upstream appends to it; if we appended too, the next merge would either
// conflict or, worse, merge cleanly with one language row a string short --
// shifting every later string in that language only. Keeping ours here means
// upstream's table and ours can never shift each other.
//
// Same rules as i18n.h, though: append a FStrId, then add its text at the SAME
// index in all six rows of FORK_STRINGS. ASCII only, in every language -- the
// bitmap font has no accents. fork_test checks both.

enum FStrId : uint8_t {
  FS_NEEDS_BOND,     // evolution is ready apart from bond (Phase 2)
  // The natures, in NATURE_* order (fork.h). natureName() indexes from here.
  FS_NAT_EASYGOING,
  FS_NAT_GLUTTON,
  FS_NAT_PLAYFUL,
  FS_NAT_TIDY,
  FS_NAT_LAZY,
  // Evolution rules (Phase 2): why a ready creature waits, and the card's labels.
  FS_EVO_WAIT_DAY,
  FS_EVO_WAIT_NIGHT,
  FS_EVO_DAY,
  FS_EVO_NIGHT,
  // Sickness, medicine and coins (Phase 3).
  FS_SICK,           // the card's status, after the nature
  FS_FEELS_SICK,     // the header's line while sick (statusMsg())
  FS_MEDICINE,       // the bag row
  FS_BUY_MEDICINE,   // the bag's shop row
  FS_COINS_FMT,      // the bag's purse, "%lu"
  FS_CURED,          // after medicine
  FS_NOT_SICK,       // medicine with nothing to cure
  FS_NO_COINS,       // the shop, short of coins
  FS_BOUGHT,         // the shop, after buying
  FS_GOT_COINS,      // a battle win, "%lu"
  FS_REMIND_SICK,    // the phone's sick reminder
  // Phase 4: the step counter
  FS_STEPS_TITLE,    // the steps page
  FS_STEPS_FMT,      // "%lu STEPS": the milestone rows
  FS_STEPS_TOTAL_FMT,// "TOTAL %lu"
  FS_STEPS_GAIN_FMT, // "JOY +%u  BOND +%u", what today's walk gave
  FS_STEPS_WILD_FMT, // "WILD LUCK %u%%": how much of the walking bonus today has earned
  FS_STEPS_NONE,     // no sensor on this phone
  FS_STEPS_OFF,      // not allowed yet: the tap target
  FS_STEPS_BANNER,   // "%lu STEPS!": a milestone's banner
  FS_BAG_FULL,       // ...when the item did not fit
  FS_RANK_BANNER,    // a new trail rank
  FS_RANK_0,         // the trail ranks, FS_RANK_0 + rank
  FS_RANK_1,
  FS_RANK_2,
  FS_RANK_3,
  FS_RANK_4,
  // Phase 5: expeditions
  FS_EXP_TITLE,      // explore's second tab
  FS_EXP_COAST,      // the sites, FS_EXP_COAST + FORK_EXP_* (forkExpSiteName())
  FS_EXP_VOLCANO,
  FS_EXP_FOREST,
  FS_EXP_MIN_FMT,    // "%u MIN": the length pill
  FS_EXP_SEND,       // the button
  FS_EXP_AWAY,       // the header's line while it is out (statusMsg())
  FS_EXP_BACK_IN_FMT,// "BACK IN %u:%02u"
  FS_EXP_BACK,       // a find's banner (size 3: at most 17 characters)
  FS_EXP_BONUS,      // the walking bonus's banner (17)
  FS_EXP_EARLY,      // called back empty-handed (17)
  FS_EXP_NOTHING,    // ...its second line
  FS_EXP_TIRED,      // why it cannot go
  FS_EXP_ASLEEP,
  FS_EXP_SICKNO,
  FS_EXP_ENERGY_FMT, // "ENERGY -%u": what the trip costs
  FS_EXP_WALK_FMT,   // "WALK %u STEPS: +1": the walking bonus
  FS_REMIND_EXP,     // the phone's "it is back" notification
  FS_EXP_AWAY_BAG,   // the bag, a training item while it is out
  FS_COUNT
};

// In the current language (gLang), never null.
const char *FT(FStrId id);
