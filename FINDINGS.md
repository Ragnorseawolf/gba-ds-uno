# GBA DS — Moto G 5G 2024 edition: Phase 1 findings & plan

*Diagnose-and-plan only. No source was changed and no build was attempted.*
*Target device: Layne's Moto G 5G 2024 (Snapdragon 4 Gen 1, Adreno 619, ~4 GB RAM, 720×1612 single screen, arm64).*
*This edition is for Layne's PERSONAL USE only — no publishing, no store/itch release, no rebranding. Upstream name, package and branding stay as-is; the plan optimises purely for his phone.*

---

## 1. What we have

- Repo cloned to `~/workspace/gba-ds-src/` from https://github.com/kyannon/gba-ds
- **Tag `v1.5`, commit `7189176afebfca838a2c7b6ee56c4033a36717ca`** ("Release v1.5") — the same version as the APK Layne installed (`gba-ds-v1.5-android`).
- No git submodules. Dependencies are fetched at build time into `deps/`:
  - **libmgba**, pinned commit `143adf5` (mGBA master, Oct 2026), cloned from mgba-emu/mgba (`scripts/mgba-options.sh`)
  - **SDL2**, branch `release-2.30.x`, cloned from libsdl-org/SDL
  - **rcheevos** is vendored in-tree (`third_party/rcheevos`, MIT)
- App code is plain C (SDL2 + libmgba): `src/*.c`. Android glue: `src/android.c` plus three Java files (`LauncherActivity`, `GbaDsActivity` extends SDLActivity, `SecondScreen` Presentation) in `android/app/src/main/java/io/github/kyannon/gbads/`.
- Package `io.github.kyannon.gbads`, minSdk 26, targetSdk/compileSdk 34, arm64-v8a only.

## 2. How the Android build works

Entry point: **`scripts/build-android.sh`** (upstream builds it on GitHub Actions; the script is fully runnable locally):

1. Clones mGBA + SDL2 into `deps/` if absent.
2. Builds **libmgba** with the NDK CMake toolchain (ABI arm64-v8a, API 26, options from `scripts/mgba-options.sh`; CPU flags `-march=armv8-a -mtune=cortex-a55` — safe on the Snapdragon 4 Gen 1).
3. Copies licence files into the APK assets.
4. Runs **`gradle -p android assembleRelease`** with `-PsdlDir/-PmgbaSrc/-PmgbaBuild`:
   - AGP **8.5.2** (`android/build.gradle`), no Gradle wrapper in the repo — CI supplies Gradle 8.10.2; the script's floor is **Gradle 8.7+** and **JDK 17**.
   - `android/app/src/main/cpp/CMakeLists.txt` (CMake ≥ 3.18) builds `libSDL2.so` from the SDL source and `libmain.so` from `src/*.c` + `src/android.c`, linking the prebuilt `libmgba.a` and a static rcheevos.
   - SDL's Java side is compiled straight from the SDL source tree, so it always matches libSDL2.so.
5. Output: `dist/gba-ds-v<VERSION>-android.apk`. Signing: release key via env (`GBADS_KEYSTORE` etc.); **without it the APK is debug-signed** — fine for personal use (see §7 for the one wrinkle).

### Local toolchain: almost everything is already here

