# TamaPoke Android: phone layout plan

2026-11-10

## Status (2026-11-10)

Phases 1-3 are built and checked on a Galaxy S24 Ultra (SM-S928B):

- **Phase 1** done. All 47 board screenshots compared pixel for pixel before
  and after: identical (`battleanim` is timing-flaky on its own and differs
  between unmodified runs too). The 15 UI suites pass, plus the new
  `tall_test`.
- **Phase 2** done. Positions anchor to the cutout inset, the panel top and
  the bottom edge. The creature draws larger (`gPetBoost`). The feed popup
  sits above the button row it came from; dialogs stay in scene coordinates.
- **Phase 3** done for paging (right-edge strip + straight scrollbar).
  `UI_TAP_FINGER` was NOT converted to millimetres: the phone's new buttons
  are 88 px drawn / 110 px pitch (~11-14 mm) by their own constants.
- Beyond the plan: square menu screens fill the phone with their page colour
  instead of a disc on black (`uiPanelBg()`), rows use the full width on a
  rectangle (`uiSafeHalfWidth()`), the battle backs onto the page colour, and
  the bands take one flat colour (stretching a row turned a star into a line).

**Phase 4 has started; the battle is converted.** `btlLayout()` is the one
description of the grid and the BACK bar for both shapes: the `BTL_*` macros
read it, so drawing, `btlCellHit()` and the tests cannot disagree. On the
phone the grid is 2x2 cells 96 px tall, with the BACK bar under it near the
bottom edge. The backdrop is drawn at 3x, with flat sky above it and flat
ground below. The plates double their text size. On FIGHT/BAG/POKEMON/RUN the
BACK strip shows the squad as dots instead, so the grid never moves between
menus. The win screen is still square. `tall_test` drives the grid through
the real touch path and was negative-checked two ways. 48 of the 49 board
shots are byte-identical; the 49th, `battleanim`, matches a frame an
unmodified build also produces. Checked by finger on the S24: FIGHT, BAG
and RUN.

**The creature card and player card are converted (Phase 4 step 2).** The
rim now pages VIEWS (`uiRimViewStarts()`); on the board a view is one page,
so nothing changed there. The page variable still holds a board page, so the
menu's "open at STATS" keeps working.

- Creature card: the board's pages stack in 330 px sections, as many to a
  view as the canvas allows (`uiStackPer()`, derived from the height): two
  views instead of four on the S24. A tap is mapped to the section it lands
  in and handed to that page's own tap rule.
- Player card: stacking would repeat the name and stats nine times, so the
  first view is new: avatar, name, stats, then every region's eight badges as
  a row (`drawBadgeAt()` is shared with the board pages). The medals and the
  goals share the second view. Nine pages become two.
- `tall_test` drives both through the real touch path; three deliberate
  breaks were each caught (one only after the move-row check was changed to
  aim at row edges -- a centred tap passed by coincidence). Checked on the
  S24 with adb: paging, the empty move slot, the medals/goals view.

**The party and box are converted (Phase 4 step 3).** On a rectangle the
ring becomes a grid: the party is 2x3 cards (thumbnail at 3x, 204 px square
on the S24), with BOX and CLOSE as pills above the tile dots. The box shows
as many whole board pages as fit, three across: 18 slots a view, so two
views instead of six. `boxPage` keeps its board meaning (`boxViewPage()`
aligns it), and the ring's hub is a sort pill. `partySlotAt()`,
`partyHubAt()` and `partyButtonRects()` answer for both shapes from one
layout (`partyTall()`), so drawing, tapping and the tests share it. A
creature's sheet stays square: `uiScreenTall()` says SCR_PARTY/SCR_BOX are
tall only while no sheet is open. `tall_test` covers corners of every card,
the BOX pill, paging, a second-view cell mapping to slot 20 (not 2), sort,
back and CLOSE; three deliberate breaks were each caught. 47 of 48 board
shots byte-identical (`battleanim` is the flaky one). Checked on the S24
with adb taps.

