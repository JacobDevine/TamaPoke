#include "fork_i18n.h"

// Rows in Lang order: ES, EN, FR, DE, IT, PT. Unaccented, as in i18n.cpp, and
// using the same word for a thing as upstream does (VINCULO / BOND / LIEN /
// BINDUNG / LEGAME / LACO), so the two tables read as one game.
static const char *const FORK_STRINGS[LANG_COUNT][FS_COUNT] = {
  { "NECESITA MAS VINCULO" },  // ES
  { "NEEDS MORE BOND" },       // EN
  { "IL FAUT PLUS DE LIEN" },  // FR
  { "BRAUCHT MEHR BINDUNG" },  // DE
  { "SERVE PIU LEGAME" },      // IT
  { "PRECISA DE MAIS LACO" },  // PT
};

const char *FT(FStrId id) {
  if (id >= FS_COUNT) return "";
  const uint8_t l = (uint8_t)gLang < LANG_COUNT ? (uint8_t)gLang : (uint8_t)LANG_EN;
  const char *s = FORK_STRINGS[l][id];
  return s ? s : "";
}
