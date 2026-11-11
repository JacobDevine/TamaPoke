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
- **Every other screen** draws in a 466 square centred on the canvas. Menu
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

**Not verified on the phone:** sound and the save backup (their
tests run on the host; see their sections above), the background -> foreground
path with the process still alive over a real wait, and anything longer than
a few hours.

## Not done

- **No serial input.** The console commands (`STATS`, `PARTY`...) have no way
  in. `EXPORT`/`IMPORT` are covered by the save backup above.
- **No LAN battles.** ESP-NOW does not exist on a phone. `link.cpp` takes its
  transport as a function pointer, so Bluetooth or Wi-Fi Direct could be
  plugged in; a phone could not fight a board without more work.
- **The minigames stay square**, by design, in the middle of the tall canvas.
  Every other screen uses the full height (see "Phone layout").
