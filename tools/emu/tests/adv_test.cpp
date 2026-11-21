// Adventures, step 12a: the kept-forms record (fork_adv.h). Which creature may
// become Mega Charizard X in a fight, or IS a Kyurem-Black, survives a restart,
// a full record, a failed write and a record from a later build.
//
// Every check runs against the real store through forkKeptBegin(), the way the
// app loads it, not against the encoder's own idea of the bytes.
#include "Arduino.h"
#include "Preferences.h"
#include "fork.h"
#include "fork_adv.h"
#include "ckpt.h"
#include "save.h"
#include "pet.h"
#include <cstdio>
#include <cstring>

#ifndef TAMA_NATIVE_TEST
uint32_t g_seed = 1;
FakeSerial Serial;
FakeESP ESP;
FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0;
volatile bool g_touchDown = false;
bool wasPressed = false;
static uint32_t gNowMs = 0;
uint32_t millis() { return gNowMs; }
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

static uint8_t rowOf(int16_t dex, const char *label) {
  for (uint16_t id = 1; id <= FORM_COUNT; id++)
    if (FORM_TBL[id].dex == dex && !strcmp(FORM_TBL[id].label, label)) return (uint8_t)id;
  return 0;
}

// A cold start: memory forgotten, the record read back out of the store.
static void reload() {
  forkKeptClear();
  forkKeptBegin();
}

static Pet pet;   // one, never copied (Pet owns a Preferences handle)

