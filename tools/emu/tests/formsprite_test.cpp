// Adventures, step 12a: the form SPRITES, as pixels.
//
// sprite_test learned the hard way that asserting "the dex I asked for is the
// dex I got" only records what was ASKED for (CLAUDE.md § 3). So everything
// here compares what was actually loaded: a Mega must not be its base species'
// picture, a shiny form must not be the plain one, and the fallback for a form
// with no shiny art must land on the FORM, never on the species.
//
// Needs the loose form sprites: `python3 tools/pack_forms.py --unpack`.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "fork_adv.h"
#include "sdmon.h"
#include <cstdio>
#include <cstring>
uint32_t g_seed = 7; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0; volatile bool g_touchDown = false;
uint32_t millis() { return 0; }
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; } String FakeSerial::readStringUntil(char) { return String(""); }
// sfxPlay comes from host_impl.cpp here -- this test links the host, not the sketch
void emuSetSpriteDir(const char *);
static int bad = 0;
static void ck(bool ok, const char *w) { printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) bad++; }

// Same picture: same palette AND same first idle frame.
static bool samePicture(const PmdMon &a, const PmdMon &b) {
  if (a.palCount != b.palCount || memcmp(a.pal, b.pal, a.palCount * sizeof(uint16_t))) return false;
  const PmdAct &x = a.acts[PMD_IDLE], &y = b.acts[PMD_IDLE];
  return x.w == y.w && x.h == y.h && !memcmp(x.data, y.data, (size_t)x.w * x.h);
}
static bool samePalette(const PmdMon &a, const PmdMon &b) {
  return a.palCount == b.palCount && !memcmp(a.pal, b.pal, a.palCount * sizeof(uint16_t));
}

static uint8_t rowOf(int16_t dex, const char *label) {
  for (uint16_t id = 1; id <= FORM_COUNT; id++)
    if (FORM_TBL[id].dex == dex && !strcmp(FORM_TBL[id].label, label)) return (uint8_t)id;
  return 0;
}

int main() {
  emuSetSpriteDir(SPRITE_DIR);

  // --- every usable form loads, with an idle, and is not its species ----------
  int usable = 0, loaded = 0, noIdle = 0, isBase = 0, wrongTag = 0;
  for (uint16_t id = 1; id <= FORM_COUNT; id++) {
    if (!forkFormUsable((uint8_t)id)) continue;
    usable++;
    const FormEntry &f = FORM_TBL[id];
    PmdMon m, base;
    if (!m.loadForm(f.dex, f.sub, false)) {
      printf("      %s %s did not load\n", DEX_TBL[f.dex].name, f.label);
      continue;
    }
    loaded++;
    if (m.dex != f.dex || m.form != f.sub) wrongTag++;
    if (!m.has(PMD_IDLE)) noIdle++;
    if (base.load(f.dex, false) && samePicture(m, base)) {
      isBase++;
      printf("      %s %s draws exactly as %s\n", DEX_TBL[f.dex].name, f.label, DEX_TBL[f.dex].name);
    }
  }
  if (usable && !loaded)
    printf("      no form sprites here: run python3 tools/pack_forms.py --unpack\n");
  printf("      %d usable forms, %d loaded\n", usable, loaded);
  ck(usable > 0, "there are usable forms to check");
  ck(loaded == usable, "every usable form's sprite loads");
  ck(wrongTag == 0, "a loaded form records the species and form it holds");
  ck(noIdle == 0, "every form has an idle animation to stand in");
  ck(isBase == 0, "no form's picture is its base species' picture");

  // --- shiny, and the fallback ----------------------------------------------
  const uint8_t mx = rowOf(6, "MEGA X");
  ck(mx && FORM_TBL[mx].shinyArt, "Mega Charizard X has shiny art");
  {
    PmdMon plain, shiny;
    bool a = plain.loadForm(6, FORM_TBL[mx].sub, false);
    bool b = shiny.loadForm(6, FORM_TBL[mx].sub, true);
    ck(a && b && !samePalette(plain, shiny), "its shiny is not the plain form's palette");
  }
  // A form WITH art but WITHOUT shiny art: the shiny draws the plain form, not
  // the species. Found from the table rather than named, so it cannot go stale.
  uint8_t noShiny = 0;
  for (uint16_t id = 1; id <= FORM_COUNT && !noShiny; id++)
    if (forkFormUsable((uint8_t)id) && !FORM_TBL[id].shinyArt) noShiny = (uint8_t)id;
  ck(noShiny != 0, "some usable form has no shiny art, so the fallback is exercised");
  if (noShiny) {
    const FormEntry &f = FORM_TBL[noShiny];
    PmdMon plain, shiny, species;
    bool a = plain.loadForm(f.dex, f.sub, false);
    bool b = shiny.loadForm(f.dex, f.sub, true);
    species.load(f.dex, true);
    printf("      fallback case: %s %s\n", DEX_TBL[f.dex].name, f.label);
    ck(a && b && samePicture(plain, shiny), "a shiny with no shiny art draws the plain form");
    ck(b && shiny.form == f.sub && !samePicture(shiny, species), "and not the species' own sprite");
  }

  // --- what must NOT load -----------------------------------------------------
  const uint8_t my = rowOf(6, "MEGA Y");
  {
    PmdMon m;
    ck(my && !forkFormUsable(my) && !m.loadForm(6, FORM_TBL[my].sub, false),
       "an art-less form (Mega Charizard Y) has no sprite to load");
    ck(!m.loaded, "and a failed load leaves nothing loaded");
  }
  {
    PmdMon m;
    ck(!m.loadForm(6, 0, false), "subfolder 0 is the species, never a form");
    ck(!m.loadForm(0, 1, false) && !m.loadForm(DEX_COUNT + 1, 1, false), "out-of-range species refuse");
  }
  // load() after loadForm() must not keep claiming the form.
  {
    PmdMon m;
    m.loadForm(6, FORM_TBL[mx].sub, false);
    ck(m.load(6, false) && m.form == 0, "load() after loadForm() holds the species, form 0");
  }

  printf("%s (%d failed)\n", bad ? "FORMSPRITE_TEST FAILED" : "ALL PASS", bad);
  return bad ? 1 : 0;
}
