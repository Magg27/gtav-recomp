# v0.5.0 build sheet

v0.5.0 replaces the unpublished v0.4.0 (everything staged for 0.4.0 is in it). Same shape as
v0.3.0/v0.4.0 (`docs/RELEASE_v0.3.0.md`, `docs/RELEASE_v0.4.0.md`); the differences are below.
Branch `perf-linux` (it contains all of `issue-fixes`). Version strings set in 05d6176: Mac app
0.5.0 / 5, iPhone 0.5.0, Android 0.5.0 / versionCode 5.

**Nothing is published until the owner says Mac and Windows passed and says to ship** (no tag, no
push to `main`, no public release). The draft and its helper uploads are fine once the owner says go.

## What changed for every platform

1. **Generated code changed.** `gtav/gtav_manifest.toml` now has `ctr_as_local`, `xer_as_local`,
   `cr_as_local` in `[entrypoint]`, and the SDK codegen (`rexglue-sdk-gtav-linux.patch`, as always the
   only SDK patch for all platforms) has the matching code. `tools/release/expected.txt` gen lines
   were regenerated (811 files, 405 changed). **Windows and Mac must rebuild `rexglue`/`rexgluerd`
   from the current patch**, or the install stops at "Checking the generated code". Determinism was
   checked: Linux x86-64 and Android arm64 codegen give the same 811 files byte for byte.
2. **Release compile inlines `__savegprlr/__restgprlr`** (`gtav/fs/gprlr_inline.h`, force-included
   into generated sources by `install.cpp Compile()` and `build_game_dll.py`) for Windows, Linux and
   Android targets; not Apple targets (Mach-O has no overridable alias). All three packagers copy
   `tools/game_dll/fs/gprlr_inline.h`. Windows: check a full install compiles (COFF uses the same
   `__attribute__((alias))` path the release pch already uses) and the game boots.
3. **Defaults uncapped:** `gtav_fps_limit` 0 and `gtav_native_vsync` false in code (the launchers
   already passed these). Vehicles uncapped too (`gtav_vehicle_30fps` false; menu option).
4. **Android builds the game on the phone.** The APK carries rexglue, clang, lld (LLVM 19.1.5),
   XenosRecomp and dxc (416fab6b) for arm64 Android plus `assets/kit.zip`
   (`tools/android/build_llvm.sh`, `build_dxc.sh`, `build_xr.sh`, `make_kit.sh`, `build_apk.sh`
   RELEASE=1). The desktop packages' `android/` folder is now just `GTAV.apk` + `LICENSES/`
   (`make_shell.sh`); the launchers' "Build for Android" / `--android` only says to install the APK.
   No code change needed on Windows/Mac for this: copy `android-v0.5.0.tar` from the draft.

## Order on the owner's go

1. **Linux** uploads `android-v0.5.0.tar` (dist/v0.5.0/android) to the draft and the source archive.
2. **Mac** reapplies the patch, rebuilds the SDK (codegen tool!), the Mac app and the iPhone shell,
   uploads `ios-shell-v0.5.0.tar`, builds `gtav-recomp-macos-arm64.zip` with both folders.
3. **Windows** reapplies the patch, rebuilds the SDK (rexgluerd!), `gtav_fast.exe`, the launcher,
   builds `gtav-recomp-windows-x64.zip` with both folders.
4. **Linux** finishes `gtav-recomp-linux-x64.tar.gz` with the iOS shell.
5. Each desktop: unattended install from the unpacked package (811/811 gen match, world reached),
   taint scan PASS. Android: the APK's on-phone install on the S23 FE.
6. Final assembly and publishing as in v0.4.0 step 5-6 (`RELEASE_NOTES_v0.5.0.md`).

## Linux status (this machine, 2026-11-22)

- Compat container build at 05d6176: runtime, rexglue, XenosRecomp, gtav_fast, launcher. Floors
  GLIBC 2.35 / GLIBCXX 3.4.32 (unchanged).
- Release-path check: release codegen (seeded partition) -> `build_game_dll.py` with the official
  LLVM 19.1.5 (407 files, 690 s on 6 jobs) -> `gtav_game.so` in a `make_release_linux.py --compat`
  folder: boots hidden, 3,600 shaders translated by the packaged XenosRecomp + dxc in ~2.5 min,
  7 min in the world, 0 failed shaders, no errors.
- Android tools on arm64 Android (Quest 3 used for the tool checks): clang/lld run; codegen
  811/811 identical to the PC (1,036 s as an adb process); one average generated file compiles in
  10 s / 133 MB; XenosRecomp + dxc output identical to the desktop's (HLSL and SPIR-V hashes).
  APK 173 MB (`dist/v0.5.0/android`).
- Left: full on-phone install + build + play on the S23 FE; package tarball after the iOS shell;
  unattended install from the unpacked tarball; `scan_release.sh` (include the APK's new tools).
