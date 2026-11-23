# Handoff: Android performance rework (for a fresh Fable session)

> **Correction, 2026-11-22 (later the same day, branch `mt-record`):** the rework below was built
> (`docs/HANDOFF_MT_RENDERER_2026-11-22.md`), and it showed that this file's main claim is wrong:
> **the phone's frame is not bound by the render thread's recording.** Adding 8 ms to every
> frame's recording (`--gtav_native_ab=gtav_native_record_pad_ms=0|8`) left the frame time where
> it was (38.0 vs 38.0, 47.4 vs 46.6, 46.5 vs 46.0 ms); the render thread idles 5-13 ms a frame
> waiting for the game. Two record workers cut recording from ~31 to ~18 ms and fps did not move.
> What sets the frame is the game's own threads: the main thread waits ~47% of the time on the
> game's job threads, and the 3 mid + 1 prime cores are full. Read "What limits the phone frame"
> below with that in mind (the fence wait is real but not on the critical path).

Written 2026-11-22 at the end of the v0.5.0 / Android perf session. Owner's words: "get the android
perf working as well as possible so the game runs super fast"; owner chose **"Do the big rework"**
(parallel draw recording) and asked for it to be done in a fresh Fable session. Read this whole
file first, then `docs/RELEASE_v0.5.0.md`, `docs/ANDROID_STATUS.md` (perf history 10-30/31) and
`docs/LINUX_PERF_2026-11-12.md`.

## Where things are

- Repo `~/gtav-linux`, branch **`perf-linux`** (pushed; contains all of `issue-fixes`). v0.5.0 is
  prepared on it (`docs/RELEASE_v0.5.0.md`), not published: Mac + Windows must rebuild and test
  first (release gate: no tag, no main push until the owner says).
- The Android APK builds the game **on the phone** (clean room, no PC pack): codegen + clang + lld +
  XenosRecomp + dxc ship as `lib*_exe.so`, the build kit as `assets/kit.zip`. Verified on the S23 FE
  (adb serial `R5CX13SPC2J`): codegen 811/811 match, ~17 min build, world reached, runtime shader
  translation 0 failures. Device `2G0YC1ZF8G0PKN` is the owner's **Meta Quest 3** (Halo 2 VR work),
  not the phone: the GTA app and `/data/local/tmp/{tc,cg,sh}` are still on it; remove them when it
  is next connected (owner agreed).
- Phone squares (16x16 light blocks on characters) **fixed** (39dd336): texture memo hits now
  Transition resolved textures again.

## Fast phone test loop (use this, not the 17-min on-phone rebuild)

Shell-only changes (anything in `libgtav_fast.so` / `librexruntime.so`, no generated-code change):
```sh
source ~/android-tools/env.sh; export ANDROID_SERIAL=R5CX13SPC2J
memgate --need 4 --name android-shell -- cmake --build build/android-dll -j6
KEEP_DEBUGGABLE=1 RELEASE=1 memgate --need 2 --name apk -- tools/android/build_apk.sh
tools/android/test_shell.sh            # installs, re-stamps files/game.txt (no game rebuild)
adb shell am start -n com.gtavrecomp.app/.SetupActivity
```
SDK runtime changes: `JOBS=6 memgate --need 5 -- bash tools/android/build_sdk.sh all` first.
Flags go in `/sdcard/Android/data/com.gtavrecomp.app/files/args.txt` (one per line; `env:NAME=V`
for env vars). **After editing it with adb, `chmod 666` it or the app reads it as empty** (cost an
hour this session). It currently still holds `--gtav_native_ab=gtav_native_direct_convert=false|true`
and `--gtav_native_stats=true`: clean it first.

Measuring: in-run A/B is the only trustworthy tool on a hot phone:
`--gtav_native_ab=<cvar>=<a>|<b>` flips every 60 frames and logs
`A/B <cvar> = a: N fps (frame, record, GPU) | = b: ...` every 1,200 frames (the first line is the
loading screen; use the in-world ones, 3+ lines). Binary changes: compare profile shares, not fps.
Profiles: `adb shell simpleperf record --app com.gtavrecomp.app -e cpu-clock -f 2000 --call-graph fp
--duration 15 -o /data/local/tmp/x.perf`, then `simpleperf report -i ... --tids <gtav-record tid>
--sort symbol --children` (find tids via `/proc/<pid>/task/*/comm`; per-thread recording is not
permitted). Other diagnostics: `--gtav_native_pass_gpu_times=true` (GPU ms by pass kind),
`env:GTAV_NATIVE_PASS_STATS=1` (why passes begin, "PASSDIAG"), `--gtav_wait_profile=true`.

**Heat dominates every number.** During this session the phone sat at 7% battery on laptop USB,
40-44 C, thermal status 2-3, CPU caps 1.2-1.7 GHz on every cluster (prime normally ~3 GHz). Ask the
owner to use a wall charger and let the phone cool; record `dumpsys thermalservice` and
`/sys/devices/system/cpu/cpu7/cpufreq/scaling_max_freq` with every result.

## What limits the phone frame (measured 11-22)

