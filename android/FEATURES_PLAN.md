# TamaPoke Android: new features plan

Written 2026-11-12.

## Status

**Phase 0 is built except 0.5** (branch `feat/fork-foundation`, 2026-11-12):

- `fork.h/.cpp`: `forkFeatures()`, the `frkA`/`frkB` record, and the
  `gForkHost` hooks. `fork_i18n.h/.cpp`: the fork's string table.
- The Android app turns the switch on before `setup()`. The desktop emulator
  takes `--fork` for trying features without the phone; `--shot` and
  `make_screens.sh` never pass it.
- `fork_test` covers it (34 checks), and each guard was negative-checked by
  breaking the code: dropping the keys from `SAVE_FIELDS`, ignoring the switch
  in `save()` or in `begin()`, writing one slot only, and trusting a short
  body all fail it.
- 53 of 53 host suites pass. All 60 board `--shot`s are byte-identical to
  `main`. The APK builds and starts on the S24 with its save intact.
- Found on the way: the app's CMake read its source list from `build.sh` only
  at configure time, so a new `.cpp` failed to link until `.cxx` was deleted
  by hand. It now reconfigures whenever `build.sh` changes.

**Phase 1 (natures) is built** (same branch, 2026-11-12). On the S24 the
existing save loaded intact and its Torchic (IVs 25/24/9/10) reads "BOLD
PLAYFUL" on the card, which is what the hash gives for those IVs. The party
and box were empty there, so the banked sheet was checked only in the emulator.

- Five natures, one per need, care only: see `android/README.md` § "Fork
  rules" for the numbers. The card shows it after the trait, and a banked
  creature's sheet after its typing.
- **Derived, not stored** (a change from the plan below, agreed 2026-11-12): a
  hash of the four IVs and the shiny flag. Creatures have no id, and storing a
  nature per party and box slot needed hooks in every upstream path that moves
  creatures (swap, switch-in, revive, handover, wild catch). The hash needs none,
  and every creature, banked and caught ones included, already has one.
- `fork_test` grew from 34 to 60 checks: the spread (each nature 19-21%), independence
  from upstream's trait (15-25% per trait), a pinned mapping, live and banked
  agreeing, drain live and offline with the floor, no extra drain asleep, the
  care bonus and the daily cap. Each was negative-checked by breaking the code.
  The asleep guard inside the hook cannot fail on its own, because both callers
  already skip sleep; the check guards the callers.
- 53 of 53 host suites pass. 59 of 60 board shots are byte-identical to
  `main`; `battleanim` is the known flaky one, and both builds produce its
  common frame.
- Found on the way: `mutations.mjs` and `native.mjs` keep their own lists of
  firmware sources, and Phase 0 had not added `fork.cpp` to them, so the
  mutation gate had not been linking since Phase 0. Both lists have it now.

**Phase 2 (evolution rules) is built** (same branch, 2026-11-12). On the S24
the save loaded intact; its Torchic's card gained an EVOLUTION page showing a
silhouetted Combusken at Lv.29 (16 plus 13 care mistakes), agreeing with the
progress page. The branching species were checked in the emulator only.

- `tools/gen_evo_rules.py` writes `evo_rules.h` from PokeAPI's evolution
  chains: 39 rules over 24 species. See `android/README.md` § "Evolution
  rules" for the table. Everything reads it through `fork.cpp`
  (`forkEvoRuled()`, `Pet::forkEvoBranches()`/`forkEvoPick()`,
  `forkEggBarred()`); `canEvolveNow()`, `evolve()`, `lineHasUnregistered()`,
  both egg rolls and the card all ask those.
- Decided while building: bond 30 for friendship evolutions (about two days of
  care); Eevee's five stone forms go by nature until Phase 5 (TIDY Vaporeon,
  PLAYFUL Jolteon, GLUTTON Flareon, EASYGOING Leafeon, LAZY Glaceon); **Sylveon
  is bond 60**, because Eevee learns no Fairy move in this game's move table;
  day for an evolution is 06:00-19:59; Tyrogue reads ATK vs DEF *training*.
- Only branch targets are barred from eggs (Hitmonchan, Hitmontop, Slowking,
  Cascoon, Gallade, Froslass, Mothim, Lunala). Single bond targets such as
  Pikachu keep upstream's rarity.
- The card: a fifth page, EVOLUTION, appended so upstream's page numbers never
  move (`cardPageCount()`). The progress page says "NEEDS MORE BOND" or
  "EVOLVES BY DAY" instead of "Ready" when the rule is all that is missing.
- `evorules_test` (39 checks) covers each condition both ways through the real
  `canEvolveNow()`/`evolve()`, the switch-off behaviour, the pack filter, both
  egg rolls and the line walk. Each hook was negative-checked by breaking it.
- 54 of 54 host suites pass. 59 of 60 board shots are byte-identical to
  `main`; `battleanim` is the known flaky one, and both builds produce its
  common frame.
- Found on the way: the `progress` shot sets `cardPage = 4`, past upstream's
  last page, which upstream draws as progress. A plain `else` for the new page
  would have changed that board shot; the page is drawn only with the switch on.
- Open: decision 3 below (offer it upstream) is still undecided.

**Phase 3 (sickness, medicine and coins) is built** (branch `feat/sickness`,
2026-11-13). The numbers are in `android/README.md` § "Sickness, medicine and
coins". The open questions were settled with the owner: sickness lowers battle
stats a little (90%); the punishment is as planned; "it's sick" is its own
reminder on its own channel; and medicine comes both as a starting stock (3)
and from a shop, paid in a new fork-only coin purse earned by winning battles.

- Kept in the fork record (`FORK_BODY_LEN` 4 -> 16): the sick creature's key,
  its sick minutes, the medicine, a flags byte and the coins. A record from
  Phases 0-2 loads with none of them and is handed the starting medicine once.
- Sick only counts awake minutes in the app. Closed or asleep it is paused,
  which is the answer to the plan's warning about the offline catch-up.
- `sick_test` drives every caller through the sketch: tick()'s dice, drain and
  mistake clock, syncClock() doing nothing, the bag's medicine and shop rows,
  buildSquad()'s weakened stats, and a real wild win paying coins.
  `phone_test` checks the reminder's delay.

**Phase 4 (the step counter) is built** (branch `feat/steps`, 2026-11-13),
and checked on the S24: the permission dialog on first opening the page, the
baseline reading, the 23:59 alarm armed (exact), and steps reaching the game
through the real sensor, JNI and the poll, with the counter, the milestone's
item and its banner. Steps were faked there by moving the app's stored last
reading back, not walked; the 23:59 reading itself waits for a night. The numbers
are in `android/README.md` § "Steps". Settled with the owner: no training
from steps; modest caps (joy +1 per 250 steps up to 20 a day, bond +1 per
1,000 up to 3 a day, inside upstream's own cap); upstream items at the
milestones; a small wild bonus.

- The wild bonus is NOT upstream's shiny bonus points, unlike what was first
  proposed: one point is a whole percentage point, 40 times the 1/4096 base,
  so "+5" would have meant a 5% shiny rate. It is in the roll's own units
  instead, at most double the base odds, and a catch chance a tenth higher.
- Kept in the fork record (`FORK_BODY_LEN` 16 -> 32): the day the counts are
  for, its steps, steps ever, that day's paid milestones, joy and bond, and
  the highest rank announced. **A day's record moves only when a newer day's
  steps arrive**, never with the clock, so yesterday's last steps arriving
  this morning still reach yesterday's milestones.
- `Steps.java`: the sensor, the reboot correction (BOOT_COUNT, or a lower
  reading), a baseline first reading so nothing from before the permission
  counts, a 23:59 exact alarm re-armed at boot, and day buckets handed to the
  game oldest first, at most eight at a time.
- The steps page is the medals screen's third page (appended, as the card's
  EVOLUTION page was). It asks for ACTIVITY_RECOGNITION the first time it is
  seen; refused, its button asks again or opens the app's settings.
- `steps_test` (60 checks) drives the poll, the sketch's loop, the page and
  its button, the shiny roll and a real ball thrown from the battle bag. 23
  deliberate breaks all fail it; three needed new checks first (two switch
  guards nothing reached, and a batch loop whose break crashed rather than
  failed). `fork_test` follows the hooks' new shape. Host suites 59/59.
  Board `--shot`: 64/64 byte-identical to Phase 3 built in the same checkout.
