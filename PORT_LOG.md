# Port log

Newest entry first. One entry per working session. Each entry says what was
done, what was learned, and exactly where to pick up. If you are a model
starting a session: read the top entry and `PORT_PLAN.md`, then continue from
"Next".

Rules for this log:

- Record facts you verified, and say how. Mark guesses as guesses.
- Record dead ends so nobody repeats them.
- End every entry with a "Next" list, most important first.

---

## 2026-10-04, session 6: start screen, zoomed out 4:3, RG DS bottom screen

State at end of session: **0.3.0 built, not released.** Release APK
`android/app/build/outputs/apk/release/app-release.apk` (sha256
d1135a10...ea325) waits for the owner's play-test on the Thor. Everything
below was checked on the arm64 Android 14 emulator (`hns64`, set to 640x480
with a 640x480 overlay display to stand in for an RG DS) and headless. None
of it has run on a Thor or an RG DS.

### The RG DS report that started it

A friend ran 0.2.0 on an RG DS: it runs, buttons map, fast forward works.
Three problems: the bottom screen stayed on the launcher (Daijisho), the
picture could not be changed from 240x160, and the bars above and below
were white (photo in the owner's chat).

### Done

- **Start screen** (`StartActivity`, `StartScreenView`, `Settings`): the
  launcher opens it before the game. Picture: original 240x160, widescreen
  288x160, zoomed out 288x216. Scaling: fit, pixel perfect (integer),
  stretch. Bottom screen on or off. A preview box and a line of numbers show
  how the picture sits on this screen ("288x216 ON 640x480, AT 2.2x").
  Drawn in the game fonts, d-pad / A / START / touch; the cursor starts on
  START GAME. Choices persist (SharedPreferences "settings") and reach the
  game as `HNS_WIDESCREEN`, `HNS_TALL`, `HNS_SCALE`, set with `Os.setenv` in
  `HeartSoulActivity.onCreate` before SDL starts. Defaults: widescreen on a
  16:10 or wider screen, zoomed out on anything squarer. If the game is
  already running, the start screen forwards straight to it.
- **Black bars**: the renderer cleared to white once and never again. Now
  black, cleared every frame (`src/platform/sdl2.c`).
- **Scale modes**: `HNS_SCALE=fit|integer|stretch` (`ApplyScaleMode`).
  Verified on the emulator: integer gives 480x320 at 2x on 640x480, stretch
  fills 640 columns.
- **Zoomed out / tall screen** (agent, branch `port-tall`, merged):
  `HNS_TALL=1` with widescreen renders 288x216, 28 more lines above and
  below. Only the overworld (`CB2_Overworld` sets `gRenderMarginYLive` each
  frame) draws there; every other screen gets black bars. HBlank DMA and
  interrupts still run once per real line. The field camera ring already
  keeps the 16 metatile rows the tall view needs once the default camera pan
  of 32 is counted; `sMapViewTopRow` in `src/field_camera.c` tracks which way
  the shared row is used. Objects spawn 2 metatiles further out vertically;
  `IsTallSpriteOffscreen` widens the sprite culls. Evidence (agent's
  worktree, gitignored): `~/HeartSoulRecomp-tall/port/out/tall-evidence/`.
  Tall off is byte-identical to before (5850 shots, 32 and 64-bit); margin
  rows checked against the centre of other frames, 0 mismatches over about
  92,000 rows including map connection crossings.
- **Dark cave margins**: an empty window (WIN0H 0..0, which the cave flash
  sets outside its circle) was stretched into the left margin and lit it.
  Fixed in `winCheckHorizontalBounds`. Affected widescreen on the Thor too.
- **Bottom screen on displays without FLAG_PRESENTATION**: the lookup asked
  only for presentation displays, the likely reason the RG DS kept its
  launcher (guess: its second panel is an ordinary display). `SecondScreen`
  now takes any other public display and logs every display once
  (`HeartSoul: displays (game on N): [...]`). Where the Presentation is
  refused, `BottomScreenActivity` is launched onto that display over its
  launcher, window `FLAG_NOT_FOCUSABLE`. Launching it moves key focus to its
  display; `moveTaskToFront` does not bring it back (the game is already the
  front task of its own display), re-starting `HeartSoulActivity` with
  `REORDER_TO_FRONT` does, retried at 0.3/0.8/1.5/3 s while the game lacks
  focus. Verified on the emulator with the debug flag file
  `files/force_bottom_activity`: focus returns to display 0, a tap on the
  bottom screen does not take it, a key press reaches the game.
- **Bottom screen size**: design height 240 instead of 250, so 640x480 draws
  at 2x (was 1x, tiny). Thor stays at 4x. Checked with the battle and
  overworld preview JSONs at 640x480.
- Version 0.3.0 (versionCode 4).

### Known issues

- Tall screen, from the agent's report: the top margin can show 64px-tall
  objects (SS Anne, cable car) as a strip at once; weather sprites do not
  cover the margins; scripted camera pans larger than about 12px may show a
  stale row at a margin edge; the saved map view covers rows 0..13 only, so
  a script-changed tile in the bottom 4 lines may revert after a menu. Not
  seen in tests; not tested either.
- Tall costs about 25% more render time headless. Not measured on an RG DS.
- The bottom screen activity path has run only on the emulator. If the RG DS
  still shows its launcher, its logcat line `HeartSoul: displays` says why.
- adb `input keyevent` presses down and up within one frame, which the game
  misses; use `--longpress` when testing on the emulator.
- The debug APK grows on incremental builds (packaging leaves gaps); the
  release APK is about 95 MB.

### Next

1. Owner plays the 0.3.0 release APK on the Thor: start screen with the
   pad, widescreen as before, the bottom screen still a Presentation there,
   then zoomed out and pixel perfect for a look.
2. Send it to the RG DS friend; ask for the start screen, zoomed out, the
   bottom screen, and `adb logcat -s HeartSoul` if the bottom screen fails.
3. Publish 0.3.0 once the owner says so.

---

## 2026-10-03, session 5: 64-bit (arm64-v8a), v0.1.1 icon, v0.2.0

State at end of session: **v0.2.0 published** (pre-release) with both
`arm64-v8a` and `armeabi-v7a` in one APK. The owner played it on the Thor,
where Android picks the 64-bit build: continue from a 0.1.x save, walking,
doors, battles with bottom-screen taps, save and reload. v0.1.1 only changed
the icon (dual-screen handheld with a gold heart and a silver soul,
`port/icon/make_icon.py`).

### Why 64-bit

A friend's Anbernic RG DS on GammaOS refused 0.1.x with "isn't compatible
with your phone". The RK3568 can run 32-bit code, so the likely cause is a
64-bit-only firmware; not confirmed (ask for "Supported ABIs" in CPU-Z). He
also hit a battle crash under BlueStacks on a PC, which runs ARM code through
translation and is not a reliable test.

### How it was done

Three agents: data (`port-arm64-data`: asm/data pointers to `ptrvalue`,
`ptr_align`, `space64`, mapjson and mid2agb output, all 394 GBS song
headers), C (`port-arm64-c`: pointer truncation, script/anim/GBS stepping by
`DSIZEPTR`, `SCRIPT_EFFECT_TAG` width), and a lead (`port-arm64`: build,
`port/build64.sh`, `port/gas-at-comments.py` for the AArch64 assembler,
dual-ABI CMake with `game_data.o` and `game_data64.o`, `_setjmp` instead of
`__builtin_setjmp`, text shift-by-32 fix, 16 KB page alignment). All merged
into `port`. Headless 64-bit runs match 32-bit frame for frame.

- **Saves:** the 64-bit build writes SaveBlock1 in the 32-bit layout
  (`save.c`), and `SECRET_BASES_COUNT` is back to 20, so saves move freely
  between the builds. The only pointer in saved data is
  `ObjectEventTemplate.script`, saved as 0 and restored on Continue.
- **32-bit bug fixed on the way:** `SetWordTaskArg` with index 2 or more
  wrote into the next task.
- **Stale generated data:** the first 0.2.0 crashed continuing an outdoor
  save, because this checkout still had `data/layouts/layouts.inc` from the
  old mapjson (8-byte map sizes). Generated map and music data now depend on
  `$(MAPJSON)` and `$(MID)` (`map_data_rules.mk`, `audio_rules.mk`). After
  pulling changes to a generator, a normal build now regenerates.
- Door animation over-read fixed (`src/field_door.c`).

### Tools added

`port/build64.sh` builds `pokehns64` natively in the arm64 Docker image;
`HNS_BIN=pokehns64` runs it with the headless scripts (no qemu).
`port/check64.sh` checks every source for 64-bit pointer diagnostics.
An Android emulator AVD `hns64` (arm64 Android 14, 64-bit only) is on the
Mac for testing what 64-bit-only devices do.

### Known issues

- Not tried on an RG DS. Whether its bottom screen works is unknown.
- `getmoverelearnerstate` emits 4 bytes where C reads 2 (upstream bug).
- `GetSrcPtrFromSprite` still keeps a pointer in halfwords (unreached).
- Earlier known issues from session 4 still apply.

### Next

1. RG DS report from the friend on 0.2.0.
2. Keep collecting crash reports from players.
3. Brightness or colour option for the "dim" night look.

---

## 2026-10-03, session 4: on the Thor, crash fixes, v0.1.0 published

State at end of session: **v0.1.0 is published** as a pre-release at
https://github.com/aabrole/HeartSoulRecomp/releases/tag/v0.1.0 and the repo
is public (the owner's decision, made knowing the APK contains the game's
assets). The owner played the exact release APK on the Thor: continue from
a save, three battles, saved again, no crash, no audio underruns.

### Fixed on the device, with causes

- **Library would not load**: the ROM header structs sat in `.text.*`
  sections; with pointers in a PIC library that made the code segment
  writable, which Android rejects. Kept out of `.text` natively.
- **NPCs piled in a corner, crash leaving the house**: the NTx86 patch padded
  `object_event` for an unpacked struct; Heart & Soul's
  `ObjectEventTemplate` is packed. Padding removed (`asm/macros/map.inc`).
- **Choppy audio**: 47 ms queue topped up once per pass. Now ~100 ms, refilled
  fully each pass. A `perf:` line in logcat every 5 s shows underruns.
- **A on the wrong button**: controllers now map by label.
- **Saves lost**: writes stayed in the stdio buffer and Android kills apps.
  Now fflush + fsync.
- **Crash at the boot intro**: `IsPokemonCryPlaying(NULL)` before any cry.
- **Battles crashed (the "null task 39" corruption)**: found with
  AddressSanitizer on the device. `InitBtlControllersInternal` assigned
  controllers to battlers 2 and 3 in single battles, whose positions are
  `B_POSITION_ABSENT` (0xFF), writing `gBattlerControllerFuncs[255]`, which
  landed in `gTasks` in the clang build. Now bounds-checked under UBFIX.
- New-game preset names were read past their end (harmless, fixed).

### Added

- `port/build-apk.sh --asan`: AddressSanitizer build (wrap.sh, recover mode,
  reports to logcat). `--release`: signed release APK. Key in
  `android/keystore/`, `android/keystore.properties` (ignored; do not lose).
- Bottom screen redrawn in the game's fonts with GBA-style panels
  (`GbaFont.java`, `GbaText.java`, `BottomScreenView.java`); fonts and
  widths are copied from `graphics/fonts` and `src/fonts.c` at build time.
- Widescreen battle margins: BG3's hidden filler rows are replaced with
  ground (`WidescreenFixBattleBg3Map` in `src/battle_bg.c`); summary screen
  pillarboxed (`gRenderPillarbox`). Headless `HNS_CLOCK`, `HNS_LAYER_DEBUG`,
  `HNS_LAYER_HIDE`.
- Android chooses widescreen from the display's shape (4:3 gets 240x160).
- App icon (`port/icon/make_icon.py`), README section, release notes.
- Null-task diagnostics in `RunTasks`, game log lines to logcat.
- Commit email scrubbed to the noreply address before going public.

### Known issues

- Once, after 40 minutes, Android aborted in `ViewRootImpl` /
  `BLASTBufferQueue` ("decStrong() called too many times") on the UI thread.
  Only seen in the ASan build so far; cause unknown. Watch for it.
- Harmless over-reads still present: `sDoorAnimTiles_HnsCerulean` in
  `src/field_door.c` (door animation reads past its tiles).
- Widescreen: battle intro slide shows repeated ground in the margins for a
  second; one-screen move backgrounds do not fill the margins.
- Not tested on an RG DS. Areas past Route 29 untested.
- Release APK cannot install over a debug build (different keys). On the
  owner's Thor: back up the save, uninstall, install, launch once, then
  `cat` the backup into the app-owned save file.

### Next

1. Collect reports from players and from the owner's continued play.
2. Clean up the remaining ASan reads; keep an ASan build handy for crashes.
3. Look into the BLASTBufferQueue abort if it shows again outside ASan.
4. Test on an RG DS through a friend.
5. Day/night "dim" colours: offer a brightness or colour-correction option.

---

## 2026-10-02, session 3: Android app, audio, widescreen, bottom screen

State at end of session: a debug APK builds with all four pieces in it
(`port/build-apk.sh --data`, output
`android/app/build/outputs/apk/debug/app-debug.apk`, about 46 MB). **It has
never run on a device.** Everything below was verified on the Linux build
under qemu, from screenshots, JSON dumps and WAV measurements. Nobody has
listened to the audio yet; captures are in `port/out/audio/`.

Audio, widescreen and the bottom screen were each done by a separate agent
in its own worktree and merged into `port`. Their branches are still there:
`port-audio`, `port-widescreen`, `port-bottomscreen`.

### Android app

- `android/`: Gradle project (AGP 8.5.0, Gradle 8.7 wrapper, compileSdk 34,
  NDK 27.2.12479018, minSdk 26, `armeabi-v7a` only). SDL 2.30.7 is a git
  submodule at `android/SDL2`: run `git submodule update --init --depth 1`
  on a fresh clone.
- All 403 sources compile with NDK clang. One fix was needed: clang ignores a
  transparent union whose members differ in size
  (`union StatChangeFlags`, `include/battle_script_commands.h`).
  `port/clang-check.sh` reruns the check on the Mac.
- C is compiled by CMake (`android/app/src/main/cpp/CMakeLists.txt`) with
  `-marm`. Data is assembled in Docker by `port/android-data.sh` into
  `build/android/game_data.o`. lld cannot apply 16-bit relocations, so
  `port/resolve-abs-relocs.py` resolves the constant ones first.
- `src/platform/sdl2.c`: game controller input (east button is A, south is B,
  right trigger fast-forwards), fullscreen, save in the app's external files
  folder. Widescreen defaults to on for Android.
- A worktree build needs `touch .histignore` because Docker cannot see the
  git history there.

### Audio (`port-audio`)

- The GBS (Game Boy) engine was never dispatched by the C player, and the CGB
  emulation ignored register writes, which is all `src/gbs.c` does. Both
  fixed. Mixer rewritten after Heart & Soul's HQ mixer; output saturates
  instead of exceeding full scale.
- Measured: title, overworld and battle music non-silent and in tune, cries
  correlate with the source samples (r 0.88 and 0.70), no NaN, no sample over
  1.0. Tools: `HNS_WAV`, `HNS_AUDIO_LOG`, `HNS_GBS`, `HNS_SONG`,
  `port/wav-stats.py`, `port/wav-pitch.py`, `port/wav-find-sample.py`.
- Judgment call to confirm by ear: no-resample voices now play 1.357x fast to
  match the GBA build (18157 Hz mixer, 13379 Hz samples). To revert, set
  `GBA_SAMPLE_RATE` equal to `GBA_FIXED_SAMPLE_RATE` in `src/sound_mixer.c`.
- Not done: DC blocking on CGB channels (possible pops), tempo is 0.46% fast
  (60 fps against 59.7275), `MP2K_event_port` is a no-op, GBS stereo untested.

### Widescreen (`port-widescreen`)

- `HNS_WIDESCREEN=1` renders 288x160. Heart & Soul's overworld BG maps are
  256px wide, not 512 as session 1 assumed, so the map layers are widened to
  64x32 tiles behind `PORTABLE` while widescreen is on (`src/fieldmap.c`,
  `src/field_camera.c`, `src/overworld.c`, `src/event_object_movement.c`).
- Verified: outdoors both margins show real map, battle backgrounds fill the
  margins, and the centre 240 columns are byte-identical to the 240-wide run
  over 286 frames. With widescreen off, 33 of 36 frames match the old build;
  the other 3 are the intro, which a ported WIN1 fix now draws (it was the
  black stretch noted in session 2).
- Title, intro, main menu, bag and wall clock are 240 wide with black margins.
- Risks: field window tiles must stay below 0x240 in widescreen (not audited
  for every Heart & Soul window); the battle transition looks different in
  the left and right margins; weather, followers, flash, Pokédex and region
  map are untested. `HNS_WARP=N:G:M:X:Y` warps on a frame, because scripted
  input could not get past the wall-clock event to leave the house.

### Bottom screen (`port-bottomscreen`)

- `src/platform/dualscreen_bridge.c`: snapshot of player, party and battle
  state as JSON, published under a mutex every 4 frames; a tap driver that
  presses the real buttons one frame at a time while watching the actual
  cursor. `HNS_STATE_DUMP=N` and `HNS_TAP=frame:MOVE1..4|FIGHT|BAG|POKEMON|RUN`
  test it headlessly. A tapped move was chosen correctly and won a battle.
- Java: `BottomScreenPresentation`, `BottomScreenView`, `BottomScreenState`,
  `DualScreenBridge`, polled every 100 ms from `HeartSoulActivity`. Does
  nothing on a single-screen device. None of the Java has run.
- Not ported from the reference: the SDL lifecycle patch
  (`android/patches/sdl2-android-lifecycle.patch` in `dualscreen/main`), touch
  bag and party screens, hiding the top-screen battle menu, game font and
  icons, region map.

### Next

1. First device run on the Thor: `adb install -r` the APK, then
   `adb logcat -s SDL libmain AndroidRuntime`. Check in order: boots to the
   title, controls, sound, save and reload, widescreen framing, then the
   bottom screen. If the top screen stays black only when the bottom screen is
   active, apply the SDL lifecycle patch.
2. Have a person listen to `port/out/audio/*.wav` and settle the no-resample
   pitch question.
3. Expect more null-read crashes in areas not yet exercised. Each has a clear
   backtrace under `port/debug-headless.sh`.
4. Widescreen follow-ups listed above, then touch bag and party screens.
5. RG DS: untested, and it is unknown how GammaOS exposes the second screen.
6. Release signing and a distribution decision (see `PORT_PLAN.md`).

---

## 2026-10-02, session 2: native build boots and plays

State at end of session: Heart & Soul builds as a native 32-bit ARM Linux
program and plays under qemu with no display. Verified from screenshots
(`port/screenshots/`): copyright and credit screens, title screen (v2.0.6),
new-game intro, challenge settings menu, overworld in New Bark Town, start
menu, bag, saving, Continue from the save, walking, and a full wild battle
through to experience gain. No Android build yet. Nobody has heard the audio.

### How it was done

The plan changed from session 1. Instead of redoing the dual-screen repo's
693-file delta by hand, the port-only changes of
`ntx86/pc_port-expansion-test-attempt` (an expansion 1.17 native port, work
in progress) were isolated by diffing it against its rh-hideout base
`c681a75d43` and applied with `git apply --3way`. 283 files, 27 conflicted.
Conflicts were resolved by keeping Heart & Soul's code and taking the port's
pointer handling. That branch was never finished upstream, so several of the
fixes below are bugs it still has.

### How to build and test

```
docker build -t hns-port port/docker        # once
port/build.sh                               # builds ./pokehns32
HNS_SHOT_EVERY=100 port/run-headless.sh 3000   # screenshots in port/out
port/sheet.py port/out/sheet.png 100 200 300   # contact sheet (macOS)
port/debug-headless.sh 3000                 # same run under gdb, backtrace on a fault
```

Headless controls are environment variables, documented at the top of
`src/platform/sdl2.c`: `HNS_INPUT` (scripted buttons), `HNS_SHOTS`,
`HNS_SHOT_EVERY`, `HNS_TEST_BATTLE` (gives a Cyndaquil and starts a wild
battle on that frame). A frame that takes over 20 seconds is reported with
the stuck program counter; resolve it with `port/out/symbols.txt` or
`arm-linux-gnueabihf-nm -n pokehns32`.

The save file is `port/out/pokeemerald.sav`. It currently holds a save in the
player's bedroom, which the battle and Continue tests start from. Input
scripts that reach each state are in `port/out/*.txt` (not committed, since
`port/out` is ignored; the timings are in this log's git history of commands
only, so regenerate them if lost: mash A to frame 4900, R ten times from 5000
to leave the challenge menu, then A with an occasional START).

### Fixes made, with the cause of each

- `tools/preproc`: any identifier ending in `DUMMY(` was treated as an
  incbin. Now requires a whole identifier.
- Makefile: cross-compile support (`PREFIX`, `ARCH_CFLAGS`, `ARCH_ASFLAGS`),
  no `-m32` or leading underscores off x86 and Windows.
- `src/decompress.c`: the smol decoders were copied to the stack and run
  from there. Native builds now call them in place.
- `src/platform/gba_easy_draw.c`, `gba_fast_draw.c`: loop counters were plain
  `char`, which is unsigned on ARM, so the scanline loop never ended.
- `include/sound_mixer.h`: the native mixer assumed 12 channels. Heart & Soul
  uses 15, and the struct is a second view of `struct SoundInfo`.
- `src/m4a.c`: 16, 3, 11, 1 tracks for the four music players, matching
  `sound/music_player_table.inc`.
- Script engine: commands, specials and natives that report effects were
  marked by adding `ROM_SIZE` to their function pointer. Natively that jumps
  into data. They are now marked with `SCRIPT_EFFECT_TAG` (bit 1) and
  untagged before the call. See `include/script.h`. This relies on functions
  being 4-byte aligned, so ARM builds must use `-marm`.
- Null reads the GBA tolerates because address 0 is the BIOS:
  `gSaveBlock1Ptr` on the copyright screen (`src/main.c`) and `gBattleStruct`
  in `Task_HandleMonAnimation` on the main menu. Expect more of these. Each
  shows up as a segfault with a clear backtrace.
- GBA-only code guarded or replaced: mGBA debug registers (log lines now go
  to stderr), the RAM copy of the sound mixer, multiboot, `ReInitializeEWRAM`,
  RTC writes, linker-script symbols. `BitUnPack` added to
  `src/platform/bios.c`.

### Known gaps

- **Audio is unverified.** The mixer runs every frame without crashing, but
  the native sound engine is a C port of the vanilla engine and Heart & Soul
  changed its assembly engine heavily (1,871 lines in `src/m4a_1.s`,
  including the GBS Game Boy music engine in `src/gbs.c`). Next step is to
  write the mixer output to a WAV in headless mode and listen.
- **Saves are not GBA-compatible.** The native build does not use the GBA
  struct ABI (`-mabi=apcs-gnu`), so struct sizes differ. The size assertion
  for `ChallengeSettings` is skipped natively.
- **The clock cannot be set.** RTC writes are no-ops; time comes from the
  host.
- About 280 frames after the credit screens are black before the title
  appears (frames 420 to 700). Not checked whether the Heart & Soul intro
  should be drawing there.
- Only the first map, one wild battle and the bag were exercised. Warps,
  trainers, followers, the Pokédex, shops, evolution and link features are
  untested.
- `port/clang-check.sh` does not work yet: it preprocesses with gcc's glibc
  headers, which clang rejects. It needs to preprocess with clang. Whether
  expansion's GCC-specific code compiles with NDK clang is still unknown and
  is the main risk for the Android step.
- 64-bit is not attempted. Heart & Soul's own data still has `.4byte`
  pointers in places.

### Next

1. Audio: dump to WAV, listen, then port the engine differences that matter.
2. Android: get the tree compiling with NDK clang for `armeabi-v7a` with
   `-marm`, using `dualscreen/main`'s `android/` project and CMake file as the
   starting point. Needs `arm-none-eabi` binutils on the Mac or the Docker
   image for the data objects. Mac has NDK 27.2.12479018 at
   `/opt/homebrew/share/android-commandlinetools`.
3. Bring over `dualscreen/main`'s widescreen PPU (`gRenderWidth`,
   `gRenderMargin` in `include/platform.h`, `src/platform/sdl2.c`,
   `gba_easy_draw.c`). Its platform files descend from the same NTx86 layer,
   so diff them file by file against ours.