**The gym list, Pokedex and region chooser are converted (Phase 4 step 4).**
The gym ladder and the region chooser (all three modes) share one list
layout, `tallList()`: rows at least 96 px apart, the items spread evenly over
as few views as that allows. On the S24 the 13 leaders show as 7 + 6 and the
regions as five a view, with LAN BATTLE and BACK at the bottom. The Pokedex
grid stays four wide, with cells widened to the list width and as many rows as
fit: 28 a view, so Kanto takes six views instead of ten. Its filter becomes a
pill. A dex entry's detail view stays square. `gymRowsPerPage()`,
`rpickPerPage()` and `galleryPerPage()` give the page sizes. The board keeps
5, 3 and 16, and on a tall canvas the page variables count views. Every row,
the pills and the LAN buttons answer through the same rectangle functions the
drawing uses. `tall_test` covers the last row of each view near its edge, a
second-view row and cell mapping past the first view, both LAN buttons, the
BACK band and the filter pill. Four deliberate breaks were each caught. All 48
board shots are byte-identical. Checked on the S24 with adb taps.

**Explore, the bag and settings are converted (Phase 4 step 5).** Explore
uses the pet screen's arrangement: the scene fills the canvas, the title stays
at the top, a bigger ball sits in the sky, and the region picker, EASY/HARD and
a 100 px EXPLORE button stack at the bottom, nearest the thumb. The bag is a
`tallList()` sized for the whole item catalogue, so the page size stays the
same as the bag fills: six rows, two views at most. Settings stacks its two
board pages, the clock above the options, drawn by their own code at two
origins (`uiClockTallOy()`), so it is one view with no paging. The reset
confirm stays centred, and taps are moved into its square. `exploreLayout()`,
`bagRowRect()` and `uiClockTallOy()` answer for both draw and tap.
`tall_test` covers both region arrows, the pill and EXPLORE near their lower
edges, PROTEIN past the first view of the bag, HOUR +, VOL +, and the reset
confirm's NO. Five deliberate breaks were each caught. All 48 board shots are
byte-identical. Checked on the S24 with adb taps: explore, the bag, and VOL.

**The keyboard is converted (Phase 4 step 6).** The 6x5 grid widens to the
canvas and sits at the bottom, nearest the thumb: keys 72 px wide on a 100 px
row pitch (62 x 90 drawn, about 9 x 14 mm on the S24), with the title and the
name field centred in the space above. `kbLayout()` is the one description for
both shapes, and `uiKbKeyRect()` gives each key's hit cell to the drawing and
to `tall_test`. The board's tap arithmetic is kept as it was; it truncates
toward zero, so on the board a tap just above the grid still types the top
row. On the phone that band is the space under the field, so it is guarded
there only. A `--shot keyboard` now exists. `tall_test` covers DEL, A, Z and
OK at their corners and both dead spots above the keys; its square-screen check
moves to the training submenu. Three deliberate breaks were each caught. All
51 board shots are byte-identical. Checked on the S24 with adb taps and by
finger.