| Need | Status |
|---|---|
| Android NDK | ✅ `~/android-sdk/ndk/26.1.10909125` (set `ANDROID_NDK_HOME` to it) |
| SDK platform android-34 | ✅ `~/android-sdk/platforms/android-34` |
| Build-tools 34 | ✅ |
| CMake ≥ 3.18 | ✅ SDK cmake 3.22.1 (and system cmake 3.28.3) |
| JDK 17 | ✅ `~/workspace/snes9x-ra/tools/jdk` (Temurin 17.0.21) — export as `JAVA_HOME` |
| Gradle 8.7+ | ✅ `~/workspace/snes9x-ra/tools/gradle` is Gradle **8.7** (AGP 8.5.2's minimum; upstream CI uses 8.10.2 — if AGP grumbles, fetching 8.10.2 is a one-download fix) |
| git | ✅ |
| `deps/mgba`, `deps/SDL2` | ⬜ Not yet — `build-android.sh` clones them on first run (mGBA is a full clone; keep it in the workspace, not /tmp) |

**The one unproven link:** Gradle's first run downloads AGP 8.5.2 and its dependencies from Google Maven / Maven Central. Other downloads have failed through this sandbox's proxy before (sdkmanager couldn't traverse it on earlier projects), so Phase 2 should test the Gradle dependency fetch *first*, before anything else. Fallbacks if it's blocked: seed the Gradle cache from a machine where it works, or build via GitHub Actions on a private fork (works, but slower loop). No other blockers identified.

## 3. The first-run flow (what actually happens on the phone)

1. **LauncherActivity** (the app icon): asks once for **All files access** (MANAGE_EXTERNAL_STORAGE), then starts GbaDsActivity on display 0 in its own task. It writes `GBA DS/launch.log` on shared storage — every launch request, the device/Android version, Java crashes.
2. **GbaDsActivity.onCreate**: picks the data folder (`/storage/emulated/0/GBA DS/` with All files access, else the app's private folder), sets `GBADS_HOME`, and only if a **second physical Display exists** creates the `SecondScreen` Presentation and calls `SecondScreen.nativeSetDisplay(w,h)`. On the Moto G there is one display → no second screen is ever registered.
3. **Native main()** (`src/main.c`): `android_data_setup()` (honours a user-chosen data folder from `data-folder.cfg`), log to `GBA DS/gba-ds.log` (truncated each start), then config in order: compiled defaults → `android_config_defaults()` → `GBA DS/gba-ds.cfg` → `states/prefs.cfg` → **`states/library.cfg`** → `states/hotkeys.cfg`.
4. **`ui_init()`** (`src/ui.c`): SDL window + renderer on view 0. A second view is added **only** if `android_second_size()` returns a size (i.e. a second display was registered by Java) — on the phone, `nViews = 1`. `layout_screens()` then splits the single landscape window by aspect: at 1612×720 it picks **"span-h" — two half-width screens side by side** (game | map) inside the one window.
5. **`library_run()`** (`src/library.c`): calls **`scan(cfg)` before drawing anything**, then the list loop. The scan recursively walks `rom_dir` (depth ≤ 3) and reads the header of every `.gba/.zip/.7z` to get its 4-letter game code (`game_code()` → `emu_rom_header()`, `src/emu.c`); for archives that means opening/unpacking the archive to find the ROM. Archive results are cached in `states/archives.cache` only after a successful pass. Only games with a second-screen profile are listed unless `show_all = 1` (default **0** — just 51 supported games).
6. **Picking a ROM folder** (Settings → Folders → Choose ROM folder): native calls Java `pickRomFolder()` → SAF tree picker (`ACTION_OPEN_DOCUMENT_TREE`) → tree URI converted to a plain path → `nativeFolderPicked()` → `settings_poll()` writes **`rom_dir` into `states/library.cfg`** (`save_library_cfg()`), then immediately re-runs `scan(cfg)` before the next frame is presented.
7. **In a game** (`game.c` + `mapcap.c`): a second, silent mGBA core (the "shadow emulator") periodically receives a copy of the live state, presses the map button, and captures the map screen for the second view. Logo extraction for the list likewise boots ROMs in a background worker (`logo.c`).

## 4. Black-screen diagnosis (ranked)

Symptom timeline: app worked → Layne selected his ROM folder → black screen → black **every** time he opens it since. Two things changed at that exact moment: the SAF picker round-tripped the SDL activity's surface, and `states/library.cfg` gained a new `rom_dir` that is re-read on every launch.

### #1 — The library scan chokes on the picked folder, before the first frame (most likely; uniquely explains "every launch")

- `library_run()` scans **synchronously before the loop draws a single frame** (`library.c:1615-1617`), and the folder-pick path re-scans the same way (`library.c`, `settings_poll` branch). There is no progress UI and no async scan: while scanning, nothing is ever presented — the window is the black that `create_view()` cleared it to (`ui.c` create_view).
- The scan is cheap for a tidy GBA folder, but expensive-to-pathological for a broad one: recursion to depth 3 over everything, and **every `.zip`/`.7z` encountered is opened and decompressed** to find a GBA ROM inside (`emu.c:579` `emu_rom_header` + `VDirOpenArchive`). If Layne picked a parent folder (a multi-system ROMs tree, Download, or storage root) containing big ROM-pack archives, the scan can run for many minutes on a Snapdragon 4 Gen 1 reading slow shared storage — indistinguishable from a dead app.
- Because `library.cfg` persists, every subsequent launch repeats the same scan before its first frame → black every time. This is the only candidate that fully explains persistence across cold starts.
- **Corroborating pattern from upstream's own notes:** `docs/ANDROID.md` documents a previous start-up black-screen complaint traced to startup ROM reading ("Start-up (5-10 s of black reported): the ROM pick reads every ROM's header (unpacking archives)…").
- Layne-side tests that would confirm it: (a) open the app and just wait 5-10 minutes — if the list eventually appears, it's this; (b) delete `Internal storage/GBA DS/states/library.cfg` (or clear the app's storage) — the app should open normally with the default folder; (c) `GBA DS/gba-ds.log` would end mid-scan (no `library: N game(s)` line).

### #2 — SDL surface/renderer loss across the folder-picker trip, never repainted (explains the *immediate* black right after picking)

- The picker is a separate activity started for result from the SDL activity; while it's up, the SDL activity is paused/stopped and Android tears down its surface. GBA DS's Android views are **"retained" — view 0 is drawn only when it changes** (`ui.c` ui_init Android block), so repainting after a surface/renderer reset depends entirely on the reset-event path (`ui.c` renderReset handling) firing correctly on this device.
- Upstream is candid that this whole area is untested on ordinary Android: frontend launching and "the pause menu on returning to the app" shipped as **experimental — "not testable on the owner's RG DS Plus (GammaOS doesn't handle app switching well enough)"** (`docs/ANDROID.md`, milestone 3). Issue #28 was exactly this bug class (background/foreground surface handling) on the second display. The app's entire Android testing history is dual-screen handhelds (RG DS Plus, AYN Thor, Retroid) — **no plain phone has ever been a test device**, and there is no matching report in the GitHub tracker (41 issues checked; none is a single-screen-phone black screen).
- Why it can *look* permanent: GbaDsActivity is `singleTask` and only calls `System.exit(0)` when finishing — reopening from recents/home resurfaces the **same process** with the same dead surface. It would look like "black every time I open it" until a force-stop. **Distinguisher from #1:** force-stop the app (App info → Force stop), then open it — if it opens fine until the next picker trip, it's #2; if it goes black again on a cold start, it's #1.

Most probable reality: #1 and #2 compound — the picker kills the surface (black immediately), and the persisted `rom_dir` makes every cold start re-run the long scan before any frame (black forever).

### #3 — Single-screen layout is a desktop code path never validated on a tall phone (contributing risk, not the trigger)

With `nViews = 1`, `layout_screens()` derives the arrangement from the window aspect (`ui.c`): 1612×720 → "span-h", two 806×720 halves. The activity is locked to landscape (`AndroidManifest.xml`), SDL is hinted `LandscapeLeft`, the window is created 1024×768 fullscreen-desktop. This is the Linux single-window path reused; nothing in the repo's docs or issues shows it ever ran at 720×1612 on a phone. It demonstrably rendered before the folder pick, so it's not the primary cause — but any phone build must set `layout = single` deliberately instead of inheriting span behaviour (see §5).

### #4 — Ruled out as the list-black cause: the shadow emulator / second-screen GPU path

The map-capture shadow core (`mapcap.c`) is only created inside a game session, never in the library, and the GPU second screen requires `android_second_size()` > 0, which never happens without a second display. Neither can black the game list. They remain **in-game** performance suspects on this phone (§5). The library *does* run background logo-extraction and CRC workers (`logo.c`, `library.c` rom_crc_worker) that boot ROMs on a 4 GB / SD 4 Gen 1 device — a plausible contributor to a long, hot, apparently-frozen first scan, but not a standalone cause.

### #5 — Permission/path trap (checked; doesn't fit black, but will bite later)

The picked SAF tree is converted to a plain path (`treeUriToPath` → `documentIdToPath`), which is only readable with All files access actually granted. Without it, `opendir` fails, the scan logs `library: cannot open …` and finishes instantly with 0 games — a drawn, empty list, not a black screen. Worth verifying the grant stuck when we set the phone build up. Separately: with the default `show_all = 0`, even a perfect scan lists **only the ~51 supported games** — Layne's other ROMs would be invisible until `show_all = 1`. Not the black screen, but it would have been his next complaint.

### Evidence already on his phone

`Internal storage/GBA DS/launch.log` and `GBA DS/gba-ds.log` record the whole story (device line, renderer lines, `startup: screens ready`, `library: …` lines, or their absence). Phase 2 should start by reading those two files before changing anything — they can settle #1 vs #2 definitively. (`gba-ds.log` is truncated at each launch, so grab it after a fresh failed open.)

## 5. The Moto G edition — scope

Design rule: **one screen, one emulator, phone-first defaults.** Everything below is classified by how it ships (overlay/config vs source patch) in §6.

### Behaviour changes (defaults for this build)

| Change | Why | Knob |
|---|---|---|
| `layout = single` | One full-screen game view; no span-h split, no phantom second screen | config key already exists (`config.c` STR layout) |
| Map/shadow core **off** by default (`map = 0`) | Kills the second silent mGBA instance and its periodic state-copy + capture work — the single biggest CPU/RAM saving on a 4 GB phone, and removes the whole class of second-screen failures. Layne loses live maps on the ~51 supported games; turning it back on per the cfg is one line if he wants it for a favourite game. | config key exists (`config.c:138`) |
| `show_all = 1` | List **all** his ROMs, not just the 51 profiled games | config key exists |
| `rewind = 0` | The 30 s rewind buffer costs RAM and per-frame copies the SD 4 Gen 1 doesn't need to spend | config key exists |
| `update_check = 0` | The app checks GitHub daily and offers upstream's release APK — which is signed with kyannon's key and **cannot install over our build** (different signature → the installer's "another key" error). Our updates come through our own flow (§6), so the built-in checker must be off. | config key exists |
| `shader = off`, integer scaling kept | The LCD-grid shader is extra GPU fill over a 1612×720 frame on an Adreno 619; integer scale of 240×160 is cheap and looks right. | config keys exist |
| `audio_latency` left at 64 ms, revisit after play-testing | SD 4 Gen 1 audio path is the unknown; 96/128 is the fallback if it crackles | config key exists |
| Frameskip | **There is no frameskip option in GBA DS** — mGBA runs full-rate; on this SoC GBA is well within budget (upstream measured ~6 ms emulation on Cortex-A55-class cores). Nothing to set; the perf levers are the shadow core (off), rewind (off) and shader (off) above. | — |
| Core/thread pinning | `pin_cores` already defaults off upstream after testing showed no benefit. Leave it. | — |

### The one real feature gap: **no on-screen controls**

GBA DS by design treats touch as menu input only — gameplay buttons come from a physical controller over SDL (the "Controls" picker in game.c only remaps *physical* layouts; there is no touch D-pad/buttons for gameplay anywhere in `game.c`/`ui.c`). On the Moto G that means:

- **Option A (zero work):** Layne plays with one of his Bluetooth/USB gamepads, as he does with his own games. The phone edition ships as-is with the defaults above.
- **Option B (the one big patch, only if Layne wants it):** add a simple on-screen overlay — D-pad left, A/B + shoulders right, drawn by ui.c, feeding the existing pointer→button queue. This is the largest item in the whole project and the highest upstream-conflict surface; it should be its own patch file and its own decision.

Recommendation: ship Option A first; add B only on Layne's say-so.

### Performance notes for Adreno 619 / 4 GB

- Renderer: `create_view()` asks for `SDL_RENDERER_ACCELERATED` (OpenGL ES on Android) with a software fallback — on the Adreno 619 the GLES path should be picked; the log line `game screen renderer: …` in gba-ds.log confirms which one we got. Software rendering at 1612×720 would be a perf problem worth a patch (force/verify GLES) only if the log shows it.
- The upstream binary is already tuned conservatively (`-march=armv8-a`, APerformanceHint session, nice -10 game thread, async save fsync off-thread). No changes needed.
- RAM: one mGBA core + no rewind + no shadow core keeps the footprint modest; the APK is arm64-only already.

## 6. Upstream-sync architecture (Layne's requirement)

Principle: **pristine upstream + a thin, isolated patch layer.** Our fork carries a `motog/` directory that upstream never sees; the source tree itself stays byte-identical to the release tag except for the numbered patches, which are regenerated as files and re-applied mechanically.

### What's overlay (zero upstream edits)

- `motog/gba-ds.cfg` — the phone defaults from §5, as a ready cfg. (On the phone it lives at `GBA DS/gba-ds.cfg`; our build can also seed it — see the one defaults patch below for why the cfg alone isn't quite enough for first-run.)
- `motog/update-and-build.sh` — the whole update flow (§6.3) as one script.
- `motog/keystore/` — **our own fixed signing key**, generated once and kept. Every Moto G build is signed with it, so builds install over each other forever. (Personal use only; the key never leaves Layne's setup.)
- `motog/NOTES.md` — what each patch does and why (this file's §5/§6 condensed).

### What truly needs source patches (kept as short as possible)

1. **`0001-phone-defaults.patch`** — one small hunk in `src/android.c`, `android_config_defaults()`: set `layout = "single"`, `showAll = true`, `map = false`, `rewind = false`, `updateCheck = false` as the *compiled* defaults. Rationale for patching instead of cfg-only: compiled defaults govern the very first run (before any cfg exists) and can't be accidentally overridden by a stale `prefs.cfg`/`library.cfg` on the phone (main.c loads those *after* gba-ds.cfg, and prefs can re-enable things). One function, ~6 lines, in a file upstream touches rarely — the lowest-conflict surface in the tree. Everything else in §5 stays cfg-only, so if this patch ever fails to apply, the cfg still gets us 90% of the behaviour.
2. *(Only if Phase 2's log evidence confirms diagnosis #1 and upstream hasn't fixed it)* **`0002-scan-progress.patch`** — draw one frame ("Scanning…") before `scan()` in `library_run`, or move the scan behind the first present. Deliberately deferred: `library.c` is upstream's most active file, so this is the highest-conflict patch; we don't carry it unless it's proven necessary.
3. *(Only if Layne chooses Option B)* **`0003-touch-controls.patch`** — the on-screen gamepad. Largest patch, isolated in ui.c/game.c input paths; its own decision, not part of the base layer.

Nothing else is patched. In particular: no package rename, no branding changes, no manifest edits (the landscape lock is upstream's and suits gamepad play; revisit only if Layne wants portrait list browsing — that would be a manifest hunk in patch 0001's file if ever approved).

### The update flow (what `motog/update-and-build.sh` will do)

```sh
# one-time setup: clone once, add motog/ on a branch, generate the keystore
git clone https://github.com/kyannon/gba-ds && cd gba-ds
git checkout v1.5                      # current base
# ... motog/ lives untracked-by-upstream on our 'motog' branch ...

# every upstream release:
./motog/update-and-build.sh v1.6       # the new tag
```

Script steps (each step fails loudly and stops):
1. `git fetch --tags upstream` (origin), verify the tag exists.
2. Check out the new tag into a **clean worktree** (the working tree is never dirtied; upstream files stay pristine).
3. Apply `motog/patches/*.patch` in order with `git apply --check` first, then `git apply`. A failed check = stop and report which patch — that patch gets a manual fix (see caveat), everything else proceeds untouched.
4. Run upstream's `scripts/build-android.sh` with our environment: `ANDROID_HOME=~/android-sdk`, `ANDROID_NDK_HOME=~/android-sdk/ndk/26.1.10909125`, `JAVA_HOME=~/workspace/snes9x-ra/tools/jdk`, Gradle 8.7 on PATH, `GBADS_KEYSTORE` + passwords pointing at `motog/keystore/`.
5. Verify the APK (`aapt2 dump badging`, `apksigner verify`) and copy it to the deliverable name **`GBA DS v<version>.apk`** style Layne uses for his own builds — actually: keep upstream's own artifact name pattern `gba-ds-v<version>-android.apk` to avoid confusion with his games; final naming is Layne's call at delivery time.
6. Print the one manual phone step if it's the first of our builds: **uninstall the official GBA DS once** (its release signature differs from ours, so Android won't install ours over it; his `GBA DS/` data folder on shared storage survives an uninstall, so states/config carry over).

Equivalent branch view for a human doing it by hand: keep branch `motog` = release tag + patch commits; updating is `git rebase --onto v1.6 v1.5 motog`, then `git format-patch v1.6 --stdout > motog/patches/…` regeneration. The script wraps exactly this.

### Honest caveat

If upstream rewrites a spot a patch touches, that patch won't apply and needs a manual fix before the build proceeds. The plan minimises that surface deliberately: the base layer is **one ~6-line hunk in one stable function** (`android_config_defaults`), everything else is config or separate overlay files that upstream releases can't break. The risky patches (scan, touch controls) are opt-in additions, each isolated in its own file so a conflict there never blocks the base build.

## 7. Phase 2 outline (for approval)

1. Read `GBA DS/launch.log` + `gba-ds.log` from Layne's phone; confirm diagnosis #1 vs #2 (and grab the renderer line).
2. Smoke-test the toolchain: fetch `deps/`, run a **pristine** `scripts/build-android.sh` — proves Gradle's dependency download works here before we change anything.
3. Add `motog/` (cfg, keystore, patch 0001, update-and-build.sh); build the Moto G edition; verify badging/signature.
4. Layne installs (after the one-time uninstall of the official app), points it at a *small* test ROM folder first, then his real one; we read the new gba-ds.log together.
5. Decide Option A vs B (touch controls) from how he actually plays it.

*End of Phase 1.*

---

## 8. Phase 2 — build log & PAUSE CHECKPOINT (2026-11-13)

**STATUS: PAUSED by Layne 2026-11-13 (~15:05 EST) — YouTube apps take priority; this build resumes after them. All build processes have been stopped. This section is the resume checkpoint: what is proven, what exists, the exact tree state, and what remains, in order.**

### 8.1 Stock build: SUCCEEDED (pipeline proven)

The **unmodified upstream v1.5** (tag `v1.5`, commit `7189176afebfca838a2c7b6ee56c4033a36717ca`, arm64-v8a) built cleanly:

- Output: `dist/gba-ds-v1.5-android.apk`, 5,607,356 bytes, `BUILD SUCCESSFUL in 2m 43s` (log: `build-logs/stock-build4.log`).
- Badging verified: package `io.github.kyannon.gbads`, versionName `1.5`, minSdk 26, targetSdk 34. (Debug-signed — no signing env was set for the stock run; signing is exercised by our build, §8.4 step 5.)

**AGP / proxy verdict — AGP 8.5.2 downloads fine through this sandbox's proxy, but only with four fixes, all now baked into the build driver (`build-logs/run-build.sh` + `build-logs/gradle-inproc.sh`, PATH shim at `~/workspace/gba-ds-tools/bin/gradle`):**

1. **Gradle must run in-process.** A forked Gradle daemon's socket protocol is corrupted by the sandbox's TCP policy interceptor ("Could not receive a message from the daemon"). The wrapper launches `org.gradle.launcher.GradleMain` from the launcher jar with the instrumentation javaagent attached, so Gradle reuses the launcher JVM and never forks. (Diagnosis detail in `gradle-inproc.sh`'s header comment.)
2. **`-Djava.net.preferIPv6Addresses=true`.** `/etc/hosts` maps the egress proxy to a fake IPv4 (198.19.0.1) first, which lands in the interceptor; the real proxy is the IPv6 entry.
3. **Merged truststore** at `~/workspace/gba-ds-tools/truststore.p12` (password `changeit`), built from `/etc/ssl/certs/ca-certificates.crt`. The MITM proxy passes Google hosts through with their real chains, whose root is missing from the JDK's bundled cacerts (118 roots) but present in the system bundle (121) — without this, dl.google.com dies with PKIX errors and AGP's plugin marker never resolves (and Gradle then caches the miss — clear with `--refresh-dependencies` if it ever recurs).
4. **SDK licenses:** the zip-assembled SDK had no `licenses/` dir; AGP refused `platforms;android-34` until the standard sdkmanager license-hash files were written into `~/android-sdk/licenses/`.

**Toolchain actually used (differs from Phase 1 assumptions, both changes forced):**

- **Gradle 8.7** (`~/workspace/snes9x-ra/tools/gradle`), NOT upstream CI's 8.10.2: 8.10.2 refuses the in-process reuse trick in this sandbox (forks a daemon, which then dies per #1). AGP 8.5.2 supports 8.7 (it is AGP 8.5's minimum), so this is in-spec.
- **NDK r27c (27.2.12479018, clang 18)**, downloaded from dl.google.com (zip kept at `~/workspace/gba-ds-tools/ndk-r27c.zip`) and installed at `~/android-sdk/ndk/27.2.12479018`. NDK 26's clang 17 **cannot compile upstream v1.5 at all**: `src/ui.c:1081` has a declaration directly after a `case` label, legal only from C23 / clang 18 (upstream CI builds with the latest NDK, which is why they never see it). The build driver exports `ANDROID_NDK_HOME` pointing at r27c.
- JDK 17.0.21 (`~/workspace/snes9x-ra/tools/jdk`), SDK platform android-34 + build-tools 36 — as planned.

### 8.2 DONE — deps, overlay, patches, keystore

- **Deps fetched** (per `scripts/build-android.sh`): `deps/mgba` at pinned commit `143adf5aa2a60c6036810b0cd72445e13d74b0e6` (144 MB), `deps/SDL2` branch `release-2.30.x` (78 MB). libmgba builds cleanly for arm64-v8a.
- **`motog/gba-ds.cfg`** — written. Keys verified against the parser in `src/config.c`: `layout = single`, `map = 0`, `show_all = 1`, `shader = off`, `rewind = 0`, `update_check = 0`, `touch_controls = auto`.
- **Patch 0001 (phone defaults) — DONE, applied, and saved as a patch file:** `motog/patches/0001-phone-defaults.patch` — an 11-line hunk at the top of `android_config_defaults()` in `src/android.c` pinning the same values as compiled-in defaults (`layout="single"`, `map=false`, `showAll=true`, `shader=SH_OFF`, `rewind=false`, `updateCheck=false`).
- **Signing keystore — DONE:** `motog/motog.keystore` (RSA 4096, alias `motog`, 30-year validity, DN "CN=GBA DS Moto G Edition, O=Personal Build"). The password exists ONLY in `motog/keystore-password.txt` (mode 600), per the agreed design.
- **Patch 0002 (touch controls) — fully written, applied, and COMPILE-VERIFIED, but NOT yet saved as a patch file** (that is resume step 1). What it contains:
  - **New files** `src/touchpad.c` + `src/touchpad.h` (complete): translucent overlay (D-pad with diagonals, A/B/X/Y diamond, L/R shoulder bars, Start/Select pills, ~40% opacity); fingers polled per finger-id via SDL touch state and converted to screen-local pixels exactly as `ui.c`'s `queue_pointer()` maps them (both `touch_map` modes); pressed state folded into `btnNow[]`, so hotkeys and the Start+Select pause-menu combo see touch presses as pad presses. Auto-hide per the brief: `touch_controls = auto|on|off` (default auto — hides 2.5 s after the last physical gamepad press, returns on the next screen touch). Menu-safe: `touchpad_poll(cfg, menuOpen, …)` disarms the pad while the pause menu is open (no injection, no drawing, no pointer claims there).
  - **Hook hunks:** `src/config.h` (`char touchControls[8]` field), `src/config.c` (`STR("touch_controls", …)` + `"auto"` default), `src/ui.h` + `src/ui.c` (a `UiPointerFilter` hook: `ui_next_pointer()` skips events the filter claims — this is what stops a tap on a pad button from also opening the pause menu; the touchpad's filter self-disarms outside the game loop via a 150 ms staleness window, so the library is never affected), `src/game.c` (include, filter registration in `game_run`, `touchpad_poll()` right after `ui_read_buttons()`, `touchpad_draw()` after `draw_bar()` before `ui_present()`), `android/app/src/main/cpp/CMakeLists.txt` and `Makefile` (both list `touchpad.c` explicitly — upstream lists sources by name, no glob).
  - **Compile verdict:** the whole patched tree built `BUILD SUCCESSFUL in 57s` (log `build-logs/dev-build1.log`; `touchpad.c.o` present under `android/app/.cxx/…/arm64-v8a/`), producing a debug-signed patched APK at `dist/gba-ds-v1.5-android.apk` (5,613,228 bytes, 15:02 — it overwrote the stock APK at the same path; the stock result above stands on its log + badging record). **Runtime behaviour on a phone is NOT yet tested** — compile-verified only.
  - Deliberate deviation: no pause-menu toggle row (config-only, as the brief allowed — the pause menu is a large hand-rolled UI). The pad code compiles on all platforms (inert without touch hardware) rather than `#ifdef __ANDROID__`-ing out.
- **Build driver:** `build-logs/run-build.sh` (exports the shim PATH, JDK, NDK r27c, Gradle 8.7, `GRADLE_USER_HOME=/home/hatch/.gradle` — the JVM ignores `$HOME` and would otherwise use `/root/.gradle`) drives `scripts/build-android.sh <abi>` in any tree. Logs for every attempt are in `build-logs/`.

### 8.3 Working-tree state at the pause (precise)

- HEAD is still the pristine v1.5 tag commit; nothing is committed or staged.
- **Modified tracked files (8, all complete edits — no half-edited file exists):** `src/android.c` (patch 0001), `src/config.c`, `src/config.h`, `src/ui.c`, `src/ui.h`, `src/game.c` (patch 0002), `android/app/src/main/cpp/CMakeLists.txt`, `Makefile`.
- **Untracked:** `src/touchpad.c`, `src/touchpad.h` (new, patch 0002), `motog/` (`gba-ds.cfg`, `motog.keystore`, `keystore-password.txt`, `patches/0001-phone-defaults.patch` — and an empty slot where 0002 goes), `build-logs/` (driver scripts + all build logs), `FINDINGS.md` itself.
- Patch 0001 exists BOTH as the applied tree edit and as `motog/patches/0001-phone-defaults.patch`. Patch 0002 exists ONLY as tree edits (see resume step 1).
- No build is running; the lingering Gradle JVM from the completed dev build was killed at the pause.

### 8.4 What remains, in order

1. **Save patch 0002 as a patch file** (tree edits already applied and compile-verified — just capture them; command in §8.5).
2. **Patch 0003 (scan UX) — assessed, small, not written.** In `library_run()` (`src/library.c`, ~line 1615), between `storage_check(cfg);` and the first `scan(cfg);`, present one frame: `ui_clear()` + `ui_label()` "Scanning…" centred on `ui_touch_screen()` + `ui_present()` (~6 lines; helpers and the `C_TEXT` colour constant are already used the same way by the list's own "No games found" label). Then capture as `motog/patches/0003-scan-frame.patch`. If it misbehaves it can be dropped without touching 0001/0002.
3. **Write `motog/update-and-build.sh`** implementing §6's flow: resolve tag (default `v1.5`) → `git worktree add` a pristine checkout at `motog/work` → symlink `deps/` from the main tree → `git apply motog/patches/*.patch` in order (loud stop on conflict) → export signing env (`GBADS_KEYSTORE=$PWD/motog/motog.keystore`, `GBADS_KEYSTORE_PASS`/`GBADS_KEY_PASS` from `motog/keystore-password.txt`, `GBADS_KEY_ALIAS=motog`) and the same toolchain env as `build-logs/run-build.sh` (parameterise that driver by tree, or fold its exports into the script) → run `scripts/build-android.sh arm64-v8a` in the worktree → copy the APK out. The Gradle shim (`build-logs/gradle-inproc.sh` + `~/workspace/gba-ds-tools/bin/gradle`) should move under `motog/tools/` so the flow is self-contained.
4. **Produce the deliverable through `motog/update-and-build.sh`** (not by hand), then verify: `apksigner verify --print-certs` (expect the motog cert, SHA-256 different from upstream's — intended) and `aapt2 dump badging` (package `io.github.kyannon.gbads`, versionName `1.5`).
5. **Deliverable copy:** name it exactly `GBA DS v1.5 Moto G.apk` in `~/workspace/gba-ds-src/`, and finish this section with the results.
6. **On-phone install steps (for Layne, when it ships):** (a) uninstall the official GBA DS app first — our signature differs, so Android will refuse an over-install; the shared **`GBA DS/` data folder survives** the uninstall (it's on shared storage, not app-private). (b) Install `GBA DS v1.5 Moto G.apk`. (c) Copy `motog/gba-ds.cfg` to **`/storage/emulated/0/GBA DS/gba-ds.cfg`** (the path `android_home()` + `find_folder()` resolve to: the data folder root, beside `gba-ds.log`) — belt-and-braces, since patch 0001 already compiles the same values in; the file matters if an old cfg with other values is already there. (d) First launch: point it at a small test ROM folder before the big one.

### 8.5 Exact resume commands

```sh
# 0. Sanity: tree state should match §8.3
cd ~/workspace/gba-ds-src && git status --porcelain

# 1. Capture patch 0002 from the (already applied, compile-verified) edits
git add src/touchpad.c src/touchpad.h src/config.h src/config.c src/ui.h src/ui.c \
        src/game.c android/app/src/main/cpp/CMakeLists.txt Makefile
git diff --cached > motog/patches/0002-touch-controls.patch
git reset -q

# 2. (Patch 0003: edit src/library.c per §8.4 step 2, then the same
#     git add / diff --cached / reset dance into 0003-scan-frame.patch.)

# 3. Fast dev rebuild of the main tree at any point (debug-signed):
build-logs/run-build.sh arm64-v8a

# 4. Signed deliverable, once update-and-build.sh exists (§8.4 step 3):
motog/update-and-build.sh            # default tag v1.5, arm64-v8a

# 5. Verify + deliver
~/android-sdk/build-tools/36.0.0/apksigner verify --print-certs "GBA DS v1.5 Moto G.apk"
~/android-sdk/build-tools/36.0.0/aapt2 dump badging "GBA DS v1.5 Moto G.apk" | head -3
```

*End of Phase 2 checkpoint (paused).*

---

## 9. Phase 2 — RESUMED AND COMPLETED (2026-11-13)

**STATUS: COMPLETE. The deliverable `GBA DS v1.5 Moto G.apk` was built end-to-end through `motog/update-and-build.sh` from a pristine v1.5 worktree and verified. On-phone runtime testing is Layne's step (no device here).**

### 9.1 What was done on resume (checkpoint §8.4 order)

1. **Patch 0002 captured:** `motog/patches/0002-touch-controls.patch` (419 lines, 18,762 bytes) via the exact §8.5 commands. Contents verified: Makefile, CMakeLists, config.c/h, game.c, ui.c/h, plus new files `src/touchpad.c` / `src/touchpad.h`. No bleed from patch 0001 (`src/android.c` is not in it).
2. **Patch 0003 (scan frame) written, applied, compile-verified, captured:** `motog/patches/0003-scan-frame.patch` (694 bytes). In `library_run()` (`src/library.c`), between `storage_check(cfg)` and the first `scan(cfg)`: `ui_touch_screen()` + `ui_clear()` + centred `ui_label(..., "Scanning...", C_TEXT)` + `ui_present()` — 9 added lines, mirroring the list's own "No games found" idiom (`screens[scr].rect`, `ui_px(scr) * 6`). ASCII `...` instead of the assessed `…` (no other `…` exists in `src/*.c`; zero glyph risk in the bundled Jost). Dev rebuild of the main tree: **BUILD SUCCESSFUL in 33s** (`build-logs/dev-build2.log`).
3. **`motog/update-and-build.sh` written** implementing the §6.3 flow: fetch tag (default `v1.5`; falls back to the local tag with a warning if the fetch fails) → `git worktree add` pristine checkout at `motog/work` (stale worktree removed first, so re-runs are idempotent) → `deps/` symlinked from the main tree → `git apply` of `motog/patches/*.patch` in filename order with a loud stop on conflict → upstream `scripts/build-android.sh` with the signing env + the sandbox toolchain env (same values as `build-logs/run-build.sh`) → APK copied out as `GBA DS <tag> Moto G.apk` in the repo root. The Gradle shim was **copied** (not moved) under `motog/tools/` (`gradle-inproc.sh` + `tools/bin/gradle`), so the flow is self-contained without breaking `build-logs/run-build.sh` or the `~/workspace/gba-ds-tools/bin/gradle` shim, which still point at the originals.
   - **Correction to §8.4:** the signing-password variable is `GBADS_KEYSTORE_PASSWORD`, not `GBADS_KEYSTORE_PASS` (per `scripts/build-android.sh`'s header and `android/app/build.gradle`). The script exports `GBADS_KEYSTORE`, `GBADS_KEYSTORE_PASSWORD`, `GBADS_KEY_ALIAS=motog`, `GBADS_KEY_PASSWORD` — both passwords read from `motog/keystore-password.txt`.
4. **Deliverable produced through the script** (`build-logs/motog-build1.log`): fetch of `v1.5` from origin OK, worktree at `7189176 Release v1.5`, all three patches applied cleanly in order, **BUILD SUCCESSFUL in 2m**, script exit 0.

### 9.2 Verification of the deliverable

- **File:** `~/workspace/gba-ds-src/GBA DS v1.5 Moto G.apk`, **5,613,836 bytes**.
- **`apksigner verify --print-certs`:** passes. Signer #1 DN `CN=GBA DS Moto G Edition, O=Personal Build`; **SHA-256 `e985d4bd2cb25780b6f2b41ebdcd230cda42208e120d6253f4e7770ee67f439f`** — our motog key, deliberately different from upstream's signer (and from the house debug cert `94690219…`).
- **`aapt2 dump badging`:** package `io.github.kyannon.gbads`, versionCode `1`, versionName `1.5`, minSdk 26, targetSdk 34. (versionCode 1 is upstream's Gradle value; the official APK's 509 came from their CI numbering. It doesn't matter here: the signature change already forces a fresh install, §9.4.)
- The patch layer reproduces the tree: pristine tag + `motog/patches/0001–0003` + `motog/` overlay = the build above. The main working tree still carries the same edits uncommitted (identical content), so dev iteration can continue there with `build-logs/run-build.sh`.

### 9.3 Final patch inventory

| Patch | File | Contents |
|---|---|---|
| 0001 | `motog/patches/0001-phone-defaults.patch` (1,022 B) | `src/android.c`: phone defaults compiled in (single layout, shadow core off, show_all, shader/rewind off, update_check off) |
| 0002 | `motog/patches/0002-touch-controls.patch` (18,762 B) | New `src/touchpad.c/.h` translucent on-screen gamepad (D-pad + A/B/X/Y + L/R + Start/Select, `touch_controls=auto|on|off`, menu-safe) + hooks in config/ui/game + CMake/Makefile source lists |
| 0003 | `motog/patches/0003-scan-frame.patch` (694 B) | `src/library.c`: "Scanning..." frame presented before the first folder scan — the black-screen fix |

### 9.4 On-phone install steps (for Layne)

1. **Uninstall the official GBA DS app first.** Our build is signed with the motog key, so Android will refuse to install it over kyannon's signature. The shared **`GBA DS/` data folder on the phone's storage survives the uninstall** (it lives on shared storage, not in app-private data) — saves, states and settings in it are kept.
2. **Install `GBA DS v1.5 Moto G.apk`.**
3. **Copy `motog/gba-ds.cfg` to `/storage/emulated/0/GBA DS/gba-ds.cfg`** (the data-folder root, beside `gba-ds.log`). Belt-and-braces: patch 0001 already compiles the same values in as defaults, but the file wins if an old cfg with different values is already sitting there.
4. **First launch: point it at a small GBA-only test folder first** (unzipped `.gba` files), confirm games list and play, *then* point it at the big folder if wanted — with patch 0003 the scan now shows "Scanning..." instead of a black screen, but a huge folder full of zips still takes a while to scan.

### 9.5 Honest caveats

- **Compile-verified, not device-verified.** The touch overlay (patch 0002) and the scan frame (patch 0003) have never run on the Moto G. First on-device checks: pad appears in-game and hides when a physical pad is used; taps on pad buttons don't open the pause menu; the "Scanning..." label really does appear before a long scan finishes.
- `motog/work/` (the build worktree) is left in place from the deliverable build; `update-and-build.sh` removes and recreates it on the next run.

*End of Phase 2 (completed).*

---

## 10. Patch 0004 — file-picker-first start-up (2026-11-13)

**STATUS: COMPLETE. The deliverable was rebuilt through `motog/update-and-build.sh` (pristine v1.5 + patches 0001–0004) and verified; it installs as an update over the §9 build (same motog signer). Layne's pick on 2026-11-13, in his words: "have it pick a file, not a folder. Every time it starts" — chosen after the §9 build hung on "Scanning..." against his real ROM folder on-device (diagnosis §4 #1, confirmed).**

### 10.1 What the patch changes (`motog/patches/0004-file-picker-start.patch`, 9,883 B, 5 files)

A first attempt at this patch was closed mid-flight earlier the same day; its tree edits were reviewed line-by-line against the existing picker patterns before being adopted (see §10.3), then captured cleanly against the 0001–0003 base — the patch file contains picker hunks only.

- **`src/library.c` — `library_run()`, new block at the top, `#ifdef __ANDROID__`:** runs **once per process** (`static bool startupPickDone`), before `storage_check()` and before any `scan()`. Calls `android_pick_file()`, then waits, pumping `ui_events()` and presenting a "Choose a game in Android's file picker..." frame every ~50 ms, until the answer arrives. SDL parks this thread while the picker activity is in front (`SDL_APP_WILLENTERBACKGROUND`), so the wait costs nothing and the app can never sit on a black screen behind the picker. Outcomes:
  - **Picked + a game name** (`emu_is_rom_name()` — the exact predicate the library scan lists by: `.gba`, `.zip`, `.7z`): writes `last_game.txt` in `stateDir` (same as the list flow), copies the path to `outPath`, returns `true`.
  - **Picked but not a game file:** logs it, falls through to the library.
  - **Cancelled / picker unavailable:** falls through to the library — `storage_check()` → patch 0003's "Scanning..." frame → `scan()` → the list, exactly as before. Leaving a game also lands on the list (the block never runs twice in one process); the picker is a **start-up** gate only.
- **`src/android.c` / `src/android.h` — `android_pick_file()` + `android_take_picked_file()` + JNI `nativeFilePicked`:** a deliberate clone of the proven folder-picker trio (`android_pick_folder` / `android_take_picked_folder` / `nativeFolderPicked`) with its **own** mutex/state (`filePickLock`/`filePickState`), so the two pickers can never cross-talk. Same shape: `0` no answer yet, `1` picked (path in out), `-1` cancelled.
- **`GbaDsActivity.java` — `pickRomFile()` + `onFilePicked()`:** `ACTION_OPEN_DOCUMENT` / `CATEGORY_OPENABLE` (request code 4712, handled first in `onActivityResult`). Type is `*/*` with `EXTRA_MIME_TYPES` = zip / 7z / octet-stream: `.gba` files report as octet-stream or nothing at all on most providers, so a narrow type filter would hide the very files being picked — the native side gates on the file name instead. Resolution runs on a worker thread (`gbads-pick`); a picker that can't even open reports `""` → native cancel → library.
- **`LauncherActivity.java` — one visibility change:** `fromUri()` is now package-visible (was `private static`) so the activity's picker resolves through it too. No behaviour change.

### 10.2 How the launch path is reused (no new emulation code)

The picked file returns through **`library_run()`'s ordinary return contract**: `main()` (`src/main.c`) takes the path and calls `game_run(&cfg, rom, /*fromLibrary=*/true)` — the identical hop a tap on the game list takes, with `last_game.txt` written the same way, so saves, states and "last played" reselect behave as if the game had been picked from the list. Nothing about loading changed: `game_run` → `emu` opens plain files directly and archives via `VDirOpenArchive`, so a picked `.zip`/`.7z` boots the same as a listed one.

**URI → readable path** reuses the app's existing, device-proven resolver for externally-opened ROMs — `LauncherActivity.fromUri()` (the frontend/VIEW-intent path): `file://` → the path; an external-storage document → its plain path (readable with the All-files-access grant the launcher already secures; the battery save stays beside the ROM); other providers → their real `_data` path when readable; otherwise the file is **copied into the app's `launched` folder** via the ContentResolver at pick time. Because resolution produces a real file (or a local copy) immediately, no long-lived SAF grant is needed — the temporary pick grant covers the read, and nothing references the content URI afterwards.

### 10.3 Review notes on the adopted draft

Checked before capture: the native trio is a faithful pattern-clone of the folder picker (same `GetObjectClass` JNI lookup, exception clearing, ref hygiene); the Java resolver, request-code plumbing and off-UI-thread copy mirror the launcher's own frontend handling; `emu_is_rom_name`, `ui_events`, `ui_label`/`ui_px`/`C_TEXT` and the `screens[]` geometry are the same helpers patches 0001–0003 and the list itself already use. Two deliberate behaviours worth knowing: if the app is told to quit while waiting, the loop breaks and control still passes through the library entry (whose own event handling then processes the quit) — no hang, no black screen; and a picked archive is accepted on its name alone, exactly as the library lists archives — a zip with no GBA ROM inside fails inside `game_run` the same way a listed one would.

### 10.4 Verification

- **Dev build of the patched main tree** (`build-logs/run-build.sh`, `build-logs/dev-build3.log`): BUILD SUCCESSFUL — Java + C picker code compile-verified before capture.
- **Deliverable built through `motog/update-and-build.sh`** (`build-logs/motog-build2.log`): pristine `7189176 Release v1.5` worktree, patches **0001–0004 applied cleanly in order**, **BUILD SUCCESSFUL in 2m 2s**, script exit 0.
- **File:** `~/workspace/gba-ds-src/GBA DS v1.5 Moto G.apk`, **5,615,292 bytes** (+1,456 over the 0001–0003 build — the picker code).
- **`apksigner verify --print-certs`:** passes. Signer DN `CN=GBA DS Moto G Edition, O=Personal Build`, **SHA-256 `e985d4bd2cb25780b6f2b41ebdcd230cda42208e120d6253f4e7770ee67f439f`** — byte-identical to the §9 build's cert, so this **installs as an update** over the build already on Layne's phone (no uninstall, data folder untouched).
- **`aapt2 dump badging`:** package `io.github.kyannon.gbads`, versionCode `1`, versionName `1.5`, minSdk 26, targetSdk 34 (unchanged from §9).
- **Artifact content proof:** the shipped APK's `classes.dex` contains `pickRomFile` and its `libmain.so` contains the "Choose a game in Android's file picker..." string — the patch is in the binary, not just the tree.
- The scratch `motog/patchwork/` worktree used to capture the patch was removed afterwards; the main tree still carries the same 0001–0004 edits uncommitted for dev iteration.

### 10.5 On-device checks (Layne's — no Android device/emulator here can prove the UI flow)

1. Install the new APK over the current Moto G build (plain update — same signature).
2. Cold start: Android's file picker should appear **before** any scanning; pick a `.gba` → the game boots.
3. Cold start again → picker → **Back/cancel** → the game list appears (with "Scanning..." while it loads the folder, as before).
4. Pick a zipped game → it should boot from the archive.
5. Exit the game → lands on the list (no second picker until the app is fully restarted).

*End of §10.*

---

## 11. Patch 0005 — two screens in the one phone window (2026-11-13)

**STATUS: COMPLETE. The deliverable was rebuilt through `motog/update-and-build.sh` (pristine v1.5 + patches 0001–0005) and verified; it installs as an update over the §10 build (same motog signer). Layne's pick on 2026-11-13, after the speed tradeoff was put to him plainly: both of the app's screens on the phone — game + live map, side by side. This reverses §5's founding design rule ("one screen, one emulator") and patch 0001's layout/map defaults, on his explicit say-so.**

### 11.1 What the patch changes (`motog/patches/0005-dual-screen-layout.patch`, 4,121 B, 3 files)

- **`src/android.c` — `android_config_defaults()` (the patch-0001 hunk, amended):** `layout` default `"single"` → `"span-h"`, `map` `false` → `true`; the 0001 comment is annotated so the tree doesn't claim the old behaviour. All other 0001 values (show_all, shader off, rewind off, update_check off) untouched.
- **`src/android.c` + `src/android.h` — new `android_config_finalise(Config*)`:** sets `layout = "span-h"`, `map = true` and logs `motog: two screens in one window (layout span-h, map on)` to stderr (→ `gba-ds.log` on the phone).
- **`src/main.c` — one `#ifdef __ANDROID__` call** to `android_config_finalise(&cfg)` immediately after the last config load (hotkeys.cfg + the second `config_parse_args`), before `ui_init`. Placement is the whole point — see §11.2.
- **`motog/gba-ds.cfg` (overlay):** `layout = span-h`, `map = 1`, comments rewritten: the two keys are documentation now — the binary pins them (see below).

### 11.2 The load-order finding (why compiled defaults alone would NOT have worked)

Load order in `main()` (`src/main.c`): `config_defaults()` → `android_config_defaults()` → **`gba-ds.cfg`** → args → **`states/prefs.cfg`** → **`states/library.cfg`** → **`states/hotkeys.cfg`** → args again → `ui_init()`. Every file loads *after* the compiled defaults, so any file containing `layout` or `map` beats patch 0001/0005's defaults. Checking who can write those keys:

- **`gba-ds.cfg` — the real risk.** §9.4 step 3 had Layne copy the old motog cfg onto the phone; that file pins `layout = single` / `map = 0`. Had 0005 only changed compiled defaults, an update installed over his current build would very likely have booted still single-screen, silently. (Whether he actually copied it is unknown — the pin removes the question.)
- **`prefs.cfg` — cannot.** `save_prefs()` (`src/game.c`) writes a fixed key list (swap, scale, map_scale, shader, overlays, rewind, …); `layout` and `map` are not in it. Upstream's own snapshot/restore block around the prefs load (protecting map_animations / map_interval / show_fps / show_logo — "gba-ds.cfg decides, whatever an older prefs.cfg says") is the same defensive idea, one file earlier; 0005's pin sits after *all* files instead, which also covers the stale-cfg case.
- **`library.cfg` — cannot.** `save_library_cfg()` writes only library_view / rom_dir / save_dir / save_ext.
- **`hotkeys.cfg` — cannot.** Its writer emits only the hotkey bindings.
- Nothing else in the tree writes `cfg->layout` or `cfg->map` (grep: only `config_defaults` and the Android defaults), and no menu persists them. So pinning the two fields once, after the final load, is complete and cannot fight a setting the player made in a menu — there is no such setting. The cost: on Android these two keys are now edition-fixed; changing them means a new build, by design.

### 11.3 What the two screens actually do (code-verified)

- **Geometry:** the phone has one display, so `ui_init` leaves `nViews = 1`. `layout_screens()` (`src/ui.c`) branches on the literal `"span-h"`: `screens[0] = {0, 0, 806, 720}`, `screens[1] = {806, 0, 806, 720}` at the Moto G's 1612×720 renderer size. (Upstream's `"auto"` would resolve to the same span at this aspect — 1612 ≥ 720×18/10 — but `"span-h"` is the deterministic branch value, independent of aspect math.) `game_screen()` is 0 while `swap` is off, so **the game is on the left half; the second screen is the right half**. The swap-screens hotkey still exchanges them within a session.
- **The right half, per game** (`draw_game()` in `src/game.c`: `mapOn = (mc || logoOnly) && cfg->map`): `src/profiles.c` carries **55 profiles — 40 full-map, 15 logo-only**:
  - *Full map profile (40 games):* the **live map**, captured by the shadow core (`mapcap_create`); until the first capture exists (title screen, intros), the game's extracted logo shows there instead (`show_logo` defaults on).
  - *Logo-only profile (15 games):* no map exists for these by design — **the game's logo stands in for the map** (or its title block if no logo has been extracted yet).
  - *No profile (everything else):* `mapcap_create` returns NULL, `mapOn` is false, and **the right half is simply black** (in its bezel, if one is drawn). There is no fallback picture. Plainly: for most of a big ROM folder, the second pane buys nothing — it exists for the 40 mapped games.
- **Library:** the list draws on the touch screen (`ui_touch_screen()` = screens[1], the right half) and, with `nScreens > 1`, the selected game's preview goes on the other (left) half — upstream's two-screen list design, inherited as-is. The 0004 start-up picker is unaffected (it runs before any of this matters, in its own activity).
- **Touch controls (0002):** the pad converts fingers to screen-local pixels within the game screen's rect, so in span mode it lives **inside the left (game) pane** — half the width it had. Whether the D-pad/buttons are comfortably sized at 806 px wide is an on-device judgement (§11.5).

### 11.4 Verification

- **Dev build of the patched main tree** (`build-logs/run-build.sh`, `build-logs/dev-build4.log`): BUILD SUCCESSFUL in 48 s — 0005 compile-verified before capture. The patch was captured against a scratch worktree holding pristine v1.5 + 0001–0004 (the §10.3 method; worktree removed afterwards) and contains only the three 0005 file hunks — no 0001–0004 bleed.
- **Deliverable built through `motog/update-and-build.sh`** (`build-logs/motog-build3.log`): pristine `7189176 Release v1.5` worktree, patches **0001–0005 applied cleanly in order**, **BUILD SUCCESSFUL in 2m 2s**, script exit 0.
- **File:** `~/workspace/gba-ds-src/GBA DS v1.5 Moto G.apk`, **5,615,620 bytes** (+328 over the 0001–0004 build — the finalise function and its log string).
- **`apksigner verify --print-certs`:** passes. Signer DN `CN=GBA DS Moto G Edition, O=Personal Build`, **SHA-256 `e985d4bd2cb25780b6f2b41ebdcd230cda42208e120d6253f4e7770ee67f439f`** — identical to the §9/§10 builds, so this **installs as a plain update**; no uninstall, no data-folder step, and **no manual config step of any kind** (the §11.2 pin is what makes that true even if the old cfg is still sitting in `GBA DS/`).
- **`aapt2 dump badging`:** package `io.github.kyannon.gbads`, versionCode `1`, versionName `1.5`, minSdk 26, targetSdk 34 (unchanged).
- **Artifact content proof:** the shipped APK's `lib/arm64-v8a/libmain.so` contains the string `motog: two screens in one window (layout span-h, map on)` — the pin is in the binary, and the same line will appear in `GBA DS/gba-ds.log` on the phone at every start, as a field check.
- Patch inventory now: 0001 phone defaults (layout/map values superseded by 0005), 0002 touch controls, 0003 scan frame, 0004 file-picker start, **0005 dual-screen layout**.

### 11.5 Honest costs & on-device checks (Layne's)

- **Performance:** the shadow core is a second full mGBA instance receiving periodic state copies plus capture work — §5 called it the single biggest CPU/RAM cost on this 4 GB / Snapdragon 4 Gen 1 phone, and that hasn't changed; Layne accepted it knowingly. Expect more heat and possibly frame pacing dips in the 40 mapped games; unprofiled games pay the (smaller) cost of the split layout only — `mapcap_create` is never created for them, though `cfg->map` still gates a profile lookup per game start. If a favourite game runs badly, there is no in-app off switch any more (the pin, §11.2) — a single-screen variant would be a new patch/build decision.
- **On-device checks:** (1) install over the current build (plain update). (2) Cold start: `gba-ds.log` shows the `motog: two screens in one window` line; the start-up picker still appears first. (3) Library: list on the right half, selected game's preview on the left. (4) A mapped game (e.g. a Castlevania — both GBA Castlevanias are full-map profiles): game left, logo then live map right once the map is opened in-game. (5) An unprofiled game: game left, right half black — expected, not a bug. (6) Touch pad: appears within the left pane, usable size; auto-hide on a physical gamepad still works. (7) Swap-screens hotkey exchanges the panes.

*End of §11.*

---

## 12. Patch 0006 — portrait layout (the two screens stacked) (2026-11-13)

**STATUS: COMPLETE. The deliverable was rebuilt through `motog/update-and-build.sh` (pristine v1.5 + patches 0001–0006) and verified; it installs as an update over the §11 build (same motog signer). Layne's direction, minutes after the §11 build landed: "just like that but make it a portrait layout" — same two screens, phone held upright, stacked like a real DS: game on top, map below. This amends patch 0005's geometry and reverses upstream's landscape lock, on his explicit say-so.**

### 12.1 What the patch changes (`motog/patches/0006-portrait-layout.patch`, 4,966 B, 3 files, plus the cfg overlay)

- **`src/android.c` — the 0005 pin, value amended:** `android_config_finalise()` and the `android_config_defaults()` layout default now pin `"span-v"` instead of `"span-h"`; the log line reads `motog: two screens in one window (layout span-v, map on)`. Nothing about the enforcement changed — §11.2's post-load pin still runs after the last config load in `main.c`, so a gba-ds.cfg on the phone (span-h or anything else) still loses. `map` stays `true`.
- **`android/app/src/main/AndroidManifest.xml` — GbaDsActivity:** `android:screenOrientation="landscape"` → `"portrait"`.
- **`src/ui.c` — `ui_init()`, `#ifdef __ANDROID__`:** `SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft")` → `"Portrait"`.
- **`motog/gba-ds.cfg` (overlay):** `layout = span-v`, comments rewritten for the stack (documentation only — the binary pins it, as in §11).

### 12.2 Why the orientation takes three changes, not one

The window is created `SDL_WINDOW_FULLSCREEN_DESKTOP` (`create_view`), so the renderer size simply follows the activity's real size — whatever makes the *activity* portrait makes the window 720×1612. Two things decide that, and they'd fight if they disagreed:

- The **manifest** value is what Android creates the activity with.
- But at window creation SDL's Java side (`SDLActivity.setOrientationBis`, called by JNI with the native hint) computes an orientation from `SDL_HINT_ORIENTATIONS` and calls `setRequestedOrientation()` — with the old `"LandscapeLeft"` hint it would have requested landscape at runtime *over* a portrait manifest. (Detection quirk, visible in the SDL source: a portrait hint only counts if the string contains `"Portrait "` or *ends with* `"Portrait"` — the bare string `"Portrait"` qualifies.) Both are changed, so manifest, hint and runtime request all say portrait.

`LauncherActivity` has no orientation lock and needs none — it hands off to GbaDsActivity in its own task. The 0004 file picker is a system activity; it presents over the portrait activity and is unaffected.

### 12.3 Geometry and ordering (code-verified)

- `layout_screens()` (`src/ui.c`), literal `"span-v"` branch at a 720×1612 renderer: `screens[0] = {0, 0, 720, 806}` (top half), `screens[1] = {0, 806, 720, 806}` (bottom half).
- **Order is DS-correct with no fix needed:** `game_screen()` (`src/game.c`) is 0 while `swap` is off, so the game draws on screens[0] — the **top**. The second screen (live map / logo / black, per §11.3's profile rules, unchanged) is screens[1] — the **bottom**. The swap-screens hotkey still exchanges them within a session, as before.
- **Library in portrait:** the list draws on the touch screen (`ui_touch_screen()` = screens[1]) and the selected game's preview on `ui_other_screen()` (screens[0]) — so **preview on top, list on the bottom half**, the span-h arrangement rotated. All library drawing goes through the screens' rects generically (including patch 0003's "Scanning..." frame, which centres on the touch screen — now the bottom half), and the list pane at 720×806 is comparable in area to span-h's 806×720. Nothing in the library is landscape-shaped by construction; it renders sanely by the same rect logic §11 verified for span-h.
- **Touch controls (0002):** `touchpad_poll`/`touchpad_draw` are called with `game_screen()` and all geometry derives from that screen's rect (`tp_layout()`), so the pad now lives **inside the top (game) pane**, screen-local as before. The pane is squarer than span-h's (720×806 vs 806×720); pad geometry scales from the pane's own w/h, but whether the D-pad/buttons are comfortably sized there is an on-device judgement (§12.5).

### 12.4 Verification

- **Dev build of the patched main tree** (`build-logs/run-build.sh`, `build-logs/dev-build5.log`): BUILD SUCCESSFUL in 34 s — 0006 compile-verified before capture; its APK's compiled manifest already showed `screenOrientation=1`.
- **Patch capture:** against a scratch worktree holding pristine v1.5 + 0001–0005 (the §10.3 method; worktree removed afterwards) — the patch contains only the three 0006 file hunks, no earlier-patch bleed.
- **Deliverable built through `motog/update-and-build.sh`** (`build-logs/motog-build4.log`): pristine `7189176 Release v1.5` worktree, patches **0001–0006 applied cleanly in order**, **BUILD SUCCESSFUL in 2m 2s**, script exit 0.
- **File:** `~/workspace/gba-ds-src/GBA DS v1.5 Moto G.apk`, **5,615,620 bytes** — byte-size identical to the §11 build (the layout strings are the same length and the orientation changes are value swaps in compiled tables; the content differs, verified below).
- **`apksigner verify --print-certs`:** passes. Signer DN `CN=GBA DS Moto G Edition, O=Personal Build`, **SHA-256 `e985d4bd2cb25780b6f2b41ebdcd230cda42208e120d6253f4e7770ee67f439f`** — identical to the §9/§10/§11 builds, so this **installs as a plain update**; no uninstall, no data-folder step, no manual config step.
- **`aapt2 dump badging`:** package `io.github.kyannon.gbads`, versionCode `1`, versionName `1.5`, minSdk 26, targetSdk 34 (unchanged).
- **Artifact content proof:** the shipped APK's compiled manifest carries `android:screenOrientation = 1` (portrait; landscape is 0) on GbaDsActivity (`aapt2 dump xmltree`), and its `lib/arm64-v8a/libmain.so` contains the string `motog: two screens in one window (layout span-v, map on)` — the pin is in the binary, and the same line will appear in `GBA DS/gba-ds.log` on the phone at every start, as a field check.
- Patch inventory now: 0001 phone defaults (layout/map values superseded by 0005, then 0006), 0002 touch controls, 0003 scan frame, 0004 file-picker start, 0005 dual-screen layout (span-h geometry superseded by 0006), **0006 portrait layout**.

### 12.5 Honest costs & on-device checks (Layne's)

- **Unchanged from §11:** the shadow core's CPU/RAM cost stands (it is what draws the map), and the second pane still only has content for the 55 profiled games — 40 live maps, 15 logos, black for everything else. Layout and map remain edition-fixed: no menu or config file can switch back to landscape or single-screen; that would be a new build, by design (§11.2).
- **New in portrait, unproven until it runs:** upstream never validated this app on a portrait phone at all (§3 #3) — every screen (pause menu, quick bar, settings pages, the mapper) draws inside the screens' rects generically, but text/pane sizing in an 720×806 pane has only ever been exercised in landscape halves. The file picker, library list and in-game flow are the same code paths as the verified span-h build with rotated rects; the residual risk is cosmetic (crowded menus), not structural.
- **On-device checks:** (1) install over the current build (plain update). (2) Cold start: the phone stays portrait; `gba-ds.log` shows the `motog: two screens in one window (layout span-v, map on)` line; the start-up picker appears, in portrait. (3) Library: selected game's preview in the top half, the list in the bottom half; "Scanning..." appears in the bottom half on a cold folder scan. (4) A mapped game (a Castlevania): game on top, logo then live map below once the map is opened in-game. (5) An unprofiled game: game on top, bottom half black — expected, not a bug. (6) Touch pad: appears within the top pane at a usable size; auto-hide on a physical gamepad still works. (7) Swap-screens hotkey exchanges the panes within the session.

*End of §12.*
