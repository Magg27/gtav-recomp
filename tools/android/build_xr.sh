#!/bin/bash
# XenosRecomp for the phone (the on-device shader translation runs it, as on the desktop), cross-built
# with the NDK for aarch64-linux-android30, libc++ static, linked against the phone's
# libdxcompiler.so from tools/android/build_dxc.sh (found through LD_LIBRARY_PATH: both sit in the APK native
# library folder). The APK carries it as lib/arm64-v8a/libxenosrecomp_exe.so.
#   tools/android/build_xr.sh [JOBS]   -> build/android-xr/out/XenosRecomp
set -e
source ~/android-tools/env.sh
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
JOBS=${1:-4}
W=$ROOT/build/android-xr
XR=$ROOT/thirdparty/XenosRecomp
DXC_LIB=$ROOT/build/android-dxc/out/libdxcompiler.so
[ -f "$DXC_LIB" ] || { echo "no $DXC_LIB (tools/android/build_dxc.sh first)"; exit 1; }
mkdir -p "$W/wrap"
# XenosRecomp's own CMake takes dxc from its prebuilt dxc-bin (no Android build there): define the
# target first, pointing at ours, then add XenosRecomp and its other third-party libraries.
cat > "$W/wrap/CMakeLists.txt" <<EOF
cmake_minimum_required(VERSION 3.20)
project(XenosRecompAndroid C CXX)
set(CMAKE_CXX_STANDARD 17)
add_library(Microsoft::DirectXShaderCompiler SHARED IMPORTED GLOBAL)
set_target_properties(Microsoft::DirectXShaderCompiler PROPERTIES
  IMPORTED_LOCATION "$DXC_LIB" IMPORTED_NO_SONAME OFF
  INTERFACE_INCLUDE_DIRECTORIES "$XR/thirdparty/dxc-bin/inc")
set(XENOS_RECOMP_THIRDPARTY_ROOT "$XR/thirdparty")
add_subdirectory("$XR/thirdparty" thirdparty)
add_subdirectory("$XR/XenosRecomp" XenosRecomp)
EOF
cmake -S "$W/wrap" -B "$W/android" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-30 -DANDROID_STL=c++_static \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -Wl,-z,max-page-size=16384"
nice cmake --build "$W/android" -j"$JOBS" --target XenosRecomp
mkdir -p "$W/out"
"$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip" -o "$W/out/XenosRecomp" \
  "$W/android/XenosRecomp/XenosRecomp"
ls -la "$W/out"
