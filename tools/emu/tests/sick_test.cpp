// The Android fork's sickness, medicine and coins (fork.h section 6).
// See android/FEATURES_PLAN.md, Phase 3.
//
// Driven through the real sketch, because every rule here has a CALLER that
// could ignore it (CLAUDE.md trap 1): Pet::tick() for the dice and the drain,
// syncClock() for the absence it must NOT touch, the bag for medicine and the
// shop, buildSquad() for the battle stats, and the win settlement for coins.
// The switch-off half proves the board sees none of it.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "battle.h"
#include "moves.h"
#include "inventory.h"
#include "fork.h"
#include "fork_i18n.h"
#include "ckpt.h"
#include <cstdio>
#include <cstring>

uint32_t g_seed = 31;
FakeSerial Serial;
FakeESP ESP;
FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0;
volatile bool g_touchDown = false;
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }
void FakeESP::restart() { exit(0); }

void emuAdvanceMs(uint32_t ms);
void setup();
void loop();
void render();
void onTap(int16_t x, int16_t y);
int uiBagRowCenterY(int i);
int btlCellIndexAt(int16_t x, int16_t y);
bool startWildBattle(uint8_t region, bool hard);
const char *statusMsg();
uint8_t forkBagRows();
extern Pet pet;
extern bool bagOpen, battleOpen, btlOver, btlWon, btlWild;
extern uint8_t bagPage, btlMenu, btlMsgCount;
extern uint16_t squadMask;
extern Combatant btlSquad[], btlFoe, btlYou;
extern uint32_t btlCoinGain;
extern char btlMsg[6][40];

static int bad = 0;
static void ck(bool ok, const char *what) {
  printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) bad++;
}

// A healthy, awake, unrisky creature: nothing about it can make it sick.
static void healthy() {
  pet.dbgHatchAs(4, false);   // CHARMANDER: SCRATCH from level 1
  // EASYGOING: no nature drain, whose minute counter is shared between runs
  // and would put the two twins below out of step.
  pet.ivDef = 20; pet.ivSpe = 7; pet.ivHp = 25;
  for (pet.ivAtk = 0; natureFromIVs(pet.ivAtk, 20, 7, 25, false) != NATURE_EASYGOING; pet.ivAtk++) {}
  pet.ageMinutes = 14 * MINUTES_PER_LEVEL;
  pet.fullness = 40;          // > 40 rolls poops in tick(); 40 is still fed
  pet.joy = pet.energy = pet.hygiene = 90;
  pet.poops = 0;
  pet.weight = 0;
  pet.sleeping = false;
  pet.careMistakes = 0;
  while (pet.hasLearnOffer()) pet.declineLearn();
}
static void makeSick() {
  forkData.st.sickKey = forkCreatureKey(pet);
  forkData.st.sickMinutes = 0;
}
static void refill() { pet.fullness = 40; pet.joy = pet.energy = pet.hygiene = 90; pet.poops = 0; }

static void tapBagRow(uint8_t row) {
  bagOpen = true;
  bagPage = 0;
  render();
  onTap(233, uiBagRowCenterY(row));
  bagOpen = true;   // a miss would have closed it; the checks below say whether it hit
}

