# Handoff: Mac and iPhone for 0.5.0 (branch `max-perf`)

Written 2026-11-23 on Linux, for the Claude session on the owner's Mac. **Read all of it first.**
The owner's words: "give handoff for my gtav mac build to do the mac and ios release for .5 so
also linux and windows can do the ios release too".

So the Mac has two jobs:

1. **Build and test the Mac app and the iPhone shell from `max-perf`**, fix what breaks, and make
   the Mac package of 0.5.0.
2. **Upload the iPhone shell (`ios-shell-v0.5.0.tar`)** as soon as it passes on the iPhone. The
   Linux and Windows packages carry that same `ios/` folder (only a Mac can build it), so they
   cannot be finished without it.

## Rules (owner's, unchanged)

- Push fixes to `max-perf` only. **Never push to `main`, never tag, never publish a release**
  until the owner says the Mac build passed and says to ship it.
- A **draft** release and its helper uploads are fine (they are how the machines exchange files).
- Hidden or automated game runs are muted (`--audio_mute=true`); warn before stopping a game the
  owner is playing.
- Commits end with the `Co-Authored-By` line of the model that made them.
- Nothing made from the game goes into a commit, a package or an upload (taint scan before any
  package leaves the Mac).

## Where things stand

- **Android 0.5.0 is published** as `v0.5.0-android` (owner's go, 11-23; tag at 0b08506): the
  standalone `GTAV.apk` that sets the game up on the phone. `main` did not move; `Latest` is
  still v0.3.0. What was checked for it: the end of `docs/RELEASE_v0.5.0.md`.
- **Linux:** `max-perf` builds and runs (developer build). The Linux package is not made yet: it
  waits for the iPhone shell.
- **Mac, iPhone, Windows: nothing after v0.3.0 was ever compiled there.** `max-perf` holds, on
  top of v0.3.0 (`main` 2a76184): the 41 commits of `issue-fixes` (test list in
  `docs/HANDOFF_MAC_WINDOWS_2026-11-11.md`), the v0.5.0 work on `perf-linux`
  (`docs/RELEASE_v0.5.0.md`), the multithreaded renderer (`docs/HANDOFF_MT_RENDERER_2026-11-22.md`),
  the Android CPU pass and the max-performance line (`docs/ANDROID_STATUS.md`, 11-23 sections).
- Versions are already set (05d6176): Mac app 0.5.0 build 5, iPhone 0.5.0, Android 0.5.0 / 5.
- Open pull requests #13 and #14 (Mac) were merged into `issue-fixes` as 63a9a1c and ed5f071 and
  are in `max-perf`; #16 (45 fps preset) is the owner's to decide.

## 1. Bring the tree up

1. `git fetch && git checkout max-perf && git pull` (at the tag `v0.5.0-android` or later).
2. Reset the SDK, FFmpeg, SDL, XenosRecomp and plume trees and apply the patches again, as for
   v0.3.0: `rexglue-sdk-gtav-linux.patch` (the one SDK patch for every platform; it matches the
   Linux SDK tree exactly), `ffmpeg-gtav.patch`, `sdl3-gtav.patch`, `xenosrecomp-gtav.patch`,
   `plume-gtav.patch`. All of them changed since v0.3.0. New untracked files come with the patch.
3. Rebuild the codegen tool and the SDK runtime (Mac and `ios-rel`), then the game and launcher.
4. Codegen check: **811/811 against `tools/release/expected.txt`**. The list changed twice since
   v0.3.0, so an old `rexglue` fails at "Checking the generated code". Linux x86-64 and Android
   arm64 (on the phone itself) both produce these 811 files byte for byte.

## 2. What is different for Apple targets

All generated code changed (this is the part most likely to need a fix):

- `gtav/gtav_manifest.toml`: `ctr_as_local`, `xer_as_local`, `cr_as_local` (v0.5.0 on
  `perf-linux`), and now `non_volatile_as_local`, `non_argument_as_local`, `skip_lr` with 66
  `share_registers` functions (4548a94). Guest registers live in host locals; functions marked
  `share_registers` and the register save/restore helpers keep them in the context. Built and
  run on Linux x86-64 and Android arm64 only. If something misbehaves on Apple arm64, compare
  with Android first: same architecture, same generated files.
- `skip_lr`: `XexGetProcedureAddress` falls back to the executable's module for its thunk.
- The register save/restore helpers are inlined in the release compile on Windows, Linux and
  Android, **not on Apple targets** (Mach-O has no overridable alias; `gtav/fs/gprlr_inline.h`).
  Nothing to do; it is a possible later gain if another way is found.

Compiled or defaulted **off** on macOS and iOS until someone tests them there (each is one switch;
they are what made Android faster, see "After it passes" below):

| switch | where | Apple default | Linux / Android |
|---|---|---|---|
| `REX_FP_SINGLE_MODE` | compile flag of the game code (`install.cpp`, `build_game_dll.py`, `gtav/CMakeLists.txt`) | off | on |
| `gtav_guest_irq_lock` | `gtav/src/guest_irq_lock.cpp` | 1 (global lock) | 0 |
| `gtav_fence_block_after_us` | `gtav/src/gpu_fence_wait.cpp` | -1 (spin) | 200 |
| `guest_poll_patience_us` | SDK | 0 | 300 on Android |
| `gtav_native_copy_resolves` | `backend.cpp` | off | on for Android |
| `GTAV_D3D_PROBES` | `gtav/CMakeLists.txt` | compiled in | compiled out |

**On** for every platform, so these need testing on the Mac and the iPhone:

- The renderer records a frame on its own thread through one draw path; extra record threads are
  a menu setting ("Record threads (experimental)", off by default).
- CPU rect lists share one expansion per frame (`gtav_native_rect_share`; the macOS / iOS /
  Android path).
- SDK waits in `threading_posix.cpp` (macOS and iOS use it): alertable waits round the remaining
  time up instead of spinning (`posix_alertable_sleep`), a semaphore release wakes one waiter
  (`posix_wake_one`), events and semaphores notify after the unlock (`posix_wake_unlocked`). Each
  is a live cvar: set it to false to rule it out.
- XMA: a kick no longer wakes the worker for nothing (`xma_kick_wakes_worker`), and FFmpeg
  computes its FFT / MDCT / VLC tables once (`ffmpeg-gtav.patch`). **Listen** to cutscene speech,
  radio and gunfire: on Linux and Android this was checked by the level meter in the log
  (`audio: 0 of 1875 frames were silence ... peak dB`), not by ear.
- Game threads are named `XThread<hardware thread>p<priority><id>`, with one lock around a
  thread's name. Without that lock Android aborted at boot (two threads naming a new thread at
  once); the code is the same on Apple.
