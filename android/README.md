# TamaPoke for Android (proof of concept)

Runs the **real firmware** on an Android phone. It is the desktop emulator's
arrangement (`tools/emu/`), pointed at a phone: `TamaPoke.ino`, `pet.cpp`,
`battle.cpp` and the rest compile **unmodified** against the same stubs, and
only the platform edge is new -- `app/src/main/cpp/android_main.cpp`.

Personal sideload only. The sprites are CC BY-NC and the trainer avatars are
unlicensed (see `CREDITS.md`); this is not something to put on a store.

## Build

Needs the Android SDK with NDK `26.3.11579264` and CMake `3.22.1`, JDK 17, and
`python3` (for `genproto.py`). Then:

```bash
bash android/fetch_sdl.sh              # pinned SDL2 2.32.10, hash-checked, gitignored
echo "sdk.dir=$HOME/Android/Sdk" > android/local.properties
cd android && ./gradlew assembleDebug
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

Which regions ship is a build property, default Kanto (~52 MB APK):

```bash
./gradlew assembleDebug -Ptamapoke.regions=kanto,johto
./gradlew assembleDebug -Ptamapoke.regions=all      # ~300 MB of sprites
```

A region not shipped is **locked** in game (NEEDS PACK), on exactly the rule a
board applies to its SD card -- the same `regionArtProbe()` decides.

Serial output goes to logcat: `adb logcat -s TamaPoke`.

## How it maps

| Board | Here |
|---|---|
| 466x466 AMOLED | an RGB565 canvas 466 wide and as tall as the phone's shape (466x1010 on a 1440x3120 screen); see "Phone layout" below |
| CST9217 touch + INT | SDL finger events, one finger, through a timed queue (below) |
| NVS | the emulator's file-backed store, `files/tamapoke.nvs`, replaced atomically |
| SD card sprites | the committed `web/sprites-*.pak`, stored uncompressed in the APK and read in place |
| PCF85063 RTC | the phone's clock, as **local** wall-clock seconds (the board's convention), in the zone the save was written in (below) |
| PMU battery gauge | the phone's battery, via `SDL_GetPowerInfo` |
| PWR off / power cut | the app going to the background, or being killed |
| serial console | logcat, output only |

The firmware's source list is **read out of `tools/emu/build.sh`** by CMake, not
restated, so a new `.cpp` added for the desktop build is picked up here too.

## Phone layout

The canvas is taller than the board's and rectangular (`gPanelH`,
`gPanelRound` in `TamaPoke.ino`, set before `setup()`). Screens are converted
one at a time; `uiScreenTall()` is the one table of which:

- **The pet screen is full height.** The scene is the square layout's,
  translated down so its ground meets a lower panel laid out for the height:
  header below the camera cutout, four labelled care bars, a straight row of
  big buttons, and the tab bar along the bottom edge.
- **A tab bar** replaces the board's six tile dots on every screen of the
  tile axis: PLAYER, PARTY, HOME, EXPLORE, GYMS, POKEDEX, each with an icon
  (your avatar, the creature you are raising), the current one named on a
  pill. A tap goes to that screen, and tapping the one you are on goes to its
  root (a gym ladder back to its region chooser). The horizontal swipe still
  works. It is refused under a confirm, an evolution or a party-replacement
  choice, and absent under a creature's sheet or a Pokedex entry.
- **The minigames** spread the board's layout down the canvas (`gameY()`),
  bigger, with the physics and their timing unchanged. A hit lands at
  touch-down and buzzes (`MainActivity.haptic()`, the vibrator's CLICK; not
  `performHapticFeedback`, which is silent when the phone's touch vibration is
  off); a miss does nothing, and the back arrow leaves.
- **Any screen not converted** draws in a 466 square centred on the canvas
  (none is, today; this is the fallback for one upstream adds). Menu
  screens fill the whole canvas with their page colour (`uiPanelBg()`); the
  bands around anything else take the commonest colour of the square's edge
  row. Touches are shifted by the firmware (`uiTouchOy()`), so their tap
  handlers are unchanged.
- **Scrolling**: a paged screen scrolls like a phone list. It follows the
  finger, coasts after a flick, and stops at both ends; it can rest between
  two views. Each screen's rows scroll inside a band while its title, page
  number and buttons stay put. Down at the very top is still BACK. The
  scrollbar at the right edge shows where you are; it is not a control. How
  it works is in the comment above `scrollBand()` in `TamaPoke.ino`.
- **Back** leaves the screen, whatever view is showing (`uiBackKey()`).
- **A back arrow** at the top left of every screen one level deep (a gym
  ladder, the Pokedex grid and an entry, the box, a creature's sheet, the
  card, the bag, settings, the move picker, team select, the LAN screen) does
  the same as Back. Not on a tile's root, in a fight, or on the keyboard, which
  ignores Back on purpose. The board's "tap: back" hints are not drawn on the
  phone, and buttons that carried them say BACK.
- The camera cutout's inset comes from `MainActivity` (`DisplayCutout`).

On the board every one of these is 0 or a no-op. To see a phone layout on a
desktop: `tools/emu/tamapoke-emu --tall 1010 --inset 42 0` (add
`--shot main --hour 13` for a headless capture). `tall_test` drives the real
touch path at that size.

## The pull-down menu

On the phone the menu is a shade hanging from the top edge: STATS, BAG,
POKEDEX and SETTINGS as tiles with an icon and a live detail (level, items
held, how many registered, the time), RETIRE across the width, and a handle
along the bottom edge that closes it. A drag down from anywhere on the pet
screen except the creature (where a drag is a stroke) pulls it down under the
finger; it opens past a third of its height or on a flick, and otherwise goes
back up. A tap on the header still opens it, and a chevron under the status
line says it is there. Pushing it back up, by the handle or anywhere on it, carries it with the
finger: it closes past a third of the way or on a flick up, and settles back
open otherwise. A tap on the handle, a tap below it or Back also put it away.
Behind it the pet screen is darkened, not striped (`uiDimRows()`), and the tab
bar stays on top and keeps working; a tab leaves the menu for that screen. The board keeps its centred panel: `menuLayout()` answers `shade` only on
a tall canvas, and every board shot is byte-identical. `tall_test` drives the
pull through the real touch path.

## Screen transitions

A change of screen animates on the phone instead of cutting
(`phone_motion.h`, drawn in `android_main.cpp`). The firmware is not involved:
the host keeps the previous frame as a texture and composites it with the new
one at the display's rate, so no render path or board shot changes.

- **A neighbouring tab** slides sideways, both frames joined, with the tab bar
  still. **A tab further away** fades, so six screens do not fly past.
- **A level in** (a card, a ladder, a sheet) rises and fades in over the old
  frame; **back out** drops into place the same way.
- **The menu** drops from the top edge as the pet screen dims, and goes back
  up; **the training sheet** slides up from the bottom, and back down.
- **Anything else** (a fight, a minigame, first boot, a screen the table does
  not know) fades. A new upstream screen lands there by default.
- Launch and each return from the background fade up from black.

Taps are not held back during a transition; they already go to the new screen.
The first frame of a new screen is drawn at once rather than at the next 100 ms
render tick, so the motion starts under the finger. `motion_test` reads which
transition joins two screens from the real sketch at 466x1010.

The board's own `audio.cpp` runs unchanged: the same task, the same synth, the
same music and cries. `audio_host/ESP_I2S.h` gives it what it expects from the
board -- a FreeRTOS queue and task (a mutex queue and a `std::thread`) and an
I2S `write()` -- and that write goes to `hostAudioWrite()` in
`android_main.cpp`, which queues it on an SDL audio device at 16 kHz stereo.
It blocks while 60 ms is already queued, which paces the task the way the
codec's DMA does on the board, and keeps an effect cutting into the music
within about that long. Backgrounding pauses the device and drops what was
queued. The sound switch and volume in settings are the board's own.

`phoneaudio_test` runs `audio.cpp` over that same header on real threads and
checks that the boot jingle, an effect sent from the game thread and a cry are
heard; that music runs at the device's rate; that volume 0 is silent but keeps
time; that sound off writes nothing; and that an idle task sleeps rather than
spinning. Whether it sounds right on a phone speaker is still ears-only.

## Motion and feel

Within a screen, too, things move rather than jump. This part is firmware, at
the end of `TamaPoke.ino`, and every piece of it asks `uiPolishOn()`, which is
false on the round board, so every board shot stays byte-identical.

- **60 fps while active.** The game draws at 16 ms while a finger is down,
  through a fight, while a bar or the tab pill is moving, and on the pet
  screen for 4 s after a touch or while the creature walks, eats, bathes or
  reacts. Otherwise it keeps the board's 10 fps, so a pet left open on a desk
  costs no more than before (`uiPhonePeriod()`).
- **Time, not frames.** The board moved two things by a fixed step each
  frame: the creature's walk (3 px) and the battle's HP drain (a fifth of the
  gap). At 60 fps both would run six times as fast, so on the phone both move
  by the clock at the board's speed (`uiWalkStep()`, `uiBtlEaseSmooth()`).
  Anything else added to a render path has to follow the same rule.
- **Bars and the tab pill slide.** The care bars ease to a new value over
  about a third of a second, and the tab bar's pill slides from the stop you
  left. Both snap instead after half a second off screen, so a screen you
  come back to shows where things are rather than replaying.
- **Pressed states.** A home button sinks into a darker well under the
  finger, and another tab's place greys, until the finger moves (then it is
  a swipe or a scroll) or lifts (then the tap lands as before).
- **Asleep, the buttons keep their icons**, faded, with a "Zz" in the corner,
  rather than the board's empty outlines, which read as broken. A tap on one
  shakes it, buzzes twice, and says "Shh, it's asleep" above the panel.
- **Haptics.** A light tick on a tab change, a home button, opening the menu;
  a double buzz on a refused tap; a firmer click on a decision with weight
  (evolve, retire, farewell, release). A minigame hit buzzes as before. All
  of it obeys one switch, **BUZZ**, beside the sound switch in settings
  (`forkHapticsOn()`, a flag in the fork record).
- **No page dots beside a scrollbar** on the stacked views and the team
  picker: the bar at the edge already shows where you are and how much there is.
- **The player card names who to beat next** on each region's ladder, in the
  colour of their type, beside the badge count: a gym leader, then the Elite 4
  and the champion, and nothing once the champion has fallen.

`polish_test` drives all of it through the real sketch at 466x1010 with the
clock paused: it renders the same half second at 60 and at 10 fps and checks
the walk and the HP drain cover the same ground.

## Save backup

Settings has two phone-only buttons under RESET GAME: **BACK UP** and
**RESTORE**. The board backs up over its serial console (`EXPORT`) and the web
installer keeps that as a `.tpsave` file; a phone has no console, so the app
writes the same file itself. It is the console's `EXPORT` block byte for byte,
so a phone backup restores on a board through the installer, and a board's on
the phone.

- **BACK UP** exports the live save, verifies the text it is about to hand over
  (the same check a restore runs, as the installer does), and opens the share
  sheet with `tamapoke-<trainer>-<date>.tpsave`. `SaveProvider` serves that one
  file read-only, to the app the player picks; it is a dozen lines because the
  app has no androidx.
- **RESTORE** opens the file picker. The file is verified **before** anything is
  asked: lines, commit, the declared byte count (a short read still ends with a
  plausible commit line), magic, version and checksum. Only then does a dialog
  ask. A yes copies the current save to `Downloads/TamaPoke/` first, and **does
  not go ahead if that copy failed**; `saveImport()` validates the whole blob
  again before touching the store, and the app relaunches as the board reboots.
- The firmware only draws the buttons when the host sets `gHostSaveBackup` and
  `gHostSaveRestore`, so the board's settings screen is unchanged. Dialogs and
  toasts are Android's, in the **game's** language (`res/values-*`, accents
  allowed there: they are not the bitmap font).
- `ESP.restart()` now relaunches the app instead of closing it, so RESET GAME
  and WIPE come back on a new game rather than at the launcher.

`phone_test` holds `phone_save.cpp` to the firmware: the phone's file equals
the console's `EXPORT` output, decodes to `saveExport()`'s bytes, the golden
file the real firmware wrote verifies and imports, and every damaged copy is
refused (a missing line by its declared length, a changed digit by its
checksum, a blob that is not ours by its magic). The CRC is pinned to the
published check value. `tall_test` covers the buttons' placement, their
corners, the gap, and that they do not answer under the reset confirm.

## Care reminder

Going to the background is switching the board off, and the absence is applied
on return by `Pet::syncClock()`: awake, food falls 2 a minute and hygiene,
energy and joy 1, all to a floor of 15; asleep they fall slowly to floors of
30, 35 and 45 while energy recovers. So when the app leaves, `phone_remind.cpp`
works out the first minute at which the creature will need anything
(`statusMsg()`'s threshold, 25, for each of the four), and `Reminder.java`
schedules one notification for it; coming back cancels it. Its words are the
game's: the creature's name and the line `statusMsg()` would show for that
need ("It's hungry!", "Needs a bath!" and so on), in the game's language. When
two run out in the same minute it names the one the game would, in
`statusMsg()`'s order: hunger, bath, energy, joy.

- Not before 5 minutes (`REMIND_MIN_MINUTES`), so putting the phone down does
  not ping at once. For testing, `./gradlew assembleDebug -Ptamapoke.remindNow=true`
  builds an app that posts it 5 seconds after leaving whenever one is due.
- Not at night: due inside the night window (`NIGHT_START`..`NIGHT_END`), it
  waits for the window's end.
- None while asleep (every floor is over the line), for an egg, or in a
  farewell.
- The alarm is exact (`setExactAndAllowWhileIdle`, `USE_EXACT_ALARM`, granted
  at install on Android 13+). The first build used an inexact one, which Android
  may deliver ~15 minutes late; with the old 20-minute floor a hungry creature's
  reminder came up to 35 minutes after leaving. A reboot clears alarms, so the pending one is kept in `SharedPreferences` and
  `BOOT_COMPLETED` re-arms it (or posts it, if its time passed while off).
- Android 13+ asks for notification permission once, on the first launch.
- The notification channel is named "Care reminders" in Settings. Its id is
  still `hunger`, from when it reminded of hunger alone, so a choice already
  made for it is kept.

The drain is restated there, so `phone_test` checks it against the real thing:
for 9604 starting states (seven values of each of the four stats, awake or
asleep, odd or even age) the forecast is the first minute at which
`syncClock()` itself leaves the creature in need, the need named is the one
`statusMsg()` then shows, and the minute before it shows none; where it
forecasts nothing, `syncClock()` over up to two weeks never gets there. Change
the drain in `pet.cpp` and that fails.

## Fork rules

The game numbers this fork changes. Upstream's manual (the README's "Game
manual") stays upstream's; these apply only in the Android app, because
`forkFeatures()` is off on the board, in the desktop emulator and in `--shot`.
The constants are in `fork.h`; `fork_test` checks the natures,
`evorules_test` the evolution rules, `sick_test` sickness, medicine and coins,
`steps_test` the step counter, and `world_test` the world from the clock.

### Natures

Every hatched creature has one of five natures. It is shown on its card next to
its trait, and on a banked creature's sheet after its typing.

| Nature | Need | Drains faster while awake | Bonds more from |
|---|---|---|---|
| EASYGOING | none | nothing | nothing |
| GLUTTON | food | +1 every 2 min (2/min becomes 2.5) | berries and candy |
| PLAYFUL | joy | +1 every 4 min (1/min becomes 1.25) | play, the ball game, the memory game |
| TIDY | hygiene | +1 every 4 min | cleaning |
| LAZY | energy | +1 every 4 min | being put to bed (not being woken) |

- **The bonus is +1 bond**, through the same daily cap of 20 as every other bond
  gain, so it changes how fast a day's bond fills, not how much it can hold.
  Stroking the creature is not counted: it costs nothing to do.
- **Asleep, a nature does nothing.** Offline catch-up applies the extra drain
  too, and stops at the same floor of 15 as everything else in it.
- **Care only.** A nature never changes a battle stat, so gym balance and a LAN
  fight against a board are upstream's.
- **It is not stored.** It is a hash of the four IVs and the shiny flag, which
  are fixed at hatch and travel with the creature into the party, the box and
  back. So every creature has one, including those raised before this existed,
  and the copies can never disagree. It is a hash rather than a reading of the
  IVs so that it is independent of the trait (BOLD, STURDY, ...), which does read
  them: for every trait, each of the five natures comes up 15-25% of the time.
- **The mapping is fixed.** Changing the hash changes the nature of every
  creature already raised, so `fork_test` pins it.
- The nature also picks the evolution branches the games leave to chance,
  trade, gender or version (below).

### Evolution rules

`evo_rules.h`, generated by `tools/gen_evo_rules.py` from PokeAPI's evolution
chains; `evorules_test` checks it. The LEVEL never changes: a rule applies at the
species' usual evolution level, plus one per care mistake, plus the retire debt,
exactly as upstream. What the rules add is which form, and for some, whether.

| Who | Condition | Where it comes from |
|---|---|---|
| Pichu, Cleffa, Igglybuff, Togepi, Azurill, Golbat, Chansey, Buneary, Munchlax, Woobat, Swadloon, Type: Null | bond 30 | the games' friendship |
| Budew, Riolu | bond 30, by day | the games |
| Chingling, Snom | bond 30, at night | the games |
| Tyrogue | Hitmonlee if ATK training > DEF, Hitmonchan if <, Hitmontop if = | the games |
| Eevee | Sylveon at bond 60; else Espeon at bond 30 by day, Umbreon by night; else by nature: TIDY Vaporeon, PLAYFUL Jolteon, GLUTTON Flareon, EASYGOING Leafeon, LAZY Glaceon | games, nature |
| Wurmple | PLAYFUL/TIDY Silcoon, others Cascoon | nature |
| Slowpoke | PLAYFUL/TIDY Slowking, others Slowbro | nature |
| Kirlia | GLUTTON/PLAYFUL Gallade, others Gardevoir | nature |
| Snorunt | PLAYFUL/TIDY Froslass, others Glalie | nature |
| Burmy | GLUTTON/PLAYFUL Mothim, others Wormadam | nature |
| Cosmoem | TIDY/LAZY Lunala, others Solgaleo | nature |

- **Day is 06:00-19:59**, the scene's own day, not the 00-06 sleep window.
- **Sylveon is bond 60**, because the games' Fairy-move condition cannot be met:
  Eevee learns no Fairy move in this game's move table. The games also ask
  affection, more than friendship, which is what the higher bond stands for.
- **The bond hint.** A creature held back only by its rule says so on the
  progress page ("NEEDS MORE BOND", "EVOLVES BY DAY") rather than "Ready".
- **The EVOLUTION page**, after progress on the creature's card, lists every
  next form with its condition and the creature's own numbers, marks what
  already holds in green, and frames the form it is heading for. A form not yet
  in the Pokedex is a silhouette named "???".
- **Only forms that can be drawn.** A form whose region pack is missing is not
  offered, as upstream does for Eevee. If that leaves a nature with no form, the
  first nature form that can be drawn stands in.
- **Branch targets do not hatch** (Hitmonchan, Hitmontop, Slowking, Cascoon,
  Gallade, Froslass, Mothim, Lunala): they are reached by evolving. Single bond
  targets such as Pikachu and Snorlax keep upstream's rarity.
- **Waiting for stones (Phase 5):** Gloom to Bellossom, Poliwhirl to Politoed,
  Scyther to Kleavor, Applin, Charcadet and Clamperl still evolve as upstream
  does. Regional-form branches (Meowth, Wooper, Yamask, Sneasel) and Shedinja are
  left alone.

### Sickness, medicine and coins

| What | Number |
|---|---|
| Can fall sick when | 2+ poops, weight over 50, or any need at 20 or below |
| Chance, while one holds | 2% per awake minute (about 50 minutes on average) |
| While sick | every need -1 more every 3 awake minutes; a care mistake every 2 awake hours; battle stats 90% (not HP) |
| Medicine | cures it; 3 to start, 30 coins each in the bag |
| Coins | wild win 5 (hard 10); gym win 10 + 2 per leader deeper (hard double); purse holds 9999 |
| Sick reminder | its own notification, 30 minutes after leaving the app sick, never in the night window |

- **Only awake minutes in the app count.** Asleep, or with the app closed (the
  board switched off), a sick creature stays sick but nothing else happens: no
  extra drain, no minutes toward a mistake, and it cannot fall sick. So a long
  absence can never stack days of mistakes, and the care reminder's forecast of
  the offline drain stays exact.
- **The header says "It feels sick..."** and a violet bubble with a cross sits
  opposite the poops; the card adds SICK after the nature.
- **Medicine and the shop are the first two rows of the bag**, above upstream's items. MEDICINE is lit
  only while something is sick, and does nothing (and is not used up) otherwise.
  BUY MEDICINE is lit while the purse holds 30. The purse is under the title.
- **Sickness belongs to the creature**, by the same IV bits as the nature. A sick
  creature banked and brought back is still sick, and a new one is not. Only one
  sick creature is tracked: if a second falls sick, the first is forgotten.
- **Not in `items.h`.** Medicine and coins live in the fork's own record, because
  upstream appends to the item catalogue and our key would collide with theirs.
- **It takes its own medicine** after ten awake minutes sick, if there is one
  in the bag (see "The pet acting on its own").

### Steps

| What | Number |
|---|---|
| Joy | +1 per 250 steps today, up to 20 a day |
| Bond | +1 per 1,000 steps today, up to 3 a day, inside the game's own 20-a-day bond cap |
| Daily milestones | 500: POKE BALL. 2,000: POTION or GREAT BALL. 5,000: PROTEIN, IRON, CARBOS or ULTRA BALL. Each once a day |
| Trail ranks (steps ever) | ROOKIE, then WALKER 10,000, HIKER 50,000, TREKKER 100,000, RANGER 250,000 |
| Wild bonus (today's steps, full at 5,000) | shiny odds up to double the base (1/4096 to 2/4096); a ball's catch chance up to a tenth more (30% to 33%), never past 95% |
| Training | none: the gym ladder's balance stays upstream's |
| Streak | walking is not care: it never keeps the daily streak going |

- **Steps count while the game is closed.** The phone's step sensor keeps its
  own count; the game reads it when it opens, every 5 seconds while open, and
  at 23:59 by an alarm, so each day keeps its own steps. If that midnight
  reading does not come, those steps go to the next day instead.
- **Yesterday's last steps still count for yesterday.** They can arrive the next
  morning; they pay yesterday's milestones, but no joy or bond, since the
  creature was not walked today.
- **A reboot of the phone** loses the steps between the game's last reading and
  the reboot, at most a day's.
- **Nothing walked before you allowed it is counted.** The first reading is the
  starting point.
- **The permission is asked the first time the steps page is opened** (the
  third page of the creature's medals screen), not at launch. Refused, the page
  shows a button to ask again, which opens the app's settings once Android has
  stopped showing the dialog.
- **Where it shows:** today's count top right on the main screen, opposite the
  streak; a banner on the main screen for each milestone and rank; the page
  with the three milestones, what today's walk gave, how much of the wild
  bonus it has earned (WILD LUCK, 100% at 5,000 steps), the total and the rank.
- **An egg walks too.** Its milestones pay; its joy and bond wait for the
  creature, if it hatches the same day.
- The wild shiny bonus is not upstream's shiny bonus points: one of those is a
  whole percentage point, 40 times the base odds.

### Expeditions

| What | Number |
|---|---|
| Lengths | 15, 30 or 60 minutes |
| Energy | 10, 20 or 35, paid when it leaves; it cannot go without enough |
| Finds | 1, 2 or 3 |
| Walking bonus | one more find for 300, 600 or 1,200 steps walked while it was out |
| COAST | potions (60%), FULL HEAL (20%), POKE BALL, GREAT BALL |
| VOLCANO | PROTEIN, IRON, CARBOS (60%), POTION, SUPER POTION, GREAT BALL, ULTRA BALL |
| FOREST | medicine (30%), POKE BALL (30%), POTION, FULL HEAL, GREAT BALL |

- **Sent from Explore.** The title has two tabs, EXPLORE and EXPEDITION. On
  the second, the row picks the site, the pill the length, and SEND sends it.
  The scene behind shows the site.
- **Never asleep or sick, and it never comes back hurt.** Its needs stand still
  while it is out: nothing drains, nothing gets dirty, it cannot fall sick,
  fall asleep or make a care mistake. It still ages.
- **It finishes with the app closed.** The trip is in the fork's save, and the
  closed-app catch-up leaves the minutes it was out undrained. A notification
  says when it is back (its own channel), moved out of the night like the
  others.
- **While it is out**, the pet screen shows a sign with where it went and the
  countdown. The care buttons are greyed, it cannot join a battle (the party
  still can), take a training item, or be offered an evolution or a farewell.
  The menu and the other tiles all work.
- **Swapped out, released or replaced while out**, it is called back with
  nothing found.
- **A full bag** keeps what fits; a find that does not fit says BAG IS FULL.
- Evolution stones are not among the finds. Eevee's stone forms stay chosen by
  nature, as in Phase 2.

### Touching the pet

| Gesture | How | What it does |
|---|---|---|
| Stroke | back and forth across it, three legs of 30 px or more | +3 joy, +1 bond, music notes |
| Tickle | a quick wiggle in place, four short legs or more | +2 joy and a giggle; the 4th inside a minute annoys it: -3 joy |
| Double tap | a second tap within 350 ms | it hops, +2 joy (the first tap is the usual caress) |
| Poke | a tap on its head or its belly | a different reaction each; pays the usual caress, nothing more |
| Shake | three hard jolts within 0.9 s | dizzy for 4 s, -3 joy. Shaking again sways it again, but the joy is taken at most once per 30 s |
| Face down | held face down for 1.5 s | the screen goes off, as the board's power button; face up turns it on |

- **At most 20 joy a day** from strokes, tickles and hops together, kept in the
  fork's save so closing the app does not reset it. Their bond goes through the
  game's own daily bond cap. Losses (annoyed, dizzy) are never capped.
- **Only on the pet screen, on the creature, awake and present** (not an egg,
  not asleep, not out on an expedition). A stroke over a sleeping creature does
  nothing, and still does not change screen.
- **It never takes a gesture the game already uses.** A swipe goes one way; a
  stroke has turned back twice, so it is told apart before the swipe checks. A
  finger still wiggling after 3 s is not the hold that offers to release it.
- **Face down puts it to bed only at night**, because it is the board's power
  button: the screen off with the hour in 00:00-06:00 is what sends it to sleep.
  At noon the screen just goes dark.

### The pet acting on its own

| What | When | What it does |
|---|---|---|
| Self-medication | sick for 10 awake minutes, medicine in the bag | uses one medicine and is cured, with a banner. No medicine, no cure |
| Self-care | on charge and untouched for 10 minutes | every need under 40 goes up to 50 |
| A nap | content and tired: energy under 35, 35 in 100 of its choices (LAZY 50); under 60, 8 (LAZY 23); a rested LAZY one still 8 | sleeps where it stands for 9 s |
| A hop | content, joy 70 or more (15 in 100; a PLAYFUL one 25, from joy 50) | hops |
| A visit to the mess | content, poops on screen (15 in 100; a TIDY one 35) | walks over to them |
| A speech bubble | a reason (below), on the pet screen | one short line above its head for 4.5 s |

- **Self-care is not care.** No bond, no streak, no heart, and care mistakes
  stay as they are. It is on by default; settings has a switch under RESET
  GAME to turn it off.
- **The phone's screen timing out pauses the app**, so self-care runs in two
  places: live, with the app open, and on coming back, when the phone was on
  charge both when the app went to the background and when it returns, at
  least 10 minutes later. When the app left is kept in the save, so a process
  Android killed meanwhile still counts. Unplugged in between cannot be seen
  and is forgiven; plugged in only after leaving does not count. It runs after
  the offline catch-up, so that catch-up's floors still apply first.
- **The bubbles say something only with a reason**, by priority: sick,
  hungry, tired, dirty, bored (each under 25), a mess (two poops or more),
  just woken up, having looked after itself, and yesterday's walk (1,000 steps
  or more, once a day). Never in its first 15 s on the pet screen, never two
  within 3 minutes, the same reason not again within 15, and never asleep,
  away, as an egg or over a touch reaction.
- **The planner adds to upstream's**, which still runs every time it picks
  nothing (at least 20 in 100), so the creature still wanders and gestures as
  before. Sitting stays out for upstream's reason (the sprite faces away), and
  the weather has its own bubble (see "The weather").

### The world from the clock

The scene follows the real day, the season and the calendar. **Only the look
changes**: sleep keeps the fixed 00:00-06:00 window, and the evolutions that
want day or night keep 06:00-20:00. The exceptions are the holiday gifts and
the standby clock below.

**Where the phone is comes from its time zone, not its location.** The IANA
time zone database records each zone's main city ("Europe/Madrid" is Madrid),
so the app looks the zone up in a table (`zone_coords.h`, written by
`tools/gen_zone_coords.py`). It needs no permission and sends nothing anywhere.
A zone with no city ("UTC") draws the old fixed hours and no seasons.

| What | Rule |
|---|---|
| The sky | night, dawn, day and dusk from today's sunrise and sunset (NOAA's equations, within two minutes of the almanac). Dawn runs from 30 min before sunrise to an hour after; dusk is the mirror. Where the sun never sets it is day all day; where it never rises, night |
| The moon | drawn in its real phase, lit on the side it is lit from where you are |
| Seasons | by month and hemisphere (Dec-Feb is winter in the north, summer in the south); none in the tropics. Spring: petals and flowers. Summer nights: fireflies. Autumn: falling leaves, rust-coloured ground. Winter: frost, snow on the hills, light snowfall |
| Holidays | New Year (1 Jan), Valentine's (14 Feb), Easter Sunday, Halloween (31 Oct), Christmas (24 and 25 Dec), the player's birthday, the creature's hatch day. Each decorates the scene and the creature greets the player once that day (after "I'm sick", before everything else) |

**Gifts**, once a year each, the first time the app is open on the day:

| Holiday | Gift |
|---|---|
| New Year | 100 coins |
| Valentine's | a Hyper Potion |
| Easter | 3 Great Balls |
| Halloween | 2 Ultra Balls |
| Christmas | a Protein, an Iron and a Carbos |
| Your birthday | a Master Ball |
| Its hatch day | 50 coins |

- **The birthday is set on the player card**: the cake chip on the PARTY line
  opens the phone's date picker. Only the month and day are kept. A 29 February
  birthday is kept on the 28th in other years.
- **The hatch day** is stamped the first time the app sees a creature, as
  today less its age. A creature brought back from the party carries an age
  that matches its level rather than its life, so its date is an estimate.
  Only an anniversary counts, never the day itself.

**The standby clock.** Plugged in and left on the pet screen for 90 s, the
screen becomes a large dim clock: the date, today's sunrise and sunset, any
holiday, the battery, and the creature small at the bottom. It moves a few
pixels every minute, so an OLED never shows the same image all night. A touch
brings the game back and does nothing else. On charge the app keeps the screen
awake so the clock can show. **While it shows, the creature is treated as if
the screen were off**, like the board's PWR button: at night it falls asleep,
and coming back wakes it. Without that, a night on the charger would keep it
awake and draining. The settings row under RESET GAME is split: SELF-CARE on
the left, CLOCK on the right; turning CLOCK off also stops the screen being
kept on.

`world_test` checks all of it: the sun against the almanac for four cities
and both poles, Easter against seven published dates, the moon against known
new and full moons, the gifts once a year through a restart, the standby clock
through the real sketch, and the settings and the birthday chip. The
emulator takes `--place LAT LON ZONE_HOURS`, `--date YYYY-MM-DD` and
`--bday M-D` with `--fork`, a `standby` shot, and `BDAY M D` on its console.

### The weather

**Off until you turn it on.** It is the app's only use of the network, so it
waits for the WEATHER switch in settings (the right half of the language row).
While it is off the app sends nothing anywhere, and turning it off forgets the
last report.

**What is sent, and to whom.** Once a switch is on, the app asks
[Open-Meteo](https://open-meteo.com) (free, no key, no account) for the current
weather at the time zone's city -- the same place the sun and the seasons use,
rounded to a tenth of a degree (about 11 km). Nothing else goes with it: no
identifier, no account, no location from the phone's GPS, and no location
permission is asked for. The request is
`api.open-meteo.com/v1/forecast?latitude=-27.5&longitude=153.0&current=weather_code,temperature_2m`.
Fahrenheit is asked for in the US and the few other countries that use it.

It is fetched only while the app is open: on opening it if the last report is
over an hour old, then every hour. A failed fetch is retried after 30 s,
1 min and 1.5 min, then every 10 minutes. A report over 3 hours old is not shown, so a phone that has been
offline draws no weather rather than this morning's.

| Weather (WMO code) | The scene | The creature |
|---|---|---|
| Clear (0-1) | as without weather | a sunny day: everybody +1 joy; a clear night: nothing |
| Cloudy (2-3) | grey sky, broken cloud, no sun or moon | nothing |
| Fog (45, 48) | pale sky, banks over the hills | Ghost and Dark like it |
| Rain (51-67, 80-82) | overcast, rain | Water and Grass like it; Fire, Rock and Ground do not |
| Snow (71-77, 85-86) | overcast, snowfall | Ice likes it; Fire does not |
| Storm (95-99) | dark overcast, heavy rain, lightning every 7 s | Electric and Water like it; thunder frightens the rest |

- **Joy, by the hour.** Once per hour on the clock, awake and at home, a
  creature that likes the weather gains 1 joy and one that does not loses 1.
  At most 4 up and 4 down a day, so the weather cannot stand in for care or
  run a creature down. A creature with one type that likes it and one that
  does not (a Fire/Water one in the rain) does not mind.
- **The bubble.** When the weather changes the creature says so once (and
  once more if the app was closed and reopened): "I love this weather!",
  "I don't like this...", or what it is ("It's raining!"). Not about a clear
  night.
- **The standby clock** shows it with the temperature: `RAIN  14C`.
- **Winter's light snowfall** (section 11's season) gives way to the report:
  with the weather on, it snows when it is snowing.
- Sleep, the drains and the evolutions do not change.

`weather_test` checks the WMO table code by code, each type against each
weather, the nudge and its caps through a restart, that a report is ignored and
the phone told to forget it while the setting is off, the rounding of what is
sent, the bubble, that the scene with the setting off is pixel-for-pixel the
scene without it, the sun going behind cloud, the lightning, the standby line
and the settings switch. The emulator takes `--weather WMO TEMP` with `--fork`,
and `WX WMO TEMP` / `WX OFF` on its console.

### The farm and the album

**The farm is the box made visible.** Banked creatures (the party and the box)
stay frozen exactly as upstream keeps them: nothing here ages, levels, trains
or feeds one, so the gym ladder's balance is upstream's. What changes is that
they can be seen.

- **The FARM tab.** The party screen's title is two tabs, PARTY and FARM. The
  farm is a field with up to 8 banked creatures wandering in it, the party
  first, then the box in slot order. With more than 8, the next 8 come out
  each day, wrapping, so everybody is out at least once every ceil(n/8) days.
  A tap on one makes it hop and shows its name, level and nature. Nothing on
  the tab changes anything. It does not show while a newcomer is waiting for
  a party slot.
- **Visitors.** On the pet screen, 40 s after arriving and then every 4
  minutes, one banked creature (any of them, at random) walks across behind
  the live one in 16 s, smaller and further back, with its name over it. Not
  while the creature sleeps, not for an egg or during a ceremony, and leaving
  the screen sends it home. With nothing banked, nobody comes.

**The album** remembers every creature whose life ended, newest first, three to
a page on the medals screen after the steps page: its name, the level it
reached, how it ended (FAREWELL, RETIRED for an early or repeated retire,
RAN AWAY, RELEASED), its medals, and the days it hatched and left. A runaway
and a release leave nothing else behind, so this is the only trace of them.
The newest 64 are kept; the count of lives ended keeps counting past that.
The hatch day is the one section 11 stamps, so a creature that hatched before
this build reads `?`.

It is its own save record, `albA` / `albB`, apart from the fork record (it is
about 2 KB and changes only when a life ends), and rides in every backup.
Each entry is written when the ceremony ends, before the party handover; a
ceremony replayed after a power cut is not entered twice.

`farm_test` drives all four endings through the real ceremony and checks the
dates, the dedupe, the cap, a restart, a later build's bigger entries and an
unknown species in them, the album pages, the daily turns, the FARM tab's taps
(a creature where it is drawn; the party's hidden BOX hub not answering; CLOSE),
the visitor's timing and drawing, and that none of it changes a banked
creature.

### LAN battle, phone to phone

Upstream's LAN BATTLE (the button on the gym chooser) pairs two boards over
ESP-NOW. The phone has no ESP-NOW, so it pairs over **UDP on the same Wi-Fi**
(plan step 12e). `link.cpp`, the protocol, is unchanged apart from one fix
below; only the transport and the pairing differ. A board and a phone cannot
fight each other.

- **HOST** opens a UDP socket on port 47474 and advertises itself over NSD
  (`_tamapoke._udp`) under the trainer name. The screen says it is waiting and
  shows the phone's own address. It waits as long as the player does: the
  link's 20 s pairing timeout starts only once a guest has arrived.
- **JOIN** lists the hosts NSD finds, one row each. Nothing is sent until one is
  tapped. **TYPE AN ADDRESS** opens the phone's own text dialog (the game's
  keyboard has no digits) for a network that blocks multicast, or for the
  Android emulator, which can reach a phone through its NAT but cannot be
  found by one. So the emulator can only ever be the guest.
- The host locks onto the first guest that reaches it and ignores everyone
  else, as the board locks onto a MAC, so two pairs on one network do not
  join each other's fights.
- Discovery is `Net.java` (with a multicast lock, only while pairing). The
  socket is `phone_net.cpp`, plain POSIX, so the desktop emulator runs it too:
  `tools/emu/tamapoke-emu --net`, then `JOIN <address>` on its console.

**Fixed upstream on the way:** the hello carried `peerName` as the sender's
name, but that field holds the PEER's name as soon as their hello arrives, so
every resent hello told the peer its own name. A phone host only speaks once
it has heard the guest, so there the guest always saw itself as the rival.
`Link` keeps its own name now (`myName`), and `lossy_test` checks the names
after resent hellos.

**And a second one, which a board pair would hit too:** once both sides were
READY, or the host was thinking over its move, nothing was outstanding, so
nothing was sent -- and each side gives up on silence after 12 s. Reading the
READY screen for 12 s ended the fight as "the rival left" on both phones, the
first time it was tried. `tick()` sends an empty `LM_PING` whenever it has
nothing else to resend (`LINK_PROTO` 2 -> 3), and `lossy_test` sits a minute
on READY and a minute mid-turn over the lossy channel.

`net_test` drives the socket over localhost (the waiting host, the lock, the
stranger dropped, the address parsing, the host list). `netlan_test` drives
the real LAN screen with the real socket against a second "phone" in the test,
as both guest and host, and checks that with no phone transport the board's
screen is unchanged.

### Adventures with other players

Plan step 12e parts 2 and 3. An adventure can have one host and up to three
guests on the same run, on the same Wi-Fi. `fork_raidnet.cpp` is the protocol
and, at its end, the game's side of it; `phone_net.cpp`'s raid mode carries it.

**Playing it.** The adventure tab shows HOST and JOIN under "WITH FRIENDS"
when the phone has a network to play over (never on the board).
- HOST starts a run at the chosen difficulty, priced as START is, and opens
  it. The lobby is the loan screen: each player picks one of their own three
  loans, a dot lights beside each seat as it picks, and the host's START lights
  once everyone has. Seats nobody joined are AI companions. The host's address
  is under the title, for a guest whose network hides it.
- JOIN lists the adventure hosts on the network by trainer name, or TYPE AN
  ADDRESS. It needs the price in coins (or the day's free run) but takes them
  only once the host's run starts for you.
- The host picks the doors. In a fight, a player who has chosen sees "WAITING
  FOR THE OTHERS", and each seat shows whose it is and whether they have
  chosen. Somebody who has not chosen 45 s into a turn, with everybody else
  ready, has the AI play that one turn for them.
- Catches and the keep are per player, as in the protocol below. The host's
  DONE waits until every connected guest has reached its own KEEP.
- A guest who loses the host sees LOST THE HOST with REJOIN (free) and GIVE UP
  (keeps what it caught). A host killed mid-run hosts the run again by itself
  when the app comes back; its guests REJOIN.

**Who you are.** A random number per device, in the app's storage next to the
save but not in it (`device.id`), so a save restored on a second phone does not
take the first phone's seat.

**The protocol:**

- **The host resolves everything.** Its `gAdv` is the run. A guest's `gAdv` is
  a MIRROR of it, drawn by the same adventure screens, and REMAPPED so the
  guest's own seat is seat 0 there: "seat 0 is mine" holds on every phone. A
  guest sends choices (a loan, a move, a catch) and never an outcome. Every
  call that would move a run refuses on a mirror.
- **The host sends absolute state**: the run as its own save record
  (`advEncode()`), plus who has chosen, the guest's own three loans and the
  last turn's narration, in one ~1 KB datagram. A guest that missed packets
  lands on the right run from the next one. Choices are stamped with the
  stage and battle turn, and resent until the host's state shows them taken.
  The host resends its state until each guest acknowledges that generation.
- **The lobby is the LOAN phase.** The host starts a run as a solo one starts
  (choosing the difficulty and paying), then hosts it. Each guest that joins
  gets a seat and three loans rolled for it. The host can start once everyone
  has picked. A seat nobody joined keeps its AI companion.
- **Each player pays for their own run.** A guest's first state marks that
  day's free run taken in the guest's own record, by the guest's own day.
  `chargeDue` asks the screen to take the coins after the mirror is saved.
- **Dropped and back.** A guest silent for 12 s is dropped: in the lobby its
  seat goes back to an AI companion, mid-run an AI plays it (the 12d team
  rules) and the seat stays the player's. The same player rejoins with the
  run id from its mirror, from any address, and gets the seat back. A killed
  guest's mirror is saved, so it knows whose run it was in. A killed host
  resumes its run with nobody connected, and its guests rejoin it. A guest
  that gives up keeps what it caught (as GIVE UP does) and says goodbye, so an
  AI takes the seat at once.
- **The catch and the keep are per player.** Every player gets a copy of each
  creature caught. The host waits for every connected player's swap decision.
  At the KEEP each phone keeps from its own mirror, and a KEEP state arriving
  after that keep was paid is not applied again (two separate guards, each
  enough on its own). `allSeen()` tells the host that every guest has the KEEP
  before it finishes.
- **The record grew a seat block** (`AdvNet`, appended): the role, the guest's
  seat, the run id, who owns each seat and their names. A record from before
  it reads as a solo run, and one cut off partway into it is refused.

**Found on the way: DITTO was a loan.** It is in the stage pool (fully evolved,
not legendary) but has no move in this game's table, so a DITTO seat counted
as decided every turn and never acted, in solo runs too. Stage foes and loans
now need an attacking move by the easiest difficulty's level, through the same
`moveUnlockLevel()` gate the moveset is built with; `advrun_test` checks every
species in the pool.

`raidscreen_test` drives the real screens with the real UDP socket against a
second phone in the test, once as the host and once as the guest: the lobby,
doors, turns, the turn deadline, a dropped guest, a killed host coming back,
REJOIN, a refused rejoin, the host's DONE waiting for its guest, and the price.

`raidnet_test` runs a host and up to four guest phones in one process, each
with its own run record and store swapped in while it runs, over a bus that
drops and duplicates packets. It covers the lobby, turns, catches, drops,
rejoins, both kinds of kill, refusals, corrupt states and a whole run with a
third of the packets lost.

## Things that were wrong the first time, and why

- **Taps vanished.** `handleTouch()` polls at most every 20 ms; the loop runs at
  vsync (~16 ms); `adb shell input tap` holds for ~17 ms. A press could fall
  between two polls. Every touch now goes through one queue in which a press
  change is held `TOUCH_HOLD_MS` (45) before the next may replace it.
- **The Back button** first shipped as its own per-frame scripted drag and
  failed for the same reason, then went through the touch queue as a real
  downward drag. Once a down-swipe paged back first, that drag would have
  turned Back into "previous page", so it calls `uiBackKey()` now: the
  down-swipe's back path with the finger's wake rules. On the main screen it
  sends the app to the background, as Back does in any app.
- **It was midnight at noon.** The board's RTC holds local time and the firmware
  reads the hour as `epoch % 86400`; the first build returned UTC.
- **It was a small disc in a tall black screen.** The first build masked the
  square to the round glass and scaled it to 96% of the width, nearest
  neighbour at x3.09 -- so every third pixel column was wider and text
  wobbled. Now the frame is shown square and edge to edge; menus, which clear
  to black and fill a disc, get their corners filled in the disc's colour;
  the bands above and below stretch the first and last rows (sky up, ground
  down); and scaling is nearest to the next whole multiple, then linear down
  the remainder. None of this touches the firmware.
- **Backgrounding is switching the board off.** The save is flushed, and on
  return the absence goes through `Pet::syncClock()` -- the offline catch-up with
  its floors. `emuClockSkipGap()` stops the same gap also reaching `millis()`,
  where `Pet::update()` would replay it as awake ticks with no floor.
- **Changing time zone aged the creature.** Local wall-clock seconds are what
  the firmware measures an absence in, so flying 10 hours east read as 10 hours
  away, flying west was refused as the clock going backwards, and daylight
  saving did either by an hour. The phone now keeps the zone the save was last
  written in (`phone_clock.cpp`, in its own NVS namespace in the same file),
  measures each absence in it, and only then adopts the new zone, moving the
  last-seen time with it so the jump is never counted. It checks on start, on
  return from the background and every 30 s while open. `phone_test` flies
  east and west and checks the age gained is the time really away.

## Verified

On an Android 15 (API 35) x86_64 emulator: first boot -> region -> starter ->
egg -> hatch; horizontal swipe to a tile; Back out of it; Back to background;
process killed while backgrounded and the save intact; clock moved forward
2 h, relaunch: level 1 -> 3, stats at the switched-off floors.

On a Galaxy S24 Ultra (arm64, camera cutout): every converted screen of the
phone layout, by finger and with adb taps, as recorded screen by screen in
`PHONE_LAYOUT_PLAN.md`, and scrolling. Time zones changed under the app with
`cmd alarm set-timezone`: Brisbane -> Tokyo and Tokyo -> Auckland while
backgrounded, Auckland -> Brisbane across a force-stop; each return measured
the absence in the zone it left in, and none aged the creature. The care
reminder with `-Ptamapoke.remindNow=true`: none while asleep; after droppings
had run hygiene down with food still high, "TORCHIC / Needs a bath!" (due in 11
minutes), cancelled on return, and opening the app 11 minutes later the
creature did read "Needs a bath!". The channel shows as "Care reminders".

Sound and the save backup, on the S24 (2026-11-11). Sound: `dumpsys
media.audio_flinger` shows the app's track at 16 kHz stereo, usage MEDIA, gain
0 dB and active in the foreground, frames advancing with zero underruns. While
the app is backgrounded the track is paused and its gain reads `-inf`. BACK UP:
the share sheet offers `tamapoke-save-<date>.tpsave` ("save" because no trainer
name was set). The file pulled from the app's cache passes the web installer's
own `verifyBackup()` (3097 bytes, as declared), which refuses it with one digit
changed. RESTORE of that file through the system picker: the dialog, the current
save copied to `Downloads/TamaPoke/` first, a relaunch, and the same creature.

**Not verified on the phone:** whether the sound is right to the ear, the
background -> foreground path with the process still alive over a real wait,
and anything longer than a few hours.

## Not done

- **No serial input.** The console commands (`STATS`, `PARTY`...) have no way
  in. `EXPORT`/`IMPORT` are covered by the save backup above.
- **No LAN battles.** ESP-NOW does not exist on a phone. `link.cpp` takes its
  transport as a function pointer, so Bluetooth or Wi-Fi Direct could be
  plugged in; a phone could not fight a board without more work.
