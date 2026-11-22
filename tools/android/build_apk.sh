#!/bin/bash
# Package build/android/libmain.so + the SDK's shared libraries into a debug-signed APK, without
# Gradle: javac (SDL's Java + GameActivity) -> d8 -> aapt2 link -> zip libs -> zipalign -> apksigner.
# Usage: tools/android/build_apk.sh [install]   -> build/android/apk/gtav-recomp.apk
# RELEASE=1: the public APK with no game code (build/android-dll, GTAV_GAME_DLL=ON: the libmain.so
# shim + libgtav_fast.so), not debuggable -> build/android/apk/gtav-recomp-release.apk. It carries
# the tools to build the game on the phone, as the desktop downloads do: rexglue (codegen), clang,
# lld, XenosRecomp, dxc as lib*_exe.so (an app may only execute files from its native library
# folder) and assets/kit.zip (tools/android/make_kit.sh). The player's libgtav_game.so is built on
# the phone from their own discs (SetupActivity).
set -e
source ~/android-tools/env.sh
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
APP=$ROOT/tools/android/app
OUT=$ROOT/build/android/apk
SDK_LIB=$ROOT/rexglue-sdk/out/install/android-arm64/lib
SDL_JAVA=$ROOT/rexglue-sdk/thirdparty/sdl3/android-project/app/src/main/java
BT=$ANDROID_HOME/build-tools/36.0.0
JAR=$ANDROID_HOME/platforms/android-36/android.jar
NDK_LIBCXX=$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so
KEY=$HOME/android-tools/debug.keystore

mkdir -p "$OUT"
cd "$OUT"
rm -rf classes stage && mkdir -p classes stage/lib/arm64-v8a

javac --release 17 -nowarn -classpath "$JAR" -d classes \
  $(find "$SDL_JAVA" "$APP/java" -name '*.java') 2>&1 | grep -v "^Note:" || true
"$BT/d8" --release --min-api 30 --lib "$JAR" --output stage $(find classes -name '*.class')

MANIFEST=$APP/AndroidManifest.xml
NAME=gtav-recomp
# The setup screen's library (SetupActivity) is in both: it has no game code.
LIBS=("$ROOT/build/android/libmain.so" "$ROOT/build/android-dll/libgtav_setup.so")
if [ "${RELEASE:-0}" = 1 ]; then
  NAME=gtav-recomp-release
  LIBS=("$ROOT/build/android-dll/libmain.so" "$ROOT/build/android-dll/libgtav_fast.so"
        "$ROOT/build/android-dll/libgtav_setup.so")
  # KEEP_DEBUGGABLE=1: a release-layout APK that run-as can enter (phone tests only, never shipped).
  if [ "${KEEP_DEBUGGABLE:-0}" = 1 ]; then cp "$APP/AndroidManifest.xml" "$OUT/AndroidManifest.release.xml"
  else sed 's/ *android:debuggable="true"//' "$APP/AndroidManifest.xml" > "$OUT/AndroidManifest.release.xml"; fi
  MANIFEST=$OUT/AndroidManifest.release.xml
fi
ASSETS=()
if [ "${RELEASE:-0}" = 1 ]; then
  TC_LLVM=$ROOT/build/android-llvm/out TC_DXC=$ROOT/build/android-dxc/out TC_XR=$ROOT/build/android-xr/out
  for f in "$TC_LLVM/clang" "$TC_LLVM/lld" "$TC_DXC/dxc" "$TC_DXC/libdxcompiler.so" "$TC_XR/XenosRecomp" \
           "$ROOT/rexglue-sdk/out/install/android-arm64/bin/rexglue"; do
    [ -f "$f" ] || { echo "missing $f (tools/android/build_llvm.sh, build_dxc.sh, build_xr.sh, build_sdk.sh)"; exit 1; }
  done
  mkdir -p assets
  "$ROOT/tools/android/make_kit.sh" "$OUT/assets/kit.zip"
  ASSETS=(-A "$OUT/assets")
fi
"$BT/aapt2" link -o base.apk --manifest "$MANIFEST" -I "$JAR" "${ASSETS[@]}" \
  --min-sdk-version 30 --target-sdk-version 34

cp "${LIBS[@]}" "$SDK_LIB/librexruntime.so" "$SDK_LIB/librexgpu-xenos.so" \
   "$NDK_LIBCXX" stage/lib/arm64-v8a/
if [ "${RELEASE:-0}" = 1 ]; then
  L=stage/lib/arm64-v8a
  cp "$TC_LLVM/clang" $L/libclang_exe.so
  cp "$TC_LLVM/lld" $L/liblld_exe.so
  cp "$TC_DXC/dxc" $L/libdxc_exe.so
  cp "$TC_DXC/libdxcompiler.so" $L/libdxcompiler.so
  cp "$TC_XR/XenosRecomp" $L/libxenosrecomp_exe.so
  cp "$ROOT/rexglue-sdk/out/install/android-arm64/bin/rexglue" $L/librexglue_exe.so
fi
"$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip" --strip-debug stage/lib/arm64-v8a/*.so
cp base.apk unaligned.apk
(cd stage && zip -q -X ../unaligned.apk classes.dex && zip -q -X -0 -r ../unaligned.apk lib)

"$BT/zipalign" -f -P 16 4 unaligned.apk aligned.apk
if [ ! -f "$KEY" ]; then
  keytool -genkeypair -keystore "$KEY" -storepass android -keypass android -alias androiddebugkey \
    -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=Android Debug,O=Android,C=US" >/dev/null
fi
"$BT/apksigner" sign --ks "$KEY" --ks-pass pass:android --key-pass pass:android \
  --out $NAME.apk aligned.apk
rm -f unaligned.apk aligned.apk base.apk
ls -la $NAME.apk
if [ "$1" = install ]; then
  adb install -r $NAME.apk
fi
