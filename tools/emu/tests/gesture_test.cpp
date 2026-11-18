// The Android fork's ways to touch the pet (fork.h section 9).
// See android/FEATURES_PLAN.md, Phase 6.
//
// Every gesture is driven as a FINGER through the real handleTouch(), and the
// phone's motion as accelerometer samples through the real loop(), because the
// danger here is not the rules but the gestures upstream already owns: a stroke
// that pages the tile axis, a tickle that turns into the 3 s release hold. Each
// "it did not steal X" check has a companion proving X still works.
//
// The switch-off half is the board: nothing changes, and the wiggling hold
// still opens the release dialog exactly as upstream's does.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "fork.h"
#include "ckpt.h"
#include <cstdio>
#include <cstring>

uint32_t g_seed = 61;
FakeSerial Serial;
FakeESP ESP;
FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0;
volatile bool g_touchDown = false;
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }
void FakeESP::restart() { exit(0); }

void emuAdvanceMs(uint32_t ms);
void emuSetClockPaused(bool paused);
void setup();
void loop();
void render();
uint32_t rtcEpoch();
void rtcSetEpoch(uint32_t e);
void uiTileGo(int i);
int uiTileNamed(const char *n);
int uiTileIndex();
int uiPetMidY();
extern Pet pet;
extern bool screenOff, menuOpen;
extern uint32_t confirmUntil, lastInteract;
extern uint8_t choiceKind;
void uiHeaderBand(int *top, int *bot);
void uiMenuRowRect(int i, int *x, int *y, int *w, int *h);
void uiConfirmRects(int *b1Top, int *b1Bot, int *b2Top, int *b2Bot);
int uiSceneOy();

static int bad = 0;
static void ck(bool ok, const char *what) {
  printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) bad++;
}

// 2026-11-12 at noon, and at one in the morning.
static const uint32_t T0 = 20769u * 86400u + 12 * 3600;
static const uint32_t NIGHT = 20769u * 86400u + 1 * 3600;

// One 25 ms poll with the finger where it is. handleTouch() polls at 50 Hz.
static void poll() { emuAdvanceMs(25); loop(); }
static void down(int x, int y) { g_touchX = x; g_touchY = y; g_touchDown = true; emuFireInterrupt(); poll(); }
static void move(int x, int y) { g_touchX = x; g_touchY = y; emuFireInterrupt(); poll(); }
static void up() { g_touchDown = false; emuFireInterrupt(); poll(); poll(); }
static void tap(int x, int y) { down(x, y); up(); }
// A straight run from the finger's x to `x1` in steps of `step` px.
static void runTo(int &x, int x1, int y, int step) {
  while (x != x1) {
    x += x1 > x ? (x1 - x < step ? x1 - x : step) : -(x - x1 < step ? x - x1 : step);
    move(x, y);
  }
}

static const int PY = 200;   // inside inPetZone on the square panel (y 95..310)

// A stroke: three long legs, fast enough that its net 140 px WOULD be a swipe.
static void stroke() {
  int x = 160;
  down(x, PY);
  runTo(x, 300, PY, 30); runTo(x, 160, PY, 30); runTo(x, 300, PY, 30);
  up();
}
// A tickle: six short legs in place.
static void tickle() {
  int x = 233;
  down(x, PY);
  for (int i = 0; i < 6; i++) runTo(x, i % 2 ? 233 : 251, PY, 6);
  up();
}

static void healthy() {
  pet.dbgHatchAs(4, false);
  pet.ageMinutes = 14 * MINUTES_PER_LEVEL;
  pet.fullness = 80; pet.joy = 50; pet.energy = 90; pet.hygiene = 90;
  pet.poops = 0; pet.weight = 0; pet.sleeping = false; pet.careMistakes = 0;
  while (pet.hasLearnOffer()) pet.declineLearn();
  pet.dbgSetSeen(T0);
  rtcSetEpoch(T0);
  confirmUntil = 0; menuOpen = false; screenOff = false;
  uiTileGo(uiTileNamed("pet"));
  lastInteract = millis();
}

