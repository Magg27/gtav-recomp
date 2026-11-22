#!/bin/bash
# The build kit inside the public APK (assets/kit.zip): what the on-phone setup needs, next to the
# tools in lib/arm64-v8a, to build the player's game library from their own discs, the way the
# desktop installers do. No game code, data or shaders: the same kind of files the desktop
# packages ship (docs/RELEASE_CLEANROOM.md "What may ship").
#   tools/game_dll/gtav_manifest.toml, codegen.partition.json   function list + partition seed
#   tools/game_dll/expected.txt                                  sizes + SHA-256 only
#   tools/game_dll/fs/                                           freestanding pch, layout check, shims
#   android/interface/                                           hook names + export list (names only)
#   android/include/simde/                                       simde (MIT)
#   android/clang/include/                                       clang's own headers (Apache-2.0 + LLVM exception)
#   tools/shader_common.h                                        XenosRecomp's header (MIT) with the Adreno edits
#   tools/android/make_kit.sh OUT_ZIP [EXPECTED]
set -euo pipefail
R="$(cd "$(dirname "$0")/../.." && pwd)"
OUT=$(realpath -m "$1")
EXPECTED=${2:-$R/tools/release/expected.txt}
DLL=$R/build/android-dll
CLANG_INC=$R/build/android-llvm/android/lib/clang/19/include
[ -d "$CLANG_INC" ] || { echo "no $CLANG_INC (tools/android/build_llvm.sh)"; exit 1; }
[ -f "$DLL/game_interface/gtav_hooks.h" ] || { echo "no $DLL/game_interface (shell build)"; exit 1; }
K=$(mktemp -d)
trap 'rm -rf "$K"' EXIT
mkdir -p "$K/tools/game_dll/fs/include" "$K/android/interface" "$K/android/include" "$K/android/clang"
cp "$R/gtav/gtav_manifest.toml" "$R/tools/game_dll/codegen.partition.json" "$K/tools/game_dll/"
cp "$EXPECTED" "$K/tools/game_dll/expected.txt"
cp "$R/gtav/fs/gtav_pch.h" "$R/gtav/fs/layout_check.h" "$R/gtav/fs/game_dll.cpp" "$R/gtav/fs/gprlr_inline.h" \
   "$K/tools/game_dll/fs/"
cp "$R/gtav/fs/include/stdlib.h" "$R/gtav/fs/include/malloc.h" "$K/tools/game_dll/fs/include/"
cp "$DLL/game_interface/gtav_hooks.h" "$DLL/game_interface/gtav_game.exports" "$K/android/interface/"
rsync -a --exclude='*.md' --exclude='*.txt' --exclude='.*' "$R/rexglue-sdk/thirdparty/simde/simde/" \
  "$K/android/include/simde/"
cp -r "$CLANG_INC" "$K/android/clang/include"
# XenosRecomp's shader header with the phone edits install.cpp PhoneShaders made on the PC:
# device-address float2 loads split into scalars (UboConstants maps scalar loads only) and no
# 64-bit shift (the Adreno compiler asserts on it).
python3 - "$R/thirdparty/XenosRecomp/XenosRecomp/shader_common.h" "$K/tools/shader_common.h" <<'EOF'
import re, sys
s = open(sys.argv[1], encoding="utf-8").read()
s = re.sub(r"vk::RawBufferLoad<float2>\((g_PushConstants\.SharedConstants \+ )(\d+)\)",
           r"float2(vk::RawBufferLoad<float>(\1\2), vk::RawBufferLoad<float>(\1\2 + 4))", s)
assert "RawBufferLoad<float2>" not in s
assert s.count("(1ull << semanticIndex)") == 1
s = s.replace("(1ull << semanticIndex)", "(1u << semanticIndex)")
open(sys.argv[2], "w", encoding="utf-8", newline="").write(s)
EOF
mkdir -p "$K/LICENSES"
cp "$R/rexglue-sdk/thirdparty/simde/COPYING" "$K/LICENSES/simde.txt"
cp "$R/build/android-llvm/llvm-project-19.1.5.src/llvm/LICENSE.TXT" "$K/LICENSES/LLVM.txt"
cp "$R/thirdparty/XenosRecomp/LICENSE.md" "$K/LICENSES/XenosRecomp.md"
rm -f "$OUT"
(cd "$K" && zip -q -X -r "$OUT" .)
echo "kit: $OUT ($(du -h "$OUT" | cut -f1), $(unzip -l "$OUT" | tail -1 | awk '{print $2}') files)"
