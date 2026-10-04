<p align="center"><img src="port/icon/icon-512.png" width="128" alt="HeartSoulRecomp icon"></p>

# HeartSoulRecomp

Pokémon Heart & Soul running natively on Android, built for dual-screen handhelds like the AYN Thor. There is no emulator: the game is compiled from Heart & Soul's source code into an Android app, with a 16:9 widescreen top screen and a touch companion screen on the bottom.

This is a fan project. It is not affiliated with or endorsed by Nintendo, Game Freak, The Pokémon Company or the Heart & Soul team.

**Status: test build 0.3.0.** It has been played on an AYN Thor from a new game through the first battles on Route 29, including saving and loading, and 0.2.0 has run on an Anbernic RG DS. Expect bugs.

## Download and install

**[Download HeartSoulRecomp-0.3.0.apk](https://github.com/aabrole/HeartSoulRecomp/releases/download/v0.3.0/HeartSoulRecomp-0.3.0.apk)** (95 MB). Every version and its notes are on the [Releases](https://github.com/aabrole/HeartSoulRecomp/releases) page.

1. Download the APK on your device, or copy it over from a computer.
2. Open it and allow installing from unknown sources when Android asks.
3. Launch **Heart & Soul**.

**Updating:** install the new APK over the old one. Your save is kept. Do not uninstall first, because uninstalling deletes the save. The title screen shows which version you are running, after Heart & Soul's own: `v2.0.6 RECOMP 0.3.0`.

Requirements: Android 8 or newer on an ARM device. The APK contains both a 64-bit (`arm64-v8a`) and a 32-bit (`armeabi-v7a`) build, and Android picks the right one, so it also installs on 64-bit-only systems such as some custom handheld firmware.

## Features

- **Native speed.** The game runs as Android code, not inside an emulator.
- **Start screen.** Before the game starts, choose the picture, the scaling and whether to use the bottom screen. A preview shows how the picture will sit on your screen. Press A to play with your last choices.
- **Widescreen and zoomed out.** Widescreen (288×160) shows more of the map to the left and right. Zoomed out (288×216, 4:3) also adds rows above and below, so a 4:3 screen like the Anbernic RG DS's is filled with more of the map instead of black bars. Original (240×160) is the GBA picture. Widescreen is the default on 16:9 screens, zoomed out on 4:3 screens.
- **Scaling.** Fit (as big as fits), pixel perfect (whole-number zoom, every pixel the same size) or stretch (fills the screen, changes the shape).
- **Bottom screen.** On a device with a second screen, it shows your party, money, badges and play time, and in battle has touch buttons for moves, Bag, Pokémon and Run, drawn in the game's own font. On handhelds whose second screen runs its own launcher, such as the RG DS, it opens over that launcher and the controller stays on the game.
- **Controller support.** Built-in controls and gamepads work. The button labelled A is A. Hold the right trigger to fast-forward.

## Saves

Your save is `Android/data/com.heartsoul.recomp/files/pokeemerald.sav` on the device's storage. Copy it somewhere safe from time to time with a file manager or `adb pull`. **Uninstalling the app deletes it.** Saves are not compatible with the GBA version or emulators.

## Known issues

- Only the start of the game has been tested. Other areas may crash.
- Very occasionally Android has closed the app while drawing the screen. Save often.
- The zoomed out picture and the new bottom screen support have not been tested on an RG DS itself yet. If the bottom screen does not appear there, please report it with the output of `adb logcat -s HeartSoul`.
- In widescreen and zoomed out, a few screens (title, menus, bag, party) keep their GBA size with black bars around them, and in zoomed out battles have bars above and below, as designed.
- Zoomed out: weather such as rain does not reach the extra rows, and very tall objects can appear at the top edge all at once.
- Start screen choices apply when the game starts. To change them mid-session, close the app from recent apps and open it again.
- Game launchers such as Cocoon Shell keep their own artwork for each game and may keep showing an old or default icon after an update. Set it in the launcher (in Cocoon: Edit Game Artwork, then Upload Icon) using [`port/icon/icon-512.png`](port/icon/icon-512.png).

Report problems on the [Issues](../../issues) page with your device model and what you were doing.

## Building

See [`PORT_PLAN.md`](PORT_PLAN.md) and the newest entry in [`PORT_LOG.md`](PORT_LOG.md). In short: Docker builds the game data, then `port/build-apk.sh --data` builds the APK with the Android NDK.

## Credits

- **[Pokémon Heart & Soul](https://github.com/PokemonHnS-Development/pokehns-expansion)** by the Heart & Soul team: the game itself.
- **[pokeemerald-expansion](https://github.com/rh-hideout/pokeemerald-expansion)** by RHH (Rom Hacking Hideout), version 1.15.
- **[pret/pokeemerald](https://github.com/pret/pokeemerald)**: the decompilation everything above is built on.
- **[NTx86/pokeemerald-sdl2pc](https://github.com/NTx86/pokeemerald-sdl2pc)**: the native PC port layer, including its expansion port work.
- **[Goldoire/pokeemerald-dualscreen](https://github.com/Goldoire/pokeemerald-dualscreen)** and **[gradenGnostic/pokeemerald-multiplatform](https://github.com/gradenGnostic/pokeemerald-multiplatform)**: the widescreen renderer and the dual-screen design this follows.
- **[fuddlesworth/pokeemerald-native](https://github.com/fuddlesworth/pokeemerald-native)**: reference for the native port.
- **[SDL](https://www.libsdl.org/)**.

Port by Aman (u/BrownCountdown), developed with the help of Claude Code.

---

The rest of this README is Heart & Soul's own.

![HnS Logo](HnS_Logo.png)

# About `pokemonHnS-expansion`

<!-- If you want to re-record or change these gifs, here are some notes that I used: https://files.catbox.moe/05001g.md -->
<!-- TODO: Actually change these gifs, and generally update contents to convey HnS-specific information -->
![HnS Collage](HnS_Collage_YourAdventure.png)

**`pokemonHnS-expansion`**, aka Pokémon Heart and Soul 2.0, is a GBA ROM hack that is both a remake of GSC and demake of HGSS, with added quality-of-life, customization, and more.  
Originally built on top of [resetes12's **`Modern Emerald`**](https://github.com/resetes12/pokeemerald).  
Now additionally built on top of [RHH's **`pokeemerald-expansion`**](https://github.com/rh-hideout/pokeemerald-expansion) GBA ROM hack base.  
Finally, all of these projects are built on top of [pret's **`pokeemerald`**](https://github.com/pret/pokeemerald) decompilation project.

> Pokémon Heart & Soul brings the classic Johto Region and its iconic story to the world of modern GBA decomp hacking. Built on Modern Emerald and pokeemerald-expansion, this project offers a fresh take on the GSC/HGSS experience, blending key aspects of the Gen 2 and Gen 4 games, while incorporating many modern QoL features, as well as some familiar mechanics from Gen 3 to Gen 9. Not only is Heart & Soul (HnS) a first-of-its-kind, fully completed, playtested, and largely faithful GSC remake / HGSS demake, it's also completely open source, and is intended to be a base for a new generation of Johto rom hacks.

Unfortunately, saves from before 2.0 will not be compatible moving forward.

2.0.1 will be the last "official" release of Pokémon Heart and Soul, after which any bug fixes, content updates, or any propogated updates from **`pokeemerald`** or **`pokeemerald-expansion`** will only be available via community forks of the project.

# [Features](FEATURES.md)

**`pokemonHnS-expansion`** includes a mix of vanilla Emerald/FRLG features, re/de-made implementations of GSC/HGSS features, custom **`Modern Emerald`** features, and both features from [core series Pokémon games](https://bulbapedia.bulbagarden.net/wiki/Core_series) and popular QOL enhancements made available by **`pokeemerald-expansion`**.  
A full list of the features present in Pokémon Heart & Soul 2.0 can be found in [`FEATURES.md`](FEATURES.md)
A full list of the features made available by **`pokeemerald-expansion`** can be found in [`AVAILABLE_FEATURES.md`](AVAILABLE_FEATURES.md).

# [Credits](CREDITS.md)

<!-- TODO: update .all-contributorsrc and CREDITS.md to match https://pokemonhns-development.github.io/pokehns-expansion-documentation/credits.html -->
<!-- [![](https://img.shields.io/github/all-contributors/pokemonHnS-Development/pokemonHnS-expansion/upcoming)](CREDITS.md) -->

<!-- TODO: confirm our actual crediting policy and how best to respect our upstreams -->
If you use **`pokemonHnS-expansion`**, please credit **Pokemon Heart and Soul**, and retain the full chain of credits as best possible.  
If you additionally use a more updated version of **`pokeemerald-expansion`**, please *specifically* credit **RHH (Rom Hacking Hideout)** and include the version number for clarity.
For example:

<!-- TODO: confirm the closest applicable expansion version number -->
```
pokemonHnS-expansion 2.0 is Based off RHH's pokeemerald-expansion version 1.15.1 https://github.com/rh-hideout/pokemonHnS-expansion/
```

Finally, please consider [crediting all contributors](CREDITS.md) involved in the project!

# **`pokemonHnS-expansion`** multiplayer compatibility

- **`pokemonHnS-expansion`** supports trade and link battle multiplayer functionality, which *should* extend to forks built on **`pokemonHnS-expansion`** but cannot be guaranteed.
- **`pokemonHnS-expansion`** is not compatible with official Pokémon games, **`pokemonHnS 1.X`**, **`Modern Emerald`**, or other **`pokeemerald-expansion`** projects.

# [Getting Started](INSTALL.md)

❗❗ **Important**: Do not use GitHub's "Download Zip" option as it will not include commit history. This is necessary if you want to update or merge other feature branches from **`pokeemerald-expansion`**.

If you're new to git and GitHub, [Team Aqua's Asset Repo](https://github.com/Pawkkie/Team-Aquas-Asset-Repo/) has a [guide to forking and cloning the repository](https://github.com/Pawkkie/Team-Aquas-Asset-Repo/wiki/The-Basics-of-GitHub). Then you can follow one of the following guides:

<!-- TODO: update INSTALL.md to refer to HnS-specific things -->
## 📥 [Installing **`pokemonHnS-expansion`**](INSTALL.md)
## 🏗️ [Building **`pokemonHnS-expansion`**](INSTALL.md#Building-pokemonHnS-expansion)

# [Documentation](https://pokemonhns-development.github.io/pokehns-expansion-documentation/)

For our player-facing documentation, visit the [**`pokemonHnS-expansion`** documentation page](https://pokemonhns-development.github.io/pokehns-expansion-documentation/).

# [Contributions and Community](https://discord.gg/ksNTFNSBj)

[![](https://dcbadge.limes.pink/api/server/ksNTFNSBj)](https://discord.gg/ksNTFNSBj)

If - in the window between 2.0 release and 2.0.1 release - you are looking to report a bug, make a suggestion, or give feedback, please join the [Pokémon Heart and Soul Discord server](https://discord.gg/ksNTFNSBj). You are also welcome to join just to participate in the community, including pinging our @guides (and only our guides) for help answering questions not sufficiently covered by our documentation or in-game resources.

# AI Disclosure
Since this is a controversial topic at the moment, we'd like to be transparent about use of AI for this project.

Every line of code written for the game is either hand-written or manually reviewed by a member of the team. However, it is still important to point out that LLMs like Claude Code and GitHub Copilot have been used for some tasks.

Here is what AI has been used for:
- Code Reviews of hand-written code
- Debugging more complex scenarios
- Auto Completion (stuff like repeating lists, DebugPrints, etc.)
- Creating Python Scripts for I/O procedures (like downloading/writing list data, I/O data with Excel, etc. namely for documentation)

AI has not been used for:
- Generating assets of any kind; Art or Music
