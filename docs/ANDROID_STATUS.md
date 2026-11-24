# Android port: status (2026-10-30)

**Next session (owner):** bring the Mac session's arm64 performance work (origin commits, the
HANDOFF_MAC_* / ARM64_NOTES_FOR_ANDROID.md updates) into this Android build and make it play as
well as it can. Nothing here is committed yet: merge it with the Mac work first.

Owner play-test 10-30 night: "plays pretty well". Menu: RB + Back on a controller, or the
overlay's Menu button. Max performance keeps per-pixel sun tiles on (off = gray 16x16 blocks).

GTA V runs in-game on a Galaxy S23 FE (Snapdragon 8 Gen 1, Adreno 730, Android 16), landscape and
letterboxed, with on-screen touch controls (a Bluetooth pad hides them). About 10-15 fps while the
GPU is thermally throttled (350 of 818 MHz after long sessions on the charger).

## Max-performance line 11-23 (branch `max-perf`, off `android-cpu`; Linux and Android only)

Owner: "keep on working on a max performance version that is solely focused on getting it to run
as fast as possible on Linux and Android". This branch turns on what the cross-platform line left
off. It rewrites every recompiled function, and nothing here was built or run on Windows, macOS or
iOS (they share the manifest): do not merge it into the release line without those tests.

What is on, all of it in the game's own recompiled code:
- Manifest `[entrypoint]`: `non_volatile_as_local` (r14-r31, f14-f31, v14-v31/v64-v127 as C++
  locals), `non_argument_as_local` (r0, r2, r11, r12, f0, v32-v63: 24% of all register mentions in
  the generated code, r11 alone 16%), `skip_lr` (no link-register store before every call: 5%).
  66 `share_registers` functions: the 64 from the nv work plus `sub_83452B24` (a fragment that
  continues with its parent's r11) and `sub_83779360` (a funclet that gets its frame in r12).
- SDK code generator (`rexglue-sdk-gtav-linux.patch`): `non_argument_as_local` could not work with
  fragments. Now share_registers functions and the `__save*/__rest*` helpers keep those registers
  in the context, and a caller hands over every local it uses (a placeholder in the call site,
  filled in once the whole body is generated: `function_graph.cpp`). `tools/native/nv_cfg.py
  --nonarg [--after-calls]` is the liveness check that found the two functions; its other hits
  were value-independent idioms (`subfe rD,rX,rX`) or code after calls that never return. With the
  options off the generator's output is byte-identical to before (767 files compared).
- `skip_lr`: the game has no `blrl` and never reads LR as data. The one kernel call that used it
  (`XexGetProcedureAddress`, to pick the caller's module for a thunk) now falls back to the
  executable's module (`function_dispatcher.cpp`). Guest stack frames hold no valid return
  addresses any more: crash reports cannot walk the guest stack on this branch.
- Byte-swap masks are constants: `VectorMaskL/R` and `VectorShiftTableL/R` were `inline uint8_t[]`
  (writable), so every vector load and store fetched its mask through the GOT and, on arm64,
  masked it again. `alignas(16) inline constexpr` in `gtav/fs/gtav_pch.h` and the SDK's
  `intrinsics.h`. Same results.
- One floating-point mode (`-DREX_FP_SINGLE_MODE` for the Linux and Android game code:
  `loader/src/install.cpp`, `tools/game_dll/build_game_dll.py`, CMake `GTAV_FP_SINGLE_MODE`). The
  generated code tested the host's denormal mode before every group of FP instructions (133,000
  sites) and wrote FPCR/MXCSR at 16,400 of them unconditionally. Threads now stay in the mode they
  start in, denormals kept. That is the exact one for scalar code; flush-to-zero would not be,
  because `lfs`/`stfs` are float<->double conversions here and would turn a small integer copied
  through a float register into 0. What differs from the console is VMX arithmetic on values
  below 1.2e-38 (the console flushes those to zero). `--guest_fp_always_flush=true` starts threads
  flushed instead, for experiments.
- `tools/release/expected.txt`: the 811 `gen` lines are the generated code of this manifest
  (installer-style run with the shipped partition seed), so the on-phone build passes its check.

The four hottest functions, arm64 instructions: audio mix loop `sub_831B5F98` 90 -> 61,
`sub_83210BD0` 540 -> 402, the main thread's `sub_83525E00` 3,164 -> 2,680, the job threads'
`sub_83594868` 1,246 -> 853. Game library 91.9 -> 66.7 MB.

Phone (S23 FE), the phone's own library (A, `android-cpu` code) against the max-perf library (B,
cross-built with `tools/game_dll/build_game_dll.py --android`), alternating boots from the same
battery temperature, 150 s warm-up, 40 s window, muted (`tools/android/lib_ab.sh`, new: it also
counts user-mode instructions and cycles per thread with simpleperf, which do not depend on the
heat caps):

| run | fps | draws | game threads M cycles / frame | job threads M instr / frame | caps mid / prime GHz | GPU idle ms / frame |
|---|---|---|---|---|---|---|
| A | 22.4 | 3,048 | 194.6 | 63.1 | 1.32-1.21 / 1.61-1.50 | 10.2 |
| B | 32.0 | 2,343 | 128.4 | 36.6 | 1.44 / 1.73 | 3.4 |
| A | 22.7 | 2,747 | 187.8 | 60.8 | 1.32-1.21 / 1.61-1.40 | 10.1 |
| B | 29.1 | 3,214 | 161.3 | 43.0 | 1.56 / 1.84 | 3.2 |
| A | 23.4 | 2,402 | 178.6 | 52.8 | 1.21 / 1.50-1.40 | 9.4 |
| B | 30.4 | 2,587 | 129.7 | 34.4 | 1.44 / 1.73 | 1.5 |

At equal draws (linear fit per side): about 25% fewer cycles a frame over all game threads and
about 35% fewer instructions on the job threads. The main and render threads spin while they wait,
so their counts include waiting and understate the gain. The audio mixer executes about 18% fewer
instructions a second but nearly the same cycles (809-843 against 818-897 M a second): its time is
not in instruction count. An earlier series the same morning (CPU time only) gave A 24.5 and 25.3
fps, B 40.6 (and 56.9 from a cold phone at 1,077 draws). No crash in any B run (five short ones, the
12-minute session below, and the GPU-side runs after it).

Two things changed with B:
- The clock caps: in every pair the B run got looser CPU caps than the A runs next to it, at the
  same or a higher thermal status. Less CPU work a frame leaves the CPU clusters cooler; the B runs
  heat the battery faster all the same (37.8 -> 44.5-44.8 C against 41.4-41.8 C in 190 s), because
  the GPU now works the whole frame.
