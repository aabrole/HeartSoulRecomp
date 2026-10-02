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
