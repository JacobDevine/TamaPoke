#include "fork_adv.h"
#include "fork.h"
#include "ckpt.h"
#include <Preferences.h>
#include <string.h>

// Adventures (android/FEATURES_PLAN.md, Phase 12). See fork_adv.h.

// A form id is a uint8_t wherever it is stored. Outgrowing it must stop the
// build rather than wrap, which would quietly hand a kept creature someone
// else's form (CLAUDE.md trap 2, "uint8_t with a dex of 386").
static_assert(FORM_COUNT <= 255, "form ids are uint8_t: widen them before adding rows");

const FormEntry *forkForm(uint8_t id) {
  if (id == 0 || id > FORM_COUNT) return nullptr;
  return &FORM_TBL[id];
}

bool forkFormUsable(uint8_t id) {
  const FormEntry *f = forkForm(id);
  return f && f->art;
}

uint8_t forkFormsOf(int16_t dex, uint8_t kind, uint8_t *out, uint8_t max) {
  uint8_t n = 0;
  for (uint16_t id = 1; id <= FORM_COUNT; id++) {
    const FormEntry &f = FORM_TBL[id];
    if (f.dex != dex || (kind != FK_NONE && f.kind != kind)) continue;
    if (!forkFormUsable((uint8_t)id)) continue;
    if (out && n < max) out[n] = (uint8_t)id;
    n++;
  }
  return n;
}

bool forkFormIsState(uint8_t kind) {
  return kind == FK_MEGA || kind == FK_PRIMAL || kind == FK_GMAX || kind == FK_ULTRA;
}

bool forkIsMythical(int16_t dex) {
  for (uint8_t i = 0; i < MYTHICAL_COUNT; i++)
    if (MYTHICAL_DEX[i] == dex) return true;
  return false;
}

// --- 12a. kept forms --------------------------------------------------------

static ForkKept gKept[FORK_KEPT_MAX];   // oldest first
static uint8_t gKeptN = 0;
static Preferences gKeptPrefs;          // its own handle, never copied
static bool gKeptOpen = false;
static uint32_t gKeptGen = 0;

uint32_t forkKeyOf(uint8_t ivAtk, uint8_t ivDef, uint8_t ivSpe, uint8_t ivHp, bool shiny) {
  return (uint32_t)(ivAtk & 31) | (uint32_t)(ivDef & 31) << 5 |
         (uint32_t)(ivSpe & 31) << 10 | (uint32_t)(ivHp & 31) << 15 |
         (uint32_t)(shiny ? 1 : 0) << 20 | 0x80000000u;
}

void forkKeptClear() {
  for (auto &k : gKept) k = ForkKept();
  gKeptN = 0;
}

uint16_t forkKeptEncode(uint8_t *buf, size_t cap, uint32_t generation) {
  const size_t bodyEnd = CKPT_HDR + FORK_KEPT_HDR + (size_t)gKeptN * FORK_KEPT_ENTRY;
  if (cap < bodyEnd + CKPT_CRC) return 0;
  memset(buf, 0, bodyEnd + CKPT_CRC);
  uint8_t *b = buf + CKPT_HDR;
  ckptWr16(b, gKeptN);
  b[2] = FORK_KEPT_ENTRY;
  uint8_t *e = b + FORK_KEPT_HDR;
  for (uint8_t i = 0; i < gKeptN; i++, e += FORK_KEPT_ENTRY) {
    ckptWr32(e, gKept[i].key);
    ckptWr16(e + 4, (uint16_t)gKept[i].dex);
    e[6] = gKept[i].form;
  }
  return ckptSeal(buf, bodyEnd, FORK_KEPT_MAGIC, FORK_KEPT_VERSION, generation);
}

