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
