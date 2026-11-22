# Linux perf pass, 2026-11-12 (branch `perf-linux`, off `issue-fixes` e0999ca)

Machine: i5-13420H (4P+4E), RTX 4050 Laptop, 32 GB, game data on the TOSHIBA USB disk.
Goal: 100+ fps uncapped (10-29 target), fewer hitches. Scope: the whole Linux build.

## Status (11-13 12:30): boot bug found and fixed, in-world numbers still pending
The loading-screen crawl that blocked every bench (0.2-1 fps, 1-5 s guest frames, "long wait
(>15s)" loader threads, CPU and disk mostly idle) was a runtime bug, not only the busy machine:
`HostPathDevice::ResolvePath` listed the whole host directory (opendir + readdir + stat per
child) on every probe for a file that does not exist, under the VFS global lock. GTA V probes
`game:\xbox360` ~10x/s while loading; on the exFAT USB disk each listing is 20-200 ms inside
`exfat_iterate` (gdb + `/proc/<tid>/wchan` sampling: Main XThread 80% there, every other guest
thread queued on the global mutex). Fixed in 90484b8 (listing cached per directory while its
write time is unchanged). First hidden boot with the fix reached the open world in 154 s on a
loaded machine (load 29); 11-08 logs on a quiet machine show 12 s, so the rest is load and the
disk. Other sessions' GPU renders (Blender, all night) and `bfs` scans of the game disk made it
worse but were not the cause.

Also seen: the hidden SDK window is still painted once per guest swap (presenter →
`WindowSDL::RequestPaint`, Vulkan present through the NVIDIA driver, ~500/s in the 400-fps logo
phase, 18% of a core on the main host thread); harmless in the world at 30-60 fps, so left alone.

**Everything below is built (`build/gtav`: pre-change game binary from e0999ca, runtime libs
`logs/libs_dircache` = Sleep fix + lock fast path + listing cache) but the perf commits are still
unmeasured: the machine has not been quiet (load 9-30, other sessions' rustc/rm/Blender).**

## Results so far (quiet windows of 11-13/14, hidden, muted, standing at the spawn point)
| Run | Result |
|---|---|
| `still_dc0` 15:26 (pre-change binary, libs_dircache, load 4.6) | 73.5 fps mean, 1% low 51, record 4.1 ms, GPU 1.9 ms, boot to world 37 s (cold cache) |
| `bin_*` 20:52-21:03 binary A/B base vs branch, 3 rounds, A always first | A 103.8/96.9/87.1 fps at ~2000 draws, B 91.7/81.9/76.3 at ~2470 draws. **Inconclusive**: the B scenes had 20% more guest draws (CP `drawidx`), load from other sessions rose 5 → 20 during the six runs, and the CP never waited on the renderer (0-8 of 600 swaps), so the renderer commits cannot move fps either way. Tooling fixed afterwards (ea445c6: A B / B A order, stats window starts in the world). |
| `pin2` 02:05 in-run flips, 4 rounds x 15 s, load 4.6 | pin=false 107.7 fps (2137 draws) vs pin=true 95.3 (2588 draws); at matched draws the two are within 3-5% (false 108.4@2044 vs true 103.0@2188; false 101.3@2535 vs true 101.9@2580). **No win from `--gtav_linux_pin` on this machine with other load on the P-cores; stays default off.** |
| `flags_*` 11-14 05:51-07:00 runtime-library A/B, libs_dircache (stock `-O2`-ish SDK flags: `-mcmodel=large`, x86-64-v2) vs libs_flags (`-mcmodel=small`, `-fno-semantic-interposition` + `-Bsymbolic-functions`, x86-64-v3), A B / B A / A B, 60 s, load 4.3 | A 138.6@1449 / 107.7@1955 / 109.6@2063 draws, B 130.1@1594 / 111.2@1857 / 131.2@1575 draws; means A 118.6, B 124.2 fps. At matched draws (fps x draws: A 201k/211k/226k, B 207k/207k/207k) the two are within the run spread. **No measurable win from runtime build flags**: the runtime .so is not where the main thread spends its time. Keep the stock SDK flags (no release-config change needed). |
| `waitprof` 11-14 08:07 standing, `--gtav_wait_profile=true` | 133 fps at 1649 draws. Main-thread GPU fence waits: avg 0.46 ms, ~42/s = ~2% of the main thread (the fence waited for is 2.6 fences ahead of the completed one, never the newest). The CP waited on the renderer 0-297 times per 300 frames (0-29 ms). **The fence path is not worth attacking.** (A 40-sample gdb profile of the main thread had put 7 samples in this wait: too few samples, and it is otherwise flat across game code.) |
| `run_new` 08:01 run-around, branch binary | 91.8 fps at 2154 draws, 1% low 49, 467 "hitches" in 92 s. **330 of them were the vehicle 30 fps cap**: the script's button presses get the player into a car and `--gtav_vehicle_30fps` (default on, issue #12 handling) holds 30 fps for 12 s. `bench.sh` now passes `--gtav_vehicle_30fps=false`. Real hitches >= 50 ms: 13. |
| `inl` / `inl2` 08:22-09:21 binary A/B, branch binary vs the same with `__savegprlr/__restgprlr` inlined (`gtav/fs/gprlr_inline.h`, 42,662 helper calls removed), standing | `inl` (idle camera, 3 rounds): A 109.1 vs B 92.3 fps raw but B scenes had 20-45% more draws: inconclusive. `inl2` (camera held by `input_stand_nudge.txt`, 5 rounds): B 123.2 vs A 96.8 fps raw, but again different draw counts per boot (traffic). Pooled fit of frame time vs draws (`ab_fit.py`): A `3.64 + 2.78 us/draw`, B `3.59 + 2.68 us/draw` → **inlined +2.6% at every draw count** (matched-draw pairs: +6% and -1%). Small, consistent with the ~5% of main-thread samples inside the helpers. Dev build keeps it (the include at the end of `gtav/generated/default/gtav_pch.h`; re-apply after codegen). Release builds (`fs/gtav_pch.h`) untouched. |
| `o3` 11-14 09:45-10:51 binary A/B, inlined-helpers binary at `-O2 -g` (dev default) vs the same at `-O3`, camera held, 5 ABBA rounds | raw A 112.7 vs B 95.1 fps with the usual per-boot draw-count spread (A 1351-2345, B 1881-2872); matched-draw pairs within +3/-4%; pooled fits cross (A `3.18 + 3.07 us/draw`, B `5.09 + 2.22 us/draw`: -7.6% at 1500, +5.3% at 3000 draws). **No measurable win from -O3**; the dev build stays at -O2. |
| `thp` 09:38 standing, 150 s, huge-page collapse from outside (`tools/native/thp_collapse.py`, `process_madvise(MADV_COLLAPSE)`) | 84 fps at 2905 draws (heavy traffic). The collapse did nothing: `process_madvise` on another process needs CAP_SYS_NICE (EPERM). In that scene the busiest thread was a non-main guest thread at 97% of a core (the D3D thread executing draw lists through the native capture hooks), the Main XThread 87%. Follow-ups: in-process collapse via `LD_PRELOAD tools/native/thp_preload.so` (`THP_COLLAPSE_AT` seconds after launch) and a gdb stack profile of that thread. |
| `thp2`/`thp3` 12:30 / 14:50 standing, camera held, 190 s, in-process huge-page collapse (`LD_PRELOAD tools/native/thp_preload.so`, `madvise(MADV_COLLAPSE)` 110 s after launch) | `thp2`: 210 of 512 MB physical collapsed, 90.8 fps before vs 86.5 after at the same 2860 draws. `thp3` (`THP_ALL_PHYS=1`): 470 MB collapsed (ShmemPmdMapped 470 MB), 139.2 fps @1411 draws before vs 135.6 @1476 after. **No gain from 2 MB pages**: TLB misses are not a limiter. (The sysfs knob `shmem_enabled` needs root; the preload shows what it would give: nothing.) |
| `prof2` 13:04 gdb stack samples of the busiest non-main thread, heavy scene | **It is the native renderer's record thread** (std::thread inherits the creating guest thread's name, so it showed as an `XThread...` at 97% of a core): 42 of 60 stacks in `Backend::Impl::DrawImpl` (ConstantUpload + Swap16Bytes, GuestBuffer, hash-table finds, memcpy, ActiveTargets), 5 in `write_watch::Harvest`, 5 in `Resolve`. Its per-draw cost (3.2 us at 2858 draws) equals the per-draw slope of the frame-time fits: **above ~2500 draws the record path sets the frame time** (that is where fps drops under 100). Native threads are now named (`thread_name.h`). |
| `sp_rec` 11-15 05:07 sampling profile of the renderer's record thread (`tools/native/sprof_preload.so`, 997 Hz, 40 s, camera held, heavy scene 2,800-2,950 draws, 73 fps, record 11.7 ms) | 36,653 samples; symbolized with DWARF (`sprof_report.py`, llvm-symbolizer; the nm-based first pass mis-attributed ~40% of the samples to neighbouring symbols). Record thread 90% busy (9.8% in the cond-var wait for the next packet). **74% in `DrawImpl`**, 4.7% `Resolve`, 6% write-watch `Harvest` (3.9% is the thread's own PAGEMAP_SCAN ioctl, the rest waiting for the helper threads). Inside DrawImpl, per draw: `Hash(fetch, 768)` 3.9% (~230 ns per call = 9x its compute cost: the packet is 12-17 MB read once, the fetch block is cold in cache), `ConstantUpload` 5.9% (81% in the 16-byte `Swap16Bytes` loop, also reading cold packet memory), `GuestBuffer` 8.2% (`buffers` unordered_map find 3.2%, `Clean()` page-epoch loop 0.9%, memo misses), `TextureDescriptor` 6.2% + `SamplerDescriptor`/`Sampler` 3.5% (hash + `samplers.find` per non-memoized slot), `vkCmdBindPipeline` in the NVIDIA driver 4.9% (line 4690), `memmove` 4.5% (fetch/shared block memo stores and the shared-constant upload copies), `std::unordered_map` finds 4.3% self in total, `ActiveTargets` 2.1%, `BindIndexBuffer` 2.4%, `std::ldexp` (RB_COLOR_INFO exponent, 4 per draw) 0.5%, `steady_clock::now` (stats timers) 0.4%. Changes made from it: next-draw prefetch of the fetch block, register shadow and first KB of each constant run in the Execute loop; AVX2 32-byte constant swap (`SwapWords`); ldexp replaced by an exponent-bit build. A/B = `pref` below. Not yet done: Harvest overlap (helpers own all views, wait lazily at the first Clean), flat hash maps / bigger memos for buffers, textures, samplers, fewer pipeline binds (count them first). |
| `sp_main` 11-15 05:46 sampling profile of the Main XThread (997 Hz, 40 s, camera held, light scene 1,483 draws, 96 fps, record 7.5 ms) | 36,369 samples. ~80% is recompiled game code with a flat profile (top functions 4.1% `sub_83807DC8` (a D3D routine, probe 527), 3.8% `sub_83525E00` (a byte-at-a-time parser/interpreter loop), 1.3% `sub_8281CCF0`; nothing above 5%). **7.7% waiting for GPU fences** (`sub_8380CEF8` via `sub_835833D8`: 5.3% in the futex, 2.4% in the guest loop) and **9.1% in a Sleep(0) spin** (`sub_8271E6C8` from `sub_827EFB28`, 3.2% of it in `sched_yield` via `XThread::Delay`: the game waits for its worker threads' jobs). Kernel-call overhead 3%: `NtReleaseSemaphore`/`NtSetEvent` (1.5% in `pthread_cond_broadcast`), 1.5% `__lll_lock_wait` + 0.9% `__lll_lock_wake` on contended pthread mutexes, `RtlEnterCriticalSection` 0.3%. So in light scenes the main thread still loses ~8% to renderer latency (fences): record-thread speedups pay there too; the rest is game code (PGO is the remaining lever). |
| `pref1r`/`pref2`/`pref3` 11-16 17:48-20:28 (`logs/chain18.sh`), record-thread batches A/B'd one after another, 4 ABBA rounds x 100 s standing, load 3-17 from other sessions | Committed 1db4360. Record ms per draw at matched draws: batch 1 -8..-10% (12.0->10.9 ms @1865, 14.9->13.4 @2870), batch 2 neutral to -13%, batch 3 inconclusive (load 17). Mean fps 65.8->67.2, 59.2->66.8, 57.8->67.2, but single runs swing +-25% under that load, so fps is not a verdict. Screenshots correct (`logs/shots_pref3_png`), write-watch verify 0 missed / 0 late. |
| `pgo` 11-16 20:34-21:32 (`logs/chain19.sh`): instrumented build (`build/gtav-pgo`, GTAV_PGO=GENERATE) profiled 300 s run-around + 200 s standing (`logs/pgo_raw`, merged `build/gtav/gtav.profdata` 19.5 MB), GTAV_PGO=USE build (`logs/gtav_pgo`, 4% smaller), A/B vs pref3, 4 ABBA rounds | Inconclusive under load 8-17: matched draws ~1860 A 63.4/56.1 vs B 74.0 fps, ~2425 A 54.4 vs B 50.7. `tools/native/cpu_per_frame.py` (thread CPU ms per frame, excludes preemption) also swings +-25% for the same binary at the same draws (pref3 9.7 vs 15.3 ms), so SMT/turbo contention dominates. Queued: `logs/chain20.sh` ThinLTO+PGO build and 6-round A/Bs pref3->pgo->lto. |
| `pgo2`/`lto` 11-16 21:33-23:28 (`logs/chain20.sh`): 6 ABBA rounds each, pref3 vs PGO, then PGO vs PGO+ThinLTO (`build/gtav-lto`, `-flto=thin`, lld, 3% smaller binary), load 6-14 | **Neither is a measurable win.** pgo2: mean 61.8 vs 60.8 fps; at ~1880 draws PGO 62-76 vs 46-51 fps, at ~1440 and ~2870 the other way. lto: mean 82.0 vs 74.2; at ~1440 LTO 114 vs 88, at ~1890 LTO 56/73 vs 105/62, at ~2870 LTO 66 vs 48. The same binary at the same draw count spans 47-105 fps in one A/B: on this shared machine (other sessions' rustc/link/indexer) changes under ~20% cannot be resolved. Binaries kept: `logs/gtav_pgo`, `logs/gtav_lto`; `build/gtav/gtav` = pref3. A verdict needs a quiet-machine window (`logs/gate_maxload` removed, 6+ rounds). |
| `texslot` 11-18 00:05-01:00 (`logs/chain21.sh`): pref3 vs 7a8ce5d (texture slots only for the fetch constants the shaders sample, slot memo invalidated per resolved address), 6 ABBA rounds x 100 s, standing, load 5-10; the Humanizer GPU jobs switched off through their worker-mode file (`tools/native/gpu_yield.sh`) | **Kept.** Record thread per draw 4.91 us (A, 4.68-5.14) -> 4.00 us (B, 3.42-4.46), every B run below every A run; textures bucket ~3.0 ms at ~1950 draws -> 1.6-2.1 ms at 1400-2800 draws; at matched draws (~1850-1900) B 94.7/84.3 fps vs A 76-87 (mean 82). Pooled ab_fit unusable (B spanned 1400-2900 draws, A 1800-2100). Main XThread 85-90% busy on both sides: the game thread is the limiter again at ~1900 draws. |
| `local2ab` 11-18 04:36-06:26 (`logs/chain24.sh`): codegen `ctr_as_local`/`xer_as_local`/`cr_as_local` (`logs/gtav_local2`, binary 11% smaller) vs `logs/gtav_gprlr` (texslot + gprlr inline force-included again: codegen had dropped it on 11-16), 6 ABBA x 100 s, standing, load 6-10. First try with `reserved_as_local` too HUNG (gdb: a thread spinning in `sub_83829CD8`, a spin-lock helper whose `stdcx.` pairs with an `ldarx` in another function, so a per-function reservation never matches): never enable it. Three 4-min hang hunts of local2: no hang. | **Kept (dev builds).** Main XThread CPU per frame (`cpu_per_frame.py`, excludes preemption) 4-14% lower at matched draws (quiet A runs 8.92 ms @1569 / 10.69 @2819; B 8.15 @1363, 9.78 @1881, 10.54 @2373; contended pairs A 10.97 @2031 vs B 9.75 @2000, A 12.23 @2338 vs B 11.16 @2357). fps: quiet runs A 87.9 @1569, 76.9 @2819; B 97.9 @1363, 81.4 @1881, 80.8 @2373; 1% lows B 28-44 vs A 17-42. Changes the generated game code: release only after v0.4.0 with the owner's OK. |
| Vehicles (#12) 11-21, `drivetest` debug command (a Blista at a fixed spot on the airport tarmac or `drivetest here`), `tools/native/drive_stats.py`, inputs `input_drive_straight/weave` | Every sleep-limited rate from 29 to 55 and 65 to 120 fps accelerates and steers like the game's own 30 fps (speed curves within ~3%, yaw rms ~60 deg/s); exactly ~60 fps runs ~50% fast (yaw 88-95 deg/s), 59/61 partly, unlimited +8%. TIMESTEP is the real frame time at every rate; nudging it out of the 1/60 band does not help (the trigger is real 60 Hz pacing). Vehicle cap now `gtav_vehicle_fps` (sleep limiter, never 56-64) and, at the owner's request, off by default (`gtav_vehicle_30fps=false`): uncapped driving. |
| Snapshot cache 11-21 (891a81f) | With vsync on in a visible window the owner's session converted 3-7 MB of fence-copied vertex snapshots a frame (streams 10-12 ms). Converted once per distinct content now (`gtav_native_snapshot_cache`); hidden benches barely produce snapshots, one run that did: 9.2 of 14 a frame from the cache. Owner's settings now vsync off. |
| `non_volatile_as_local` 11-21 (parked) | Crashes at boot. Fixed in the SDK codegen (9892780): conditional tail calls to share_registers fragments, full r14-r31/f14-f31 + CR/CTR/XER handover, caller ctx saved/restored around share calls; 62 live-in + 2 live-out functions marked share_registers (`tools/native/nv_livein.py`, `nv_liveout.py`). Gets past CRT init, then the D3D device global is still 0 on the render setup thread. Manifest attempt: `logs/gtav_manifest.nv_try2.toml`. |

What the numbers say: fps tracks the guest draw count almost linearly (130 fps at 1,430 draws,
78 fps at 3,260), the Main XThread is at 90-95% of a core in every run, the render thread records
4-9 ms of a 9-13 ms frame, the GPU 2-4 ms. The limiter is the recompiled game code on the main
thread. Scene drift between (and within) runs is ±20% in draws, so separate-run A/Bs need matched
draw counts or many ABBA rounds; in-run cvar flips are the trustworthy tool. Standing at the spawn,
the idle camera pans after ~30 s (`input_stand_nudge.txt` taps the stick every 5 s to stop that:
fps spread within a run 3-5 instead of 15-30) and traffic differs per boot (1,400-3,000 draws);
`tools/native/ab_fit.py` fits `ms = a + b * draws` per side over all runs and compares at fixed draw
counts, which is how binary A/Bs are judged now.

## Findings (verified in code)
1. **Benches were refresh-capped.** `gtav_settings.ini` (`gtav_native_vsync=true`) loads after
   the command line (`renderer.cpp` LoadSettings), so `--gtav_native_vsync=false` was overridden
   and `ab.sh` flips were saved into the ini. `bench.sh`/`ab.sh` now export `GTAV_IGNORE_SETTINGS=1`
   and the log prints the effective settings. Earlier Linux fps numbers near 144 were the cap.
2. **Guest `Sleep(0)` was a 100 µs nanosleep.** `rexglue-sdk/src/system/xthread.cpp` compared the
   guest priority increment (0) with `ThreadPriority::kBelowNormal`, which is 8 on POSIX. GTA's
   update thread calls SleepEx ~1,500 times a frame from its backoff spin (Windows wait profile),
   so this was up to ~150 ms of forced sleep per second on the critical thread. Fixed (yield).
3. **`GetNativeObject` locked the global recursive mutex twice per call.** Now lock-free once the
   handle is stashed (acquire/release fences; `StashHandle` writes the handle before the signature).
4. **No thread placement on Linux.** New `--gtav_linux_pin` (default off): hot threads one P-core
   each, background Vulkan/shader work on the E-cores. Windows lost fps when everything was pinned,
   so this stays off until the A/B says otherwise.
5. Renderer per-draw waste (see commits 8a47b12, 29cb2e4): shared constants zeroed+copied+pushed
   every draw; 32-slot fetch loop with per-slot memo wiped per resolve; 60-frame walk of all
   textures and buffers; `vkGetPipelineCacheData` on the render thread.
6. The frame is game-thread bound (Main XThread + D3D thread ~85-90% busy; render thread idles
   2-4 ms, GPU idles 6-9 ms per frame at ~3,500 draws). Render-thread savings only pay in heavy
   scenes and when the command processor waits on the renderer (rare in logs: 0-2 of 600 swaps).

## Commits on `perf-linux`
| Commit | What | How to measure |
|---|---|---|
| 2412e4b | tooling: ini ignored, `bench_stats.py` (mean, 1%/5% lows, record breakdown, per-thread CPU), `lib_ab_linux.sh`, `bin_ab_linux.sh` | — |
| 7cd394a | SDK: Sleep(0) yield, gated SCHED_FIFO, lock-free GetNativeObject | `lib_ab_linux.sh sleep logs/libs_base logs/libs_fix2 3 60`; `--gtav_wait_profile=true` SleepEx share |
| 2913c9c | `--gtav_linux_pin` thread placement (off) | `BENCH_BOOT_SECS=900 tools/native/ab.sh pin0 gtav_linux_pin false true 4 15` |
| 29cb2e4 | cache byte totals, desktop buffer budget 512 MB, pipeline-cache get off the render thread | hitches ≥25 ms in `bench_stats.py` over a 5-min run-around |
| 8a47b12 | shared-constant reuse, push-constant skip, fetch-block memo | `bin_ab_linux.sh` old vs new binary; textures/consts ms vs `logs/walk2.log` (2.2 / 0.9 ms); screenshots for texture mix-ups |
| a99a0cd | `BENCH_BOOT_SECS` | — |
| e0855db | fix: `push_set` reset after the non-draw push-constant writes (self-review of 8a47b12) | — |
| c857388, 0bbd86d, cd11af6 | `quiet_gate.sh` (load, GPU, disk), `gpu_watch.sh` | — |
| 90484b8 | SDK: host directory listing cached for missing-file probes (boot crawl); SDK build options `REX_LARGE_CODE_MODEL`/`REX_SYMBOLIC_RUNTIME` (defaults unchanged) | boot-to-world time: `grep -m1 -E 'frame [0-9]+: [0-9]{3,} draws'` vs log start; 11-08 = 12 s |

Runtime library sets saved for A/B: `logs/libs_base` (stock), `logs/libs_sleepfix` (Sleep fix
only), `logs/libs_fix2` (Sleep fix + lock fast path), `logs/libs_dircache` (fix2 + listing cache, currently
in `build/gtav`; the boot fix is needed to reach the world at all, so A/B the Sleep fix as
`libs_base`+listing cache vs `libs_dircache` if a separate build is wanted).

## Measurement queue (quiet machine: no `bfs`, load < 4, swap free)
1. `BENCH_BOOT_SECS=900 tools/native/ab.sh pin0 gtav_linux_pin false true 4 15` (pinning).
2. `tools/native/bench.sh still_new 60` with `BENCH_INPUT=none`, 3 repeats; `bench.sh run_new 90` 3 repeats.
3. `tools/native/lib_ab_linux.sh sleep logs/libs_base logs/libs_fix2 3 60` (runtime fixes).
4. `tools/native/bench.sh wait_new 60 --gtav_wait_profile=true` (SleepEx share should drop from ~2.4%).
5. `tools/native/bin_ab_linux.sh draws build/gtav/gtav <binary built from 2913c9c> 3 60` (renderer per-draw).
6. `BENCH_PERF=1 tools/native/bench.sh perf_new 60` then the remaining plan items
   (`gpu_idle_spins` 500→50, runtime build flags, object table shared_mutex, snapshot-stream reuse,
   record_thread/convert_threads/slot_targets, RPF prefetch/fadvise, audio health).
Acceptance: interleaved runs, mean delta > spread of repeats, 1% low not worse; visual check
(screenshots, flicker_catch, phone squares) after every kept renderer change; owner play-test.

## Not done (deferred)
Command-processor atomic wait instead of 50 µs polls and publishing `recorded_through_` right
after `Execute()`: logs show the CP almost never waits, so these are low value until measured.