4. Then the bottom-screen bridge.
5. Keep extending the headless tests: leave the house, a trainer battle, a
   Pokémon Center.

---

## 2026-10-02, session 1: project setup and research

State at end of session: repo created, nothing built yet, no code changed.

### Done

- Cloned PokemonHnS-Development/pokehns-expansion (full history, 21,250
  commits) to `~/HeartSoulRecomp`. `master` is at `167aa6d537` "2.0.6".
- Remotes set up: `upstream` (HnS), `dualscreen` (Goldoire), `native`
  (fuddlesworth), `ntx86` (NTx86), `origin` (aabrole/HeartSoulRecomp,
  private). Reference branches fetched: `dualscreen/main`, `native/vanilla`,
  `ntx86/pc_port-expansion-test-attempt`,
  `ntx86/pc-port-widescreen-extended-tilemap`,
  `ntx86/pc-port-widescreen-hackjob2`, `ntx86/pc_port-64-bit-lactozilla`.
- Created `port` branch with `PORT_PLAN.md`, this log, and `CLAUDE.md`.

### Verified facts

- HnS source is fully public and is a fork of pokeemerald-expansion.
  `include/constants/expansion.h` says 1.15.2, untagged. 388 files in
  `src/*.c`. Builds with `MODERN=1`, gnu17. agbcc is not supported.