**The menu and team select are converted (beyond the plan's list).** The
menu becomes a sheet across the canvas with CLOSE last, nearest the thumb:
six rows at `UI_TAP_FINGER` (94 px), shrinking on a short canvas rather than
covering the header band that opens it. The dimmed pet screen above it still
closes it. Team select is a 2-column grid of 204 x 160 cards (the thumbnail
at 3x, the level and typing at size 2) with BACK and FIGHT at the bottom. A
live pet plus six banked members is seven, so on the S24 everyone is in one
view; `pickPerPage()` derives the page size, so a shorter canvas pages.
`menuLayout()`, `uiMenuRowRect()`, `uiPickCellRect()` and
`uiPickButtonRect()` answer for both shapes, draw and tap. `tall_test` covers
BAG, SETTINGS, CLOSE and STATS at row edges, the gap between rows, a tap above
the sheet, the 7th card and the 1st at their corners, FIGHT inert over the cap
and BACK/FIGHT at their corners. Three deliberate breaks were each caught. All
51 board shots are byte-identical. Both were checked on the S24 by finger.

**The move picker and the learn prompt are converted.** Both draw their rows
with `drawMoveRowAt()`, the board's move row generalised to any rectangle: the
name at size 3, the type chip, STAB and power at size 2. The board's row is
`drawMoveRowAt(70, y, 326, 50, ...)` and draws exactly what it always did. The
picker is a `tallList()` sized for the creature's own learnset: seven rows a
view instead of five, with a page number and BACK at the bottom. The learn
prompt shows the move on offer as a full row at the top -- its type and power,
which the board has no room for -- and your four just above SKIP, nearest the
thumb. `movePickPerPage()`, `uiMovePickRowRect()`, `uiLearnRowRect()` and
`uiLearnSkipRect()` answer for both shapes, draw and tap. `tall_test` covers
the picker's last row of a view and the second view's first (move 7, not 5),
the edge strip and backing out; the learn prompt's fourth row and SKIP at
their corners, and the gap above the four doing nothing. Three deliberate
breaks were each caught. All 51 board shots are byte-identical.

**The win screen is converted.** It used to drop the full-height battle back
into a square the moment you won. `winLayout()` gives each line's position
and size for both shapes: on a rectangle the title is size 5, the rest size 3,
the badge is drawn at 6x with its hard-mode halo doubled, and the block is
centred between the cutout and the BACK hint. A line too wide for its size
drops a size on the phone only. A region without badge art gets its plain
medal at the same proportions. A tap anywhere still dismisses, so there are
no hit areas to share. `tall_test` takes the fight team select starts to its
win screen and leaves it from the BACK hint; one deliberate break (the screen
left square) was caught. All 51 board shots are byte-identical. Checked on
the S24 after a real gym win.

**The training submenu is converted.** It takes the menu's arrangement: a
sheet across the width at the bottom, over the dimmed pet screen, with the
title at size 3 and four 94 px rows, each label at size 3 above a thicker
progress bar (MEMORY shows its record there instead). A short canvas shrinks
the rows rather than covering the header band, never below the board's 44.
`trainLayout()` is the one description for both shapes; the tap handler,
`uiTrainRowRect()` and hit_test's panel checks all read it. `tall_test` covers
a tap between rows, a tap above the sheet, and MEMORY at its bottom-right
corner; its square-screen check moves to the memory game, square by design.
Two deliberate breaks were each caught. 51 of 52 board shots are
byte-identical (`battleanim`, the flaky one, is the 52nd). Checked on the S24
with adb taps: SPEED at its left edge, MEMORY at its corner, tap-outside.

Still square: the starter, LAN and transfer screens, a creature's sheet and a
dex entry. The minigames stay square by design.

Upstream bug, left alone on purpose: on the board the bag's page number
(y 386) overlaps its fifth row (y 344..394).

## Goal and approach

The Android app should use the whole phone screen, starting with the pet
screen, while the board's round 466 x 466 layout stays exactly as it is.

Today the game draws everything onto a 466 x 466 square. On Android that
drawing area becomes 466 px wide and as tall as the phone's shape allows: about
1010 px on a Galaxy S24 Ultra, worked out when the app starts. The board keeps
466 x 466.

Screens are converted one at a time:

- **A converted screen** draws across the full height.
- **An unconverted screen** draws in a square in the middle, exactly as it
  looks today, with its background filling the bands above and below.
- **Touches** shift to match, so taps on unconverted screens still land on the
  right buttons.

This avoids a big switchover where all ~35 screens must be redone at once.
Every step leaves a working app, and the board's code paths do not change.
Below, "px" means the game's own pixels; on the phone one of them is about 3
screen pixels.

```
Phase 1 ──> Phase 2 ──> Phase 3 ──> <decide after using it> ──> Phase 4
Groundwork  Pet screen  Gestures                                 Other screens
~1 day      1-2 days    half a day                               1.5-2 weeks
└──────── recommended now: about 3 days ────────┘
```

## Phase 1: groundwork (about 1 day, nothing visibly changes)

Phase 1 builds the machinery for tall screens without converting any of them.

1. **Canvas size.** Replace the fixed 466 height with a size the game is told
   at startup: 466 on the board, the phone's shape on Android. Add a "round or
   rectangular" setting too. The circle-fitting helper (`uiSafeHalfWidth()`)
   then returns the full width on a rectangle, which alone gives the square
   screens wider rows on the phone.
2. **One table of converted screens.** A single list says which screens use the
   full height. The drawing offset, the touch shift and the tests all read it,
   per the project rule from CLAUDE.md: one answer, never several copies.
3. **Centring old screens.** The offset that centres unconverted screens lives
   only in the desktop/Android graphics stand-in. On the board it does not
   exist, because there the square is the whole screen.
4. **Touch and Back.** The touch mapping in `android_main.cpp` stops clamping
   to the square. The scripted Back drag stops assuming fixed square positions
   (y 150 to 310).
5. **A guard for the board.** Take every board screenshot (`make_screens.sh` /
   `--shot`) before and after, and require them to match pixel for pixel. That
   proves the board's look did not change, which a passing test run alone
   cannot.

**Done when:** the app looks the same as today, the full test suite passes, and
the real board firmware still compiles with `arduino-cli`.

## Phase 2: the pet screen at full height (1 to 2 days)

The pet screen goes first because it is where players spend about 90% of their
time.

| Area | Now, in the square | Phone layout |
| --- | --- | --- |
| Top | Name and status squeezed under the circle's top | Header pinned to the top, below the camera cutout |
| Middle | Scene in roughly y 0 to 312 | Sky and ground fill the middle; a bigger creature; poops and bath laid out on the ground |
| Stats | Four arcs around the rim | Four labelled horizontal bars, readable at a glance |
| Buttons | Four 80 px icons along an arc | Four bigger buttons in a straight row, near the thumb |
| Bottom | PARTY and EXPLORE labels with dots | The same, pinned to the bottom above the gesture bar |

- **Also covered:** the egg (with its region pill and the guard band from trap
  4 in CLAUDE.md), goodbye ceremonies, the feed menu, sleep "Zz", the evolve,
  farewell and runaway buttons, and celebrations.
- **Positions flex,** because phones come in different shapes. Things anchor to
  the top, the bottom or the middle rather than sitting at fixed y values.
- **Screen insets.** Android reports the heights of the camera cutout and the
  gesture bar. They are passed in so the layout keeps clear of both.

## Phase 3: gestures on a rectangle (half a day)

Page flipping is the one gesture that needs a new design; the rest needs only a
check with a real finger.

- **Page flipping.** Today a drag that starts more than 186 px from the centre
  (the rim) flips pages. A rectangle has no rim, so use a drag along the right
  edge, a scrollbar-like strip, and draw the scrollbar there. `uiRimTarget()`
  is already the single list of paged screens, so the gesture, the scrollbar
  and `swipe_test` all follow from one change.
- **Swipe distances.** The 80 px swipe and 40 px tap thresholds are measured in
  game pixels, which are larger on the phone. They probably work as they are;
  confirm with a finger.
- **Finger-size targets.** `UI_TAP_FINGER` (94 px) is 9 mm on the board but
  about 22 mm on the phone, because it is set in screen pixels when it is
  really a physical size. Make it a size in millimetres per platform so phone
  targets are not oversized.

## Phase 4: the other screens, most used first (about half a day to a day each)

The remaining screens are converted in order of use, and any screen never
converted keeps working in the centred square.

1. **Battle:** the opponent at the top, your creature at the bottom, a bigger
   move grid within thumb reach.
2. **Creature card and player card:** more fits on each page, so there are
   fewer pages. Page counts are calculated from the layout, never retyped.
3. **Party and box:** the ring of six could stay as it is in the middle, or
   become a 2 x 3 grid.
4. **Gym list, Pokedex and region chooser:** more rows per page.
5. **Explore, bag, settings and clock.**
6. **Keyboard:** bigger keys.
7. **Minigames:** stay square. Their physics are tuned to 466 x 466 and redoing
   that is not worth it.

## Testing throughout

Every phase is tested at both sizes, and touch is checked on a real phone.

- **A tall build of the layout tests** (`hit_test`, `swipe_test`, `flush_test`,
  `touch_test`) at about 466 x 1010, alongside the existing square build.
- **Every converted screen still draws and stays responsive** at both sizes.
- **A tall mode for the desktop emulator** (`tamapoke-emu --tall`), so phone
  layouts can be screenshotted and checked on a PC without a phone.
- **Real-finger checks on the S24 after each phase.** Per trap 4 in CLAUDE.md,
  touch accuracy is the one thing the emulator cannot see.

## Risks, effort and recommendation

Recommendation: do Phases 1 to 3 now (about 3 days), then decide on Phase 4
after using the full-height pet screen on the phone.

- **Two layout paths per converted screen.** That is the setup behind trap 1 in
  CLAUDE.md, where a rule is fixed in one place but not its twin. To limit it,
  the round and tall versions share data and decisions and differ only in
  placement, and each screen's hit-testing asks the same position function its
  drawing uses.
- **The board must not change.** The pixel-for-pixel screenshot check from
  Phase 1 is what proves it.
- **Effort.** Phases 1 to 3 take about 3 days and cover the most-used screen.
  All of Phase 4 adds about 1.5 to 2 weeks.

## Separate from layout

Three Android gaps are independent of the layout work and can be done whenever:
sound (an audio output feeding the game's existing synthesizer), save backup
through the Android share sheet, and "your pet is hungry" notifications.
