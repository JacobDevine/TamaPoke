#include "fork_i18n.h"

// Rows in Lang order: ES, EN, FR, DE, IT, PT. Unaccented, as in i18n.cpp, and
// using the same word for a thing as upstream does (VINCULO / BOND / LIEN /
// BINDUNG / LEGAME / LACO), so the two tables read as one game.
static const char *const FORK_STRINGS[LANG_COUNT][FS_COUNT] = {
  { // ES
    "NECESITA MAS VINCULO",
    "TRANQUILO", "GLOTON", "JUGUETON", "PULCRO", "PEREZOSO",
  },
  { // EN
    "NEEDS MORE BOND",
    "EASYGOING", "GLUTTON", "PLAYFUL", "TIDY", "LAZY",
  },
  { // FR
    "IL FAUT PLUS DE LIEN",
    "INSOUCIANT", "GOURMAND", "JOUEUR", "SOIGNEUX", "PARESSEUX",
  },
  { // DE
    "BRAUCHT MEHR BINDUNG",
    "GELASSEN", "VERFRESSEN", "VERSPIELT", "ORDENTLICH", "FAUL",
  },
  { // IT
    "SERVE PIU LEGAME",
    "TRANQUILLO", "GOLOSO", "GIOCOSO", "ORDINATO", "PIGRO",
  },
  { // PT
    "PRECISA DE MAIS LACO",
    "TRANQUILO", "GULOSO", "BRINCALHAO", "ASSEADO", "PREGUICOSO",
  },
};

const char *FT(FStrId id) {
  if (id >= FS_COUNT) return "";
  const uint8_t l = (uint8_t)gLang < LANG_COUNT ? (uint8_t)gLang : (uint8_t)LANG_EN;
  const char *s = FORK_STRINGS[l][id];
  return s ? s : "";
}
