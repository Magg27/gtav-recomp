#!/bin/bash
# Phone test loop for shell-only changes (libgtav_fast.so, no game-code change): install a
# KEEP_DEBUGGABLE=1 RELEASE=1 APK over the installed game and re-stamp files/game.txt with the new
# libgtav_fast.so hash, so the setup screen does not rebuild the game (it binds by symbol name, and
# a shell-only change keeps the interface). Never for releases.
#   tools/android/test_shell.sh [APK]
set -e
source ~/android-tools/env.sh
APK=${1:-$(cd "$(dirname "$0")/../.." && pwd)/build/android/apk/gtav-recomp-release.apk}
adb shell am force-stop com.gtavrecomp.app
adb install -r "$APK" | tail -1
LIB=$(adb shell pm path com.gtavrecomp.app | sed 's/package://; s/base.apk$//' | tr -d '\r')lib/arm64
SHA=$(adb shell sha256sum "$LIB/libgtav_fast.so" | cut -d' ' -f1)
adb shell "run-as com.gtavrecomp.app sh -c 'printf \"gtav-recomp android game 1\nshell $SHA\n\" > files/game.txt; cat files/game.txt'"
