// See phone_motion.h.
#include "phone_motion.h"
#include <string.h>

// --- the sketch ---
uint8_t uiCurrentScreen();
extern const char *const SCREEN_NAME[];
int uiTileIndex();
bool uiTabBarOn();
int uiTabBarTop();
void uiMenuPanelRect(int *x, int *y, int *w, int *h);
void uiTrainPanelRect(int *x, int *y, int *w, int *h);
extern uint8_t partyDetail;
extern uint16_t boxDetail;
extern int16_t galleryDetail;

static bool is(const char *a, const char *b) { return a && b && !strcmp(a, b); }

bool motionSameView(const MotionView &a, const MotionView &b) {
  return is(a.scr, b.scr) && a.sub == b.sub;
}

// How deep each screen sits. Written as what each screen IS, so a screen
// upstream adds lands in the cross-fade (special) rather than guessing a
// direction: a wrong slide reads as a bug, a fade never does.
void motionClassifyScreen(MotionView &v) {
  const char *s = v.scr;
  // a tile's root
  if (is(s, "main") || is(s, "player") || is(s, "medals") || is(s, "explore") ||
      is(s, "gympick") || is(s, "dexpick")) { v.depth = 0; return; }
  if (is(s, "party")) { v.depth = v.sub ? 1 : 0; return; }
  // one level in
  if (is(s, "gym") || is(s, "card") || is(s, "clock") || is(s, "bag")) { v.depth = 1; return; }
  if (is(s, "gallery") || is(s, "box")) { v.depth = v.sub ? 2 : 1; return; }
  // two levels in
  if (is(s, "movepick") || is(s, "pick") || is(s, "lan")) { v.depth = 2; return; }
  // sheets over the pet screen
  if (is(s, "menu") || is(s, "train")) { v.depth = 0; v.sheet = true; return; }
  // fights, minigames, first boot, the keyboard, prompts, anything new
  v.special = true;
}

MotionView motionViewNow() {
  MotionView v;
  v.scr = SCREEN_NAME[uiCurrentScreen()];
  if (is(v.scr, "party")) v.sub = partyDetail ? 1 : 0;
  else if (is(v.scr, "box")) v.sub = boxDetail ? 1 : 0;
  else if (is(v.scr, "gallery")) v.sub = galleryDetail ? 1 : 0;
  v.tile = (int8_t)uiTileIndex();
  motionClassifyScreen(v);
  if (uiTabBarOn()) v.tabTop = (int16_t)uiTabBarTop();
  if (v.sheet) {
    int x, y, w, h;
    if (is(v.scr, "menu")) uiMenuPanelRect(&x, &y, &w, &h);
    else uiTrainPanelRect(&x, &y, &w, &h);
    v.sheetX = x; v.sheetY = y; v.sheetW = w; v.sheetH = h;
  }
  return v;
}

Motion motionClassify(const MotionView &a, const MotionView &b) {
  Motion m;
  if (motionSameView(a, b)) return m;
  m.kind = MOTION_FADE;
  if (!a.scr || a.special || b.special) return m;
  // a sheet over the pet screen
  if (b.sheet && !a.sheet && is(a.scr, "main")) { m.kind = MOTION_SHEET_IN; return m; }
  if (a.sheet && !b.sheet && is(b.scr, "main")) { m.kind = MOTION_SHEET_OUT; return m; }
  if (a.sheet && b.sheet) return m;   // menu -> training: one sheet for another
  // along the tile axis, root to root
  if (a.depth == 0 && b.depth == 0 && !a.sheet && !b.sheet && a.tile >= 0 && b.tile >= 0 &&
      a.tile != b.tile) {
    const int d = b.tile - a.tile;
    if (d == 1 || d == -1) { m.kind = MOTION_SLIDE; m.dir = (int8_t)d; }
    return m;   // further than a neighbour: fade, so six screens do not fly past
  }
  if (b.depth > a.depth) m.kind = MOTION_DEEPER;
  else if (b.depth < a.depth) m.kind = MOTION_SHALLOWER;
  return m;
}

uint32_t motionDurationMs(MotionKind k) {
  switch (k) {
    case MOTION_SLIDE: return 260;
    case MOTION_DEEPER: case MOTION_SHALLOWER: return 240;
    case MOTION_SHEET_IN: return 260;
    case MOTION_SHEET_OUT: return 200;
    case MOTION_FADE: return 200;
    default: return 0;
  }
}

float motionEaseOut(float t) {
  if (t <= 0) return 0;
  if (t >= 1) return 1;
  const float u = 1 - t;
  return 1 - u * u * u;
}

// How far a level-in/out frame travels, canvas px (about 125 phone px).
#define MOTION_DEPTH_PX 40.0f

MotionPose motionPose(const Motion &m, float t, int canvasW, int canvasH, const MotionView &from,
                      const MotionView &to) {
  MotionPose p;
  const float e = motionEaseOut(t);
  switch (m.kind) {
    case MOTION_SLIDE:
      p.oldX = -m.dir * e * canvasW;
      p.newX = m.dir * (1 - e) * canvasW;
      break;
    case MOTION_FADE:
      p.newA = e;
      break;
    // The old frame stays where it is: moving it bared a strip of black at
    // the edge it left, as a dark seam over the tab bar.
    case MOTION_DEEPER:
      p.newY = (1 - e) * MOTION_DEPTH_PX;
      p.newA = e;
      break;
    case MOTION_SHALLOWER:
      p.newY = -(1 - e) * MOTION_DEPTH_PX;
      p.newA = e;
      break;
    case MOTION_SHEET_IN:
      p.newY = (1 - e) * (canvasH - to.sheetY);
      p.dimA = e;
      break;
    case MOTION_SHEET_OUT:
      p.oldY = e * (canvasH - from.sheetY);
      p.dimA = 1 - e;
      break;
    default:
      break;
  }
  return p;
}