- **Found on the S24: walking kept the daily streak alive.** Upstream's
  `addBond()` registers the day's care, so the first bond point from steps
  advanced the streak with nobody caring for the creature (the phone showed
  "3 DAY STREAK!" beside "2000 STEPS!"). `addBond()` takes a `care` flag now,
  default true for every upstream caller; walking passes false. `steps_test`
  fails with it put back.
- **Found on the S24: steps arrived up to 30 s late**, on the sketch's loop
  timer. The app polls every 5 s itself now.
- Found on the way, in UPSTREAM code: hatching cleared `newMedal` but not
  `medalUntil`, so a medal earned in the four seconds before a hatch made
  `drawCelebration()` print a null name and crash. Fixed in `pet.cpp` with
  one line; it belongs in an upstream PR.
- Found on the way: comparing board shots against a `main` worktree reports
  22 false differences, because a fresh worktree has no sprites and draws
  every creature as its fallback. Compare builds made in the same checkout.
- `mutations.mjs`' combined legacy-migration control links `inventory.cpp`
  now, since `fork.cpp` pays milestones into the bag.
- Not built: Health Connect (the plan's alternative), and a reward for a
  trail rank beyond its banner.

**Phase 5 (expeditions) is built** (branch `feat/expeditions`, 2026-11-14).
The numbers are in `android/README.md` § "Expeditions". The open questions
were settled with the owner: it cannot be sent asleep or sick, it never comes
back hurt (energy is the only cost), and there are three fixed sites (coast,
volcano, forest) rather than one per region. Checked on the S24
(2026-11-17) with adb taps: the tab, the three sites and their scenes, the
length pill, SEND, the sign and countdown, greyed care buttons that do nothing,
and the squad offering only the party.

- **Found on the S24: a care reminder fired mid-trip.** Sent out already
  hungry, the creature's "It's hungry!" was scheduled 5 minutes later, while
  FEED was greyed. `needAfterMinutes()` froze the stats for the trip but still
  asked whether one was low. It skips those minutes now; `phone_test` failed
  on the old code ("forecast 2, inside the trip"), having only checked the
  forecast against `syncClock()`, which agreed the creature was hungry.

- Kept in the fork record (`FORK_BODY_LEN` 32 -> 46): the creature's key (the
  owner stamp), the end time, the steps total when it left, the site and the
  length. A record from Phase 4 loads with no trip.
- Its needs stand still while it is out: `Pet::tick()` returns early, and
  `syncClock()` skips the absence's first `forkExpMinutesLeft()` minutes. The
  care reminder's forecast skips the same minutes, and `phone_test` holds the
  forecast to `syncClock()` with trips of 1 to 60 minutes running.
- One answer, `forkAway()`, asked by the care buttons (`uiButtonDisabled()`),
  the pet screen's taps and its hold, the squad (`pickExists(0)`,
  `buildSquad()`), the bag's training items, the evolution and farewell offers
  and `statusMsg()`.
- Owned by the creature, like sickness: a different live pet means the one
  that left is called back, empty-handed, with no hook in upstream's swap,
  revive or hatch paths.
- Coming back writes the cleared trip and any medicine in one record write
  before upstream's items go into the bag, so a cut can lose those items but
  can never pay a trip twice.
- A third notification kind in `Reminder.java`, EXPED, with its own channel.
- Evolution stones (decision 2) are still not finds: Eevee's stone forms stay
  chosen by nature.
- `exped_test` drives the tab and its button, the poll, the live tick, the
  offline catch-up, the buttons, the squad, a wild battle, the bag, the
  evolution offer, a call-back, a full bag and the switch off.

**The medals screen scrolls smoothly** (same branch). It arrived from upstream
after smooth scrolling was built, so it was still a square screen flipping
whole pages. It stacks its pages like the card now; `tall_test` covers it.

**Phase 6 (more ways to touch the pet) is built** (branch `feat/touch`,
2026-11-17). The numbers are in `android/README.md` § "Touching the pet". The
open questions were settled with the owner: stroke, tickle, double tap, poke
and face down are in, shake costs a little joy, and drag-to-walk, the circle
and tilt are left out (the riskiest against the tile axis). Checked on the S24
(2026-11-18): the gestures with adb taps and drags, face down and shake by
hand. That check found three things, all fixed: the marks floated a third of a
creature above its head (the idle frame's top padding), the anger mark did not
read as one, and the hop shrank the creature for its length (its taller frame
was capped to the usual box). Each gesture now logs one `fork: touch` line.

**Phase 7 (the pet acting on its own) is built** (branch `feat/behaviour`,
2026-11-18). The numbers are in `android/README.md` § "The pet acting on its
own"; the decisions are under Phase 7 below. Fork record `FORK_BODY_LEN`
51 -> 63: when the app left on charge, and the day before's steps for
"yesterday you walked". `behave_test` drives self-medication through
`tick()`, self-care through `loop()` and after `syncClock()`, the bubbles
through the loop's poll, and the switch through settings' own tap; fifteen
guards were broken on purpose and each failed its check (two only after the
test was tightened: a gap with nothing else to say, and a bubble that came and
went before the check looked). `sick_test`'s sick creature now has no
medicine, or it would cure itself before the mistake clock ran. Checked on the
S24: the greeting bubble on waking, and the settings switch. Self-care on
coming back and the planner's choices are checked by the tests only so far.

- Face down is the board's PWR short press, so it sleeps only in upstream's
  night window, as the board does. At noon the screen only goes dark.
- Fork record `FORK_BODY_LEN` 46 -> 51: the day and the joy the gestures have
  given that day. Player-wide, so no owner stamp.
- The accelerometer is read through SDL's sensor API in `android_main.cpp`; no
  Java. No sensor is no feature.
- `gesture_test` drives every gesture as a finger through `handleTouch()` and
  the motion as samples through `loop()`, each "did not steal it" check with a
  companion proving the thing still works. Negative-checked by breaking six
  guards (the hold, the order against the swipes, the cap, sleep, the
  face-down hold, the shake cooldown); each failed its check.

**Phase 8a (the world from the clock) is built** (branch `feat/world-clock`,
2026-11-18). The rules are in `android/README.md` § "The world from the
clock". Settled with the owner: the sun changes only the LOOK (sleep and the
day/night evolutions keep their fixed hours), and holidays bring gifts as well
as decorations. Added beyond this plan: the player's birthday (set from the
player card with the phone's date picker), the creature's hatch day, and the
moon's real phase. Fork record `FORK_BODY_LEN` 63 -> 76: this year's gifts,
the birthday, and the hatch day with its owner stamp.

- The place is the time zone's principal city (IANA `zone.tab`, 549 IDs with
  the old aliases), not a location: no permission and nothing sent. 8b's
  weather is the first feature that would need the network.