int main() {
  gRegionArt = 0xFFFF;

  // ---- the switch off: the board ------------------------------------------------
  forkSetFeatures(false);
  setup();
  if (pet.awaitingStarter()) pet.dbgHatchAs(4, false);
  healthy();
  forkData.st.sickKey = forkCreatureKey(pet);   // even a record saying so
  ck(!forkSick(pet), "off: a creature is never sick, whatever the record says");
  pet.poops = 3;
  ck(!forkSickRisk(pet), "off: no risk is ever reported");
  ck(forkBagRows() == 0, "off: the bag has no fork rows");
  ck(forkWinCoins(true, true, 5) == 0 && forkAddCoins(50) == 0, "off: a win pays nothing");
  forkData.st = ForkState();

  // ---- on: the phone --------------------------------------------------------------
  forkSetFeatures(true);
  forkData.begin();
  ck(forkData.st.medicine == FORK_MED_START && (forkData.st.flags & FORK_FLAG_MED_GIVEN),
     "a player starts with the starting medicine");
  forkFlush();
  forkData.begin();
  ck(forkData.st.medicine == FORK_MED_START, "and gets it once, not again on the next load");

  // the record: new fields round-trip, and an old 4-byte body reads them as zero
  {
    ForkState s; s.createdEpoch = 7; s.sickKey = 0x80012345u; s.sickMinutes = 300;
    s.medicine = 9; s.flags = 1; s.coins = 4321;
    uint8_t buf[FORK_BLOB_CAP];
    uint16_t n = Fork::encode(s, buf, sizeof(buf), 3);
    ForkState r;
    ck(n && Fork::decode(buf, n, r) && r.sickKey == s.sickKey && r.sickMinutes == 300 &&
       r.medicine == 9 && r.flags == 1 && r.coins == 4321, "the Phase 3 fields round-trip");
    // a Phase 0-2 record: header + 4 bytes of body + crc
    uint8_t old[64];
    memcpy(old, buf, CKPT_HDR + 4);
    memcpy(old + CKPT_HDR + 4, buf + CKPT_HDR + FORK_BODY_LEN, CKPT_CRC);
    ForkState o;
    ck(Fork::decode(old, CKPT_HDR + 4 + CKPT_CRC, o) && o.createdEpoch == 7 &&
       o.sickKey == 0 && o.medicine == 0 && o.flags == 0 && o.coins == 0,
       "a record from before Phase 3 loads with no sickness, no medicine flag and no coins");
  }

  // the conditions, at their boundaries
  healthy();
  ck(!forkSickRisk(pet), "a cared-for creature is at no risk");
  pet.poops = FORK_SICK_POOPS - 1; ck(!forkSickRisk(pet), "one poop is no risk");
  pet.poops = FORK_SICK_POOPS;     ck(forkSickRisk(pet), "two poops are a risk");
  healthy(); pet.weight = FORK_SICK_WEIGHT;     ck(!forkSickRisk(pet), "weight 50 is no risk");
  pet.weight = FORK_SICK_WEIGHT + 1;            ck(forkSickRisk(pet), "overweight is a risk");
  healthy(); pet.joy = FORK_SICK_NEED + 1;      ck(!forkSickRisk(pet), "a need at 21 is no risk");
  pet.joy = FORK_SICK_NEED;                     ck(forkSickRisk(pet), "a need at 20 is a risk");

  // the dice
  forkData.st.sickKey = 0;
  healthy(); pet.poops = 2;
  forkSickTick(pet, FORK_SICK_CHANCE);
  ck(!forkSick(pet), "at risk, a roll of 2 does not make it sick");
  forkSickTick(pet, FORK_SICK_CHANCE - 1);
  ck(forkSick(pet), "at risk, a roll under 2 does");
  forkData.st.sickKey = 0;
  healthy();
  forkSickTick(pet, 0);
  ck(!forkSick(pet), "with no risk even a roll of 0 does not");
  healthy(); pet.poops = 2; pet.sleeping = true;
  forkSickTick(pet, 0);
  ck(!forkSick(pet), "asleep, it cannot fall sick");

  // the caller: tick() really rolls. Over 600 at-risk awake minutes at 2% it
  // falls sick (the chance it would not is 0.98^600, about 5e-6).
  forkData.st.sickKey = 0;
  healthy();
  bool fell = false;
  for (int i = 0; i < 600 && !fell; i++) {
    refill(); pet.poops = 2;
    pet.dbgTick();
    fell = forkSick(pet);
  }
  ck(fell, "tick() rolls the dice: an at-risk creature falls sick");

  // the drain, through tick(): over three minutes a sick twin has lost one more
  // of every need. Three, because fullness starts at 40 (no poop rolls) and
  // must stay over 30, where joy's own drain would start to differ.
  {
    healthy(); forkData.st.sickKey = 0;
    g_seed = 1234;
    for (int i = 0; i < 3; i++) pet.dbgTick();
    const uint8_t f = pet.fullness, j = pet.joy, e = pet.energy, h = pet.hygiene;
    healthy(); makeSick();
    g_seed = 1234;
    for (int i = 0; i < 3; i++) pet.dbgTick();
    ck(pet.fullness + 1 == f && pet.joy + 1 == j && pet.energy + 1 == e && pet.hygiene + 1 == h,
       "sick, tick() drains every need one point more each three minutes");
  }

  // the mistake clock, through tick()
  healthy(); makeSick();
  for (int i = 0; i < FORK_SICK_MISTAKE_MIN - 1; i++) { refill(); pet.dbgTick(); }
  ck(pet.careMistakes == 0, "119 sick minutes are not yet a care mistake");
  refill(); pet.dbgTick();
  ck(pet.careMistakes == 1, "the 120th is one");
  for (int i = 0; i < FORK_SICK_MISTAKE_MIN; i++) { refill(); pet.dbgTick(); }
  ck(pet.careMistakes == 2, "and every 120 after that another");

  // asleep: nothing moves
  healthy(); makeSick(); pet.sleeping = true;
  for (int i = 0; i < 3 * FORK_SICK_MISTAKE_MIN; i++) pet.dbgTick();
  ck(forkData.st.sickMinutes == 0 && pet.careMistakes == 0,
     "asleep, six hours sick add no minutes and no mistakes");

  // offline: ten hours closed, sick, at risk -- nothing (sleep_test's shape)
  {
    healthy(); makeSick(); pet.poops = 3;
    // 2026-11-12 12:00 UTC: past CLOCK_EPOCH_FLOOR, or the absence is refused
    // and every check below passes without anything having been applied.
    const uint32_t t0 = 20769u * 86400u + 12 * 3600;
    pet.dbgSetSeen(t0);
    const uint32_t age0 = pet.ageMinutes;
    pet.syncClock(t0 + 10 * 3600);
    ck(pet.ageMinutes == age0 + 600, "(the ten hours were really applied)");
    ck(forkSick(pet) && forkData.st.sickMinutes == 0 && pet.careMistakes == 0,
       "ten hours closed: still sick, but no minutes and no mistakes counted");
    ck(pet.fullness >= 15 && pet.joy >= 15 && pet.energy >= 15 && pet.hygiene >= 15,
       "and the offline floor of 15 holds");
    forkData.st.sickKey = 0;
    healthy(); pet.poops = 3;
    pet.dbgSetSeen(t0);
    pet.syncClock(t0 + 10 * 3600);
    ck(!forkSick(pet), "ten hours closed at risk do not make it sick");
  }

  // the owner
  healthy(); makeSick();
  const uint8_t keepIv = pet.ivAtk;
  pet.ivAtk = (uint8_t)(keepIv ^ 1);
  ck(!forkSick(pet), "another creature is not the sick one");
  pet.ivAtk = keepIv;
  ck(forkSick(pet), "and the sick one, back, is still sick");
  pet.speciesId = 5;
  ck(forkSick(pet), "evolving does not cure it");
  healthy();

  // the header and the card say so
  makeSick();
  ck(!strcmp(statusMsg(), FT(FS_FEELS_SICK)), "the header says it feels sick");
  forkData.st.sickKey = 0;
  ck(strcmp(statusMsg(), FT(FS_FEELS_SICK)) != 0, "and stops once it is not");

  // medicine, through the bag
  bag.clear();
  makeSick();
  forkData.st.medicine = 2;
  tapBagRow(0);
  ck(!forkSick(pet) && forkData.st.medicine == 1, "the bag's MEDICINE row cures it and uses one");
  tapBagRow(0);
  ck(forkData.st.medicine == 1, "with nothing to cure, no medicine is used");
  forkData.st.medicine = 0;
  makeSick();
  tapBagRow(0);
  ck(forkSick(pet), "with no medicine, nothing is cured");

  // the shop
  forkData.st.coins = FORK_MED_PRICE - 1;
  tapBagRow(1);
  ck(forkData.st.medicine == 0 && forkData.st.coins == FORK_MED_PRICE - 1,
     "the shop refuses a player short of coins");
  forkData.st.coins = FORK_MED_PRICE + 5;
  tapBagRow(1);
  ck(forkData.st.medicine == 1 && forkData.st.coins == 5, "and sells one for 30");
  {
    Fork reload; reload.begin();
    ck(reload.st.medicine == 1 && reload.st.coins == 5 && reload.st.sickKey == forkData.st.sickKey,
       "the purchase and the sickness are on disk");
  }
  // upstream's items still work, one row down past ours
  bag.clear();
  bag.add(IT_PROTEIN);
  healthy();
  pet.trAtk = 0;
  tapBagRow(forkBagRows());
  ck(pet.trAtk == 5 && !bag.has(IT_PROTEIN), "upstream's items follow the fork rows and still work");

  forkData.st.coins = FORK_COINS_MAX - 2;
  ck(forkAddCoins(10) == 2 && forkData.st.coins == FORK_COINS_MAX, "the purse stops at 9999");

  // what a win pays
  ck(forkWinCoins(true, false, -1) == 5 && forkWinCoins(true, true, -1) == 10,
     "a wild win pays 5, hard 10");
  ck(forkWinCoins(false, false, 0) == 10 && forkWinCoins(false, false, 7) == 24 &&
     forkWinCoins(false, true, 7) == 48, "a gym win pays 10 + 2 a leader, hard double");

  // the battle: buildSquad() weakens the live pet while sick
  bagOpen = false;
  healthy();
  forkData.st.sickKey = 0;
  squadMask = 1;
  ck(startWildBattle(REGION_ALL, false), "a wild battle starts");
  Combatant well = btlSquad[0];
  battleOpen = false;
  makeSick();
  ck(startWildBattle(REGION_ALL, false), "and starts while sick");
  bool weaker = true;
  for (int i = 0; i < SI_COUNT; i++)
    if (btlSquad[0].base[i] != (uint16_t)(well.base[i] * FORK_SICK_BATTLE_PCT / 100 ? well.base[i] * FORK_SICK_BATTLE_PCT / 100 : 1))
      weaker = false;
  ck(weaker && btlSquad[0].maxHp == well.maxHp, "sick, its battle stats are 90% and its HP is not");

  // and a win pays, through the real settlement
  forkData.st.coins = 0;
  // One HP out of one: a hurt wild foe may flee (wildFoeEscapeChance), and at
  // full health it does not.
  btlFoe.hp = btlFoe.maxHp = 1;
  btlFoe.base[SI_SPE] = 1;
  // and a sure, damaging first move (CHARMANDER's own first is GROWL)
  btlYou.moves[0] = MV_SCRATCH;
  btlYou.hp = btlYou.maxHp = 999;
  int16_t cell[4][2];
  for (int c = 0; c < 4; c++) {
    long sx = 0, sy = 0, n = 0;
    for (int16_t y = 0; y < 466; y += 2)
      for (int16_t x = 0; x < 466; x += 2)
        if (btlCellIndexAt(x, y) == c) { sx += x; sy += y; n++; }
    cell[c][0] = n ? (int16_t)(sx / n) : 0;
    cell[c][1] = n ? (int16_t)(sy / n) : 0;
  }
  for (int step = 0; step < 300 && !btlOver; step++) {
    emuAdvanceMs(400);
    render();
    onTap(cell[0][0], cell[0][1]);   // dismiss, FIGHT, then the first move
  }
  ck(btlOver && btlWon && btlWild, "the wild battle is won");
  ck(btlCoinGain == FORK_COINS_WILD && forkData.st.coins == FORK_COINS_WILD,
     "the win settlement pays the wild coins");
  battleOpen = false;

  forkSetFeatures(false);
  printf("%s: %d failure(s)\n", bad ? "FAIL" : "PASS", bad);
  return bad ? 1 : 0;
}
