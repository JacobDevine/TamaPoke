#include "fork_adv.h"

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