- Frame ~44 ms (22-23 fps) when warm, 49-55 ms hot; record 23-35 ms, GPU 19-33 ms.
- **The game cannot run ahead of the renderer's recording**: the guest GPU (command processor)
  waits for the renderer at fence copies ("fence copies per 300 frames: ... GPU waited for the
  renderer 300 times (9118 ms)" = ~30 ms a frame). So frame time ~ the render thread's recording
  time + what cannot overlap. GPU-side changes do not move fps; record-time changes do.
- Render thread (gtav-record) profile, warm: DrawImpl ~70% (children); RectDraw/RectExpand 15%
  (CPU rect-list expansion: the rect-list geometry shader crashes the Adreno compiler); memmove
  ~8% (RectDraw, ConvertInto scratch copy, Execute); XXH3 ~7% (DrawImpl buffer hashing,
  HashGuestTexture, SampledHash; no write watch on kernel 5.10); Adreno driver (vkCmdDrawIndexed
  6%, vkCmdBindDescriptorSets 4.4% = the per-draw dynamic UBO constant set, BeginRenderPass 2.8%,
  barriers 2.4%); scudo calloc ~4% (inside the driver). Record breakdown per frame: setup ~2,
  consts ~1.2, textures ~3.5, streams ~10, pipeline ~1, bind+draw ~12.5 ms.
- Other threads: game audio thread ~94% busy (game mixer code + XMA), D3D capture thread ~55%,
  main thread ~53% (waits 47%; 12% in the Sleep(0) spin), 3 gtav-hash workers on little cores.
- GPU (pass_gpu_times, 19 ms total that run): G-buffer 4.1 ms in 5.6 passes, lighting 2.9 ms in 5
  passes; 176-193 passes and ~340 barriers a frame; PASSDIAG: 70 resolve copies/frame, 22 "RESTART
  after resolve copy" (G-buffer reloaded mid-pass).

## Tried this session (same-run A/Bs unless noted)

| change | result | state |
|---|---|---|
| slot memo fix: Transition resolved textures on memo hits | squares gone, memo kept | **kept** 39dd336 |
| `gtav_native_fence_wait` true/false | neutral (23.0 vs 23.8, 22.3 vs 22.3) | default unchanged |
| in-pass resolves (`env:GTAV_NATIVE_FETCH_RESOLVE=1`, `gtav_native_fetch_resolves`) | neutral/slightly worse | off |
| `gtav_native_slot_targets` | neutral | off |
| render scale 0.5 vs 0.75 (separate runs) | GPU lower, fps same (not GPU-bound) | 0.75 |
| `xma_flush_streams` | +16% / -5% / -1%: noise | off |
| `guest_sleep_zero_us` 0 vs 50 (new SDK cvar) | yield slightly better | 0 (92fdc8e) |
| `gtav_native_convert_threads` 0 vs 2 | 2 slower (record 31.5 vs 25.9) | 0 |
| `gtav_native_direct_convert` | neutral/slightly worse | off |
| plume: reset command pools instead of buffers | no gain (5.3% vs 4.3%) | reverted |
| rect expansion: fixed-size vertex copies | RectDraw 18.8% -> 15.5% of the thread | **kept** eec0df3 |
| main command list ended on the submit thread | render-thread submit 2.4 -> 0.45 ms | **kept** 1deffd4 |
| command lists reset in the background (2 pairs per slot, plume `resetAhead`) | vkReset/vkEnd gone from the thread (~9%) | **kept** 6b2e798 |

## The rework (owner approved): plan

Goal: roughly halve the render thread's recording time, which sets the phone frame.

1. **First, re-measure `--gtav_native_record_thread`** (deferred_list.{h,cpp}, b7a3809): the backend
   writes a compact command stream and a second thread replays it into Vulkan (the ~12 ms of
   driver calls move off the render thread). On 10-31 it gave "record -6 ms hot; fps unchanged while
   the GPU limits" - but the frame is now known to be bound by recording, not the GPU, and the
   command-list changes above shifted the balance. It is live: `--gtav_native_ab=
   gtav_native_record_thread=false|true`. If it wins, make it the Android default and stop there
   for a release.
2. Then the parallel part: split a frame into chunks at render-pass boundaries (the backend already
   knows them: BindTargets / Transition / Resolve end passes) and record chunks on 2-3 worker
   threads into secondary command buffers or separate primary lists submitted in order. The hard
   part is the backend's serial state: layouts map + Transition, pending clears, bound_fb/targets,
   texture/buffer caches (creation and upload copies inside a frame), the upload ring, slot/block
   memos, resolve_seen, descriptor allocation. A workable shape: a serial "plan" pass on the render
   thread that resolves everything stateful (targets, transitions, resolves, texture uploads,
   pipeline lookups) into per-draw records, then parallel workers that do the per-draw CPU work
   (stream conversion, constants, descriptor slots) and the Vulkan calls for their chunk.
   Keep desktop on the old path until it is proven (cvar, default on only for Android first);
   Windows/Mac/iOS must keep building (plume and backend are shared).
3. Smaller items still open: per-draw dynamic UBO bind (vkCmdBindDescriptorSets 4.4%) could become
   one combined allocation/offset set; RectDraw content hashing and scratch copies; HashGuestTexture
   on the render thread; consider a 30 fps "steady" phone preset (cooler, less throttling) as a
   menu option in any case.

Every change: A/B on the S23 FE, screenshot check for squares/flicker, commit with numbers, update
`docs/ANDROID_STATUS.md` and this file. v0.5.0 release still waits on Mac/Windows; perf work can go
on `perf-linux` (or a new branch off it) and into the release only after the owner's phone test.