int main() {
  forkSetFeatures(true);
  nvs().clear();

  const uint8_t mx = rowOf(6, "MEGA X"), my = rowOf(6, "MEGA Y"), gx = rowOf(6, "G-MAX");
  const uint8_t kb = rowOf(646, "BLACK");
  ck(mx && my && gx && kb, "the forms this test uses exist");

  // --- the key is forkCreatureKey()'s, for every creature that can exist -----
  {
    pet.dbgHatchAs(6, false);
    bool same = true;
    for (int a = 0; a < 32 && same; a++)
      for (int d = 0; d < 32 && same; d++)
        for (int s = 0; s < 32 && same; s++)
          for (int h = 0; h < 32 && same; h++)
            for (int sh = 0; sh < 2 && same; sh++) {
              pet.ivAtk = a; pet.ivDef = d; pet.ivSpe = s; pet.ivHp = h; pet.shiny = sh;
              if (forkKeyOf(a, d, s, h, sh) != forkCreatureKey(pet)) {
                same = false;
                printf("      differs at %d/%d/%d/%d shiny=%d\n", a, d, s, h, sh);
              }
            }
    ck(same, "forkKeyOf() equals forkCreatureKey() for every IV and shiny combination");
  }

  const uint32_t K = forkKeyOf(31, 20, 25, 28, false);
  const uint32_t K2 = forkKeyOf(1, 2, 3, 4, true);

  // --- keep, look up, survive a restart ---------------------------------------
  reload();
  ck(forkKeptCount() == 0 && forkKeptForm(K, 6) == 0, "a fresh store keeps nothing");
  ck(forkKeepForm(K, 6, mx), "a Charizard is kept as Mega X");
  ck(forkKeptForm(K, 6) == mx, "and carries Mega X");
  ck(nvs().count("frmA") || nvs().count("frmB"), "it was written to the store");
  reload();
  ck(forkKeptForm(K, 6) == mx && forkKeptCount() == 1, "it survives a restart");
  ck(forkKeptForm(K, 3) == 0, "a VENUSAUR with the same IVs carries nothing");
  ck(forkKeptForm(K2, 6) == 0, "another Charizard carries nothing");
  ck(forkKeepForm(K2, 646, kb) && forkKeptForm(K2, 646) == kb, "a fusion is kept as itself");

  // --- what is refused, leaving everything as it was --------------------------
  const uint8_t before = forkKeptCount();
  ck(!forkKeepForm(K2, 6, my), "an art-less form (Mega Y) is refused");
  ck(!forkKeepForm(K2, 3, mx), "another species' form is refused");
  ck(!forkKeepForm(0, 6, mx), "key 0 (an egg) is refused");
  ck(!forkKeepForm(K2, 6, 0) && !forkKeepForm(K2, 6, FORM_COUNT + 1), "no form and past the end are refused");
  ck(forkKeptCount() == before, "and none of them added an entry");

  // --- keeping again replaces, it does not add --------------------------------
  ck(forkKeepForm(K, 6, gx) && forkKeptForm(K, 6) == gx, "keeping again replaces the form");
  ck(forkKeptCount() == before, "without a second entry");
  reload();
  ck(forkKeptForm(K, 6) == gx, "and the replacement is what restarts");

  // --- a failed write changes nothing, in memory or in the store --------------
  {
    const uint32_t K3 = forkKeyOf(9, 9, 9, 9, false);
    nvsFailWritesAfter(0);
    const bool ok = forkKeepForm(K3, 6, mx);
    nvsResumeWrites();
    ck(!ok, "a keep whose write fails reports failure");
    ck(forkKeptForm(K3, 6) == 0 && forkKeptCount() == before, "and memory does not claim it");
    reload();
    ck(forkKeptForm(K3, 6) == 0 && forkKeptForm(K, 6) == gx, "nor does the store, which still has the rest");
  }

  // --- full: the oldest drops, the newest stays, through a restart ------------
  {
    nvs().clear();
    reload();
    bool all = true;
    for (int i = 0; i < FORK_KEPT_MAX + 1; i++)
      all &= forkKeepForm(forkKeyOf(i & 31, i >> 5, 0, 0, false), 6, mx);
    ck(all, "every keep up to and past the cap succeeds");
    ck(forkKeptCount() == FORK_KEPT_MAX, "the record holds FORK_KEPT_MAX");
    ck(forkKeptForm(forkKeyOf(0, 0, 0, 0, false), 6) == 0, "the oldest dropped off");
    ck(forkKeptForm(forkKeyOf(1, 0, 0, 0, false), 6) == mx, "the second-oldest is still there");
    ck(forkKeptForm(forkKeyOf(FORK_KEPT_MAX & 31, FORK_KEPT_MAX >> 5, 0, 0, false), 6) == mx,
       "the newest is there");
    reload();
    ck(forkKeptCount() == FORK_KEPT_MAX, "and a restart reads all of them back");
  }

  // --- records from elsewhere: a later build, and damage ----------------------
  {
    // A later build writing 10-byte entries: this one reads its 7 known bytes.
    uint8_t buf[256] = { 0 };
    uint8_t *b = buf + CKPT_HDR;
    ckptWr16(b, 2);
    b[2] = 10;
    ckptWr32(b + 4, K); ckptWr16(b + 8, 6); b[10] = mx; b[11] = 0xEE; b[12] = 0xEE; b[13] = 0xEE;
    ckptWr32(b + 14, K2); ckptWr16(b + 18, 646); b[20] = kb;
    uint16_t n = ckptSeal(buf, CKPT_HDR + 4 + 20, FORK_KEPT_MAGIC, 2, 99);
    nvs().clear();
    nvs()["frmA"] = std::vector<uint8_t>(buf, buf + n);
    reload();
    ck(forkKeptCount() == 2 && forkKeptForm(K, 6) == mx && forkKeptForm(K2, 646) == kb,
       "bigger entries from a later build read by their prefix");

    // Entries no build should have written are dropped, not trusted.
    memset(buf, 0, sizeof(buf));
    ckptWr16(b, 4);
    b[2] = 8;
    ckptWr32(b + 4, K);  ckptWr16(b + 8, 6);  b[10] = mx;          // good
    ckptWr32(b + 12, K); ckptWr16(b + 16, 3); b[18] = mx;          // a Charizard form on Venusaur
    ckptWr32(b + 20, K); ckptWr16(b + 24, 6); b[26] = 250;         // past the table
    ckptWr32(b + 28, 0); ckptWr16(b + 32, 6); b[34] = mx;          // key 0
    n = ckptSeal(buf, CKPT_HDR + 4 + 32, FORK_KEPT_MAGIC, 1, 100);
    nvs().clear();
    nvs()["frmB"] = std::vector<uint8_t>(buf, buf + n);
    reload();
    ck(forkKeptCount() == 1 && forkKeptForm(K, 6) == mx, "entries that index nothing real are dropped");

    // A count claiming more than the body holds is not a record.
    ckptWr16(b, 200);
    n = ckptSeal(buf, CKPT_HDR + 4 + 32, FORK_KEPT_MAGIC, 1, 101);
    ck(!forkKeptDecode(buf, n), "a count past the body is refused");
    ckptWr16(b, 1);
    b[2] = 4;
    n = ckptSeal(buf, CKPT_HDR + 4 + 32, FORK_KEPT_MAGIC, 1, 102);
    ck(!forkKeptDecode(buf, n), "an entry too small to hold a form is refused");
  }

  // --- the backup carries it ---------------------------------------------------
  {
    bool a = false, b = false;
    for (uint16_t i = 0; i < SAVE_FIELD_COUNT; i++) {
      if (!strcmp(SAVE_FIELDS[i].key, "frmA")) a = true;
      if (!strcmp(SAVE_FIELDS[i].key, "frmB")) b = true;
    }
    ck(a && b, "both slots are in SAVE_FIELDS, so a backup keeps the forms");
  }

  // --- the board: features off, nothing opens or writes ------------------------
  {
    nvs().clear();
    forkSetFeatures(false);
    reload();
    ck(!forkKeepForm(K, 6, mx), "with fork features off nothing is kept");
    ck(forkKeptCount() == 0, "and memory holds nothing");
    ck(!nvs().count("frmA") && !nvs().count("frmB"), "and the store gained no key");
    forkSetFeatures(true);
  }

  printf("%s (%d failed)\n", bad ? "ADV_TEST FAILED" : "ALL PASS", bad);
  return bad ? 1 : 0;
}
