#include "fork_i18n.h"

// Rows in Lang order: ES, EN, FR, DE, IT, PT. Unaccented, as in i18n.cpp, and
// using the same word for a thing as upstream does (VINCULO / BOND / LIEN /
// BINDUNG / LEGAME / LACO), so the two tables read as one game.
static const char *const FORK_STRINGS[LANG_COUNT][FS_COUNT] = {
  { // ES
    "NECESITA MAS VINCULO",
    "TRANQUILO", "GLOTON", "JUGUETON", "PULCRO", "PEREZOSO",
    "EVOLUCIONA DE DIA", "EVOLUCIONA DE NOCHE", "DIA", "NOCHE",
  },
  { // EN
    "NEEDS MORE BOND",
    "EASYGOING", "GLUTTON", "PLAYFUL", "TIDY", "LAZY",
    "EVOLVES BY DAY", "EVOLVES AT NIGHT", "DAY", "NIGHT",
  },
  { // FR
    "IL FAUT PLUS DE LIEN",
    "INSOUCIANT", "GOURMAND", "JOUEUR", "SOIGNEUX", "PARESSEUX",
    "EVOLUE LE JOUR", "EVOLUE LA NUIT", "JOUR", "NUIT",
  },
  { // DE
    "BRAUCHT MEHR BINDUNG",
    "GELASSEN", "VERFRESSEN", "VERSPIELT", "ORDENTLICH", "FAUL",
    "ENTWICKELT SICH TAGS", "ENTWICKELT SICH NACHTS", "TAG", "NACHT",
  },
  { // IT
    "SERVE PIU LEGAME",
    "TRANQUILLO", "GOLOSO", "GIOCOSO", "ORDINATO", "PIGRO",
    "EVOLVE DI GIORNO", "EVOLVE DI NOTTE", "GIORNO", "NOTTE",
  },
  { // PT
    "PRECISA DE MAIS LACO",
    "TRANQUILO", "GULOSO", "BRINCALHAO", "ASSEADO", "PREGUICOSO",
    "EVOLUI DE DIA", "EVOLUI DE NOITE", "DIA", "NOITE",
  },
};

const char *FT(FStrId id) {
  if (id >= FS_COUNT) return "";
  const uint8_t l = (uint8_t)gLang < LANG_COUNT ? (uint8_t)gLang : (uint8_t)LANG_EN;
  const char *s = FORK_STRINGS[l][id];
  return s ? s : "";
}
