// The Android fork's evolution rules (fork.h section 5, evo_rules.h).
// See android/FEATURES_PLAN.md, Phase 2.
//
// Every condition is checked BOTH ways -- the creature that meets it goes
// there, the one that does not, does not -- through the real canEvolveNow()
// and evolve(), not through forkEvoPick() alone: the rule being right while a
// caller ignores it is CLAUDE.md trap 1, and the evolution threshold already
// shipped that way once. The switch-off half proves the board's evolutions are
// still upstream's.
#include "Arduino.h"
#include "Preferences.h"
#include "fork.h"
#include "fork_i18n.h"
#include "evo_rules.h"
#include "pet.h"
#include "dex.h"
#include <cstdio>
#include <cstring>

#ifndef TAMA_NATIVE_TEST
uint32_t g_seed = 7;
FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0; volatile bool g_touchDown = false;
bool wasPressed = false;
uint32_t millis() { return 0; }
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }
void sfxPlay(uint8_t) {}
#endif

static int bad = 0;
static void ck(bool ok, const char *what) {
  printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) bad++;
}

// 2026-11-12, 12:00 and 23:00 UTC: the hour is read straight off the epoch.
static const uint32_t NOON = 20404u * 86400u + 12 * 3600;
static const uint32_t NIGHT = NOON + 11 * 3600;

// IVs that give each nature, found by asking the real hash.
static uint8_t NAT_IV[NATURE_COUNT][4];
static void findNatureIVs() {
  bool got[NATURE_COUNT] = {};
  for (int a = 0; a < 32; a++)
    for (int d = 0; d < 32; d++) {
      const uint8_t n = natureFromIVs(a, d, 10, 10, false);
      if (!got[n]) { got[n] = true; NAT_IV[n][0] = a; NAT_IV[n][1] = d; NAT_IV[n][2] = 10; NAT_IV[n][3] = 10; }
    }
}

// A creature of `dex`, fed and at `lvl`, with `nature`, awake at `epoch`.
static void raise(Pet &p, int16_t dex, uint8_t lvl, uint8_t nature, uint32_t epoch) {
  p.begin();
  p.dbgHatchAs(dex, false);
  p.ivAtk = NAT_IV[nature][0]; p.ivDef = NAT_IV[nature][1];
  p.ivSpe = NAT_IV[nature][2]; p.ivHp = NAT_IV[nature][3];
  p.ageMinutes = (uint32_t)(lvl - 1) * MINUTES_PER_LEVEL;
  p.fullness = p.joy = p.energy = p.hygiene = 100;
  p.careMistakes = 0;
  p.bond = 0;
  p.trAtk = p.trDef = 0;
  p.dbgSetSeen(epoch);
}

// registerSpecies() is private; the bitmap is the public state it writes.
static void reg(Pet &p, int16_t d) { p.dexReg[(d - 1) >> 3] |= (uint8_t)(1 << ((d - 1) & 7)); }

// Evolve through the real gate; -1 when it would not.
static int16_t evolveNow(Pet &p) {
  if (!p.canEvolveNow()) return -1;
  p.evolve();
  return p.speciesId;
}

