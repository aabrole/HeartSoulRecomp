# HeartSoulRecomp

Native Android port of Pokémon Heart & Soul (pokehns-expansion) with
widescreen and dual-screen support. Primary device: AYN Thor. Secondary:
Anbernic RG DS.

## Start here

1. Read the top entry of `PORT_LOG.md`. It says where the last session
   stopped and what to do next.
2. Read `PORT_PLAN.md` for the approach, the reference repos and the
   milestones.
3. Before ending a session, add a new entry at the top of `PORT_LOG.md`.

## Rules

- `master` is pristine HnS upstream. All work goes on `port`.
- Never commit a ROM, a save, a built APK or binary, or the signing key
  (`android/keystore/`, `android/keystore.properties`).
- APKs are published only as GitHub release assets, and only after the
  owner has played that exact build on the Thor. The owner decides when a
  release is published and which one is Latest. (Decided 2026-10-03: public
  repo and public APK releases.)
- Commits use the owner's GitHub noreply address
  (8591368+aabrole@users.noreply.github.com), not a personal email.
- Changes to shared game code (`src/`, `include/`, `data/`, `asm/`, `sound/`)
  go behind `#ifdef PORTABLE` so the GBA ROM still builds unchanged.
- Test on the device before calling anything working. Do not send blind
  input to the Thor over adb. The owner plays and reports.
- Match the style of the file being edited. Game files follow pret style
  (Allman braces, 4 spaces, `gGlobal`, `sStatic`, PascalCase functions).