- `DISPLAY_WIDTH` is 240 in `include/gba/defines.h`, unchanged from vanilla.
- `dualscreen/main` is 331 commits past its pret base, builds Android
  `armeabi-v7a` only (`android/app/build.gradle`), NDK 26.3.11579264, minSdk
  21, and its desktop `Makefile_pc` uses `-m32`. So it is a 32-bit port.
- Its widescreen is a PPU margin: `WIDESCREEN_MARGIN 24` in
  `include/platform.h`, giving 288x160.
- Its shared-code delta against its pret base is 693 files, 14,671
  insertions, 3,942 deletions (excluding `src/platform`, `include/platform`).
- Its `mods/pokemonHnS` is a data-only import: 0 maps, 0 scripts. Not usable
  as a route to real HnS.
- HnS and `dualscreen/main` share pret history. Merge base is `208f97e1e3`
  (2025-08-21). HnS and `native/vanilla` merge base is `041ff613ca`
  (2026-04-07).
- This Mac has cmake, ninja, sdl2, adb and Homebrew. It does not have
  `arm-none-eabi-gcc`. No Android NDK found under `~/Library/Android/sdk/ndk`.

### Not verified

- Whether a plain `git merge dualscreen/main` into HnS is workable. Guess: no,
  too many conflicts in expansion-rewritten files. A file-by-file transplant
  is the assumed approach.
- How GammaOS on the RG DS exposes its second screen to Android apps.
- Whether the Thor needs anything beyond what `dualscreen/main` already does.
  Its README names the Thor as the target, so probably not.

### Next

1. Install the ARM toolchain (`brew install --cask gcc-arm-embedded` or
   `brew install arm-none-eabi-gcc`, check `INSTALL.md` for what expansion
   wants) and libpng, then build the stock ROM from `master`. This is M0.
2. Install Android NDK 26.3.11579264 and build `dualscreen/main` unchanged in
   a separate worktree (`git worktree add ../hns-dualscreen-ref
   dualscreen/main`). Install on the Thor. This proves the Android toolchain
   and gives a working reference to compare against. It needs a vanilla
   Emerald ROM on the device.
3. Produce the transplant inventory: `git diff 208f97e1e3 dualscreen/main
   --stat -- src include data asm sound` and sort the 693 files into (a)
   mechanical guards, (b) real logic changes, (c) dual-screen hooks. Save it
   as `docs/port/transplant_inventory.md`.
4. Copy `src/platform`, `include/platform*`, `android/` onto `port` and start
   on the build system (M1).