- **The phone is GPU-bound now.** With B the GPU is idle 1.5-4.7 ms a frame (9-12 ms with A) and
  the record thread waits 2-5 ms a frame for a free frame slot (0.3 ms with A). GPU-side changes,
  which did not move fps while the game's code set the frame (render scale, load-op clears, coarse
  shading: all "no gain" on 10-31 and 11-22), are worth measuring again.

Heat-soaked, one 12-minute session per library from the same battery temperature (36.8 C), a 40 s
window every 150 s (`logs/android/mpL_longprobe.out`):

| window | A fps | A draws | A battery C | A caps mid / prime GHz | A GPU idle ms | B fps | B draws | B battery C | B caps mid / prime GHz | B GPU idle ms |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 24.4 | 2,477 | 41.4 | 1.44-1.21 / 1.73-1.50 | 10.7 | 47.0 | 1,676 | 44.9 | 2.34-1.56 / 2.73-1.84 | 5.5 |
| 2 | 22.0 | 2,616 | 42.1 | 1.21 / 1.40 | 14.7 | 29.1 | 2,958 | 45.1 | 1.32 / 1.61 | 3.6 |
| 3 | 23.1 | 2,830 | 42.8 | 1.21 / 1.40 | 9.7 | 35.8 | 1,046 | 45.4 | 1.32 / 1.61 | 0.9 |
| 4 | 21.8 | 2,742 | 43.2 | 1.21 / 1.40 | 15.9 | 34.9 | 1,065 | 45.6 | 1.32 / 1.61 | 1.0 |
| 5 | 23.8 | 1,619 | 43.5 | 1.21 / 1.40 | 13.9 | 42.0 | 732 | 45.7 | 1.32-1.21 / 1.61-1.50 | 1.1 |

A stays at 22-24 fps whatever is on screen (window 5: 1,619 draws, still 23.8 fps), so its frame
is the game's code. B holds 29 fps in the one heavy window and 35-42 in the lighter ones, where the
GPU has no idle time left and the record thread waits 7-14 ms a frame for a free frame slot. B ends
2.2 C hotter (thermal status 3 against 2) and still keeps looser CPU caps. In windows 3 and 4 B's
main thread executes 84 M instructions a frame against 20-25 M elsewhere: it polls while the GPU
finishes, on a prime core.

What limits B, measured the same morning (same-run A/Bs with `tools/android/ab.sh`, 4 minutes each
from 41.8 C, a | b fps in the in-world windows, heaviest scenes 2,300-3,000 draws):
- The GPU's clock is heat-capped: `/sys/class/kgsl/kgsl-3d0/thermal_pwrlevel` 10 = 285 MHz of 818
  at thermal status 3 (skin 44.8 C), 492 MHz at 44.0 C, 545 MHz at the start of a run. GPU frame
  22-33 ms at 285 MHz. Heat is the budget; every milliwatt the CPU side wastes is GPU clock.
- GPU ms per frame by pass kind (`--gtav_native_pass_gpu_times=true`, 2,280 draws, 150 passes,
  35 ms with the timestamps in): nine full-size colour+depth passes 5.5, G-buffer 5.4 in 5 passes,
  shadow map (906 draws) 4.6, reflection cube (6 multisampled faces, 343 draws) 3.7, all resolve
  copies together 4.6, nothing else above 1.7.
- Skipping every colour resolve copy (`gtav_native_skip_resolves=1`, wrong picture, the upper bound
  of what resolve work costs): 38.4 | 40.0, 33.5 | 34.0, 27.7 | 29.5, 28.4 | 30.3, 32.5 | 31.9.
  GPU 3-4 ms less. Resolve copies are not the lever.
- Clears as load operations (`gtav_native_load_clears`): 39.0 | 37.9, 36.5 | 37.5, 33.6 | 32.1,
  30.8 | 30.5, 28.9 | 28.2. Nothing, as before. Off.
- 2x2 coarse shading (`gtav_native_coarse_shading=2`): 27.4 | 28.1, 28.3 | 28.8, 26.3 | 26.8,
  26.4 | 27.3, 25.7 | 27.1. GPU 2-3 ms less, 2-5% fps, for a softer picture. Off.
- One record worker (`gtav_native_record_workers=1`): 35.2 | 36.6, 30.3 | 30.5, 28.5 | 28.5,
  28.2 | 28.7, 28.4 | 27.6. Recording 25-30 -> 18-22 ms, and all of it becomes record-thread idle
  time (1 -> 5-12 ms): in these scenes the record thread and the GPU were both at the frame
  (record 30 of 35 ms, GPU 32), and with the worker the GPU alone is. Off: no fps, one more core.

Linux (i5-13420H, GPU shared with another job, so `--gtav_native_render_scale=0.25` to let the
CPU side set the frame): 14 standing runs, old binary against max-perf. At equal draws the game's
threads use roughly 5-9% less CPU a frame; fps is inside the run-to-run spread (about 10% here).
The record thread, whose code did not change, drifted 4-6% the same way, so the real figure is at
the low end. The game's code does not set the frame on this laptop. Flush-to-zero against
denormals kept: no difference in two runs. 5-minute run-around soaks: clean on both builds of it.

Second batch, same day (the heat budget):
- **D3D's fence wait sleeps instead of spinning** (`gtav_fence_block_after_us`, 200 on Linux and
  Android, `gtav/src/gpu_fence_wait.cpp`). The game's main thread waits for the GPU inside D3D
  (8380CEF8), which loops on a spin step (83807DC8) with no kernel call. With the GPU as the slower
  side that is a core at full clock doing nothing: 66% of the main thread in a Linux profile, the
  84 M instructions a frame above. The spin step is hooked: after 200 us of a wait it sleeps until
  the renderer writes the fence (`capture.cpp` calls `gtav::FenceWritten()` at both writes), 2 ms
  at most at a time, then runs the game's step. Same-run A/B on the phone, spin | sleep:
  32.4 | 32.8, 36.8 | 37.2, 32.4 | 32.2, 30.1 | 29.6, 31.8 | 31.5, 30.6 | 30.5 fps. Linux: main
  thread 97% -> 41% of a core at full size, fps equal there and at quarter size.
  One 8-minute session each on the phone (`tools/android/lib_ab.sh mpb2f - A B`, spin first):

  | window | spin fps | draws | status | caps mid / prime GHz | GPU cap MHz | main M instr | sleep fps | draws | status | caps mid / prime GHz | GPU cap MHz | main M instr |
  |---|---|---|---|---|---|---|---|---|---|---|---|---|
  | 1 | 32.4 | 2,321 | 2 | 1.65-1.44 / 1.96-1.73 | 421 | (not counted) | 26.8 | 2,577 | 2 | 1.21 / 1.40 | 285 | 16.8 |
  | 2 | 26.1 | 2,598 | 3 | 1.32-1.21 / 1.61-1.50 | 350 | 21.1 | 27.9 | 2,422 | 2 | 1.21 / 1.40 | 285 | 15.7 |
  | 3 | 23.4 | 2,747 | 3 | 1.00 / 1.17 | 220 | 46.8 | 27.8 | 2,721 | 2 | 1.32-1.21 / 1.61-1.50 | 317 | 15.9 |

  Not a clean pair: the second session started from the same battery temperature (39.9 C) but a
  warmer phone, so its first window is already capped. What it shows: the spinning session slides
  to status 3 and a 220 MHz GPU (23.4 fps), the sleeping one stays at status 2 and 27-28 fps at the
  same battery temperature (45.2 | 45.3 C) and the same draws. One pair, order not reversed.