- The standby clock counts as the screen being OFF for the creature, because
  the app keeps the screen awake on charge to show it, and a night awake on
  the charger would otherwise drain with no floor (CLAUDE.md § "The three
  drain paths").
- `world_test` (the 65th suite) checks the sun against the almanac, Easter and the moon
  against published dates, gifts across a restart, and standby through the
  real sketch; eight guards broken on purpose all failed it. 284 board shots
  (every shot at four hours) are byte-identical to `main`. The APK builds;
  **not yet checked on the S24**, which was not connected.

**Phase 8b (the weather) is built** (branch `feat/weather`, 2026-11-18). The
rules are in `android/README.md` § "The weather". Settled with the owner: the
weather changes the look and nudges joy by type (no spawns: wild encounters on
the main screen do not exist to bias, and expeditions were left alone); the
place is section 11's time-zone city, so no location permission; and it is OFF
until the player turns it on, the app's first and only network use. The
settings' language row is split to hold the switch, as the sound row was for
BUZZ, because a third settings row would push BACK UP / RESTORE under the
finger floor at 466x1010.

- Open-Meteo (no key) is fetched on the Java side (`Weather.java`) with the
  `INTERNET` permission, hourly and only while the app is open; a report over
  3 hours old is not shown. The firmware decides when to fetch
  (`forkWeatherSync()`), so the Java side never fetches on its own.
- Region backgrounds were NOT added: the weather is drawn over the existing
  scene in code, so the unresolved licence of the battle backgrounds (TODO D)
  does not grow.
- Fork record `FORK_BODY_LEN` 76 -> 86: the hour last nudged, and the day's
  joy given and taken. The setting is `FORK_FLAG_WEATHER`, the first flag that
  turns something ON, so a save from before it reads as off.
- `weather_test` (the 66th suite); fourteen guards broken on purpose, each
  failing it. 288 board shots (72 screens at four hours) are byte-identical
  to `main`.
- **Checked on the S24** (2026-11-18): WEATHER OFF on the existing save and
  nothing fetched; switched on in settings, the first fetch hit a read timeout
  (Open-Meteo was briefly slow from here too), which is why a failure is now
  retried after 30 s rather than 10 minutes; after that, overcast at 21C for
  Brisbane, drawn as a clouded night sky with no moon. The setting survived a
  reinstall. Then, with a storm report planted in the app's cache: dark
  overcast and rain, lightning caught on a screen recording twice 7.1 s apart,
  the awake Blaziken (Fire) nudged `joy -1`, its bubble "I don't like
  this..." (after the morning, hungry and bored ones, which outrank it), and
  the standby clock on a faked charger reading `STORM  17C`. The save was
  restored byte-identical afterwards.

**Phase 9 (the farm and the album) is built** (branch `feat/farm`,
2026-11-18). The rules are in `android/README.md` § "The farm and the album".
Settled with the owner: Option A (the box made visible; banked creatures stay
frozen), and the album in this phase.

- The farm is a FARM tab beside PARTY on the party screen: up to 8 banked
  creatures (party first, then the box) in a field, the next 8 each day so a
  big box takes turns. A tap shows name, level and nature. A visitor crosses
  the pet screen behind the live creature every 4 minutes while it is awake.
- The album is the medals screen's pages after the steps page, newest first,
  three a page, the newest 64 kept. It is its own record, `albA`/`albB`
  (about 2 KB, written only when a life ends), with its entry size and count
  in the body, and in `SAVE_FIELDS`. One hook, in `Pet::update()` where every
  ceremony ends; a replayed ceremony is not entered twice.
- The plan's "box (36)" was stale: the box is 1,200 slots and farewells
  already fall through to it, so the album matters most for runaways and
  releases, which leave nothing else behind.
- `farm_test` (the 67th suite). Fifteen guards broken on purpose: thirteen
  fail it (two of them only after the test was tightened: the switch-off
  check ran with nothing banked, and the album check compared albums of
  different page counts, so the scrollbar alone told them apart). Two do not,
  and cannot: the farm's own CLOSE branch, which upstream's CLOSE does the same
  as, and the visitor poll's switch, which `forkVisitor()` checks again.
  `steps_test` now expects the album's pages after the steps page. 288 board
  shots are byte-identical to `main` (`game` is timing-flaky like
  `battleanim`: it differed once and matched on four repeats).
- **Checked on the S24** (2026-11-18): the save loaded intact; the tabs, the
  farm at night with the four party members, a tap naming WAILMER LV.34 TIDY,
  and the empty album after the steps page. That check moved the herd: with
  few creatures the rows sat at the field's far edges, so the row spacing is
  capped and the herd centred. The visitor was not seen there (the creature
  was asleep, which is the rule); it is checked by the tests and in the
  emulator.

**0.5 is won't-do** (owner, 2026-11-17). The phone's `millis()` pauses while
the app is in the background (`emuClockSkipGap()`), so it would need ~50 days
of the app open on screen to wrap: the bug is real on the board and close to
impossible here. It touches about 38 comparisons in upstream's code, and this
fork sends no upstream PRs.

These ideas come from three other forks, compared against v4.1.1:

- **ShadowEnemyx** (`tamapoke-expanded-update`): evolution rules, steps,
  expeditions, collector frames, the bond hint, the help screen, the clean and
  type minigames.
- **Mu-Muu** (`mods`): natures, sickness, more ways to touch the pet, the pet
  acting on its own, the world (weather, seasons, holidays), the farm,
  accessories.
- **socquique upstream, as carried by Mu-Muu**: the `millis()` wraparound fix.

Those forks are only a source of ideas. They branched from a June version of
the game, so none of their code applies cleanly here, and each feature below is
written fresh against our code.

We go through these one at a time. For each one, settle its **open questions**
first, then build it, then tick it off in the checklist at the end.

---

## Phase 0: the groundwork every feature depends on

This fork keeps merging upstream, and every feature below adds game state,
strings and drawing to code that upstream also edits. If we skip this, the
first feature works and the next upstream merge corrupts saves. So this comes
first.

### 0.1 A switch for fork-only features

The board shots must stay byte-identical (`PHONE_LAYOUT_PLAN.md`, Phase 1).
Natures on the card, a hat on the creature, rain on the main screen: every one
of these changes a board shot.

- Add one predicate, `forkFeatures()`. It is true in the Android build and
  false in the board build and in `--shot`. Every new feature asks this one
  function. Write it as "what is allowed", never as a list of exclusions
  (CLAUDE.md, trap 1).
- The game logic behind a feature (sickness ticking, rolling a nature) is
  gated by the same switch. A board save is then never touched by fork state.
- For the tests, add a way to turn it on in the host suites, so fork features
  get tested at all.

### 0.2 A save record of our own: `frkA` / `frkB`

**This is the most important rule in this plan.** The pet and player
checkpoints are append-only. Upstream appends fields to them. If we append
ours too, then the first upstream release that also appends puts its field at
the same byte offset as ours, and every phone save is silently reread as
something else. That is CLAUDE.md § "wearing an INDEX" happening across two
repositories.

So everything this fork stores goes in **its own CRC-checked,
two-slot record**, built like `petA`/`petB`:

- Its own magic number and its own generation counter. Never share a counter
  with another record (see § "The save is CHECKPOINTED").
- The body is append-only too. Natures, sickness, step totals, expedition
  state, accessories and fork inventory all go in it.
- Add it to `SAVE_FIELDS`, so `EXPORT` and the Android share-sheet backup
  include it. `save_test` will fail if we forget.
- Anything sized by a count that grows (the farm list, accessory flags) puts
  its dimensions in the header, like the player record does.
- Fork items (medicine, potions, expedition finds) go in a fork inventory
  inside this record. They do **not** get appended to upstream's `items.h`,
  for the same reason: inventory indices are stored raw.

### 0.3 A string table of our own

`STRINGS` is positional, so if we append to it and upstream appends to it, we
get a merge conflict at best and a shifted language row at worst. Keep a
separate `fork_i18n.h/.cpp` table with all six languages, ASCII only, plus an
`i18n_test`-style check for blank and non-ASCII entries.

### 0.4 A bridge for phone sensors and data

There is already a `phone_*.cpp` layer (`phone_clock`, `phone_save`,
`phone_remind`). Add one host interface with a stub, so the emulator and the
tests can feed in fake values. **Built as `gForkHost` in `fork.h`:** function
pointers the host sets before `setup()`, like `gHostHaptic`, each read through
a wrapper that gives the "nothing" answer when unset. Only `takeSteps` exists
so far; each phase adds its own hook:

- `phoneSteps()`: steps since the last time we read them (Phase 4).
- `phoneAccel()`: tilt, face-down and shake events (Phase 6).
- `phoneCharging()`: whether the phone is plugged in (Phase 7).
- `phoneWeather()`: the current weather, fetched on the Java side (Phase 8).

On the board each of these returns "nothing", so the board code paths keep
working and simply never see the feature.

### 0.5 The fixes

- **The `millis()` wraparound.** About 13 `*Until` deadlines are compared
  with `millis() < deadline` (for example `TamaPoke.ino:3591` and `:4789`).
  Those comparisons give the wrong answer after about 49.7 days of uptime.
  Add one `timeLeft()` helper that does the subtraction unsigned and reads the
  result as signed, and use it for every deadline. This is a real board bug,
  so **send it upstream as a PR** rather than keeping it fork-only. That way it
  never becomes a merge conflict.
- Do one sweep while we are there for the same pattern in `wild.cpp` and in
  the minigame timers.

**Open questions:** none. This phase is mechanical. The one decision is
whether 0.5 goes upstream first. I recommend that it does.

---

## Phase 1: natures (first, because evolution depends on it)

A nature is rolled at hatch. With it, one need drains a little faster and its
favourite kind of care gives more (easygoing, glutton, playful, tidy, lazy).

- ~~Store it in the fork record~~ **Derived instead** (decided 2026-11-12):
  a scrambled hash of the IVs and the shiny flag, independent of upstream's
  `personality()`, which reads the IVs rather than hashing them. Existing and
  banked creatures have one with no migration, and no slot map is kept.
- Shown on the card when the switch is on.
- **It also decides evolution branches** (Phase 2): the ones the games leave
  to chance, trade or gender. So each nature needs to map to one form per such
  branch, and the evolution display has to be able to name it.
- **Balance doc:** CLAUDE.md requires README numbers to follow the code. Fork
  balance goes in a "Fork rules" section of `android/README.md`, so that
  upstream's manual stays upstream's.

**Decided (2026-11-12):** Mu-Muu's five, one per need (EASYGOING, GLUTTON,
PLAYFUL, TIDY, LAZY), so the name says what it does. **Care only**: no battle
stat, so gym balance and LAN fights against a board stay upstream's.

