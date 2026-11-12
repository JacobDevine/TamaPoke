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
  FS_COUNT
};

// In the current language (gLang), never null.
const char *FT(FStrId id);
