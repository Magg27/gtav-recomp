#!/bin/bash
# The Android part of every desktop release (the package's android/ folder): the public APK with
# no game code. Since v0.5.0 it holds the tools to set the game up on the phone itself (codegen,
# clang, lld, XenosRecomp, dxc and the build kit, see build_apk.sh), so the desktop step only
# carries the APK and the licenses of what it adds:
#   OUT_DIR/GTAV.apk             the app: SDL + setup screen, libmain.so (shim), libgtav_fast.so,
#                                the SDK runtime, the build tools; no game code, data or shaders
#   OUT_DIR/LICENSES/            simde, SDL, libc++, LLVM, DirectXShaderCompiler, XenosRecomp
#   tools/android/make_shell.sh OUT_DIR
# Inputs: build/android-dll (tools/android/build_game.sh layout with -DGTAV_GAME_DLL=ON, see
# docs/ANDROID_STATUS.md), the Android SDK install (tools/android/build_sdk.sh) and the phone
# tools (build_llvm.sh, build_dxc.sh, build_xr.sh).
set -euo pipefail
R="$(cd "$(dirname "$0")/../.." && pwd)"
OUT=$1
DLL=$R/build/android-dll
source ~/android-tools/env.sh
NM=$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-nm
READELF=$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-readelf

# The shell must be the release layout: no recompiled game function defined anywhere in it.
"$READELF" -d "$DLL/libgtav_fast.so" | grep -q "librexruntime.so" || { echo "no $DLL/libgtav_fast.so"; exit 1; }
defined=$("$NM" -D --defined-only "$DLL/libgtav_fast.so" | grep -c -E " __imp__sub_[0-9A-F]{8}$" || true)
[ "$defined" = 0 ] || { echo "libgtav_fast.so defines $defined game functions: not the shell build"; exit 1; }

RELEASE=1 "$R/tools/android/build_apk.sh" >/dev/null
APK=$R/build/android/apk/gtav-recomp-release.apk
rm -rf "$OUT/link" "$OUT/interface" "$OUT/include" "$OUT/LICENSES" "$OUT/GTAV.apk"
mkdir -p "$OUT/LICENSES"
cp "$APK" "$OUT/GTAV.apk"
cp "$R/rexglue-sdk/thirdparty/simde/COPYING" "$OUT/LICENSES/simde.txt"
cp "$R/rexglue-sdk/thirdparty/sdl3/LICENSE.txt" "$OUT/LICENSES/SDL.txt"
cp "$R/build/android-llvm/llvm-project-19.1.5.src/llvm/LICENSE.TXT" "$OUT/LICENSES/LLVM.txt"
cp "$R/build/android-dxc/out/LICENSE-dxc.txt" "$OUT/LICENSES/DirectXShaderCompiler.txt"
cp "$R/thirdparty/XenosRecomp/LICENSE.md" "$OUT/LICENSES/XenosRecomp.md"
echo "android: $OUT ($(du -sh "$OUT" | cut -f1); APK $(du -sh "$OUT/GTAV.apk" | cut -f1))"