- Two new hooks (`sub_83829CC0`, `sub_83829CD8`): the shell and the game library of a package
  must come from the same commit, as always.

iPhone only:

- **New settings menu layout** for phones (`GTAV_MENU_PHONE` in `gtav/src/native/menu.cpp`): one
  sheet under the overlay's Menu button, two columns, large rows, a Close button, "Touch button
  size" slider, "Reflections every other frame" under Advanced. Checked on a 2340x1080 Android
  phone only. It replaces the centred iPhone window: look at it on the 13 mini (notch, rounded
  corners, does it fit without scrolling while Advanced is closed) and fix what does not fit.
- The on-screen Back / Menu / Start buttons are 20% larger and further apart
  (`touch_controls.cpp`); the iPhone's scale (1.3) and inset (0.16) are unchanged.
- `gtav_native_cube_every=2` (the menu's reflections option) skips the reflection cube's draws on
  every other frame. Measured on Android (+2-5% fps); on the iPhone check that reflections look
  right with it on before leaving it in the menu.

## 3. Tests

Developer build first (fast to fix), then the release package. One line per test: pass / fail +
a sentence. Fix on `max-perf`, push, write it under "Results".

| # | test | pass |
|---|---|---|
| 1 | Build + codegen | compiles; 811/811 |
| 2 | Boot (new game folder and an existing save) | world reached, lit, no black world |
| 3 | 20 minutes of play: on foot, driving, a shoot-out, a cutscene | no crash, no hang, picture right |
| 4 | Sound by ear | speech to the end of a cutscene, radio, effects; no dropouts or bursts |
| 5 | WASTED (#21) above 30 fps | fades, respawn at a hospital |
| 6 | Keyboard + mouse (#11), F1 settings menu (#6), fullscreen (#8), Controls remap | as in the 11-11 handoff |
| 7 | Retina window (#14), input-attachment resolves off (#13) | fills the window; no grey world |
| 8 | "Record threads" 1-3 in the menu | no picture change; fps noted |
| 9 | Bug report from the menu | file written |
| 10 | **iPhone shell compiles** (`build/release-ios-shell`) | b2276fa fixed the same gap for Android; never compiled for iOS |
| 11 | iPhone 13 mini: ipa from the package's `--iphone` | world loads, touch plays, WASTED respawns, sound |
| 12 | iPhone settings menu and touch buttons | fits, every row can be hit, Close works |
| 13 | Release package, unattended install from the unzipped zip | 811/811, world reached |
| 14 | `scan_release_macos.sh` on the package | PASS |

Numbers worth writing down (same scene as for v0.3.0): Mac fps, iPhone fps cool and hot.

## 4. The iPhone shell, for everyone

When test 11 passes:

1. `tools/ios/make_shell.sh OUT` from the release commit (as for v0.3.0: `build/release-ios-shell`,
   SDK `rexglue-sdk/out/install/ios-rel`, `-mcpu=apple-a14 -ffile-prefix-map`; it refuses prebuilt
   native shaders and checks for build paths).
2. Tar it without AppleDouble files (`COPYFILE_DISABLE=1 tar ...`) as `ios-shell-v0.5.0.tar`.
3. Upload it to the **draft** release `v0.5.0` and tell the owner the commit it was built from:
   `gh release upload v0.5.0 ios-shell-v0.5.0.tar --clobber`. Linux created that draft on 11-23
   and put `android-v0.5.0.tar` there (the published `GTAV.apk` + `LICENSES/`). If the draft is
   gone, create it: `gh release create v0.5.0 --draft --target max-perf --title "v0.5.0 (draft)"`.

Linux (and Windows, when the owner boots it) then download it and finish their packages; their
`--iphone` builds the ipa from the player's own game with that shell. If the shell changes after
the upload, upload again and say so: all packages must carry the identical `ios/` folder.

## 5. The Mac package

1. `gh release download v0.5.0 -p android-v0.5.0.tar` and unpack it: the package's `android/`
   folder is just `GTAV.apk` + `LICENSES/` now (the app builds the game on the phone; the
   launcher's "Build for Android" only says to install the APK). Its `GTAV.apk` must have the
   SHA-256 printed in the `v0.5.0-android` release notes.
2. `tools/release/build_release_macos.sh OUT LLVM LIC --ios-shell <ios> --android <android> --zip`.
3. Tests 13 and 14 from the unzipped package, plus `GTAV --iphone` (811/811, 0 failed shaders)
   and the ipa on the iPhone.
4. Upload `gtav-recomp-macos-arm64.zip` to the draft `v0.5.0` and **ask the owner**. On their go
   the Mac release follows the pattern of `v0.2.0-macos` / `v0.5.0-android`: its own tag on the
   commit it was built from (`v0.5.0-macos`), the zip, and the complete corresponding source
   (`tools/release/make_source_archive.sh gtav-recomp-src-v0.5.0-macos.tar.xz v0.5.0-macos
   rexglue-sdk thirdparty/XenosRecomp thirdparty/plume`), notes with what was tested and the
   SHA-256 of each asset. Whether the desktop packages later also go out together as one `v0.5.0`
   marked Latest is the owner's call when Linux and Windows are ready.

## After it passes: the switches that made Android faster

Not needed for the release, and each wants its own A/B on the Mac and the iPhone
(`--gtav_native_ab='cvar=a|b'` flips a live cvar every 60 frames inside one run):

- `gtav_guest_irq_lock=0` (the game's interlocked operations without the global lock): Linux job
  threads 56-59% -> 35-38% of a core, Android +2-9% fps.
- `gtav_fence_block_after_us=200` (the game's GPU fence wait sleeps instead of spinning): a full
  core less heat on Linux and Android, same fps. On the iPhone heat is the frame rate.
- `REX_FP_SINGLE_MODE` for the game code (no denormal-mode switch around FP instructions).
- `gtav_native_copy_resolves=true` on the iPhone's tile GPU (Android: +1-3%).
- On the iPhone the record thread's core matters (Android: recording 23-28% faster on the prime
  core with the main thread moved off it, `gtav/src/android_threads.cpp`): iOS has no affinity
  call, QoS classes are the lever.

## Results (fill in)

| machine | commit | result | notes |
|---|---|---|---|
| Linux (developer build) | 51d0fea | runs | hidden run-arounds clean; package waits for the iPhone shell |
| Android (S23 FE) | 51d0fea | PASS | release APK, game built on the phone, see `docs/RELEASE_v0.5.0.md` |
| Mac | | | |
| iPhone | | | |
| Windows | | | |