int main() {
  findNatureIVs();
  gRegionArt = 0xFFFF;

  // --- the table itself --------------------------------------------------------
  {
    bool ok = true;
    for (int i = 0; i < EVO_RULE_COUNT; i++) {
      const EvoRule &r = EVO_RULES[i];
      if (r.from < 1 || r.from > DEX_COUNT || r.to < 1 || r.to > DEX_COUNT) ok = false;
      // the level comes from dex.h, so a source must have one
      else if (DEX_TBL[r.from].evolvesTo == 0 || DEX_TBL[r.from].evolveLevel == 0) {
        printf("      %s has rules but no dex.h evolution\n", DEX_TBL[r.from].name); ok = false;
      }
    }
    ck(ok, "every rule is in range and its source has a dex.h level");
  }
  {
    // nature groups cover all five natures exactly once, so every creature with
    // a nature branch has exactly one nature destination
    bool ok = true;
    for (int i = 0; i < EVO_RULE_COUNT; i++) {
      if (i && EVO_RULES[i - 1].from == EVO_RULES[i].from) continue;
      uint8_t seen = 0, any = 0;
      bool clash = false;
      for (int j = i; j < EVO_RULE_COUNT && EVO_RULES[j].from == EVO_RULES[i].from; j++) {
        if (EVO_RULES[j].cond != EVC_NATURE) continue;
        any = 1;
        if (seen & EVO_RULES[j].arg) clash = true;
        seen |= EVO_RULES[j].arg;
      }
      if (any && (clash || seen != (1u << NATURE_COUNT) - 1)) {
        printf("      %s: nature masks overlap or leave a gap\n", DEX_TBL[EVO_RULES[i].from].name);
        ok = false;
      }
    }
    ck(ok, "each source's nature branches cover the five natures exactly once");
  }
  {
    // the rules never drop what dex.h already does
    bool ok = true;
    for (int i = 0; i < EVO_RULE_COUNT; i++) {
      const int16_t f = EVO_RULES[i].from;
      bool has = false;
      for (int j = 0; j < EVO_RULE_COUNT; j++)
        if (EVO_RULES[j].from == f && EVO_RULES[j].to == DEX_TBL[f].evolvesTo) has = true;
      if (!has) ok = false;
    }
    ck(ok, "every ruled source keeps its dex.h target among its forms");
  }

  // --- switch OFF: upstream's evolutions exactly ---------------------------------
  forkSetFeatures(false);
  {
    bool none = true;
    for (int16_t d = 1; d <= DEX_COUNT; d++)
      if (forkEvoRuled(d) || forkEggBarred(d)) none = false;
    ck(none, "off: no species is ruled and none is barred from eggs");
    Pet p; raise(p, 236, 30, NATURE_EASYGOING, NOON);
    p.trDef = 20;                                        // would be Hitmonchan with the rules
    ck(evolveNow(p) == 106, "off: Tyrogue still becomes Hitmonlee whatever its training");
    Pet q; raise(q, 172, 30, NATURE_EASYGOING, NOON);   // no bond at all
    ck(evolveNow(q) == 25, "off: Pichu still evolves on level alone");
    ck(q.forkEvoWaitText() == nullptr, "off: the card has no wait text to show");
  }

  forkSetFeatures(true);

  // --- Tyrogue: Attack vs Defence training ----------------------------------------
  {
    Pet a; raise(a, 236, 30, NATURE_EASYGOING, NOON); a.trAtk = 12; a.trDef = 5;
    Pet b; raise(b, 236, 30, NATURE_EASYGOING, NOON); b.trAtk = 5;  b.trDef = 12;
    Pet c; raise(c, 236, 30, NATURE_EASYGOING, NOON); c.trAtk = 9;  c.trDef = 9;
    ck(evolveNow(a) == 106, "Tyrogue, ATK > DEF: Hitmonlee");
    ck(evolveNow(b) == 107, "Tyrogue, DEF > ATK: Hitmonchan");
    ck(evolveNow(c) == 237, "Tyrogue, ATK = DEF: Hitmontop");
  }

  // --- the level gate is unchanged, and the card's level is the real one ------------
  {
    Pet p; raise(p, 236, 1, NATURE_EASYGOING, NOON);
    p.careMistakes = 2;
    const uint16_t need = p.forkEvoLevel();
    p.ageMinutes = (uint32_t)(need - 2) * MINUTES_PER_LEVEL;   // level need-1
    const bool before = p.canEvolveNow();
    p.ageMinutes = (uint32_t)(need - 1) * MINUTES_PER_LEVEL;   // level need
    const bool at = p.canEvolveNow();
    ck(need == 20 + 2 && !before && at,
       "the card's level is where canEvolveNow() flips, care mistakes included");
  }

  // --- bond ------------------------------------------------------------------------
  {
    Pet p; raise(p, 172, 30, NATURE_EASYGOING, NOON);
    p.bond = FORK_EVO_BOND - 1;
    ck(!p.canEvolveNow(), "Pichu one bond short does not evolve");
    ck(p.forkEvoWaitText() && !strcmp(p.forkEvoWaitText(), FT(FS_NEEDS_BOND)),
       "...and the card says it needs more bond");
    p.bond = FORK_EVO_BOND;
    ck(p.forkEvoWaitText() == nullptr, "with enough bond the hint goes away");
    ck(evolveNow(p) == 25, "Pichu with enough bond becomes Pikachu");
  }
  {
    Pet p; raise(p, 406, 30, NATURE_EASYGOING, NIGHT);     // Budew: by day
    p.bond = 100;
    ck(!p.canEvolveNow(), "Budew at night does not evolve");
    ck(p.forkEvoWaitText() && !strcmp(p.forkEvoWaitText(), FT(FS_EVO_WAIT_DAY)),
       "...and the card says it waits for day");
    p.dbgSetSeen(NOON);
    ck(evolveNow(p) == 315, "Budew by day becomes Roselia");
  }
  {
    Pet p; raise(p, 433, 30, NATURE_EASYGOING, NOON);      // Chingling: by night
    p.bond = 100;
    ck(!p.canEvolveNow(), "Chingling by day does not evolve");
    p.dbgSetSeen(NIGHT);
    ck(evolveNow(p) == 358, "Chingling at night becomes Chimecho");
  }

  // --- Eevee: high bond, then bond by time, then the nature ---------------------------
  {
    Pet a; raise(a, DEX_EEVEE, 40, NATURE_EASYGOING, NOON);  a.bond = FORK_EVO_BOND_HIGH;
    Pet b; raise(b, DEX_EEVEE, 40, NATURE_EASYGOING, NOON);  b.bond = FORK_EVO_BOND;
    Pet c; raise(c, DEX_EEVEE, 40, NATURE_EASYGOING, NIGHT); c.bond = FORK_EVO_BOND;
    ck(evolveNow(a) == 700, "Eevee with high bond: Sylveon");
    ck(evolveNow(b) == 196, "Eevee with bond by day: Espeon");
    ck(evolveNow(c) == 197, "Eevee with bond at night: Umbreon");
    bool ok = true;
    const int16_t want[NATURE_COUNT] = { 470, 136, 135, 134, 471 };
    for (uint8_t n = 0; n < NATURE_COUNT; n++) {
      Pet q; raise(q, DEX_EEVEE, 40, n, NOON);
      const int16_t got = evolveNow(q);
      if (got != want[n]) { printf("      %s Eevee became %d\n", natureName(n), got); ok = false; }
    }
    ck(ok, "Eevee with little bond: its nature picks one of the five other forms");
  }

  // --- the nature, for the branches the games leave to chance ------------------------
  {
    struct { int16_t from, a, b; } br[] = {
      { 265, 266, 268 }, { 79, 80, 199 }, { 281, 282, 475 },
      { 361, 362, 478 }, { 412, 413, 414 }, { 790, 791, 792 },
    };
    bool ok = true, bothSeen = true;
    for (auto &x : br) {
      bool sawA = false, sawB = false;
      for (uint8_t n = 0; n < NATURE_COUNT; n++) {
        Pet q; raise(q, x.from, 60, n, NOON);
        const int16_t got = evolveNow(q);
        // exactly the branch whose mask holds this nature
        int16_t want = 0;
        for (int i = 0; i < EVO_RULE_COUNT; i++)
          if (EVO_RULES[i].from == x.from && EVO_RULES[i].cond == EVC_NATURE &&
              (EVO_RULES[i].arg >> n) & 1) want = EVO_RULES[i].to;
        if (got != want) ok = false;
        if (got == x.a) sawA = true;
        if (got == x.b) sawB = true;
      }
      if (!sawA || !sawB) { printf("      %s reaches only one form\n", DEX_TBL[x.from].name); bothSeen = false; }
    }
    ck(ok, "Wurmple, Slowpoke, Kirlia, Snorunt, Burmy, Cosmoem: the nature picks");
    ck(bothSeen, "...and every one of them reaches BOTH forms across the five natures");
  }

  // --- the card's list agrees with what evolve() does ---------------------------------
  {
    Pet p; raise(p, DEX_EEVEE, 40, NATURE_LAZY, NIGHT); p.bond = FORK_EVO_BOND;
    ForkEvo b[FORK_EVO_MAX];
    const uint8_t n = p.forkEvoBranches(b);
    int picked = 0; int16_t pickedTo = 0;
    for (uint8_t i = 0; i < n; i++) if (b[i].picked) { picked++; pickedTo = b[i].to; }
    ck(n == 8 && picked == 1, "Eevee's card lists all eight with exactly one marked");
    ck(evolveNow(p) == pickedTo, "...and the marked one is what it becomes");
  }

  // --- only forms this player can be shown --------------------------------------------
  {
    gRegionArt = 1u << 0;                                    // Kanto pack only
    bool ok = true;
    for (uint8_t n = 0; n < NATURE_COUNT; n++)
      for (int t = 0; t < 2; t++) {
        Pet q; raise(q, DEX_EEVEE, 40, n, t ? NIGHT : NOON); q.bond = t ? FORK_EVO_BOND : 0;
        const int16_t got = evolveNow(q);
        if (got < 134 || got > 136) { printf("      %s Eevee became %d with Kanto only\n", natureName(n), got); ok = false; }
      }
    ck(ok, "with only the Kanto pack, Eevee only ever becomes a Kanto form");
    Pet q; raise(q, 236, 30, NATURE_EASYGOING, NOON);
    ck(!q.canEvolveNow(), "and Tyrogue, whose forms are all past Kanto, waits for a pack");
    gRegionArt = 0xFFFF;
  }

  // --- eggs: a branch target evolves, it never hatches ----------------------------------
  {
    const int16_t barred[] = { 107, 237, 199, 268, 475, 478, 414, 792 };
    bool ok = true;
    for (int16_t d : barred) if (!forkEggBarred(d)) ok = false;
    ck(ok, "Hitmonchan, Hitmontop, Slowking, Cascoon, Gallade, Froslass, Mothim, Lunala are barred");
    ck(!forkEggBarred(25) && !forkEggBarred(143) && !forkEggBarred(1),
       "a single bond target (Pikachu, Snorlax) and a plain species are not");

    // and the egg pool honours it. Upstream v4.3.0 grew its own EVO_BRANCHES
    // table, whose targets are evolution-only, so these species no longer hatch
    // with the switch off either: the fork's barring is now a second guard over
    // the same set. The "off" half pins that, so upstream dropping a branch
    // target back into the egg pool fails here rather than going unnoticed.
    const uint8_t johto = regionOfDex(237);
    auto rolls = [&](bool on) {
      forkSetFeatures(on);
      Pet p; p.begin();
      memset(p.dexReg, 0, sizeof(p.dexReg));
      int hits = 0;
      for (int i = 0; i < 4000; i++) {
        const int16_t d = p.rollInRegion(johto, R_RARO);
        if (d == 237 || d == 107 || d == 199) hits++;
      }
      return hits;
    };
    const int off = rolls(false), on = rolls(true);
    ck(off == 0, "off: upstream's branch table keeps Hitmontop/Hitmonchan/Slowking out of rare eggs too");
    ck(on == 0, "on: it never does");
    // the same for a fresh egg roll, which has its own candidate loop
    auto eggs = [&](bool on) {
      forkSetFeatures(on);
      Pet p; p.begin();
      p.setRegion(johto);
      // The earlier checks' creatures saved their Pokedex to the shared store,
      // and a finished line is skipped by the first pass. Start it empty.
      memset(p.dexReg, 0, sizeof(p.dexReg));
      reg(p, 1);   // a first game hatches a starter; this is not one
      int hits = 0;
      for (int i = 0; i < 6000; i++) {
        const int16_t d = p.pickEggSpecies();
        if (d == 237 || d == 107 || d == 199) hits++;
      }
      return hits;
    };
    const int eOff = eggs(false), eOn = eggs(true);
    ck(eOff == 0, "off: and out of a fresh Johto egg");
    ck(eOn == 0, "on: a fresh egg never is");
    forkSetFeatures(true);
  }

  // --- lineHasUnregistered walks every branch -------------------------------------------
  {
    Pet p; p.begin();
    memset(p.dexReg, 0, sizeof(p.dexReg));
    reg(p, 236); reg(p, 106);
    forkSetFeatures(false);
    const bool offDone = !p.lineHasUnregistered(236);
    forkSetFeatures(true);
    const bool onOpen = p.lineHasUnregistered(236);
    reg(p, 107); reg(p, 237);
    const bool onDone = !p.lineHasUnregistered(236);
    // Upstream v4.3.0 walks every branch too (evoOptions), so off and on agree.
    ck(!offDone, "off: upstream also keeps the line open until Hitmonchan and Hitmontop are in");
    ck(onOpen && onDone, "on: the line is open until Hitmonchan and Hitmontop are in too");
  }

  // --- the strings ------------------------------------------------------------------------
  {
    bool ok = true;
    for (uint8_t l = 0; l < LANG_COUNT; l++) {
      gLang = (Lang)l;
      for (int id = FS_EVO_WAIT_DAY; id <= FS_EVO_NIGHT; id++)
        if (!*FT((FStrId)id) || strlen(FT((FStrId)id)) * 12 > 360) ok = false;
    }
    gLang = LANG_EN;
    ck(ok, "the new strings exist in all six languages and fit the card at size 2");
  }

  printf("%s\n", bad ? "FAILURES" : "all good");
  return bad ? 1 : 0;
}
