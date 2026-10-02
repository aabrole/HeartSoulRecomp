# Heart & Soul native port: plan

Goal: run Pokémon Heart & Soul (Johto in the Gen 3 engine) as a native Android
app, no emulator, with 16:9 widescreen on the top screen and a useful bottom
screen on dual-screen handhelds (AYN Thor, Anbernic RG DS on GammaOS).

Read `PORT_LOG.md` for what has actually been done. This file is the plan and
the reasoning behind it. Update both when things change.

## What we are building from

| Remote | Repo | Role |
|---|---|---|
| `upstream` | PokemonHnS-Development/pokehns-expansion | The game. Full source, `master` at tag 2.0.6. Fork of rh-hideout/pokeemerald-expansion 1.15.x, built with arm-none-eabi-gcc (`MODERN=1`, gnu17). |
| `dualscreen` | Goldoire/pokeemerald-dualscreen (`main`) | The platform layer we want. Vanilla Emerald running natively on the Thor with widescreen and a bottom-screen UI. MIT for its own code. Fork of gradenGnostic/pokeemerald-multiplatform. |
| `native` | fuddlesworth/pokeemerald-native (`vanilla`) | Cleanest desktop port of vanilla Emerald. 64-bit clean, macOS build, headless frame-hash tests. Reference for doing the 64-bit work properly. |
| `ntx86` | NTx86/pokeemerald-sdl2pc | The original PC port everything above descends from. Has `pc_port-expansion-test-attempt` (expansion 1.17, builds with "very bad and hacky fixes", Sept 2026) and two older widescreen experiments. |

All four share pret/pokeemerald history, so `git diff` and `git show
<remote>/<branch>:<path>` work across them.

## Do we need a ROM?

No. HnS publishes full source, and the decomp tree carries every asset
(graphics as PNG, audio as MIDI and samples, maps as JSON). The `.ups` on
their release page is only for people patching a ROM. We compile the game
straight into the app.

The dual-screen repo asks for a vanilla Emerald ROM at first launch because it
does not ship game data in its APK. That gate only matters if we ever publish
builds. For private builds on our own devices it does not apply.

## Status

Session 2 got the game running natively (see `PORT_LOG.md`). The route taken
differs from the one described below: the shared-code changes came from
NTx86's unfinished expansion port rather than from the dual-screen repo.
Milestones M1 and most of M2 are done on Linux. The Android, widescreen and
dual-screen steps below still stand, and the 32-bit decision held up.

## Why this is a port and not a 30 minute job

The dual-screen repo says "currently no ROM hacks are supported". Its
`mods/pokemonHnS` folder is a data-only import (starters, 147 trainers, 261
encounter tables, some text) laid over vanilla Hoenn. It reports 0 maps and 0
scripts imported and lists the HnS engine files as unsupported. That is not
Heart & Soul.

Real HnS means compiling HnS's own engine natively. The work:

1. **Platform layer transplant.** Bring `src/platform/`, `include/platform*`,
   and `android/` from `dualscreen/main` into this tree. These are new files
   and mostly drop in.
2. **Shared-code changes.** The dual-screen repo touches 693 shared files
   (14.6k insertions) to make vanilla Emerald portable: `#ifdef PORTABLE`
   guards, hardware register access through the software PPU, asm data made
   assembler-neutral, save and RTC hooks. None of that applies cleanly to
   expansion code, which has diverged heavily. It has to be redone against
   this tree, using their diff as the guide.
3. **Build system.** Expansion's Makefile assumes the ARM toolchain and its
   own tool pipeline. It needs a native target that runs the same asset tools
   and then compiles with the NDK (or host cc).
4. **Expansion-only code.** Things vanilla never had: followers, DexNav,
   HGSS Pokédex, the newer battle engine, the debug menu, any `.s` files or
   inline ARM. Each needs checking for hardware assumptions.

`ntx86/pc_port-expansion-test-attempt` is proof that expansion can be made to
build natively and is the first place to look when stuck.

## 32-bit first

The dual-screen repo builds `armeabi-v7a` only. On a 32-bit ARM target the
game's pointers are the same size as on the GBA, so the whole class of
pointer-in-u32 bugs and save layout problems does not exist. The Thor
(Snapdragon 8 Gen 2) and RG DS (RK3568) both run 32-bit apps.

Decision: target 32-bit Android first. Cost: no native run on this Mac (Apple
Silicon cannot run 32-bit binaries), so the test loop is build, `adb install`,
play on device. A 64-bit desktop build is a later milestone, done the way
`native/vanilla` does it (`ptrvalue` data macros, `T1_READ_PTR`, save
conversion).

## Widescreen

Already solved upstream in a way that suits us. The game keeps computing in
240-wide space. The software PPU renders 24 extra columns each side (288x160,
1.8:1) and shows what the overworld BG maps already hold off screen, since
those maps are 512px wide. Pixels stay square. See `include/platform.h` in
`dualscreen/main`.

Known rough edges to expect in HnS: sprites that pop in at the old screen
edge (object event spawn and despawn ranges), menus and battle backgrounds
that are exactly 240 wide and need a border or centering, screen fades and
window effects, and the HnS follower Pokémon.

Wider than 16:9 or zooming out further means changing the camera and map
buffer sizes in game code, the approach in
`ntx86/pc-port-widescreen-extended-tilemap` (sets `DISPLAY_WIDTH` to 288 and
edits about 50 files). Not planned unless the margin approach falls short.

## Dual screen

Feasible, and mostly built already. `dualscreen/main` has a Java
`DualScreenView` plus `src/platform/dualscreen_bridge.c` that reads live game
state and draws party, bag, map, trainer card, and Gen 4 style touch battle
menus on the second display.

What needs redoing for HnS:

- The bridge reads vanilla structs. Expansion changed Pokémon, move, item and
  battle structures, so every read needs re-pointing.
- The map panel draws Hoenn. HnS needs Johto and Kanto region map art and
  section ids.
- Bag pockets and counts differ in expansion.
- Battle menu geometry depends on the HnS battle UI.

RG DS: both of its screens are 640x480 (4:3), so widescreen does not apply
there, only dual screen. It also depends on how GammaOS exposes the second
display to apps. Unverified. Thor is the primary target.

## Milestones

1. **M0, baseline.** Install `arm-none-eabi-gcc`, build the stock HnS GBA ROM
   from `master`, confirm it boots in an emulator. Proves the tree and tools
   work before we change anything.
2. **M1, native build boots.** Platform layer in, shared-code guards redone,
   32-bit Android build reaches the title screen on the Thor. Single screen,
   240x160.
3. **M2, playable.** New game through the first gym with sound, saves that
   survive a restart, no crashes. Fix expansion-specific hardware assumptions
   as they surface.
4. **M3, widescreen.** Margin on, then fix pop-in and 240-wide screens.
5. **M4, dual screen.** Bridge re-pointed at expansion structs. Party and bag
   first, then battle menus, then the Johto map.
6. **M5, RG DS.** Test on GammaOS, handle the 4:3 panels.
7. **Later.** 64-bit desktop build, upstream HnS merges, voxel renderer
   (exists in `dualscreen/main` under `src/platform/voxel`, out of scope).

## Branches

- `master`: pristine HnS upstream. Never commit here. Update with
  `git fetch upstream && git merge --ff-only upstream/master`.
- `port`: default branch, all our work.

## Licensing and distribution

Game code and assets derive from a copyrighted game. Keep the GitHub repo
private. Never commit or upload a ROM, a save, or a built APK. Decide on a
distribution model (ROM gate or patch) before anything goes public, and
credit HnS, RHH, pret, Goldoire, gradenGnostic and NTx86.
