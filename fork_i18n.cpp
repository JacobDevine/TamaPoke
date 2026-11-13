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
    "PASOS", "%lu PASOS", "TOTAL %lu", "ALEGRIA +%u  VINCULO +%u", "SUERTE SALVAJE %u%%",
    "SIN PODOMETRO", "TOCA PARA CONTAR PASOS", "%lu PASOS!", "BOLSA LLENA", "NUEVO RANGO",
    "NOVATO", "PASEANTE", "EXCURSIONISTA", "TROTAMUNDOS", "GUARDABOSQUES",
  },
  { // EN
    "NEEDS MORE BOND",
    "EASYGOING", "GLUTTON", "PLAYFUL", "TIDY", "LAZY",
    "EVOLVES BY DAY", "EVOLVES AT NIGHT", "DAY", "NIGHT",
    "SICK", "It feels sick...", "MEDICINE", "BUY MEDICINE", "COINS %lu",
    "It feels better!", "It is not sick", "Not enough coins", "Bought medicine",
    "+%lu coins", "It is sick and needs medicine",
    "STEPS", "%lu STEPS", "TOTAL %lu", "JOY +%u  BOND +%u", "WILD LUCK %u%%",
    "NO STEP SENSOR", "TAP TO COUNT STEPS", "%lu STEPS!", "BAG IS FULL", "NEW TRAIL RANK",
    "ROOKIE", "WALKER", "HIKER", "TREKKER", "RANGER",
  },
  { // FR
    "IL FAUT PLUS DE LIEN",
    "INSOUCIANT", "GOURMAND", "JOUEUR", "SOIGNEUX", "PARESSEUX",
    "EVOLUE LE JOUR", "EVOLUE LA NUIT", "JOUR", "NUIT",
    "MALADE", "Il est malade...", "REMEDE", "ACHETER REMEDE", "PIECES %lu",
    "Il va mieux !", "Il n'est pas malade", "Pas assez de pieces", "Remede achete",
    "+%lu pieces", "Il est malade et a besoin d'un remede",
    "PAS", "%lu PAS", "TOTAL %lu", "JOIE +%u  LIEN +%u", "CHANCE SAUVAGE %u%%",
    "PAS DE PODOMETRE", "TOUCHE POUR COMPTER", "%lu PAS !", "SAC PLEIN", "NOUVEAU RANG",
    "DEBUTANT", "PROMENEUR", "RANDONNEUR", "GRAND MARCHEUR", "GARDE NATURE",
  },
  { // DE
    "BRAUCHT MEHR BINDUNG",
    "GELASSEN", "VERFRESSEN", "VERSPIELT", "ORDENTLICH", "FAUL",
    "ENTWICKELT SICH TAGS", "ENTWICKELT SICH NACHTS", "TAG", "NACHT",
    "KRANK", "Es ist krank...", "MEDIZIN", "MEDIZIN KAUFEN", "MUENZEN %lu",
    "Es geht ihm besser!", "Es ist nicht krank", "Zu wenig Muenzen", "Medizin gekauft",
    "+%lu Muenzen", "Es ist krank und braucht Medizin",
    "SCHRITTE", "%lu SCHRITTE", "GESAMT %lu", "FREUDE +%u  BINDUNG +%u", "WILDGLUECK %u%%",
    "KEIN SCHRITTZAEHLER", "TIPPEN ZUM ZAEHLEN", "%lu SCHRITTE!", "BEUTEL VOLL", "NEUER RANG",
    "NEULING", "SPAZIERGAENGER", "WANDERER", "TREKKER", "RANGER",
  },
  { // IT
    "SERVE PIU LEGAME",
    "TRANQUILLO", "GOLOSO", "GIOCOSO", "ORDINATO", "PIGRO",
    "EVOLVE DI GIORNO", "EVOLVE DI NOTTE", "GIORNO", "NOTTE",
    "MALATO", "Sta male...", "MEDICINA", "COMPRA MEDICINA", "MONETE %lu",
    "Sta meglio!", "Non e malato", "Monete insufficienti", "Medicina comprata",
    "+%lu monete", "E malato e ha bisogno di medicina",
    "PASSI", "%lu PASSI", "TOTALE %lu", "GIOIA +%u  LEGAME +%u", "FORTUNA SELVATICA %u%%",
    "NESSUN CONTAPASSI", "TOCCA PER CONTARE", "%lu PASSI!", "BORSA PIENA", "NUOVO GRADO",
    "NOVIZIO", "PASSEGGIATORE", "ESCURSIONISTA", "GIRAMONDO", "GUARDIAPARCO",
  },
  { // PT
    "PRECISA DE MAIS LACO",
    "TRANQUILO", "GULOSO", "BRINCALHAO", "ASSEADO", "PREGUICOSO",
    "EVOLUI DE DIA", "EVOLUI DE NOITE", "DIA", "NOITE",
    "DOENTE", "Esta doente...", "REMEDIO", "COMPRAR REMEDIO", "MOEDAS %lu",
    "Ja esta melhor!", "Nao esta doente", "Faltam moedas", "Remedio comprado",
    "+%lu moedas", "Esta doente e precisa de remedio",
    "PASSOS", "%lu PASSOS", "TOTAL %lu", "ALEGRIA +%u  LACO +%u", "SORTE SELVAGEM %u%%",
    "SEM PEDOMETRO", "TOQUE PARA CONTAR", "%lu PASSOS!", "MOCHILA CHEIA", "NOVO NIVEL",
    "NOVATO", "CAMINHANTE", "EXCURSIONISTA", "ANDARILHO", "GUARDA FLORESTAL",
  },
};

const char *FT(FStrId id) {
  if (id >= FS_COUNT) return "";
  const uint8_t l = (uint8_t)gLang < LANG_COUNT ? (uint8_t)gLang : (uint8_t)LANG_EN;
  const char *s = FORK_STRINGS[l][id];
  return s ? s : "";
}