---

## Phase 2: evolution rules

This is pure game logic, testable on the host with no UI to design, and it
fixes real gaps in the game. Tyrogue only ever becomes one of its three forms.
Gloom, Poliwhirl and Slowpoke can never reach Bellossom, Politoed or Slowking.
Wurmple has only one path. Pichu, Cleffa, Igglybuff and Togepi evolve by level
rather than by friendship. Espeon and Umbreon are random, not tied to day and
night.

- Add a generated `evo_rules.h` holding `{from, to, minLevel, condition}`.
  Conditions: level, bond, bond by day, bond by night, Atk > Def,
  Def > Atk, Atk = Def. Later conditions could include holding an item or the
  creature's gender. Generate the table from PokeAPI evolution chains in
  `tools/`. Do not type it by hand, and keep it in a separate file from
  `dex.h`, so upstream regenerating `dex.h` never conflicts with it.
- Everything goes through **one** function, which replaces how
  `eeveeOptions()` and `evolvesTo` are read today. `canEvolveNow()`, the card's
  progress display and `lineHasUnregistered()` all ask it. This is CLAUDE.md
  trap 1, the evolution threshold row, which has already happened once.
- Keep the existing filters: only species from installed regions with art
  (`regionAvailable()`, `speciesHasArt()`), because evolution cannot be undone.
- The rarity derivation has to see every branch target as reachable only by
  evolving, or Bellossom will start hatching from eggs. That is exactly how
  Espeon hatched for six generations.
- **Bond hint.** When bond is the only thing blocking an evolution, the card
  says so. This is the "a disabled control must say why" lesson from
  BRING BACK.
- **How to evolve is always shown.** Each possible next form appears on the
  card with what it needs, in words ("LV 20", "BOND 70 BY DAY", "ATK > DEF",
  "TIDY NATURE"), and what is already met is marked. A player should never
  have to look an evolution up.
- **Undiscovered forms are black silhouettes.** A next form the player has not
  registered draws as its outline, so the creature's future is visible but not
  given away. The Pokedex already draws unregistered species this way
  (`drawThumb(..., sil = true)`), so this reuses that. Like the Pokedex, a
  silhouette plays no cry and shows no name ("???").
- Tests: an `evorules_test` that walks every rule and checks each condition
  both ways, and negative-checks that a branch target is unhatchable. The
  existing `eevee_test` gets pointed at the shared function.

**Decided (2026-11-12):**

1. **A rule decides, never the player.** Tamagotchi-style: how the creature
   was raised, and what it is like, pick the branch, by the rule below.
2. **Stone evolutions wait for Phase 5** (expeditions), where stones become
   items. Until then they stay level-based, as they are now. So branches whose
   only route in the games is a stone (Gloom to Bellossom, Poliwhirl to
   Politoed, the Eevee stones) move to Phase 5 with them.

**The rule (agreed 2026-11-12).** Use the game's own condition wherever it
maps onto something we already track, and how the creature was raised only
where the games use something we do not have:

- Atk vs Def training decides Tyrogue, as in the games.
- Bond plus the time of day decides Espeon and Umbreon; bond decides the baby
  forms (Pichu, Cleffa, Igglybuff, Togepi).
- The nature (Phase 1) decides the branches the games leave to chance, trade
  or gender (Wurmple, Kirlia, Snorunt, Slowpoke).

Two constraints come with it:

- **Not care mistakes on their own.** They already delay every evolution by a
  level, so using them to pick the branch too would punish the same thing
  twice.
- **Neither branch is the "bad" one.** A player who wants Dustox can aim for it
  on purpose, and the card says which form the creature is heading towards,
  next to the bond hint.

**Decided: the nature decides** the branches the games leave to chance,
trade or gender. The games decide Wurmple by a hidden personality value, and a
nature is a visible one, so the player can see the branch coming and aim for
it. This is why natures are Phase 1 and this is Phase 2.

3. Is this upstreamable? It has nothing to do with phones. If upstream would
   take it, putting it in their code removes the merge cost entirely.

---

## Phase 3: sickness and medicine

Two or more poops, being overweight, or a need at 20 or below can make the
creature sick. While sick, the needs drain faster and every two hours counts
as a care mistake. Medicine cures it.

- Our existing `poops` count and care mistakes are the inputs.
- Medicine is a fork item (0.2). Where it comes from is decided in Phase 5.
- **Watch the offline catch-up.** CLAUDE.md § "The three drain paths" shows
  how losing a creature while the board was switched off was once reported as
  a bug. Sickness must respect the same floors, and must not stack mistakes
  over a two-week `deltaMinutes` jump. Add a `sleep_test`-style ten-hour check.
- Self-medication (Mu-Muu's: after ten minutes awake and sick, it takes
  medicine from the bag by itself) belongs to Phase 7.
- Show it as a sick icon or tint on the main screen, and a status on the card.

**Open questions:** Should being sick affect battles (lower stats, or refuse to
fight)? How punishing should it be? It needs to fit with the care reminder
notification. Perhaps "it's sick" should be its own reminder.

---

## Phase 4: the step counter

**Yes, steps taken while the game is closed do count.** Android's
`TYPE_STEP_COUNTER` sensor is kept by the phone's own low-power sensor hub. It
counts every step since the phone booted, whether or not any app is running.
On every launch or resume we read it, subtract the value we stored last time,
and that difference is what the player walked while the game was closed. The
S24 has this sensor.

There are three catches, and the design deals with each one:

1. **It resets to zero when the phone reboots.** If the reading is lower than
   our stored value, there was a reboot, so the reading itself is the number of
   steps since that reboot. A few steps from before the reboot are lost.
2. **It has no timestamps.** If the game was not opened yesterday, yesterday's
   steps arrive today, and a daily milestone could be missed or doubled. The
   fix: `Reminder.java` already wakes the app with an exact alarm and survives a
   reboot (`BOOT_COMPLETED`). Add a midnight alarm that only reads the counter
   and stores the value, so each day gets its own count with no app open.
3. **Permission.** On Android 10 and later this needs
   `ACTIVITY_RECOGNITION`, a runtime permission. Ask for it the first time the
   steps card opens, not at launch. If the player says no, the feature is just
   off.

The alternative is **Health Connect**, which gives exact per-day history. It is
heavier: another dependency, its own permission screen, and a separate app on
older phones. I recommend starting with the sensor, and only moving to Health
Connect if the midnight split turns out not to be good enough.