- **The D3D call-count probes are compiled out on Linux and Android** (`GTAV_D3D_PROBES`, CMake):
  `d3d_probe.cpp` hooked all 2,888 functions of the D3D runtime, so with the separately built game
  library each of those calls left the library and came back (two PLT jumps and the probe's test).
  The game library now imports 55 hooks instead of 2,935. The render thread's instruction count at
  equal draws is within the spread of the runs (0 to -5%): not resolvable, kept because it is free.
  **The shell and the game library must match again**: a library built against the old hook list
  does not load with the new shell (`files/libgtav_game.so.phonebuilt` on the test phone is such
  a library; the setup screen rebuilds).
- `tools/android/lib_ab.sh` can now compare two sets of flags across boots (B_LIBRARY `-`,
  `EXTRA_A` / `EXTRA_B`) and prints the GPU's thermal clock cap and the net battery current
  (`dumpsys battery`, "current now") of each window. The current only says something while it is
  negative (the phone drawing more than USB delivers).
- **Long Sleep(0) polls nap** (SDK, `guest_poll_patience_us`, 300 on Android; `guest_poll_nap_us`
  100). With the fence wait asleep the main thread still spent a quarter of a core in the kernel:
  its Sleep(0) poll loops (`XThread::Delay` -> `sched_yield`). A loop that has polled for 300 us
  without a pause (calls at most 50 us apart) now sleeps 100 us between polls. Per-thread CPU
  from `/proc` over two 47 s windows each (`lib_ab.sh mpb3n`): main thread kernel time 25.4 and
  28.9% of a core with plain yields, 14.0 and 13.1% with the nap (user time 36-40% either way);
  all threads' kernel time 99-107% -> 80-87%. Same-run A/B, yield | nap: 37.2 | 36.0, 36.9 | 37.0,
  33.0 | 33.4, 31.5 | 31.8, 30.2 | 30.4, 30.2 | 30.3 fps (and with a 1,000 us patience: 28.1 | 28.1,
  46.6 | 45.1, 36.6 | 34.6, 26.1 | 26.1, 25.6 | 25.1). Linux keeps plain yields (no CPU difference
  measured there; a yield that finds nobody waiting costs well under a microsecond on x86).
- Tried on the phone with this build, no change, left off or removed:
  - `gtav_native_fence_wait_draw` (fences released per draw instead of per frame): 27.2 | 27.3,
    33.2 | 33.4, 38.4 | 37.7, 32.8 | 32.2, 34.3 | 34.1, 31.5 | 31.6 fps.
  - Render passes that cover only the drawn area (an experiment in plume, removed again; with
    `gtav_native_load_clears`): the host targets are EDRAM-shaped (a 160x720 multisampled target
    for the 128x128 cube faces, 1280x721 for the 720 rows of the scene), so all but one of the
    game's ~22 clears a frame were "partial" and never became load ops. With the pass area
    following the scissor, 14-22 of them do and 38-66 of ~155 passes get a smaller area, none
    restarted. 30.0 | 29.6, 27.1 | 26.9, 28.6 | 28.6, 27.4 | 27.1, 28.9 | 28.6, 28.9 | 28.7 fps, GPU
    28.94 | 29.12, 34.46 | 34.14, 33.22 | 33.13, 34.42 | 34.30, 33.16 | 33.16, 33.30 | 32.87 ms; same
    picture (`ab_shots_diff.py`, 0 of 30 pairs out of range). Adreno does not spend time on the
    tiles nothing is drawn into, and loading the drawn ones is not what costs. Linux (NVIDIA,
    two 5-minute A/Bs): 36.2 | 34.3, 28.2 | 27.5, 25.7 | 26.6, 26.7 | 26.4, 28.5 | 25.3 fps, nothing
    there either.

Third batch, same day:
- **The game's interlocked operations no longer take the runtime's global lock**
  (`gtav_guest_irq_lock`, 0 on Linux and Android, `gtav/src/guest_irq_lock.cpp`). The 360's inline
  `Interlocked*` code switches interrupts off around its load-reserve / store-conditional pair
  (`mfmsr; mtmsrd r13; lwarx; ...; stwcx.; mtmsrd`), and the runtime maps "interrupts off" to its
  one global recursive mutex. So every reference count and counter update of every game thread
  queued for the same lock, about a million times a second: 5.5% of the process's CPU samples in
  the phone profile (a quarter of a core), most of it kernel time, since a thread that finds the
  lock taken sleeps for a section a few instructions long. Even asking "are interrupts on" took
  and released the lock.
  GTA V has 1,309 such sections (counted in the generated code). 1,308 are a load, register
  arithmetic and a store-conditional: exchange 424, add 778, compare-exchange 93, and/or 13. The
  generated store-conditional is a host compare-and-swap against the value the load read, atomic by
  itself, and nothing in between reads shared memory, so these need no lock. The last one is split
  over a helper pair (83829CC0 / 83829CD8) used by the pop and push of a lock-free pool's free list whose
  head is {first node, depth}: the pop reads the first node's link between the two calls and
  relies on the reservation, which a compare-and-swap does not emulate (a pop + push in between
  gives the same head value with another link). That pair keeps the global lock, through two
  hooks, so pool operations exclude each other as before.
  The shell and the game library must match again (57 hooks). With the game code inside the
  executable (the Linux dev build) the same switch is a build define, `REX_GUEST_IRQ_LOCK=0`, read
  by the generated `gtav_pch.h` (SDK template).
  Same-run A/B on the phone, lock | no lock: 40.0 | 42.1, 45.0 | 46.0, 46.4 | 50.7, 50.4 | 51.2,
  43.3 | 44.4 fps while the GPU clock was still 420-545 MHz, then 37.4 | 37.8, 41.8 | 41.1,
  44.5 | 44.9, 43.1 | 43.0, 43.0 | 43.2 once it was capped at 285-350 (the GPU sets the frame
  there). CPU over four alternating boots (lock, none, lock, none; `lib_ab.sh mpb7_glx - A B A B`,
  user + kernel % of a core from the thread snapshots): the 018xx job pool 76+24 | 59+8 and
  58+22 | 52+9, all threads' kernel time 91 | 59 and 85 | 69, all threads 5.54 | 4.82 and
  5.62 | 5.20 cores busy (the no-lock boots at lower CPU clocks, 1.34 and 1.30 against 1.50 and
  1.59 GHz mean on the mid cores), net battery current -652 | -276 and -564 | -271 mA.
  The fps of those four windows does not follow: 32.4 | 28.9 and 33.5 | 33.7 at 2,479 | 2,560
  and 2,677 | 2,212 draws. With the lock the GPU sat idle 7-9 ms of every frame waiting for the
  game (and stayed at thermal status 2 with a 350 MHz cap); without it the GPU works the whole
  frame, the phone reaches status 3 during the warm-up and the window is measured at a 285 MHz
  cap. Profile with the new defaults (`mpb7_prof`): the three lock calls 0.2% of the process's
  samples (were 3.9), mutex lock + unlock 0.5% (5.6), all `syscall` time 6.9% (11.0), the 018xx
  job pool 57% of a core (71). One 12-minute session per mode from the same battery
  temperature (36.9 C, `mpb8_long`) says nothing about fps: the two sessions drifted into
  different scenes (2,650-3,270 draws with the lock, 1,010-1,800 without; 24-29 against
  35-40 fps in windows 2-5), so only the same-run A/B above compares like with like.
  Linux at quarter size, same scene (2,035 draws), old | new binary: the four job threads
  56.3 | 38.1 and 59.4 | 35.2 % of a core, main thread 46.5 | 43.9 and 54.4 | 44.3, fps 54.6 | 62.1
  and 58.9 (1,721 draws) | 59.7.
- **Colour resolves between two textures of the same host format are image copies**
  (`gtav_native_copy_resolves`, on for Android). 44-46 of the ~64 resolves of an open-world frame
  were a render pass with one full-rectangle draw that loads each texel and stores it; they are
  `vkCmdCopyImage` now (not the multisampled ones, the depth ones, or those that convert 7e3).
  The record thread spent 15% of its time in resolves on the phone, and a third of the driver's
  allocations came from their draws. Same-run A/B, drawn | copied: 44.1 | 45.3, 47.8 | 47.8,
  45.9 | 46.3, 40.8 | 41.6, 42.5 | 42.2, 38.8 | 39.4, 33.5 | 34.3, 31.7 | 32.8, 31.2 | 31.8 fps; GPU
  27.5 | 26.6, 30.0 | 28.9, 30.0 | 29.4 ms in the last three. Same picture (`ab_shots_diff.py`,
  0 of 39 pairs out of range). In the profile after it the record thread's resolves are 10.8%
  (were 15.3), `vkCmdBeginRenderPass` 4.4% (6.8), the driver's `calloc` 4.4% (7.0). Linux
  (NVIDIA): 46 fewer passes a frame, fps and record time within the noise (30.8 | 29.4,
  26.1 | 23.6, 30.5 | 34.0, 33.2 | 31.2), same picture (no in-world pair out of range); left off
  there.
- Measured the same way and dropped: a lock-free "are interrupts on" test plus 200 tries before a
  waiting thread sleeps, still with the global lock (38.0 | 38.4, 42.3 | 44.8, 31.4 | 30.7,
  30.5 | 31.6, 28.7 | 28.9, 27.3 | 27.2, 27.0 | 26.9 fps): superseded by not taking the lock.

Fourth batch, same day (SDK waits, from the profile taken after the third batch):
- **Events and semaphores wake their waiter after releasing the handle's mutex**
  (`posix_wake_unlocked`, SDK `threading_posix.cpp`). The signalling thread called
  `notify_one` while it still held the mutex the woken thread needs to return from its wait, so
  that thread went back to sleep on the mutex at once and the unlock had to make a second kernel
  call to wake it; neither bionic nor glibc moves the waiter over. A thread in a multi-handle
  wait woken that way found the mutex taken and spun on `sched_yield` until the signaller got
  to its unlock: that was most of the game's audio callback thread. Kernel objects are
  reference-counted and the caller holds a reference across the call, and the host-side events
  are members of long-lived objects, so the handle outlives the wake-up.
  Profile before | after (`mpb7_prof` | `mpb9_prof`, share of the process's samples): kernel calls
  made by a mutex unlock 1.20 | 0.27%, threads sleeping on a mutex 0.53 | 0.14%, `sched_yield`
  inside multi-handle waits 0.86 | 0.00%, the audio callback thread 1.59 | 0.58%, all kernel
  calls (`syscall`) 6.96 | 5.29%, all `sched_yield` 1.81 | 0.76%. Same-run A/B on the phone,
  under | after: 34.2 | 33.9, 34.6 | 34.5, 33.7 | 33.4, 31.5 | 32.0, 37.3 | 39.6, 30.1 | 30.4,
  28.8 | 29.2 fps with the GPU capped at 285-317 MHz and working 23-32 ms a frame (it sets the
  frame; the saving is CPU time, not fps), 0 bad lines.
  Linux same-run A/B at quarter size, under | after: 71.5 | 69.6, 51.9 | 47.7, 44.2 | 45.9,
  55.0 | 65.6, 62.9 | 59.5, 52.4 | 53.7, 45.5 | 47.9, 57.7 | 59.2 fps (mean 55.1 | 56.1: the shared
  GPU sets the frame there, nothing to see).
- Measured and not kept: the critical-section spin loop reading the lock word before it tries
  the swap, with a pause in between. Four job threads spend about half of their job function
  (833CB750, which takes one global section for every batch of work) spinning for that section,
  1.5% of the process's samples; the change moved the spinning from the failed swaps into the
  loop (swaps 1.19 -> 0.33%, the loop itself 0.36 -> 1.40%) and saved nothing.
- What the profile says is left on the CPU side, largest first: hashing guest textures and
  vertex buffers every frame for want of a write watch (three `gtav-hash` threads on the little
  cores, 11-12% of the process's samples: 340-530 textures up to 64 KB, 7-10 MB, and 1,000-2,000
  vertex buffers a frame), the game's own audio mixer and XMA decoding (11-12%), the driver under
  the record thread, and ~5% in kernel calls for waking and parking job threads (~4,000 tiny jobs
  a second).

Fifth batch, same day (where the GPU's time goes, thread placement, fewer render passes):
- GPU ms per frame by kind of pass with the fourth batch's build
  (`--gtav_native_pass_gpu_times=true`, `mpc1_pass`, 2,650-2,880 draws, GPU capped at 421 MHz,
  25.4 ms with the timestamps in): the G-buffer fill (four colour targets + depth, 1280x721) 8.2
  in 7 passes; the lit scene's target with depth 3.9 in 8.5 passes; the shadow map (640x2561, one
  pass, ~1,000 draws) 2.5; the reflection cube (six multisampled 128x128 faces, ~345 draws) 2.3;
  the first G-buffer target alone with depth 1.0 in 4.9 passes; full-size resolve copies 0.9;
  640x720 depth-only 0.6; depth resolve copies 0.6; nothing else above 0.5.
- No gain, same-run A/Bs: anisotropic filtering capped at 2 instead of 16
  (`gtav_native_max_anisotropy`: 45.5 | 44.9, 47.4 | 47.3, 45.3 | 45.2, 40.9 | 40.4, 39.8 | 39.4,
  38.4 | 38.4, 34.6 | 33.8 fps) and the top mip level skipped (`gtav_native_texture_min_lod=1`:
  42.2 | 43.2, 42.5 | 42.2, 35.0 | 35.0, 31.9 | 32.1, 31.9 | 33.1). Both stay as they were.
- **The record thread has the prime core, the main thread runs on the middle cores**
  (`gtav_android_record_cores`, 3 by default now, live; `android_threads.cpp`). The record thread
  is at the frame time in every thermal state and shared the three middle cores with the game's
  render, audio and job threads, while the main thread, which waits for its jobs and for the GPU
  about half of every frame, had the prime core to itself. Same-run A/B, middle cores | prime
  core with the main thread on the middle cores (`mpc2_rc3`, GPU capped at 285-421 MHz):

  | fps | recording ms | GPU ms | draws |
  |---|---|---|---|
  | 48.0 \| 49.1 | 17.7 \| 13.4 | 18.0 \| 17.9 | 1,662 \| 1,674 |
  | 48.7 \| 51.2 | 16.9 \| 12.1 | 18.2 \| 17.9 | 1,593 \| 1,574 |
  | 34.3 \| 34.0 | 25.1 \| 19.4 | 26.2 \| 27.5 | 2,385 \| 2,441 |
  | 28.9 \| 30.2 | 29.3 \| 22.2 | 32.6 \| 32.0 | 2,751 \| 2,816 |
  | 29.6 \| 30.6 | 30.3 \| 22.9 | 31.8 \| 31.7 | 2,876 \| 2,861 |
  | 28.4 \| 29.3 | 31.1 \| 24.0 | 32.6 \| 32.9 | 2,893 \| 2,911 |

  Recording takes 23-28% less time; fps moves 0-5% because the GPU is the other side at the
  frame (its time is unchanged). The other placements are worse than the old one: the record
  thread on the prime core next to the main thread (1) 29.5 | 26.1, 32.2 | 30.6, 34.1 | 30.0,
  31.2 | 28.5, 27.4 | 25.1 fps (recording 25-31 -> 31-39 ms); the record thread free to use the
  middle and the prime cores (2) 26.7 | 24.9, 39.1 | 40.3, 26.3 | 24.8, 35.9 | 32.7, 29.1 | 26.6.
- **Draws into some of the bound targets keep the render pass** (`gtav_native_keep_targets`, live,
  off until it is measured on the phone). The G-buffer fill alternates between its four colour
  targets and the first one alone (the guest only changes its colour mask), and the lit scene
  between colour + depth and colour alone; the renderer started a pass with the smaller set every
  time. In a frame dumped on Linux (`GTAV_NATIVE_PASS_STATS=1 --gtav_native_seqdiag=1`) the
  G-buffer is 443 draws into 4 targets, 69 into 1, 69 into 4, 8 into 1, 106 into 4, 1 into 3,
  31 into 4: seven passes over the same five attachments, each a store and a load of all of
  them on a tile GPU, and those seven passes are the 8.2 ms above. With the setting on such a
  draw is recorded with the bound set and writes nothing to the rest (the colour mask of the
  attachments it does not use is already 0, a draw without depth has its depth and stencil tests
  off). Linux (NVIDIA), same-run A/B `mp_kt_ab`: 153 -> 144.5 passes a frame, same picture
  (`ab_shots_diff.py`, 0 of 425 pairs out of range; in-world neighbours differ only where
  something is animated), fps and recording time inside the noise there. **Not measured on the
  phone yet** (it was not connected): `logs/phone_c3.sh` installs the build and runs the A/B, a
  screenshot comparison, the GPU time by pass kind and the placement A/B again with the merge on.
- The per-texture hash timers read the clock on the record profile's sampled frames only (every
  8th): 800-2,500 pairs of clock reads a frame were 2.5% of the record thread in `mpb9_prof`.
- Measured on Linux and removed: the count of recorded draws that the GPU fences wait on moved
  every 8 draws instead of at every draw (the store was 3.9% of the record thread in the phone
  profile: the game's GPU thread reads the same cache line at every fence). Record thread CPU
  per draw 4.92 | 4.98 us over ten same-run windows (`mp_ps_ab`), and that run copied far more
  draw data at fences (up to 37,900 draws, 1 GB, per 300 frames against 0-5,000 in the run
  before it): a count that is behind makes fences wait or copy.

## Perf session 11-23: the game's CPU side (branch `android-cpu`, off `mt-record`)

Owner: "let's get android running insanely fast, like how can we speed up the game cpu stuff?"
Method: measure where the CPU time goes, fix, same-run A/B (`tools/android/ab.sh`, flips every 60
frames, both sides see the same heat), keep or drop. All runs muted; **sound was never checked by
ear**.

Tools (new):
- `tools/android/cpu_probe.sh NAME [WARM] [SECS]`: per thread busy %, runnable-but-waiting %,
  switches a second, which cores it ran on, the clock each cluster really ran at, and the hottest
  functions of the busiest threads (simpleperf). `ATTACH=1` probes a game that is already running.
- `tools/android/sched_trace.sh NAME` + `sched_trace.py`: from an atrace scheduler trace, who
  wakes whom, wake-up delays split by idle/busy target core, and what the job threads were doing
  whenever the main thread was not running.
- The SDK now names every game thread `XThread<hardware thread>p<priority><id>`, so the tools (and
  `android_threads.cpp`) can tell job threads (priority 1-15), the audio mixer (16+) and the rest.

Where the phone is after this pass (final probe `cpu_final`, thermal status 3 at 44 C, caps
1.21 GHz middle / 1.40-1.50 prime / 1.36 little): 4.93 cores busy and 2.37 cores' worth of
threads waiting for a core. Main thread 68% of the prime core (+17% waiting; it was 40% + 39%
before the pass), the game's audio mixer 65%, the game's render thread 61%, record thread 61%
(70% before shared expansions), eight job threads 14-25% each. The frame is set by the game's own
code; the record thread waits 4-9 ms a frame for it. Picture check with shared expansions off | on
while walking (`walk_squares.sh --ab`): 0 wrongly lit tiles on both sides.

Kept (a | b fps, same run, heaviest scenes last):
- Four SDK fixes, commit 9391045 (alertable waits spinning on the clock, wake-all on semaphores
  and auto-reset events, a useless XMA worker wake per kick, decoder tables rebuilt per sound):
  28.3 | 30.2, 25.0 | 27.3, 25.2 | 27.4, 22.5 | 24.3, 21.3 | 23.5.
- Main thread at nice -10 (`gtav_android_main_nice`): 24.7 | 25.7, 22.9 | 23.9, 19.6 | 22.2. It
  sleeps and wakes ~2,400 times a second and found its core taken each time. -20 adds nothing.
- Job threads at nice -8 (`gtav_android_job_nice`): 28.1 | 29.3, 28.4 | 28.2, 23.8 | 24.3,
  21.5 | 22.5, 24.3 | 24.8.
- Background helpers ("xma-fresh") on the little cores at low priority.
- CPU rect lists share one expansion per frame (`gtav_native_rect_share`): the tiled lighting
  draws the same 3,600 rects eight times with eight shader pairs, and each draw expanded and
  uploaded them again. Record thread 21.98 | 18.82, 28.80 | 24.59, 32.51 | 29.05, 37.00 | 32.96,
  37.43 | 33.39 ms a frame; fps 36.9 | 38.0, 28.7 | 29.6, 27.4 | 27.6, 24.1 | 24.3, 22.7 | 22.8
  (the game sets the frame, so most of it shows up as record-thread idle time). Linux on the CPU
  path: 17.3 -> 13.9 ms at ~2,650 draws, same picture (0 of 176 capture pairs differ).
- `DrawsRecorded` on a cache line of its own (it shared one with a value the game's render thread
  writes at every draw; 2.7% of the record thread).

Measured and left off / removed:
- Audio mixer thread at nice -10 (`gtav_android_audio_nice`): 32.4 | 31.3, 29.0 | 28.8,
  30.4 | 29.8, 26.9 | 26.5, 25.4 | 24.7. Slightly worse.
- The game's render thread at nice -8 (`gtav_android_render_nice`): 37.8 | 37.4, 31.2 | 29.4,
  27.5 | 26.8, 23.9 | 23.2, 22.3 | 21.4. Worse: it takes time from the threads the frame waits for.
- One record worker (`gtav_native_record_workers=1`): 33.1 | 31.0, 29.4 | 29.0, 30.9 | 30.2,
  27.3 | 26.7, 25.9 | 24.8. Recording 24-29 -> 17-20 ms and the record thread's idle time doubles
  (5-9 -> 13-19 ms): the extra thread only takes a core from the game.
- Job threads off the little cores (`gtav_android_job_cores=1`): 29.5 | 28.5, 39.8 | 38.2,
  35.1 | 32.6, 32.5 | 31.8, 27.2 | 24.8. Worse: they push the record and render threads around.
- Two record workers on the big cores: 27.2 | 26.0, 23.5 | 21.7, 23.3 | 21.4, 23.3 | 21.9 (record
  33 -> 19 ms, and 25 ms of it idle); on the little cores 26.3 | 25.8, 30.6 | 28.1, 31.2 | 27.9.
- Spinning before a zero-length guest sleep (16 spins): 32.5 | 31.8 ... 24.2 | 23.3. Removed.
- A 100 us sleep instead of a yield for low-priority guest threads' `Sleep(0)`: 27.7 | 27.2,
  25.4 | 25.1, 25.6 | 26.1, 23.5 | 22.8, 23.7 | 23.9. No difference; the SDK keeps its 100 us.
- Vblank thread slowed when vsync is off, skipping wakes of idle waiters, the prime core for the
  main thread alone: no difference. Removed (the last one stays as a cvar, off).

- Codegen option `non_volatile_as_local` (the game's r14-r31 and friends as C++ locals instead of
  context fields): the 11-21 boot failure is solved in the SDK's code generator (details in
  `docs/LINUX_PERF_2026-11-12.md`). Game library cross-built both ways and swapped on the phone,
  four alternating runs with cooling waits, CPU time of all game threads a frame: 155.9 ms at
  2,508 draws and 135.8 at 1,902 without, 151.9 at 2,467 and 145.8 at 2,371 with: 2-4% less at
  equal draws, fps inside the run-to-run spread, no crash in eight runs. Left off: it rewrites
  every game function on every platform for a gain that small, and indirect calls into fragments,
  setjmp and long play are untested. (On in the `max-perf` line with the other register options,
  see the top section; the manifest patch file is gone there.)
  (Two libraries cannot be flipped inside one run, so this comparison is between boots: the scene
  differs by several hundred draws from boot to boot, which is why it needs the per-frame CPU
  figure and cannot resolve less than about 3%.)

What is left, in order of what it could give (none of it is a quick switch):
- The phone runs at about half its clock when warm (1.2-1.45 of 2.5-3.0 GHz, GPU 285-490 of
  818 MHz). A run started at 27 C battery gave 41-44 fps at 2,000-2,750 draws; started at 36 C
  the same draw counts give 25-31. A cooler phone (not charging while playing, out of its case)
  is worth more than any item below.
- The game's audio mixer thread: 65-75% of a middle core, its own mixing code plus XMA decoding.
- The record thread's per-frame hashing of vertex data (6% of it) and the Adreno driver's share
  (about 18%): only matter once the game stops setting the frame.
- Fewer draws (shadow cascades, tiled lights): a picture trade-off, the owner's call.

The rect-list geometry shader on Adreno 730, second look: DXC's SPIR-V still asserts in the
driver's compiler in every form. The same shader written in GLSL and built with the NDK's glslc
compiles, and then the tiled lighting's rects come out sheared/black, in six forms (select,
branch, arithmetic weights, no rejection, two triangles, zero-padded streams); the same GLSL
shader is right on Linux. Not pursued: with shared expansions the CPU path costs within 0.4 ms a
frame of the geometry shader on a desktop (12.42 vs 12.07 ms at ~2,800 draws).

