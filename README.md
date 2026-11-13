# GBA DS Uno

**A phone-first fork of [kyannon/gba-ds](https://github.com/kyannon/gba-ds).**

GBA DS is a Game Boy Advance emulator that gives GBA games a second screen: the game runs on one screen, and the game's own map stays up on the other screen and keeps itself current as you explore, the way the DS Castlevanias work. Emulation is [libmgba](https://mgba.io); upstream supports 51 games with second-screen content (regional releases and some romhacks included), and any other GBA game also runs — it just plays on one screen. Full upstream documentation is in the [gba-ds wiki](https://github.com/kyannon/gba-ds/wiki).

**GBA DS Uno** takes upstream **v1.5** and retunes it for one specific job: playing on an ordinary Android phone (built and tested on a Moto G 5G 2024), held in portrait, with both screens stacked on the phone's single display.

## What this fork changes

Upstream code stays pristine. Everything in this fork is a small patch layer in [`motog/`](motog/), applied in order by `motog/update-and-build.sh` — so a future upstream release folds in by re-running one script. The six patches:

1. **0001 — Phone defaults.** Sensible compiled-in defaults for a phone: every ROM in the library is listed (not just the ~51 games with map profiles), no display shader, no rewind ring, no upstream update offers (an upstream-signed APK couldn't install over this build anyway).
2. **0002 — Touch controls.** Upstream has none; this adds an on-screen touch gamepad (D-pad, A/B/X/Y, L/R, Start/Select) that shows until a physical gamepad is used, then comes back on the next screen touch.
3. **0003 — Scan frame.** The library paints a "Scanning…" frame instead of sitting on a black screen during a long first scan of a big ROM folder.
4. **0004 — File-picker startup.** Every cold start opens Android's file picker first: pick a game (`.gba`, `.zip`, `.7z`) and it boots immediately through the app's own launch path. Cancel and you land on the library as before. No folder scan ever blocks startup again.
5. **0005 — Two screens, one phone screen.** The layout becomes two screens inside the phone's single window, with the live map (shadow) core on. The setting is pinned in the binary after all config files load, so an older settings file on the phone can't override it.
6. **0006 — Portrait layout.** The app runs in portrait and the two screens stack like a real DS: **game on top, map below** (supersedes 0005's landscape side-by-side).

`FINDINGS.md` at the root is the full engineering log for this fork — root causes, design decisions, and verification notes for every patch. The patch files themselves are in [`motog/patches/`](motog/patches/), and `motog/gba-ds.cfg` documents the phone configuration.

## Building

This fork's Android build (SDK + NDK r27c, Gradle 8.7 in-process):

```sh
motog/update-and-build.sh   # pristine upstream v1.5 + patches 0001–0006 -> signed APK
```

Upstream's own build routes still apply to the unpatched base — see upstream's README and `scripts/` (`scripts/build-rocknix.sh` for ROCKNIX handhelds, `scripts/setup-host.sh` + `make` for a host build, `scripts/build-android.sh` for the stock Android build). The wiki's [Playing](https://github.com/kyannon/gba-ds/wiki/Playing), [Settings](https://github.com/kyannon/gba-ds/wiki/Settings), and [Supported games](https://github.com/kyannon/gba-ds/wiki/Supported-games) pages describe the app itself.

## Snapshot note

This repository was posted as a text snapshot of the working tree. All source code is here, but 26 binary asset files from upstream v1.5 (launcher icons, the Jost font files, brand/overlay images, documentation screenshots, and logo-tool masks/stencils) could not be carried by the snapshot tool. They are **byte-identical to upstream v1.5** — copy them from [kyannon/gba-ds](https://github.com/kyannon/gba-ds) at tag `v1.5` if you need a tree that builds standalone.

## Credit

- **Upstream:** [kyannon/gba-ds](https://github.com/kyannon/gba-ds) — this fork exists because of that project. Thank you.
- **This fork:** Ragnorseawolf · Sköll

## Not affiliated with Nintendo

GBA DS Uno is an independent project. It is not affiliated with, endorsed by or sponsored by Nintendo or any device maker. Game Boy Advance, Nintendo DS and the names of the games are trademarks of their owners, used here only to say what the emulator runs. This project includes no games, BIOS files or other copyrighted game data — use your own legally obtained copies of your games.

## Licence

GBA DS Uno is free software: you can redistribute it and/or modify it under the terms of the [GNU General Public License](LICENSE), version 3, as published by the Free Software Foundation. See the [LICENSE](LICENSE) file for the full text.

Parts made by others keep their own licences, all compatible with the GPL: libmgba (© Jeffrey Pfau, MPL-2.0, source at <https://github.com/mgba-emu/mgba>), the Jost font (SIL Open Font License 1.1, `assets/fonts/Jost-OFL.txt`), stb_truetype (public domain), and rcheevos (© RetroAchievements, MIT, vendored in `third_party/rcheevos`).
