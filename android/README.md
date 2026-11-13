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

## Sound

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
and `steps_test` the step counter.

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
- Self-medication waits for Phase 7.

### Steps

| What | Number |
|---|---|
| Joy | +1 per 250 steps today, up to 20 a day |
| Bond | +1 per 1,000 steps today, up to 3 a day, inside the game's own 20-a-day bond cap |
| Daily milestones | 500: POKE BALL. 2,000: POTION or GREAT BALL. 5,000: PROTEIN, IRON, CARBOS or ULTRA BALL. Each once a day |
| Trail ranks (steps ever) | ROOKIE, then WALKER 10,000, HIKER 50,000, TREKKER 100,000, RANGER 250,000 |
| Wild bonus (today's steps, full at 5,000) | shiny odds up to double the base (1/4096 to 2/4096); a ball's catch chance up to a tenth more (30% to 33%), never past 95% |
| Training | none: the gym ladder's balance stays upstream's |

- **Steps count while the game is closed.** The phone's step sensor keeps its
  own count; the game reads it when it opens, every 30 seconds while open, and
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
