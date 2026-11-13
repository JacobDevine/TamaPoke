#include "fork_i18n.h"

// Rows in Lang order: ES, EN, FR, DE, IT, PT. Unaccented, as in i18n.cpp, and
// using the same word for a thing as upstream does (VINCULO / BOND / LIEN /
// BINDUNG / LEGAME / LACO), so the two tables read as one game.
static const char *const FORK_STRINGS[LANG_COUNT][FS_COUNT] = {
  { // ES
    "NECESITA MAS VINCULO",
    "TRANQUILO", "GLOTON", "JUGUETON", "PULCRO", "PEREZOSO",
    "EVOLUCIONA DE DIA", "EVOLUCIONA DE NOCHE", "DIA", "NOCHE",
    "ENFERMO", "Esta enfermo...", "MEDICINA", "COMPRAR MEDICINA", "MONEDAS %lu",
    "Ya esta mejor!", "No esta enfermo", "Faltan monedas", "Medicina comprada",
    "+%lu monedas", "Esta enfermo y necesita medicina",
  },
  { // EN
    "NEEDS MORE BOND",
    "EASYGOING", "GLUTTON", "PLAYFUL", "TIDY", "LAZY",
    "EVOLVES BY DAY", "EVOLVES AT NIGHT", "DAY", "NIGHT",
    "SICK", "It feels sick...", "MEDICINE", "BUY MEDICINE", "COINS %lu",
    "It feels better!", "It is not sick", "Not enough coins", "Bought medicine",
    "+%lu coins", "It is sick and needs medicine",
  },
  { // FR
    "IL FAUT PLUS DE LIEN",
    "INSOUCIANT", "GOURMAND", "JOUEUR", "SOIGNEUX", "PARESSEUX",
    "EVOLUE LE JOUR", "EVOLUE LA NUIT", "JOUR", "NUIT",
    "MALADE", "Il est malade...", "REMEDE", "ACHETER REMEDE", "PIECES %lu",
    "Il va mieux !", "Il n'est pas malade", "Pas assez de pieces", "Remede achete",
    "+%lu pieces", "Il est malade et a besoin d'un remede",
  },
  { // DE
    "BRAUCHT MEHR BINDUNG",
    "GELASSEN", "VERFRESSEN", "VERSPIELT", "ORDENTLICH", "FAUL",
    "ENTWICKELT SICH TAGS", "ENTWICKELT SICH NACHTS", "TAG", "NACHT",
    "KRANK", "Es ist krank...", "MEDIZIN", "MEDIZIN KAUFEN", "MUENZEN %lu",
    "Es geht ihm besser!", "Es ist nicht krank", "Zu wenig Muenzen", "Medizin gekauft",
    "+%lu Muenzen", "Es ist krank und braucht Medizin",
  },
  { // IT
    "SERVE PIU LEGAME",
    "TRANQUILLO", "GOLOSO", "GIOCOSO", "ORDINATO", "PIGRO",
    "EVOLVE DI GIORNO", "EVOLVE DI NOTTE", "GIORNO", "NOTTE",
    "MALATO", "Sta male...", "MEDICINA", "COMPRA MEDICINA", "MONETE %lu",
    "Sta meglio!", "Non e malato", "Monete insufficienti", "Medicina comprata",
    "+%lu monete", "E malato e ha bisogno di medicina",
  },
  { // PT
    "PRECISA DE MAIS LACO",
    "TRANQUILO", "GULOSO", "BRINCALHAO", "ASSEADO", "PREGUICOSO",
    "EVOLUI DE DIA", "EVOLUI DE NOITE", "DIA", "NOITE",
    "DOENTE", "Esta doente...", "REMEDIO", "COMPRAR REMEDIO", "MOEDAS %lu",
    "Ja esta melhor!", "Nao esta doente", "Faltam moedas", "Remedio comprado",
    "+%lu moedas", "Esta doente e precisa de remedio",
  },
};

const char *FT(FStrId id) {
  if (id >= FS_COUNT) return "";
  const uint8_t l = (uint8_t)gLang < LANG_COUNT ? (uint8_t)gLang : (uint8_t)LANG_EN;
  const char *s = FORK_STRINGS[l][id];
  return s ? s : "";
}