bool forkKeptDecode(const uint8_t *buf, uint16_t n) {
  forkKeptClear();
  if (n < CKPT_HDR + FORK_KEPT_HDR + CKPT_CRC) return false;
  const uint8_t *b = buf + CKPT_HDR;
  const size_t body = n - CKPT_HDR - CKPT_CRC;
  const uint16_t count = ckptRd16(b);
  const uint8_t size = b[2];
  // The prefix rule, per entry: a later build may write bigger entries, and
  // this one reads the 7 bytes it knows out of each. A smaller one is not a
  // record any build wrote.
  if (size < 7 || FORK_KEPT_HDR + (size_t)count * size > body) return false;
  // Keep the NEWEST if a later build kept more than this one can hold.
  const uint16_t skip = count > FORK_KEPT_MAX ? count - FORK_KEPT_MAX : 0;
  for (uint16_t i = skip; i < count; i++) {
    const uint8_t *e = b + FORK_KEPT_HDR + (size_t)i * size;
    ForkKept k;
    k.key = ckptRd32(e);
    k.dex = (int16_t)ckptRd16(e + 4);
    k.form = e[6];
    // Nothing off a record is trusted to index a table (as link.cpp does).
    if (!k.key || k.dex < 1 || k.dex > DEX_COUNT || !forkForm(k.form) ||
        FORM_TBL[k.form].dex != k.dex)
      continue;
    gKept[gKeptN++] = k;
  }
  return true;
}

void forkKeptBegin() {
  forkKeptClear();
  gKeptGen = 0;
  if (!forkFeatures()) return;
  if (!gKeptOpen) gKeptOpen = gKeptPrefs.begin("tamapoke", false);
  if (!gKeptOpen) {
    Serial.println("save: NVS would not open for the kept-forms record");
    return;
  }
  static uint8_t buf[FORK_KEPT_BLOB_CAP];
  const uint16_t n = ckptReadNewest(gKeptPrefs, "frmA", "frmB", FORK_KEPT_MAGIC, buf, sizeof(buf));
  if (n && forkKeptDecode(buf, n)) gKeptGen = ckptRd32(buf + 8);
  else forkKeptClear();
}

static bool keptSave() {
  if (!forkFeatures() || !gKeptOpen) return false;
  static uint8_t buf[FORK_KEPT_BLOB_CAP];
  const uint32_t next = gKeptGen + 1;
  const uint16_t n = forkKeptEncode(buf, sizeof(buf), next);
  if (!n) return false;
  // The slot the loaded record did NOT come from (ckpt.h, ckptSlot()).
  const char *key = ckptSlot(next, "frmA", "frmB");
  if (gKeptPrefs.putBytes(key, buf, n) != n) {
    Serial.printf("save: the kept-forms record (%s) did not write\n", key);
    return false;
  }
  gKeptGen = next;
  return true;
}

static int16_t keptFind(uint32_t key, int16_t dex) {
  for (uint8_t i = 0; i < gKeptN; i++)
    if (gKept[i].key == key && gKept[i].dex == dex) return i;
  return -1;
}

bool forkKeepForm(uint32_t key, int16_t dex, uint8_t form) {
  if (!key || !forkFormUsable(form) || FORM_TBL[form].dex != dex) return false;
  ForkKept was[FORK_KEPT_MAX];
  memcpy(was, gKept, sizeof(was));
  const uint8_t wasN = gKeptN;
  const int16_t at = keptFind(key, dex);
  if (at >= 0) {
    gKept[at].form = form;
  } else {
    if (gKeptN == FORK_KEPT_MAX) {   // full: the oldest drops off
      memmove(gKept, gKept + 1, sizeof(ForkKept) * (FORK_KEPT_MAX - 1));
      gKeptN--;
    }
    gKept[gKeptN].key = key;
    gKept[gKeptN].dex = dex;
    gKept[gKeptN].form = form;
    gKeptN++;
  }
  if (keptSave()) return true;
  // Not written: put it back, so memory never claims what the store does not.
  memcpy(gKept, was, sizeof(was));
  gKeptN = wasN;
  return false;
}

uint8_t forkKeptForm(uint32_t key, int16_t dex) {
  const int16_t at = keptFind(key, dex);
  return at < 0 ? 0 : gKept[at].form;
}

uint8_t forkKeptCount() { return gKeptN; }