Game side (from Shadow's fork):

- Daily milestones at 500 / 2,000 / 5,000 steps give an item each.
- Lifetime trail ranks at 10k / 50k / 100k / 250k.
- Walking slowly raises joy and bond, within daily caps (upstream already has
  `bondToday`, so we go through `addBond()`).
- A small, capped bonus to wild shiny and catch odds.
- A steps card, plus a small counter on the main screen.

**Open questions:** Do steps also give training points? Shadow's fork does
not. What should the caps be, so that walking a lot cannot replace caring for
the creature?

---

## Phase 5: expeditions, and where items come from

Send the creature out for 15, 30 or 60 minutes. It costs energy, and it comes
back with an item. Sites (coast, volcano, forest, and later one per region)
change which items it finds. Walking a set number of steps while it is away
earns a bonus item. That is the link to Phase 4.

- While it is away, the main screen shows an empty spot with a countdown.
  Needs are paused or drain slowly. Battles and minigames are not available.
- Store the end time as an epoch, so it finishes while the app is closed, and
  use the existing offline-progress clock. The care reminder notification can
  also say "it's back".
- **This is where fork items come from:** medicine (Phase 3), potions, snacks,
  training items, possibly evolution stones (Phase 2, decision 2), and
  possibly accessories (Phase 10).
- Interaction with Explore and wild battles: an expedition is passive and
  Explore is active. Both should exist.

**Open questions:** Can the creature be sent out while sick or asleep? Can it
come back hurt? Should there be one site per installed region?

---

## Phase 6: more ways to touch the pet

- On screen: stroke it back and forth, tickle it (too much and it gets
  annoyed), double-tap to make it hop, poke its head or belly, drag it to
  make it walk, draw a circle to make it spin.
- With motion (`phoneAccel()`): tilt and it walks downhill, lay the phone
  face-down and it sleeps (this uses the same `screenSleep()` path as the
  board's power button), shake hard and it gets dizzy.
- **Conflicts with existing gestures:** the tile axis owns horizontal swipes,
  and holding for three seconds opens the release dialog. A stroke must not
  page the screen, and a drag must not count as the hold. Gestures are only
  read inside `inPetZone`, and only on `SCR_MAIN` (CLAUDE.md, the long-press
  lesson). Add the whole gesture table to a test, the way `swipe_test` does.
- Each gesture gives a small capped amount of joy or bond, so they are fun
  and cannot be farmed.

**Settled (2026-11-17):** stroke, tickle, double tap, poke and face down;
shake is rough handling and costs a little joy. Drag-to-walk, the circle and
tilt are left out.

---

## Phase 7: the pet acting on its own

- **A behaviour planner:** wander, sit, nap, look at the player, chase a poop,
  react to the weather. It picks from the creature's needs and its nature
  (Phase 1).
- **Speech bubbles:** short ASCII lines, all six languages, from the fork
  string table. For example "I'm hungry", "Yesterday you walked 5k steps!",
  "Brr, it's snowing".
- **Self-care while charging:** while plugged in and not touched for ten
  minutes, it raises any need below 40 back up to 50. This does not count as
  the player's care: no bond and no streak.
- Self-medication from Phase 3 goes here.

**Settled (2026-11-18):**

- **Bubbles: sometimes.** Only with a reason (a need low, sick, a mess,
  waking up, yesterday's walk, having looked after itself), never idle
  chatter. At most one every few minutes on the pet screen, and the same
  reason not again for a while.
- **Self-care while charging: on by default**, with a switch in settings to
  turn it off.
- **Self-medication uses the player's medicine.** After ten awake sick
  minutes it takes one from the bag, with a banner. No medicine, no cure.

**How it is built:**

- On the phone the screen timing out pauses the app, so self-care cannot
  rely on the app staying open. It runs in two places: live (the app open,
  charging, untouched for ten minutes) and on coming back (charging when the
  app went to the background AND when it returns, at least ten minutes
  later). The record keeps when the app left on charge, so a process Android
  killed meanwhile still counts.
- The planner adds to upstream's `behNext()` rather than replacing it, and
  only with the switch on: a nap when tired (more for a LAZY creature), a hop
  when happy (more for a PLAYFUL one), a visit to the mess when there are
  poops (more for a TIDY one). Otherwise upstream's own choice runs. Sit
  stays out for upstream's reason (the sprite faces away); looking at the
  player is upstream's own "face forward". The weather waits for Phase 8.

---

## Phase 8: the world

Split in two, because the first half needs no network.

**8a. From the clock only:**

- Sunrise and sunset from the date and a coarse latitude, replacing the fixed
  night window for drawing (not for sleep rules unless we decide so).
- Seasons, and holidays: New Year, Valentine's, Easter, Halloween,
  Christmas and so on. Shown as decorations and greetings in bubbles.
- A birthday: the player's, and the creature's hatch day.
- A standby clock: plugged in and untouched for 90 seconds, show a large
  clock with the date and the weather. On the phone this is closer to an
  always-on mode. Check how it interacts with Android's screen timeout.

**8b. Weather:**

- Open-Meteo needs no API key. Fetch on the Java side with the `INTERNET`
  permission, every hour or two, and pass the result through
  `phoneWeather()`. Rain, snow, fog and storms are drawn on screen, and each
  type nudges moods.
- **Location privacy.** Use coarse location, or a city typed in once, and
  store only rounded coordinates. Nothing else is sent. This is the first time
  the app talks to the network, so `android/README.md` should say so plainly.
- **Region backgrounds** (Mu-Muu's are AI-generated). Ours are the 12 battle
  backgrounds, whose license is already unresolved (CLAUDE.md, TODO D). Decide
  where new art comes from before adding any.

**Open questions:** Does weather affect gameplay (wild spawns by type, as in
the games; moods), or is it only visual? Do holidays come with gifts?

---

## Phase 9: the farm

Mu-Muu's farm keeps retired companions, which keep growing at a tenth of the
speed, and sometimes walk across the main screen.

Our game already has the party (6) and the box (36), and banked creatures are
frozen on purpose: CLAUDE.md § "B3" makes freezing part of the design. So the
farm needs a role that does not overlap with those:

- **Option A:** the farm is the box, made visible. Box creatures do not grow,
  but one or two wander the main screen or the farm screen. This is cheap.
- **Option B:** a third place where creatures slowly level (one-tenth speed,
  never evolving). This changes the "banked creatures are frozen" balance that
  the gym ladder depends on.
- **The album** (one page per creature that has left, with dates, level,
  medals and how it ended) fits either option. Farewells are currently
  forgotten once the party is full, so this records something nothing else
  does.

**Settled (2026-11-18):** Option A, and the album in this phase.

---

## Phase 10: accessories

Bow, flower, cap, witch hat, top hat, crown, worn by the live creature.

- **The hard part is where to draw them.** PMD sprites differ per species and
  per animation frame, so "the top of the head" changes every frame. Options:
  the topmost opaque pixel of each frame (automatic, sometimes wrong), or a
  hand-made table of anchor points per species (accurate, 1025 entries). A
  first version could draw on the tile the creature stands on, or on the
  card, rather than on its head.
- **Art:** these need small original sprites. Do not copy Mu-Muu's without
  checking the license.
- **How you get them:** expedition finds (Phase 5), step milestones (Phase 4),
  holidays (Phase 8), collector ranks (Phase 11). This replaces Mu-Muu's
  points-and-shop, which we are not taking.

**Open questions:** On the creature, or on the card only? Do they carry over
to a banked creature?

---

## Phase 11: smaller ideas

Each of these is self-contained. Do them between bigger phases.

- **Collector ranks and card frames:** frames unlocked by dex count or caught
  count, chosen on the player card.
- **Help screen:** one page per tile on the tile axis, explaining its gestures.
  The phone layout has room for it.
- **Clean minigame:** wipe the dirt off the creature, which trains or gives
  hygiene.
- **Type minigame:** pick the move that is super effective against the shown
  type. It teaches the type chart, which matters in hard mode.

---

## Phase 12: adventures (a raid run, Dynamax Adventures-shaped)

**Planned (2026-11-20).** A run, not a fight. You get a loan creature and go
through five stages. Each stage is a battle, and you may catch what you beat and
swap it for the creature you are holding. Stage 5 is the boss. At the end you
keep ONE creature you caught during the run.

Reached from the Explore tile (a WILD | ADVENTURE chooser), not a new tile.
§ "Gesture map" is settled, and Explore already means "go out and meet things".
Fork-only behind `forkFeatures()`. The board never sees it.

### Settled with the owner

- **Three difficulties, fixed levels 30 / 50 / 70.** Not the pet's level: the
  loan, every foe and the creature you keep all use the run's level, and the
  kept creature is banked frozen at that level like any party member.
- **Pools by difficulty.** Easy: commons and rares, with Megas as bosses.
  Normal: adds G-Max and the sub-legendaries (birds, beasts, Regis). Hard:
  box legendaries, mythicals and fusions, plus hard-mode AI.
- **Shiny odds are boosted in hard only.**
- **Mega and G-Max are a battle STATE, not a species.** Bosses and loans can
  transform. A kept Mega Charizard X comes home as CHARIZARD carrying a
  "Mega X" flag that lets it transform in later fights. Fusions (Kyurem-B/W,
  Necrozma DM/DW, Calyrex riders) ARE kept as themselves, because they are
  separate creatures in the games too.
- **Dynamax is a boss treatment.** Drawn giant, extra HP, and shields that
  must be broken before damage lands. There is no player-side mechanic.
- **Hearts.** The team has four hearts per run. A faint costs a heart, not
  the run. HP carries between stages, which is a reason to swap the loan for a
  healthier catch.
- **One free run a day, coins after that.** The anti-farming rule, the same
  concern as egg blessing.
- **1-4 players, with AI companions filling the empty seats.**

### Build order (each step is usable on its own)

**12a. Forms: data and art, before any gameplay.** Nothing in the project
knows about forms. `PartyMon` holds only `dex` and `shiny`, `pack_pmd.py`
fetches only the base sprite and `/0000/0001`, and `DexEntry` has one set of
stats and types per species.
- A form table generated from PokeAPI (`charizard-mega-x` and the rest): stats,
  types, the base species, and the kind (mega, gmax, fusion, regional).
- `check_sprites.py` extended to forms: SpriteCollab stores them as form
  subdirectories (the PYROAR case). **A form with no art never spawns**, the
  same rule as `noart.h`. Run this FIRST. Its count decides how big each pool
  can be.
- Sprites packed as their own files beside `pNNNN.bin`, in a fork pack.
- **Measured 2026-11-20** (one HEAD per form for `AnimData.xml`. The
  `tracker.json` index under-reports: 11 forms it marks missing do exist):

  | kind | forms upstream | with art | with shiny art |
  |---|---|---|---|
  | Mega | 105 | 45 (41 species) | 32 |
  | Gigantamax / Eternamax | 34 | **3** | 0 |
  | Primal | 3 | 3 (Kyogre, Groudon, Dialga) | 2 |
  | Fusion | 7 | Kyurem-B, Kyurem-W (plus Necrozma-Ultra) | all |

  What that means:
  - **G-Max art effectively does not exist** (only Charizard, Alcremie and
    Eternamax). Dynamax and G-Max bosses are therefore drawn as the BASE
    sprite scaled up and tinted, not as form art. That suits "Dynamax is a
    boss treatment" anyway.
  - **Megas are uneven per species.** Charizard has X but not Y, and Mewtwo
    has Y but not X. A species offers only the Megas that have art.
  - **Fusions are thin.** Necrozma Dusk Mane and Dawn Wings and both Calyrex
    riders have no art, so hard mode's fusion pool is the two Kyurem plus
    Ultra Necrozma until upstream draws more.
  - A shiny form with no shiny art falls back to the plain form, never to a
    dex number.
- ~~On the creature: a form id appended to `PartyMon`~~ **wrong for this
  fork**, and not done: the fork keeps NOTHING on `PartyMon`, because upstream
  moves creatures between the pet, the party, the box and catches in too many
  places to hook (fork.h section 3). A kept form lives in its own record
  instead, looked up by the creature's IVs, shiny bit and species.
- Mythicals need their own mark. `R_LEGENDARIO` covers both today (MEW is
  tagged legendary).

**12a built (2026-11-20).**
- `fork_forms.h`, generated by `tools/gen_forms.py`: 131 forms with stats and
  typing, 49 with art. Form ids are persisted, so the table is append-only
  (the generator reads the committed order back). Art-less rows keep their id
  but are never offered: `forkFormUsable()` is the one answer.
- Missing from PokeAPI, so not in the table: Dialga-Primal (a spin-off form),
  Meowstic-Mega and Tatsugiri-Mega (newer than PokeAPI's data), and the
  Toxtricity and Urshifu G-Max (named differently there, and art-less anyway).
  Of these, only Mega Tatsugiri has art.
- Sprites: `pack_pmd.py forms` -> `tools/pack_forms.py` ->
  `android/packs/sprites-forms.pak` (26 MB, 93 files), shipped in every APK.
  `PmdMon::loadForm()` loads one; on the board it returns false.
  `pack_bundle.py` now skips form files, which its digit-reading `dex_of()`
  would have put into the Unova pack.
- Kept forms: `frmA`/`frmB` (`fork_adv.h`), 64 creatures, oldest dropped
  when full, in `SAVE_FIELDS`.
- Mythicals: `MYTHICAL_DEX`, 23 of them. MANAPHY is `R_EVO` in dex.h
  (PHIONE evolves into it), so asking the legendary tier alone missed it.
- Tests: `forms_test`, `formsprite_test`, `adv_test`. Every guard was
  negative-checked.
- **For 12f** (it was noted here for 12c, which did not do it): drawing a
  kept FUSION as itself on the party, box and card. A kept Mega needs nothing
  outside battle, since it looks like its species there.

**12b. A team-vs-boss battle with four seats.** `battle.cpp` resolves 1v1.
A raid is up to four of yours against one boss, which takes several actions a
turn and has shields. Build it with **four seats from the start, each seat
filled by a chooser**: a local player, an AI, or later a remote player. Then
AI companions are just the solo game, and multiplayer only swaps a seat's
chooser. Keep it pure and roll-injectable like `wild.cpp`, so tests can pin
the shield and heart rules at their boundaries.

**12b built (2026-11-20).** Engine and tests only; no screen yet (that is 12c).
- `fork_adv.h/.cpp`: `RaidBattle` with four `RaidSeat`s (`SEAT_LOCAL`,
  `SEAT_AI`, `SEAT_REMOTE`, or empty) against a `RaidBoss`. A local or remote
  seat's action is set with `raidSetAction()`, an AI seat's by `raidFillAI()`,
  and `raidResolve()` runs one turn into a `RaidTurn` of narratable events.
  Raid rolls (the boss's target, speed ties) come from a `RaidRoll` passed in.
  Damage, accuracy and ailments still use `battleAct()`'s own dice.
- **The rules, as built.** The foe's HP is multiplied (stage and stage-5
  multipliers per difficulty). It has barriers at fixed HP shares (easy 50%,
  normal 66/33%, hard 75/50/25%). A hit that crosses a barrier stops ON it,
  so no damage carries through and no knockout either, and the shields go up.
  While shields stand nothing lands on the boss: no damage, ailment, stage or
  burn chip. Each damaging hit breaks one shield, a super-effective hit two.
  Breaking the last staggers it (DEF and SP.DEF -2). Its first action is in
  speed order and the rest come after everyone. Every faint costs a heart,
  and the faint that spends the last loses. A boss knockout wins even on that
  same action. A fainted seat sits out the turn and returns at 50% HP with its
  ailments and stages cleared. The numbers live in `RAID_RULES` and are 12d's
  to tune.
- **A form counts in a fight.** `Combatant` gained an optional typing
  (`typed`, `type1`, `type2`). `battle.cpp` reads every typing through
  `battleEffVs()` / `battleStab()`, so damage, STAB, immunity and the AI all
  see Mega Charizard X as FIRE/DRAGON. Untyped is upstream's lookup exactly
  (checked over every type). `forkApplyForm()` moves each stat by (form base -
  species base) and keeps damage already taken. That is exact only because
  the stat formula is additive in the base, so `raid_test` pins that
  additivity through the real `Party` accessors. A seat may BECOME a state form
  (Mega, G-Max) once a battle, before anyone acts. Fusions are never a state.
- `battle.cpp` also exposes `battlePriority()` and `battleSpeed()`, the two
  halves of `battleMovesFirst()`, so the raid's five-actor order uses upstream's
  rule rather than a copy of it.
- `raid_test` (the 71st suite, 78 checks). The boundaries use SEISMIC TOSS,
  which hits for exactly the user's level, so a hit can be landed one HP short
  of a barrier, exactly on it, and past it. 33 mutations of the engine and the
  typing hooks: 32 fail it. The one that does not changes the faint count from
  "+1" to "= 1", which cannot be told apart because one action can knock out
  at most one seat. The first run also showed that dropping STAB from
  `battleDamage()` survived, so a damage-level STAB check was added.
- **Measured, not tuned** (four AI seats, 60 fights each, four fresh hearts):
  easy won 59, normal 58, hard 21. A first hard setting (two actions on every
  stage) won 6 of 60, so stage foes now act once there too. Hard is still the
  one to tune in 12d, where the hearts are shared over five stages.
- 71 host suites pass. 71 of 72 board shots are byte-identical to HEAD.
  `battleanim` is the known flaky one: HEAD alone produces both of its frames.

**12c. The run.** Five stages, a path choice between stages (two doors that
show the type they lead to), hearts, catch-and-swap, a boss shown as a
silhouette from stage 1, and the keep screen at the end. The kept creature goes
party -> box through the existing fall-through. **A run in progress is saved**
in the fork's own record, because Android kills apps without warning, and it
resumes on the next launch.

**Settled for 12c (2026-11-20):** the three other seats are AI companions
from the start, each with its own random loan, using 12b's `raidFillAI`. 12d
adds their team rules and measures the help. Stage foes and loans are FULLY
EVOLVED non-legendaries (no Caterpie at level 70). Spending the last heart
still lets you keep one creature caught so far, but never the boss. Delivered
in two commits: the engine and record, then the screens.

Defaults chosen with no owner call (change freely, they are one table each):
catching is guaranteed after a stage win (the choice is whether to SWAP); an
extra run costs 50 coins; a win pays 20 / 40 / 80 coins and a GREAT BALL /
ULTRA BALL / two ULTRA BALLs; a loss pays a fifth of that per stage beaten;
keeping nothing doubles the coins; hard shinies are 1 in 100; every stage 1-4
has two doors, rerolled to differ in type where they can.

**12c part 1 built (2026-11-20): the run engine and its record.** No screens
yet.
- `fork_adv.h` section 12c: `gAdv`, the phases LOAN -> DOOR -> FIGHT ->
  CATCH (x4) -> boss FIGHT -> KEEP, `advStart / advPickLoan / advPickDoor /
  advTurn / advCatch / advForfeit / advReward / advFinish`. Rolls are injected
  as in 12b.
- **Pools.** `advStageOk()` and `advBossOk(diff, dex, form)` are the only
  answers, and both start from "has art and its region is installed". Box
  legendaries are a list (`BOX_LEGENDS`), because nothing in the data separates
  them from sub-legendaries: Regigigas has a box legendary's stat total, and
  SOLGALEO and LUNALA are `R_EVO` (they evolve from COSMOEM), so the legendary
  tier alone misses them. That last one also decides the "three perfect IVs"
  rule for a legendary boss.
- **Between stages** a seat keeps its HP and nothing else. A transformed seat
  keeps its HP SHARE back on the species' maximum. A seat that ended a fight
  fainted starts the next at the revive share. A swapped-in catch is at full
  HP. A seat's Mega is per battle, not per run.
- **The keep order** is: store the creature, pay, THEN `advFinish()`. A cut in
  between can repeat a payout but never lose the creature.
- **One free run a day**, the day held in the run record so it outlives the
  run. Giving up at the loan still spends it, or a player could reroll runs
  until the boss suited. An unreadable record drops the run but keeps the day.
- **The record**, `advA` / `advB`, in `SAVE_FIELDS`, is written after every
  step and every TURN, battle state included (HP, stages, ailments, shields,
  barriers), so a killed app resumes mid-fight. The two entry sizes travel in
  the body (prefix rule); the counts are the game's shape and a change to one
  is a magic bump. `RaidBattle::hearts` is a pointer and is re-bound on every
  load and every turn.
- `advrun_test` (the 72nd suite). It compares runs FIELD BY FIELD, not through
  the encoder, since a field the encoder forgot would vanish from both sides
  of an encoded comparison. A resumed fight is checked to play its next turn
  exactly as the original would have.

**12c part 2 built (2026-11-20): the screens.** Explore's title has three
tabs now, EXPLORE | EXPEDITION | ADVENTURE. The third shows the run's phase:
idle (difficulty, the free run or its price, START), the three loans, two
doors in their types' colours, the raid fight (the boss's PMD sprite in its
form with a Dynamax glow, its HP with the barriers marked, the shields as
diamonds, the four seats, your move grid with a TRANSFORM toggle when your
creature has a Mega, and the turn narrated a page at a time with upstream's
battle wording), the catch (SWAP / KEEP MINE), and the keep (one catch or
NOTHING for double coins, with the reward shown before DONE). GIVE UP asks
twice, and so does a run that costs coins.
- **The keep banks through the wild capture's own path.** `wildStoreCaught()`
  and `wildToPartyMon()` are now thin callers of `storeCaughtMon()` and
  `caughtPartyMon()`, so party -> box -> chooser and the caught-record rules
  are one function each. A boss in a form records it with `forkKeepForm()`.
- **Found on the S24, not by any test:**
  - Tapping a move fast carried over onto the catch screen and pressed SWAP.
    Likewise, DONE and START sit in the same place, so a double tap on DONE
    started (and paid for) another run. Every adventure tap now waits 0.8 s
    after a fight or a run ends, and `advscreen_test` fails without that.
  - An EASY run drew MEGA RAYQUAZA at level 30, which took all four hearts.
    Megas now go by whose Mega they are. Easy gets ordinary species' Megas.
    Normal adds the sub-legendaries' (LATIAS, LATIOS). The box legendaries'
    and mythicals' (MEWTWO, RAYQUAZA, DIANCIE) are hard's.
  - A companion chose THUNDERSHOCK against a ground foe. That is
    `aiChooseMove`'s choice, left for 12d's team rules.
- **Found by the tests:** narration left over from a fight's last turn kept
  GIVE UP from working for the rest of the run. Narration now clears whenever
  a fight ends. Also, `exped_test` aimed at the old two-tab title by typing
  x=300; it asks `uiExploreTabRect()` now.
- `advscreen_test` (the 73rd suite) taps a whole run on the phone's canvas
  through `uiAdvRect()`. It checks that every phase flushes, that the keep
  banks the boss with its IVs and form and pays exactly once, the price and
  its double tap, GIVE UP, and a relaunch mid-fight. `--fork` shots:
  `adventure`, `advloan`, `advdoor`, `advfight`, `advcatch`, `advstage5`,
  `advkeep`.
- Checked on the S24 by finger: a whole easy run from START to the keep,
  including a reinstall mid-run, which resumed on the same screen with every
  HP as it was. The kept creature arrived in the party at Lv.30.
- **Not done:** the kept-forms record is WRITTEN by the keep but READ by
  nothing outside adventures (only fork_adv.cpp calls `forkKeptForm()`), so a
  kept form has no effect yet: a kept Kyurem-Black looks and fights like plain
  Kyurem. That is step 12f. Separately, bosses with small sprites (ROTOM) draw
  small, because the fit is the PMD idle frame's.

**12f. Kept forms at home.** What the keep records, made real outside the run.
Comes straight after 12c and can go before 12d. Two parts, each usable alone:

1. **Drawing.** A kept fusion (Kyurem-Black, Kyurem-White, Ultra Necrozma: the
   only ones with art) shows as itself on the party, the box, the party sheet
   and the card, and anywhere else a party creature's picture is drawn.
   - The 49 drawable forms need THUMBNAILS: those screens draw `thumbs.bin`'s
     per-species thumbnails, not PMD sprites. Generate form thumbnails from the
     form sprites (`pack_forms.py`, beside the form `.bin`s, in the forms pack)
     rather than adding rows to `thumbs.bin`, whose index is the dex number
     and which upstream regenerates.
   - ONE function answers "which picture for this creature", taking the kept
     form into account, and every caller asks it (CLAUDE.md trap 1: the
     sprite-loading rule already had a twin that disagreed once). The card's
     PMD sprite uses `loadForm()` for a fusion.
   - A kept MEGA still draws as its species: it is only a Mega mid-fight.
   - Test: pixels, as `sprite_test` and `formsprite_test` do. A kept
     Kyurem-Black's party thumbnail must not equal plain Kyurem's, and a
     creature with no kept form must draw exactly as before.
2. **Fighting.** In gym and wild fights:
   - A kept fusion fights AS the fusion (stats and typing, `forkApplyForm()`)
     wherever the party or the live pet become combatants. Find the one place
     those are built (`buildSquad()` and the squad picker) and apply it there,
     not per screen.
   - A creature with a kept Mega or G-Max gets a once-per-battle TRANSFORM in
     the battle menu, as in the raid fight. The transform happens before
     anyone acts that turn, and lasts the battle only, never written back.
   - **Never in a LAN fight.** A board cannot know forms, and the link sends
     species, so both sides would narrate different fights. A link squad goes
     out as plain species, TRANSFORM is hidden, and `lan_test` should prove it.
   - **The board path stays untouched.** All of it asks `forkFeatures()`, so
     the board's battle screen and every board shot stay byte-identical.
     Upstream changes the battle screen often, so keep the hooks small.
   - Test through the real squad builder (trap 1, "test the CALLER"): a kept
     Kyurem-Black in the party enters a gym fight as DRAGON/ICE with Black's
     stats; a kept Mega Charizard X transforms once and only once, and is
     plain CHARIZARD again in the next fight.

~~Open question for 12f: should a TRANSFORM in a gym fight be allowed in HARD
mode?~~ **Settled (owner, 2026-11-20): no.** Easy gym fights and wild fights
(hard wild included) only. Hard mode caps levels and team size so that a raised
creature cannot just overpower the leader, and a Mega's stat jump works against
that.

**12f part 1 built (2026-11-20): drawing.**
- `forkDrawnForm(key, dex)` (fork_adv.h) is the rule: the kept form when it is
  a creature of its own, else 0, and always 0 with `forkFeatures()` off. **Only
  the two Kyurem fusions draw as themselves today.** Ultra Necrozma is
  `FK_ULTRA`, which `forkFormIsState()` treats as a battle state like a Mega, so
  a kept one draws as NECROZMA (this section's part 1 list above named it a
  fusion; the code's rule won). The other fusions have no art.
- `creatureThumb(dex, key)` in the sketch is the one picture function, with
  `monThumb(PartyMon)` and `petThumb()` over it. Callers: the party grid (both
  shapes), the party sheet, the farm, the farm visitor (`ForkVisitor` carries
  the key now), the album, the squad picker (both cells), the home tab and the
  menu's STATS tile. Species-only screens (Pokedex, starters, evolutions,
  adventure screens) still ask `thumbs.get()`.
- The live pet's PMD sprite: `ensureMon()` loads the form with `loadForm()`
  and reloads when the drawn form changes (`monFormFor`), since two Kyurem can
  share a species and differ only in their kept form.
- Thumbnails: `pack_forms.py` writes `formthumbs.bin` (TPTH, indexed by form
  id, an empty blob for a form with no art) and packs it into
  `sprites-forms.pak`. `SdThumbs::load()` takes a file name, defaulting to
  `thumbs.bin`. The file is loaded the first time a kept fusion is drawn, so
  the board never looks for it.
- `forkKeyOf()` moved from fork_adv.cpp to fork.cpp, and `forkCreatureKey()`
  now calls it, so the key's bits are laid out once.
- Console: `FORM <slot> <form id>` keeps a form on a party creature (fork
  only), for trying this without an adventure.
- `keptform_test` (the 74th suite): thumbnails compared by bytes, and whole
  frames of the party grid, sheet, farm and squad picker through the real
  sketch, with a kept fusion against none; a kept Mega and a kept Ultra
  Necrozma leave the frame unchanged; the switch off changes nothing; the
  pet's sprite reloads as the form and back. Seven mutations (each caller put
  back to `thumbs.get()`, the reload condition, the state filter) each fail it.
- Also fixed: the mutation gate's capture control had been anchored on
  `wildStoreCaught()`'s old body since 12c part 2 and could not run. It
  anchors on `storeCaughtMon()` now.
- 74 host suites pass. 79 of 79 board shots match HEAD (`game` is timing-flaky
  and matches one of the two frames HEAD itself produces).
- Checked on the S24, with a Kyurem given a kept Black through `FORM` in an
  emulator copy of the save: Kyurem-Black on the party grid and its sheet, then
  MAKE ACTIVE, and it is the live pet's animated sprite and the HOME tab icon.
  The sheet's stats are still plain Kyurem's: that is part 2.

**12f part 2 built (2026-11-20): fighting.**
- `forkSquadForm(c, key)` (fork_adv.h) is the rule: a kept fusion gets its
  stats and typing through `forkApplyForm()`, by the same `forkDrawnForm()`
  that draws it; a kept Mega or G-Max only sets `Combatant::canForm`. Nothing
  with the switch off. `buildSquad()` asks it for the live pet and every party
  member, so gym, wild and debug fights all get it from one place.
- `Combatant` gained `form` (the form it is in now, set by `forkApplyForm()`)
  and `canForm`. `btlSyncSprite()` loads the form's sprite when `form` is set,
  so a fusion and a transformed Mega look like themselves in a fight.
- **TRANSFORM:** on the move screen the BACK bar splits into BACK and a toggle
  named for the form (MEGA X, G-MAX). Arm it, then pick a move: the creature
  transforms before anyone acts, says "<name> became <form>!", and stays
  transformed for the rest of the battle, switched out and back in included.
  `btlCanTransform()` is the one predicate for the draw and the tap: once per
  battle (`btlFormUsed`, across the whole squad), never in a LAN fight, never in
  a HARD gym fight, never when fainted. Nothing is written back; the next fight
  rebuilds the squad from the stored creature.
- **LAN:** `lanOffer()` builds its squad with `forms = false`, so a kept
  Kyurem-Black goes out with plain Kyurem's stats. The link would carry a
  fusion's stats but not its typing, and a board knows no forms.
- The party sheet and the card show what the creature fights as: a kept
  fusion's typing and stats (built through `forkSquadForm()` too).
- Also fixed: the debug `BATTLE` command left `btlTrainer` / `btlHard` /
  `btlLink` from the previous fight, so winning a debug fight after a gym fight
  awarded that leader's badge.
- `formfight_test` (the 75th suite) drives the real `startTrainerBattle()`,
  `startWildBattle()` and the LAN picker, and taps TRANSFORM on the phone's
  battle screen: the fusion's typing, stats and sprite; Mega X's typing, stats,
  sprite and message; once a battle, even for a second creature with its own
  kept form; plain again next fight; no toggle in hard gyms (its spot is BACK);
  allowed in easy and hard wild fights; a plain LAN squad; nothing with the
  switch off. Nine mutations (party members skipped, the LAN flag, the
  once-a-battle and hard-mode gates, the toggle ignored, the sprite cache
  ignoring the form, the reset, the fusion not applied, the switch) each fail it.
- Not tested by a suite: the sheet's and the card's numbers (the frame also
  changes with the thumbnail, so a frame compare cannot isolate them).
- 75 host suites pass. 79 of 79 board shots match HEAD byte for byte.
- Checked on the S24 (2026-11-20) on an emulator-edited copy of the save (a
  Lv.50 Charizard with Mega X kept through `FORM`), then the original save put
  back: the card shows Kyurem-Black as DRAGON/ICE with ATK 280 and DEF 210
  (plain 240/200); in an easy Misty fight Kyurem-Black fights in its own
  sprite, MEGA X sits beside BACK, lights red when armed, and DRAGON CLAW
  transformed Charizard first ("CHARIZARD became MEGA X!", Mega X sprite),
  after which the toggle is gone and DRAGON CLAW shows STAB; the next fight
  starts plain with MEGA X back; hard Brock shows BACK alone.

**12d. AI companions.** Three loan creatures with the hard-mode `aiChooseMove`,
plus a light team rule: do not all target the same shield, and heal or swap at
low HP. Measure how much they help (win rate solo vs with three AI) instead of
assuming it, the way `ai_test` does.

**12e. Multiplayer, phone to phone.** There is no ESP-NOW on a phone, but
`link.cpp` takes its transport as a function pointer, and its rules
(host-authoritative, turn-stamped resends, absolute state rather than deltas)
carry straight over. The host resolves every turn, and up to three guests send
actions. Transport options, best first:
- **Same Wi-Fi: UDP with Android NSD discovery.** No dependencies and no
  server, but players must share a network.
- **Google Nearby Connections:** Bluetooth or Wi-Fi with no shared network.
  Adds a Play Services dependency.
- **Over the internet:** needs a relay server. The project deliberately has
  none (see "No remote storage" in CLAUDE.md), so this is a separate decision.
- A board cannot join: it has no forms and a different radio.

### Settled in the second round (2026-11-20)

- **A killed app resumes the run** exactly where it was, from the saved run
  record. In multiplayer, the host's record is the one that counts.
- **Every player who wins a stage gets their own copy** of that creature to
  catch or ignore. Nobody has to claim it.
- **The loan is picked from three random choices**, rolled separately for each
  player from the difficulty's pool.
- **Rewards are items and coins** on top of the kept creature. **Keeping
  nothing pays more coins**, so a run that caught nothing worth keeping is
  still worth finishing, and declining is a real choice rather than a loss.

---

## Checklist

- [x] 0.1 `forkFeatures()` switch, with tests able to turn it on
- [x] 0.2 `frkA`/`frkB` save record, in `SAVE_FIELDS`
- [x] 0.3 fork string table, with its test
- [x] 0.4 phone bridge interface with stubs (`gForkHost`)
- [-] 0.5 `millis()` wraparound fix -- won't do (see Status)
- [x] 1 natures
- [x] 2 evolution rules: the rule table, how-to-evolve on the card,
      silhouettes for undiscovered forms, the bond hint
- [x] 3 sickness and medicine (and the coin purse that pays for it)
- [x] 4 step counter (sensor, midnight split, permission)
- [x] 5 expeditions and fork items
- [x] 6 more ways to touch the pet
- [x] 7 the pet acting on its own (self-care on return and the planner: tests only)
- [x] 8a world: sun, seasons, holidays, standby clock (and birthdays, gifts)
- [x] 8b world: weather (look and joy by type; off until turned on)
- [x] 9 the farm and the album (box made visible, visitors; album on the medals screen)
- [ ] 10 accessories
- [ ] 11 collector frames, help screen, clean and type minigames
- [ ] 12 adventures: 12a forms (done), 12b four-seat boss battle (done), 12c the run (done),
      12f kept forms at home (done), 12d AI companions,
      12e phone-to-phone multiplayer

Each feature is done only when the host suites pass, the 466x466 board shots
are byte-identical, and it has been checked on the S24.
