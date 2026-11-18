// Screen transitions on the phone (android/README.md § "Screen transitions").
//
// The firmware draws whole frames and has no idea one screen follows another;
// on the board a change of screen is simply the next frame. The phone keeps
// the previous frame as a texture and moves between the two on the GPU, at the
// display's own rate, so none of this touches a render path or a board shot.
//
// Split in two so the rules can be tested without SDL:
//   motionViewNow()   reads the firmware's state into a MotionView (which
//                     screen, where on the tile axis, how deep, any sheet);
//   motionClassify()  says which transition joins two views;
//   motionPose()      says where each frame is at a point in that transition.
// android_main.cpp does the drawing.
#pragma once
#include <stdint.h>

// What a frame shows, as far as a transition cares.
struct MotionView {
  const char *scr = nullptr;   // SCREEN_NAME[uiCurrentScreen()]
  uint8_t sub = 0;             // a sheet or entry open INSIDE that screen
  int8_t tile = -1;            // uiTileIndex(), -1 off the tile axis
  int8_t depth = 0;            // 0 a tile's root, 1 a level in, 2 two levels
  bool special = false;        // a fight, a minigame, first boot: cross-fade only
  bool sheet = false;          // a sheet over the dimmed pet screen (menu, training)
  int16_t sheetX = 0, sheetY = 0, sheetW = 0, sheetH = 0;   // canvas px
  int16_t tabTop = -1;         // the tab bar's top on this frame, -1 none
  // The firmware is moving the pull-down menu itself (under a finger, or
  // settling): the frame already shows it, so a transition would repeat it.
  bool pull = false;
};

// A sheet hanging from the top edge (the phone's pull-down menu) rather than
// rising from the bottom (training).
bool motionSheetFromTop(const MotionView &v, int canvasH);

// Fills depth/special/sheet from scr and sub. A screen it does not know is
// special, so it cross-fades.
void motionClassifyScreen(MotionView &v);

bool motionSameView(const MotionView &a, const MotionView &b);

enum MotionKind : uint8_t {
  MOTION_NONE,
  MOTION_SLIDE,       // a neighbouring tile: both frames move sideways together
  MOTION_FADE,        // anything else: the new frame fades in over the old
  MOTION_DEEPER,      // a level in: the new frame rises and fades in
  MOTION_SHALLOWER,   // back out: the new frame drops into place and fades in
  MOTION_SHEET_IN,    // a sheet slides up over a dimming screen
  MOTION_SHEET_OUT,   // and back down, the dimming lifting
};

struct Motion {
  MotionKind kind = MOTION_NONE;
  int8_t dir = 0;     // SLIDE: +1 to a tile on the right (content moves left)
};

Motion motionClassify(const MotionView &from, const MotionView &to);

// How long each transition runs, in ms.
uint32_t motionDurationMs(MotionKind k);

// Where things are at time t (0..1, linear) through a transition. Offsets are
// in canvas px; alphas 0..1.
struct MotionPose {
  float oldX = 0, oldY = 0, oldA = 1;   // the previous frame (or its sheet)
  float newX = 0, newY = 0, newA = 1;   // the frame now being drawn (or its sheet)
  float dimA = 1;                       // SHEET_*: the alpha of what is not the sheet
};
MotionPose motionPose(const Motion &m, float t, int canvasW, int canvasH, const MotionView &from,
                      const MotionView &to);

float motionEaseOut(float t);

// The firmware's current view. Needs the sketch linked in.
MotionView motionViewNow();