## Perf session 11-22: multithreaded recording built, phone not bound by it (branch `mt-record`)

Full notes: `docs/HANDOFF_MT_RENDERER_2026-11-22.md`. `--gtav_native_record_workers=N` records a
frame on N extra threads (off by default everywhere). On the S23 FE two workers cut recording
29-32 -> 18-19 ms, and the frame got 1-2 ms *longer*: the renderer was not the limit.
- Proof: `--gtav_native_ab=gtav_native_record_pad_ms=0|8` (8 ms added to every frame's recording):
  frame 38.0 vs 38.0, 47.4 vs 46.6, 46.5 vs 46.0 ms. The A/B line's new `idle` column (render
  thread waiting for the game's next frame) reads 5-13 ms on the phone.
- The game's main thread sets the frame: it waits ~47% of the time in ~80 short waits a frame for
  the game's job threads; the game's render thread waits on it 32%. The four fast cores are full
  (renderer ~68% of one, audio thread ~59%, the game's render thread ~50%, command processor ~43%,
  main thread, job threads).
- Tried on top, all left off: workers on the little cores (too slow: recording becomes the limit,
  27-32 fps against 30-37), with small chunks (neutral), nothing but the main thread on the prime
  core (`gtav_android_prime_exclusive`, neutral), the game's threads off the little cores
  (`gtav_android_guest_big`, worse).
- Also off after a test: the guest GPU thread released when the frame is recorded instead of
  after the present (`gtav_native_early_fence`): 44.47 vs 44.50, 44.55 vs 44.17 ms.
- The one-piece draw code (`DrawImpl`) is gone: every draw now goes through the two halves, on the
  render thread when workers are off. Same run, old against new on the phone: render thread CPU
  26.05 vs 26.35 and 22.35 vs 22.63 ms, frame 33.5 vs 34.3 and 31.1 vs 31.4 ms; walk check 0
  wrongly lit tiles on both. Commit 74463ae is the last with both paths.
- What would move phone fps now: less CPU work in total on the fast cores (the renderer's vertex
  hashing without a write watch ~10 ms a frame, the driver's ~10 ms of draw calls, the audio
  thread), or a faster main thread. Not more threads.

## Perf session 10-30/31 (Mac arm64 work merged; measured on the phone, live A/B per change)

Branch `android` (not pushed). Numbers: same-run `tools/android/ab.sh NAME 'cvar=a|b'` A/Bs.
Cold the game reaches 28-29 fps in the open world; hot (battery 40-45 C on the charger, GPU
220-317 MHz, CPU clusters ~1.1-1.4 GHz) 14-17 fps: record 40-45 ms, GPU 38-42 ms per frame.

Kept:
- Leaving the app (Home, app switch, lock screen) crashed in vkCreateFramebuffer
  (VK_ERROR_SURFACE_LOST_KHR): the swap chain is rebuilt on SDL's new window (24 ms after resume).
- Main game thread pinned to the prime core (`gtav_android_main_prime`): +3-4%. It is busy ~80%
  of the frame (`--gtav_wait_profile`) and the scheduler had it on little cores 45% of the time.
- Fence wait on (as on macOS): neutral to +5%, prevents the open-world one-frame slabs.
- New packets start at the largest arena seen (no growth copies).
- args.txt `env:` lines are set before libmain.so loads (static initializers read some).
- Presets: Best image = game limiter 30 + vsync; Max performance = uncapped.
- appCategory=game in the manifest (Game Booster options for the app).

Measured and left off on Android (all reversible by cvar):
| change | result |
|---|---|
| SDK page-protection write watch | 2x slower (its global lock on every fault) |
| hash worker threads (macOS win) | not a win |
| in-pass (fetch) resolves | neutral (GTAV_NATIVE_FETCH_RESOLVE=1 opt-in) |
| draw clears off (vkCmdClearAttachments) | neutral |
| texture recheck 2 vs 8 frames | neutral |
| worker conversion of per-frame vertex data (`gtav_native_convert_threads`) | 1-6 ms slower recording |
| XMA decoder flush instead of reopen (`xma_flush_streams`) | ~20% less audio-thread CPU but 13-16 audio bursts vs 4 in the scripted desktop capture |

Profile (simpleperf, cold): render thread ~16% of all samples (XXH3 hashing 14%, vertex
conversion 12% of it), game D3D thread 12%, main thread 11%, audio 17% (a fifth of that was the
XMA codec reopen), command processor 8-9%. Per frame ~7-8 MB of fence-copied vertex data is
converted (snapshots), 1.7-2 MB of rect lists.

What 30 fps hot would take (not started): halve both CPU recording and GPU time. Candidates:
render resolution below 720p (shaders compute 16 px tile indices from FragCoord: needs a scale in
every recompiled shader), recording on several threads (secondary command buffers), fewer GMEM
loads per pass (LOAD_OP_CLEAR/DONT_CARE where a pass starts with a full clear), less charging heat
(Game Booster "Pause USB Power Delivery", or play unplugged).

## Release layout (11-01): the APK holds no game code

The owner's rule: nothing Rockstar-derived is published (Take-Two / re3). The developer APK
(`build_apk.sh`) has the recompiled game inside libmain.so (~300 MB) and must stay private.
The public one (`RELEASE=1 build_apk.sh`, packaged by `tools/android/make_shell.sh dist/android`):

| library | what | from |
|---|---|---|
| libmain.so (41 KB) | shim: SDL calls its SDL_main; it dlopens `<files>/libgtav_game.so` and calls SDL_main in libgtav_fast.so | src/android_main_shim.cpp |
| libgtav_fast.so | all our code (renderer, hooks, bridge), `GTAV_GAME_DLL=ON`; the 2931 game functions it calls stay undefined | build/android-dll |
| libgtav_setup.so | setup screen's data install (install.cpp RunDataInstall over JNI) | src/android_setup_jni.cpp |
| librexruntime.so, librexgpu-xenos.so, libc++_shared.so | SDK, as before | |
| libgtav_game.so | **not in the APK**: the player's game code, from their PC | Build for Android |

Binding: bionic resolves undefined symbols inside the tree one dlopen loads (game ->
libgtav_fast -> librexruntime -> libc), so the game finds the hooks/bridge and libgtav_fast finds
the game functions, both at load (`-z now`). The game library must be in private storage
(`getFilesDir()`): shared storage is noexec.

Player flow: PC launcher "Build for Android" / `--android` (install.cpp RunAndroid: codegen +
reference check, freestanding aarch64-linux-android30 compile with the bundled LLVM 19, lld
`-flavor gnu` against `android/link/`, Adreno shaders through adreno_shader_fix.cpp) ->
`android/gtav-android-pack.zip` (libgtav_game.so, spirv/, expected.txt, pack.txt with the hash of
the libgtav_fast.so it fits). On the phone SetupActivity takes the pack and the three disc files
(SAF; the installer reads `/proc/self/fd/N`), unpacks, installs `data/{game,update,user}` into the
external files dir, then starts GameActivity, which then passes those roots and the built-in
release flags.

Not done yet: a phone test of the whole flow; on-device codegen/compile (clang/lld/dxc for
Android: tools/android/build_llvm.sh exists, building downloaded LLVM needs the owner's OK).

## Build and run (from Linux, phone on adb)

```sh
tools/android/build_sdk.sh            # SDK for arm64-v8a (NDK r29, API 30, armv8.2-a+lse)
tools/android/build_game.sh           # libmain.so (codegen runs with the host rexglue)
tools/android/make_spirv.sh           # game SPIR-V for Adreno -> build/android/spirv (push it)
tools/android/build_apk.sh install    # debug APK without Gradle (SDLActivity + GameActivity)
tools/android/set_args.sh [--flag=v ...] [env:NAME=V]   # files/args.txt on the phone
tools/android/run.sh [secs]           # restart, wait, print crash + log tail
```
Data on the phone: `/sdcard/Android/data/com.gtavrecomp.app/files/` with `game/disc2_files`,
`game/update`, `userdata`, `spirv`, `args.txt`, `logs/gtav.log`, `logs/stderr.txt`. Files pushed
with adb belong to the shell user: `chmod -R a+rwX` them (the app sees its own folder directly).
Phone storage is case-insensitive, like the Xbox.

Debugging without root: crashes write a tombstone (`adb logcat -b crash`) and, for faults the
SDK's handlers see, a backtrace in `logs/stderr.txt`. `tools/android/debug_crash.sh` runs the game
under lldb (lldb-server via run-as); `simpleperf record --app com.gtavrecomp.app` profiles.

## Adreno 730 / driver 0615.103 facts (Vulkan 1.1, no shaderInt64)

- 64-bit shader integers: `tools/android/lower_int64.py` lowers them to uvec2 (game shaders in
  make_spirv.sh, the renderer's own shaders via lower_header.py). The compiler also asserts on
  64-bit shifts and 2-component device-address loads; the menu shader uses a vertex buffer.
- `vkWaitForFences(UINT64_MAX)` returns VK_TIMEOUT early: plume now waits in slices. Treating
  the timeout as "done" reused in-flight command buffers and looked like random GPU faults.
- Core `vkGetBufferDeviceAddress` is a null driver entry: plume prefers the KHR one.
- minImageCount is 3: 5 swap chain images, or acquire blocks with frames in flight.
- The rect-list geometry shader crashes the compiler: rect lists on the CPU (gtav_native_rect_gs).
  (11-23: a GLSL build compiles but draws the lighting tiles wrong; see the 11-23 section.)
- Failed pipelines: plume returns null now (it returned objects with null handles).

## SDK / SDL changes (rexglue-sdk-gtav-linux.patch, sdl3-gtav.patch, plume-gtav.patch)

Android platform bits (fibers without ucontext, memfd guest arena reserved with
MAP_FIXED_NOREPLACE, ANativeWindow surface, SDL_main, GPU plugin path), `window_presenter` cvar
(one surface per window), SEH/fault handlers chain instead of dying silently, SDL's Android event
wait no longer spins (poll sentinel woke it), controller pumping rate-limited.

## Performance notes (10-30 evening)

- Pixel shaders were ~60% of the GPU frame (flat-PS A/B: 44 -> 18 ms). Shader constants now come
  from dynamic uniform buffers (set 4; tools/android/ubo_constants.py + plume
  CONSTANT_BUFFER_DYNAMIC): GPU frame at full clock ~20-29 ms (was 43-46).
- CPU recording is now the limit (~25-36 ms): vertex streams (big-endian conversion, now with a
  bulk NEON path), texture/stream hashing (no write watch on kernel 5.10), bind+draw.
- libmain is a shared library: -Bsymbolic-functions / -fno-semantic-interposition (was 10% PLT).
- Audio folded to stereo (audioserver 40% -> 9% of a core). Renderer threads pinned to big cores.
- Heat: the phone throttles the GPU to 220-350 MHz within minutes on the charger.

## Next

1. (FIXED) Blocky 16x16 tile artifacts: the resolve-to-memory compute shader (native_memory.hlsl)
   did not compile on Adreno, so no resolve reached guest memory and the game built its
   lighting-tile lists from a stale table. It compiles once lowered (lower_header.py).
2. CPU recording: write watch via mprotect, or parallel stream conversion.
3. Intermittent guest null read (sub_833CB750, once) - watch for repeats.
4. Thermal: frame cap / vsync defaults for sustained play.

## 10-31: rendering fixes and render scale

- Render scale 0.75 by default on Android (gtav_native_render_scale): GPU -4..9 ms a frame when
  throttled; the G-buffer and shadow passes are geometry-bound and do not shrink.
- Grey/mosaic blocks on textures: new render targets and resolves start zeroed (Adreno hands out
  recycled memory); R16G16B16A16_UNORM (not sampleable on Adreno) textures stored as SNORM; point
  filtering for 32-bit float fetches (no linear filtering on Adreno).
- Light 16x16 squares in shade while moving: the tile classification variant (_tiles2) now keeps
  fully shadowed tiles in the per-pixel sun draw (they were drawn by neither sun variant). Found
  with frame dumps (mem_*.bin) and a flat-colour view of the sun draws. Tools:
  tools/native/count_squares.py, tools/android/walk_squares.sh --ab (live A/B in one walk).
- Final sustained run (cool start to 44 C battery, GPU 317-492 MHz): 24-36 fps, mostly 26-35.
- Measured off: load-op clears, record thread, coarse shading (available as cvars).
