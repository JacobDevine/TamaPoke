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
  // Phase 7: the pet acting on its own
  FS_SELFMED,        // banner: it took its own medicine (size 3: at most 17)
  FS_SELFCARE_ON,    // the settings switch, on and off
  FS_SELFCARE_OFF,
  // The bubbles, in FORK_SAY_* order from SICK (fork.h section 10). At most 24.
  FS_SAY_SICK,
  FS_SAY_HUNGRY,
  FS_SAY_TIRED,
  FS_SAY_DIRTY,
  FS_SAY_BORED,
  FS_SAY_MESS,
  FS_SAY_MORNING,
  FS_SAY_SELFCARE,
  FS_SAY_WALKED_FMT, // "%lu": yesterday's steps
  // The phone's retire confirm: what retiring EARLY costs, in plain words.
  FS_RETIRE_GONE,    // the creature is not kept anywhere
  FS_RETIRE_LATE,    // the next creature's evolutions wait a day
  // Polish Tier 2 (android/README.md): the settings' haptics switch, and what
  // a tap on a home button says while the creature sleeps (at most 24).
  FS_BUZZ_ON,
  FS_BUZZ_OFF,
  FS_ASLEEP_TAP,
  // Phase 8a: the world from the clock (fork.h section 11).
  // Holiday banner titles, in FORK_HOL_* order from NEWYEAR (at most 17).
  FS_HOL_NEWYEAR, FS_HOL_VALENTINE, FS_HOL_EASTER, FS_HOL_HALLOWEEN,
  FS_HOL_CHRISTMAS, FS_HOL_BIRTHDAY, FS_HOL_HATCHDAY,
  // ...and the creature's greeting bubble, same order (at most 24).
  FS_HOLSAY_NEWYEAR, FS_HOLSAY_VALENTINE, FS_HOLSAY_EASTER, FS_HOLSAY_HALLOWEEN,
  FS_HOLSAY_CHRISTMAS, FS_HOLSAY_BIRTHDAY, FS_HOLSAY_HATCHDAY,
  FS_GIFT_FMT,       // "GIFT: %s", a gift's banner
  // The standby clock's date, short: Sunday first, then January first.
  FS_WD_SUN, FS_WD_MON, FS_WD_TUE, FS_WD_WED, FS_WD_THU, FS_WD_FRI, FS_WD_SAT,
  FS_MON_JAN, FS_MON_FEB, FS_MON_MAR, FS_MON_APR, FS_MON_MAY, FS_MON_JUN,
  FS_MON_JUL, FS_MON_AUG, FS_MON_SEP, FS_MON_OCT, FS_MON_NOV, FS_MON_DEC,
  FS_BDAY_ASK,       // the player card's birthday chip, before one is set
  // The settings' charging row, split in two halves (at most 13 each), and
  // the caption above it.
  FS_SELFCARE_SHORT_ON, FS_SELFCARE_SHORT_OFF, FS_STANDBY_ON, FS_STANDBY_OFF,
  FS_ON_CHARGE,
  // Phase 8b: the weather (fork.h section 12). The settings switch, the
  // language row's right half (at most 13 each).
  FS_WEATHER_ON, FS_WEATHER_OFF,
  // The kinds' names for the standby clock, FORK_WX_* order from CLEAR (at most 7).
  FS_WX_CLEAR, FS_WX_CLOUDY, FS_WX_FOG, FS_WX_RAIN, FS_WX_SNOW, FS_WX_STORM,
  // The bubbles (at most 24): liking it, disliking it, and per kind from CLEAR
  // when it does not mind either way.
  FS_WXSAY_LIKE, FS_WXSAY_DISLIKE,
  FS_WXSAY_CLEAR, FS_WXSAY_CLOUDY, FS_WXSAY_FOG, FS_WXSAY_RAIN, FS_WXSAY_SNOW, FS_WXSAY_STORM,
  // Phase 9: the farm and the album (fork.h section 13).
  FS_FARM_TAB,          // the party screen's second tab (at most 9)
  FS_FARM_COUNT_FMT,    // "%u ON THE FARM", everybody shown (at most 24)
  FS_FARM_SHOWN_FMT,    // "%u OF %u OUT TODAY", a day's turn (at most 24)
  FS_FARM_EMPTY,        // nobody banked yet (at most 24)
  FS_ALBUM_TITLE,       // the medals screen's album pages (at most 9)
  FS_ALBUM_EMPTY,       // (at most 24)
  // The endings, FORK_END_* order from FAREWELL (at most 12).
  FS_END_FAREWELL, FS_END_RETIRED, FS_END_RUNAWAY, FS_END_RELEASED,
  FS_ALBUM_MEDALS_FMT,  // "%u MEDALS" (at most 14)
  // Phase 12c: adventures (fork_adv.h). Explore's third tab and its run.
  FS_ADV_TITLE,         // the tab (shrinks to fit a third of the bar)
  FS_ADV_EASY, FS_ADV_NORMAL, FS_ADV_HARD,   // RAID_* order
  FS_ADV_START,
  FS_ADV_FREE,          // the day's free run is still there
  FS_ADV_COST_FMT,      // "%u COINS (YOU HAVE %lu)"
  FS_ADV_BLURB1, FS_ADV_BLURB2,
  FS_ADV_LOAN, FS_ADV_DOOR,
  FS_ADV_STAGE_FMT,     // "STAGE %u/%u"
  FS_ADV_BOSS,
  FS_ADV_CAUGHT_FMT,    // "CAUGHT %s!"
  FS_ADV_SWAP, FS_ADV_STAY,
  FS_ADV_KEEP, FS_ADV_NONE, FS_ADV_DONE,
  FS_ADV_WON, FS_ADV_LOST,
  FS_ADV_GIVEUP, FS_ADV_SURE,
  // The fight's narration (at most 35 with a name filled in).
  FS_ADV_BECAME_FMT,    // "%s became %s!" -- the name, then the form's label
  FS_ADV_REVIVED_FMT, FS_ADV_HURT_FMT,
  FS_ADV_SHIELDS_HIT_FMT,   // "Shields -%u"
  FS_ADV_STAGGER, FS_ADV_SHIELDS_UP, FS_ADV_HEART,
  FS_ADV_KEPT_FMT,      // after the keep, "%s JOINED YOU!"
  // Phone to phone (plan step 12e): the LAN screen's host list and address.
  FS_NET_LOOKING, FS_NET_NONE,
  FS_NET_TYPE,          // the button, and the address dialog's title
  FS_NET_ADDR_FMT,      // "ADDRESS %s", under a waiting host's status
  FS_NET_WAIT_GUEST,
  // Adventures with other players (plan step 12e part 3).
  FS_RAID_FRIENDS, FS_RAID_HOST, FS_RAID_JOIN,
  FS_RAID_FIND,         // the host list's title
  FS_RAID_JOINING,
  FS_RAID_WAIT,         // this phone has chosen; somebody else has not
  FS_RAID_WAIT_HOST,    // a guest's lobby, until the host starts
  FS_RAID_AI, FS_RAID_YOU,   // whose seat: a companion, or this phone's
  FS_RAID_AWAY,         // a guest's run with no host to talk to
  FS_RAID_REJOIN,
  FS_RAID_NO_NET,
  // Why a host said no (RNR_*), or never answered.
  FS_RAID_R_VERSION, FS_RAID_R_FULL, FS_RAID_R_STARTED, FS_RAID_R_GONE, FS_RAID_R_SILENT,
  FS_RAID_OK,
  FS_ADV_ENDED,         // the KEEP's heading after a GIVE UP (yours or the host's)
  FS_COUNT
};

// In the current language (gLang), never null.
const char *FT(FStrId id);
