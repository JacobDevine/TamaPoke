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
  FS_COUNT
};

// In the current language (gLang), never null.
const char *FT(FStrId id);
