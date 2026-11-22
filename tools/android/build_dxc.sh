#!/bin/bash
# dxc for the phone: the on-device shader translation (XenosRecomp -> HLSL -> dxc -> SPIR-V, as on
# the desktop) runs it. DirectXShaderCompiler at 416fab6b (the build the desktop packages bundle,
# "1.8 (dev;4662-416fab6b)"), cross-built with the NDK for aarch64-linux-android30, libc++ static.
# The APK carries the results as lib/arm64-v8a/libdxc_exe.so (the dxc tool; the app starts it with LD_LIBRARY_PATH at that folder) and
# libdxcompiler.so: Android only lets an app execute files from its native library folder.
#   tools/android/build_dxc.sh [JOBS]   -> build/android-dxc/out/{dxc,libdxcompiler.so}
# Memory: ~1.5 GB per job compiling, ~3 GB linking libdxcompiler.so.
set -e
source ~/android-tools/env.sh
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
COMMIT=416fab6b5
JOBS=${1:-4}
W=$ROOT/build/android-dxc
# This dxc's CMake files set policies CMake 4 no longer has: a CMake 3.31 (Kitware's binary
# release, unpacked into build/) does the configure and build.
CMAKE=${DXC_CMAKE:-$ROOT/build/cmake-3.31.6-linux-x86_64/bin/cmake}
[ -x "$CMAKE" ] || { echo "no CMake 3.x at $CMAKE (cmake-3.31.6-linux-x86_64.tar.gz from github.com/Kitware/CMake/releases)"; exit 1; }
SRC=$W/src
if [ ! -d "$SRC/.git" ]; then
  git clone --filter=blob:none https://github.com/microsoft/DirectXShaderCompiler.git "$SRC"
  git -C "$SRC" checkout -q $COMMIT
  git -C "$SRC" submodule update --init --depth 1
fi
[ "$(git -C "$SRC" rev-parse --short=9 HEAD)" = $COMMIT ] || { echo "dxc source is not at $COMMIT"; exit 1; }

# Host tablegen first (the cross build runs llvm-tblgen / clang-tblgen here).
if [ ! -x "$W/host/bin/clang-tblgen" ]; then
  "$CMAKE" -S "$SRC" -B "$W/host" -G Ninja -C "$SRC/cmake/caches/PredefinedParams.cmake" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-20 -DCMAKE_CXX_COMPILER=clang++-20 \
    -DLLVM_INCLUDE_TESTS=OFF -DCLANG_INCLUDE_TESTS=OFF -DSPIRV_BUILD_TESTS=OFF -DHLSL_INCLUDE_TESTS=OFF
  nice "$CMAKE" --build "$W/host" -j"$JOBS" --target llvm-tblgen clang-tblgen
fi

"$CMAKE" -S "$SRC" -B "$W/android" -G Ninja -C "$SRC/cmake/caches/PredefinedParams.cmake" \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-30 -DANDROID_STL=c++_static \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-Wno-invalid-specialization" \
  -DLLVM_TABLEGEN="$W/host/bin/llvm-tblgen" -DCLANG_TABLEGEN="$W/host/bin/clang-tblgen" \
  -DLLVM_DEFAULT_TARGET_TRIPLE=dxil-ms-dx -DLLVM_HOST_TRIPLE=aarch64-linux-android \
  -DLLVM_INCLUDE_TESTS=OFF -DCLANG_INCLUDE_TESTS=OFF -DSPIRV_BUILD_TESTS=OFF -DHLSL_INCLUDE_TESTS=OFF \
  -DLLVM_ENABLE_TERMINFO=OFF -DLLVM_ENABLE_ZLIB=OFF -DLLVM_ENABLE_LIBXML2=OFF \
  -DCMAKE_SHARED_LINKER_FLAGS="-static-libstdc++ -Wl,-z,max-page-size=16384" \
  -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -Wl,-z,max-page-size=16384"
nice "$CMAKE" --build "$W/android" -j"$JOBS" --target dxc dxcompiler
mkdir -p "$W/out"
STRIP=$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip
"$STRIP" -o "$W/out/dxc" "$W/android/bin/dxc"
"$STRIP" -o "$W/out/libdxcompiler.so" "$(readlink -f "$W/android/lib/libdxcompiler.so")"
cp "$SRC/LICENSE.TXT" "$W/out/LICENSE-dxc.txt"
ls -la "$W/out"