// Runs `g` and returns the joy it changed. A tick landing inside the gesture
// drains joy too, so a gesture a tick interrupted is simply run again.
template <class G> static int joyOf(G g) {
  for (int tries = 0; tries < 4; tries++) {
    poll();   // a tick already due lands here, before the measurement
    const uint32_t age = pet.ageMinutes;
    const int before = pet.joy;
    lastInteract = millis();
    g();
    if (pet.ageMinutes == age) return pet.joy - before;
  }
  return 9999;
}

static void shake(uint32_t gap = 120) {
  for (int i = 0; i < FORK_SHAKE_JOLTS; i++) {
    forkMotionSample(0, 30.0f, 9.8f, millis());   // a jolt, ~3 g
    emuAdvanceMs(gap / 2);
    forkMotionSample(0, 0, 9.8f, millis());       // and back to rest
    emuAdvanceMs(gap / 2);
  }
  loop();
}
static void lie(float az, uint32_t ms) {
  for (uint32_t t = 0; t <= ms; t += 100) { forkMotionSample(0, 0, az, millis()); emuAdvanceMs(100); }
  loop();
}

int main() {
  gRegionArt = 0xFFFF;
  emuSetClockPaused(true);   // time moves only when the test says so

  // ---- the switch off: the board ----------------------------------------------
  forkSetFeatures(false);
  setup();
  for (int i = 0; i < 4; i++) render();
  if (pet.awaitingStarter()) pet.dbgHatchAs(4, false);
  healthy();
  {
    int x = 233;
    down(x, PY);
    for (int i = 0; i < 4; i++) runTo(x, i % 2 ? 233 : 251, PY, 6);
    ck(!forkStrokeBusy(), "off: a wiggle is never 'busy'");
    for (int i = 0; i < 140; i++) poll();   // 3.5 s, still inside 30 px
    ck(confirmUntil != 0, "off: a wiggling hold on the creature still offers release, as upstream's");
    g_touchDown = false; emuFireInterrupt(); poll();
    confirmUntil = 0;
  }
  ck(!forkPetStroke(pet, FORK_GESTURE_STROKE, millis()), "off: a stroke is not the fork's");
  ck(!forkPetTap(pet, 233, PY, true, millis()) && !forkPetTap(pet, 233, PY, true, millis() + 10),
     "off: a double tap does not hop");
  lie(-9.8f, 2000);
  ck(forkFaceDownChange() == -1 && !screenOff, "off: face down does nothing");
  shake();
  ck(forkReact(millis()) == FORK_REACT_NONE, "off: no reaction ever shows");
  ck(forkReactDx(millis()) == 0 && forkReactDy(millis()) == 0, "off: and nothing moves the creature");

  // ---- on: the phone ------------------------------------------------------------
  forkSetFeatures(true);
  forkData.begin();
  forkTestSetSickness(false);
  forkTouchReset();
  healthy();

  // the record: the touch cap round-trips, and a Phase 5 body reads as a fresh day
  {
    ForkState s; s.touchDay = 20769; s.touchJoy = 17; s.expLen = 1;
    uint8_t buf[FORK_BLOB_CAP];
    const uint16_t n = Fork::encode(s, buf, sizeof(buf), 3);
    ForkState back;
    ck(n && Fork::decode(buf, n, back) && back.touchDay == 20769 && back.touchJoy == 17,
       "record: the touch day and its joy round-trip");
    const uint16_t old = ckptSeal(buf, CKPT_HDR + FORK_OFF_TOUCH_DAY, FORK_MAGIC, FORK_VERSION, 4);
    ForkState prev;
    ck(old && Fork::decode(buf, old, prev) && prev.touchDay == 0 && prev.touchJoy == 0 && prev.expLen == 1,
       "record: a Phase 5 record loads with no touch joy spent");
  }
  forkData.st.touchDay = 0; forkData.st.touchJoy = 0;

  // A DECLINED DIALOG leaves strokes working. RETIRE asks through the choice
  // dialog, whose timer was never cleared on NO, and the stroke gate read the
  // timer: one "no" and stroking was dead until a restart.
  {
    int ht, hb;
    uiHeaderBand(&ht, &hb);
    tap(233, (ht + hb) / 2);
    ck(menuOpen, "declined: the header opens the menu");
    int rx, ry, rw, rh;
    uiMenuRowRect(4, &rx, &ry, &rw, &rh);
    tap(rx + rw / 2, ry + rh / 2);
    ck(choiceKind == 3, "declined: RETIRE asks first");
    int b1t, b1b, b2t, b2b;
    uiConfirmRects(&b1t, &b1b, &b2t, &b2b);
    tap(233, (b2t + b2b) / 2 + uiSceneOy());
    ck(choiceKind == 0 && !pet.ceremony, "declined: NO keeps the creature");
    forkData.st.touchDay = 0; forkData.st.touchJoy = 0;
    pet.joy = 50;
    ck(joyOf(stroke) == FORK_STROKE_JOY, "declined: a stroke still pays straight after");
    forkData.st.touchDay = 0; forkData.st.touchJoy = 0;
  }

  // STROKE: joy and bond, and it does not page the tile axis
  {
    const int tile = uiTileIndex();
    pet.caress();   // the day's first care pays upstream's streak bond; take it out of the way
    const uint8_t bond0 = pet.bond;
    const int dj = joyOf(stroke);
    ck(dj == FORK_STROKE_JOY, "stroke: gives its joy");
    ck(pet.bond == bond0 + FORK_STROKE_BOND, "stroke: and its bond");
    ck(forkReact(millis()) == FORK_REACT_STROKE, "stroke: the creature shows it");
    ck(uiTileIndex() == tile && uiTileIndex() == uiTileNamed("pet"), "stroke: the tile axis did not move");
    // the companion: a plain swipe over the same place still moves it
    int x = 160;
    down(x, PY); runTo(x, 320, PY, 40); up();
    ck(uiTileIndex() != tile, "swipe: a plain swipe across the creature still moves the axis");
    uiTileGo(uiTileNamed("pet"));
  }

  // TICKLE: joy three times a minute, then it is annoyed
  forkTouchReset();
  healthy();
  {
    int ok = 0;
    for (int i = 0; i < FORK_TICKLE_ANNOY; i++) ok += joyOf(tickle) == FORK_TICKLE_JOY;
    ck(ok == FORK_TICKLE_ANNOY && forkReact(millis()) == FORK_REACT_TICKLE, "tickle: gives joy and giggles");
    ck(joyOf(tickle) == -FORK_ANNOY_JOY && forkReact(millis()) == FORK_REACT_ANNOYED,
       "tickle: one too many annoys it and costs joy");
    emuAdvanceMs(FORK_TICKLE_WINDOW_MS + 1000);
    ck(joyOf(tickle) == FORK_TICKLE_JOY, "tickle: a minute later it is welcome again");
  }

  // the HOLD: a tickle running past 3 s is not the release hold; holding still is
  forkTouchReset();
  healthy();
  {
    int x = 233;
    down(x, PY);
    for (int i = 0; i < 70; i++) runTo(x, i % 2 ? 233 : 251, PY, 6);   // ~3.5 s of wiggling
    ck(forkStrokeBusy() && confirmUntil == 0, "hold: a long tickle never offers to release the creature");
    up();
    confirmUntil = 0;
    down(233, PY);
    for (int i = 0; i < 140; i++) poll();
    ck(confirmUntil != 0, "hold: holding still still does");
    g_touchDown = false; emuFireInterrupt(); poll();
    confirmUntil = 0;
  }

  // the daily CAP
  forkTouchReset();
  healthy();
  forkData.st.touchDay = 0; forkData.st.touchJoy = 0;
  {
    int total = 0;
    for (int i = 0; i < 12; i++) { pet.joy = 50; total += joyOf(stroke); }
    ck(total == FORK_TOUCH_JOY_MAX && forkData.st.touchJoy == FORK_TOUCH_JOY_MAX,
       "cap: a day's strokes give FORK_TOUCH_JOY_MAX joy and no more");
    pet.dbgSetSeen(T0 + 86400);
    ck(joyOf(stroke) == FORK_STROKE_JOY, "cap: the next day it pays again");
    pet.dbgSetSeen(T0);
  }

  // DOUBLE TAP and POKE
  forkTouchReset();
  healthy();
  forkData.st.touchDay = 0; forkData.st.touchJoy = 0;
  {
    const int mid = uiPetMidY();
    const int head = mid - 30 < 100 ? 100 : mid - 30, belly = mid + 30 > 300 ? 300 : mid + 30;
    ck(head < mid && belly > mid, "poke: the creature's middle is inside the pet zone");
    tap(233, head);
    ck(forkReact(millis()) == FORK_REACT_HEAD, "poke: a tap on its head");
    emuAdvanceMs(1000);
    tap(233, belly);
    ck(forkReact(millis()) == FORK_REACT_BELLY, "poke: a tap on its belly");
    emuAdvanceMs(1000);
    const uint8_t bond0 = pet.bond;
    const int dj = joyOf([&] { tap(233, belly); tap(233, belly); });
    ck(forkReact(millis()) == FORK_REACT_HOP, "double tap: it hops");
    ck(dj == 5 + FORK_HOP_JOY && pet.bond == bond0 + 1,
       "double tap: the first tap is the caress, the second only the hop");
    emuAdvanceMs(1000);
    tap(233, belly); emuAdvanceMs(600); tap(233, belly);
    ck(forkReact(millis()) == FORK_REACT_BELLY, "double tap: two taps 600 ms apart are two pokes");
  }

  // ASLEEP: nothing pays, and the stroke is still not a swipe
  forkTouchReset();
  healthy();
  pet.sleeping = true;
  {
    const int tile = uiTileIndex();
    ck(joyOf(stroke) == 0 && forkReact(millis()) == FORK_REACT_NONE && uiTileIndex() == tile,
       "asleep: a stroke does nothing, and pages nothing");
    shake();
    ck(forkReact(millis()) == FORK_REACT_NONE, "asleep: a shake does not make it dizzy");
  }
  pet.sleeping = false;

  // SHAKE
  forkTouchReset();
  healthy();
  {
    int dj = joyOf([] { shake(); });
    ck(forkReact(millis()) == FORK_REACT_DIZZY && dj == -FORK_SHAKE_JOY, "shake: dizzy, and it costs joy");
    ck(forkReactDx(millis() + 300) != 0, "shake: it sways");
    dj = joyOf([] { shake(); });
    ck(forkReact(millis()) == FORK_REACT_DIZZY && dj == 0, "shake: again at once sways but costs nothing");
    emuAdvanceMs(FORK_SHAKE_COOLDOWN_MS);
    ck(joyOf([] { shake(); }) == -FORK_SHAKE_JOY, "shake: after the cooldown it costs again");
    forkTouchReset();
    joyOf([] { shake(1200); });   // jolts 600 ms apart: 1.2 s for three
    ck(forkReact(millis()) == FORK_REACT_NONE, "shake: three slow bumps are not a shake");
    forkTouchReset();
    menuOpen = true;
    shake();
    ck(forkReact(millis()) == FORK_REACT_NONE, "shake: not while another screen is up");
    menuOpen = false;
  }

  // FACE DOWN: the PWR short press
  forkTouchReset();
  healthy();
  lie(9.8f, 300);
  lie(-9.8f, 1000);
  ck(!screenOff, "face down: a second face down is not enough");
  lie(9.8f, 300);
  lie(-9.8f, 1800);
  ck(screenOff && !pet.sleeping, "face down: the screen goes off; at noon it stays awake (upstream's rule)");
  lie(9.8f, 400);
  ck(!screenOff, "face up: the screen comes back");
  pet.dbgSetSeen(NIGHT);
  rtcSetEpoch(NIGHT);
  lie(-9.8f, 1800);
  ck(screenOff && pet.sleeping, "face down at night: it goes to sleep");
  lie(-5.0f, 600);
  ck(screenOff, "face down: tipped up a little is still face down");
  lie(9.8f, 400);
  ck(!screenOff && !pet.sleeping, "face up: and wakes it, as the screen coming on does");

  printf("%s\n", bad ? "FAILED" : "ALL PASS");
  return bad ? 1 : 0;
}
