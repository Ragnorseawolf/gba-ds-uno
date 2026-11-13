#!/bin/bash
# motog/update-and-build.sh — build the GBA DS Moto G edition from a
# pristine upstream tag + the motog patch layer (FINDINGS.md §6.3).
#
#   motog/update-and-build.sh [tag]
#     tag   upstream tag to build (default: v1.5)
#
# Flow:
#   1. fetch the tag from origin (falls back to the local tag)
#   2. pristine checkout of the tag in a git worktree at motog/work
#   3. symlink deps/ from the main tree (deps are pinned and huge; the
#      worktree build shares them, and their CMake cache, in place)
#   4. git apply motog/patches/*.patch in filename order — LOUD STOP
#      on any conflict: upstream moved under a patch, fix it by hand
#   5. build with upstream's scripts/build-android.sh, signed with
#      motog/motog.keystore (env names per android/app/build.gradle)
#   6. copy the APK out as "GBA DS <tag> Moto G.apk" in the repo root
#
# The toolchain exports mirror build-logs/run-build.sh: in-process
# Gradle via motog/tools/bin/gradle (a forked daemon's sockets are
# corrupted by this sandbox's TCP interceptor), Gradle 8.7 (AGP 8.5.2's
# minimum; 8.10.2 refuses the in-process trick), JDK 17, NDK r27c
# (clang 18 — upstream v1.5 does not compile on NDK 26's clang 17).
set -euo pipefail

TAG="${1:-v1.5}"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MOTOG="$ROOT/motog"
WORK="$MOTOG/work"

# --- toolchain (sandbox) ---------------------------------------------------
export PATH="$MOTOG/tools/bin:$PATH"
export JAVA_HOME="$HOME/workspace/snes9x-ra/tools/jdk"
export PATH="$JAVA_HOME/bin:$PATH"
export ANDROID_HOME="$HOME/android-sdk"
export ANDROID_SDK_ROOT="$HOME/android-sdk"
export ANDROID_NDK_HOME="$HOME/android-sdk/ndk/27.2.12479018"
export GRADLE_HOME="$HOME/workspace/snes9x-ra/tools/gradle"
# the JVM ignores $HOME here and would otherwise use /root/.gradle
export GRADLE_USER_HOME="/home/hatch/.gradle"

# --- signing ---------------------------------------------------------------
export GBADS_KEYSTORE="$MOTOG/motog.keystore"
KS_PASS="$(cat "$MOTOG/keystore-password.txt")"
export GBADS_KEYSTORE_PASSWORD="$KS_PASS"
export GBADS_KEY_ALIAS="motog"
export GBADS_KEY_PASSWORD="$KS_PASS"

cd "$ROOT"

# --- 1. tag ------------------------------------------------------------------
if git fetch origin "refs/tags/$TAG:refs/tags/$TAG" 2>/dev/null; then
	echo "fetched tag $TAG from origin"
else
	echo "WARNING: fetch of tag $TAG failed (offline?); using local tag if present" >&2
fi
git rev-parse --verify "refs/tags/$TAG" >/dev/null || {
	echo "ERROR: tag $TAG not found locally or on origin" >&2
	exit 1
}

# --- 2. pristine worktree ------------------------------------------------------
if [ -e "$WORK" ]; then
	git worktree remove --force "$WORK" 2>/dev/null || rm -rf "$WORK"
	git worktree prune
fi
git worktree add --detach "$WORK" "$TAG"

# --- 3. deps symlink -------------------------------------------------------------
ln -s "$ROOT/deps" "$WORK/deps"

# --- 4. patches ------------------------------------------------------------------
for p in "$MOTOG"/patches/*.patch; do
	echo "applying $(basename "$p")"
	if ! git -C "$WORK" apply --whitespace=nowarn "$p"; then
		echo "ERROR: patch $p does not apply cleanly to $TAG — upstream moved under it." >&2
		echo "Fix the patch by hand (FINDINGS.md §6), then re-run." >&2
		exit 1
	fi
done

# --- 5. build ----------------------------------------------------------------------
"$WORK/scripts/build-android.sh" arm64-v8a

# --- 6. deliverable ------------------------------------------------------------------
VERSION="$(cat "$WORK/VERSION")"
SRC_APK="$WORK/dist/gba-ds-v$VERSION-android.apk"
OUT="$ROOT/GBA DS $TAG Moto G.apk"
cp "$SRC_APK" "$OUT"
echo "deliverable: $OUT"
ls -la "$OUT"
