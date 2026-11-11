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
| PCF85063 RTC | the phone's clock, as **local** wall-clock seconds (the board's convention) |
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
  big buttons, and the PARTY / EXPLORE nav along the bottom edge.
- **Every other screen** draws in a 466 square centred on the canvas. Menu
  screens fill the whole canvas with their page colour (`uiPanelBg()`); the
  bands around anything else take the commonest colour of the square's edge
  row. Touches are shifted by the firmware (`uiTouchOy()`), so their tap
  handlers are unchanged.
- **Paging** is a vertical swipe anywhere on the screen, as a list scrolls:
  up for the next view, down for the previous one, and down on the first view
  is still BACK. Up past the last view bumps. The scrollbar at the right edge
  only shows where you are; it is not a control (`uiSwipePage()`).
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

## Verified on an Android 15 (API 35) x86_64 emulator

First boot -> region -> starter -> egg -> hatch; horizontal swipe to a tile;
Back out of it; Back to background; process killed while backgrounded and the
save intact; clock moved forward 2 h, relaunch: level 1 -> 3, stats at the
switched-off floors.

**Not verified:** a physical phone (real touch latency, a notch or rounded
corners, battery), arm64, the background -> foreground path with the process
still alive over a real wait, and anything longer than a few minutes.

## Not done


- **No serial input.** The console commands (`STATS`, `EXPORT`...) have no way
  in. `EXPORT`/`IMPORT` would be the obvious first thing to expose, as a save
  backup through the share sheet.
- **No LAN battles.** ESP-NOW does not exist on a phone. `link.cpp` takes its
  transport as a function pointer, so Bluetooth or Wi-Fi Direct could be
  plugged in; a phone could not fight a board without more work.
- **Changing time zone ages the creature.** Flying east by 10 hours reads as 10
  hours away. The board has the same property when its clock is re-set; on a
  phone it happens by itself.
- **Notifications.** Nothing tells the player the creature is hungry while the
  app is closed.
- The UI is still the 466 px panel's layout, in a square across the middle of
  the phone, with the bands above and below only continuing the background.
  Using that space (or a phone-native layout) is a firmware change: ~35
  screens draw at fixed coordinates sized to the circle.
