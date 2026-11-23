# Handoff: multithreaded frame recording (for the Mac, iOS and Windows sessions)

Written 2026-11-22. Owner's request: "build the multithreaded renderer for android and linux that
will also work mac ios pc too and ill test it there once we get android and linux dialed".
Branch **`mt-record`** (off `perf-linux` @ 6dbeaeb), commits f5a6eb3 and 6b5d508 plus the ones after
them. Nothing is on main or in a release. **The worker threads are off by default on every
platform** (measured on Linux and the S23 FE: no gain on either, see below). What did change for
everyone: the draw code is now the two-half path, run by the render thread itself when there are
no workers.

## What it is

The native renderer's render thread (`gtav-record`) used to turn a captured frame into Vulkan
commands alone. With `--gtav_native_record_workers=N` (N = 1..6, live, also A/B-able) it only
prepares each draw and N worker threads finish them:

- **Front half, render thread** (`Backend::Impl::DrawFront`): skip filters, shaders, render
  targets, texture slots (hashing, uploads), vertex layout, pipeline, `BindTargets`, viewport,
  scissor, depth bias. Result: a plain `DrawRecord`.
- **Back half, any thread** (`DrawBack`): constants (byte swap + upload), vertex and index data
  (hash, convert, snapshot and rect caches), binds, the draw call. It touches only its
  `RecordContext`, the record, guest vertex memory and the caches (locked per entry).
- **Stream** (`deferred_list.{h,cpp}`): every plume call of the frame, and the draw records between
  them, go into a compact stream. It is **cut into chunks where a render pass begins**
  (`MaybeCutStream` in `BindTargets`, never inside an occlusion query) once a chunk holds
  `--gtav_native_record_chunk_draws` draws (default 96). Each chunk is replayed into a command list
  of its own by whichever worker is free; the render thread records what nobody has started once it
  has prepared the last draw; the lists are ended and submitted in order in one `vkQueueSubmit`.
- **plume patch** (`plume-gtav.patch`, apply it again): barriers are resolved against plume's
  per-texture state when the call is made, in frame order (`resolveBarriers`), and recorded later
  from the copied old state (`barriersResolved`); copies and resolves carry the layouts their
  textures had (`trackedLayout`, `overrideNextCopyLayouts`). The stat counters are atomic. The new
  virtuals have defaults, so a backend without them still works (it then never cuts).
- **Guest memory**: `DrawsRecorded` is now an in-order watermark over the chunks
  (`Impl::ChunkDone`, `capture.cpp`: the count only moves forward).

**There is one draw path now.** With 0 workers the render thread runs both halves itself, draw by
draw, into the frame's own command list. The one-piece `DrawImpl` and `--gtav_native_split_draw`
were removed once the two halves matched it in picture and cost (numbers below). Commit 74463ae is
the last one with both paths and the switch, if a picture ever has to be compared against the old
code. Dump frames (F9, `--gtav_native_dump_frame`), the flicker inspector and
`--gtav_native_convert_threads` work on the new path; such frames are recorded on the render
thread alone, whatever the worker setting.

## Settings

| flag | default | |
|---|---|---|
| `--gtav_native_record_workers=N` | 0 | worker threads; 0 = the render thread does both halves |
| `--gtav_native_record_chunk_draws=N` | 96 | draws per chunk before a cut (0: one chunk, in order) |
| `--gtav_native_record_chaos=1` | 0 | test: random cuts, workers pause at random draws |
| `--gtav_native_record_pad_ms=N` | 0 | test: sleep N ms inside every frame's recording |
| `--gtav_native_record_worker_cores=0/1/2` | 0 | Android: workers on the big cores / little cores / unpinned |
| `--gtav_native_early_fence` | false | test: let the guest GPU thread go on when the frame is recorded, not after the present |
| `--gtav_android_prime_exclusive`, `--gtav_android_guest_big` | false | Android thread placement tests (below) |
| `--gtav_native_ab='a=x\|y;b=p\|q'` | | the in-run A/B can now flip several settings together |

The in-game menu (F1 / Esc menu; RB + Back or the overlay button on phones) has **"Record threads
(experimental)": Off / 1 / 2 / 3**, live, saved in `gtav_settings.ini` with the other settings.
`gtav_settings.ini` is applied over the command line, so `--gtav_native_record_workers=N` only
takes effect with `GTAV_IGNORE_SETTINGS=1` (the bench scripts set it) or before the menu has saved
anything; `--gtav_native_ab=...` is not affected.

The A/B line now reads `frame, median, p90; record, cpu, idle, GPU; draws, skipped, passes, barriers`
per side. **`idle`** is the render thread waiting for the game's next frame: when it is several ms,
the game sets the frame time and a faster renderer cannot raise fps. `cpu` is the render thread's
own CPU time (0 on Windows).

## Is it right? (Linux, laptop loaded by other jobs)

- Same-run A/B, capture pairs 30 frames apart (`GTAV_NATIVE_SHOT_DIR`, `GTAV_SHOT_FRAMES=15`,
  `tools/native/ab_shots_diff.py`): cross-side difference inside the same-side range for 1 worker
  (0 of 162 pairs out), 2 workers (0 of 184) and 3 workers + chaos at 43 chunks a frame (0 of 191).
  Draws, passes and barriers per frame equal on both sides.
- Vulkan validation layer (`VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation`, messages land in
  `logs/NAME.stdout`): 3 workers + chaos gives the same messages as 0 workers (a fence used on two
  threads, one render pass format mismatch, an acquire timeout: all there before), none new.
- S23 FE: `tools/android/walk_squares.sh mt_sq --ab 'gtav_native_record_workers=0|2'` (40 s walk,
  a frame captured every second): 0 wrongly lit tiles in 8 frames without workers and 0 in 7 with
  them; frames from both sides looked over by eye (whole picture, HUD, phone overlay).
- No crash in any run on Linux or the phone.

**Old one-piece path against the two halves on the render thread** (same run,
`gtav_native_split_draw=false|true`, commit 74463ae, before the old path was removed):

| | render thread CPU ms, old vs new | frame ms, old vs new |
|---|---|---|
| Linux, 3,400-3,560 draws, 15 lines | mean 10.08 vs 10.12 | mean 12.0 vs 12.0 |
| S23 FE, 2,675 draws | 26.05 vs 26.35 | 33.5 vs 34.3 |
| S23 FE, 1,460 draws | 22.35 vs 22.63 | 31.1 vs 31.4 |

- Picture, Linux: 0 of 50 in-world cross-side capture pairs outside the same-side range. S23 FE walk
  (`walk_squares.sh --ab 'gtav_native_split_draw=false|true'`): 0 wrongly lit tiles in 7 frames on
  each side.
- An in-world dump frame from each path has the same kinds of lines, the same draw line layout and
  the same kinds of files (different frames, so not byte-equal: 2,778 draws / 434 files old,
  2,034 draws / 451 files new).
- What it took to get the cost level: the shared constant block is not rebuilt when the draw
  before used the same slot block and regs-derived words; `DrawRecord` is not zero-filled per draw.
  Before that the new path cost 1-2% more CPU on the phone and 4-7% on Linux.

**After the old path was removed:**

- Linux, 0 against 3 workers with `--gtav_native_record_chaos=1` (33 chunks a frame): 0 of 64
  in-world cross-side capture pairs outside the same-side range.
- Linux, hidden runs that all ended clean: 2 and 3 workers, chunk size 96|32, early release,
  `gtav_native_convert_threads=0|2` with the flicker inspector on, and an in-world dump frame
  (3,592 draws, 500 files). The inspector flagged six in-world frames, all of them the input
  script's one-frame camera taps (the flagged frame's own draw list differs from its neighbours').
- S23 FE: `gtav_native_record_workers=0|2` record 27.5 -> 16.6, 32.1 -> 19.3, 33.2 -> 18.9 ms,
  frame 36.1 -> 37.5, 43.1 -> 45.7, 46.9 -> 49.8 ms (as before the removal); walk check 0 wrongly
  lit tiles in 7 frames on each side.

## Does it help?

**Linux** (12 threads; load average 12-20 from other sessions for the first table, 8-15 for the
second):

| A/B against 0 workers | record ms | render thread CPU ms |
|---|---|---|
| 1 worker | 38.5 -> 34.9, 39.6 -> 32.6 | 17.6 -> 7.0, 19.8 -> 8.0 |
| 2 workers | 19.2 -> 14.3, 34.7 -> 21.0, 35.3 -> 29.8 | 15.0 -> 8.1, 18.6 -> 10.0, 19.1 -> 9.5 |
| 3 workers + chaos | 21.7 -> 18.8, 22.2 -> 20.1, 28.0 -> 23.8 | 16.6 -> 9.4, 17.6 -> 9.4, 18.6 -> 9.6 |

The render thread's own part (~9 ms there: targets, textures, pipelines, the non-draw commands) is
the floor now. Later the same day, with the laptop less loaded (camera held, 2,100-2,450 draws,
7-13 in-world lines per run, means):

| A/B against 0 workers | load average | frame ms | record ms | idle ms |
|---|---|---|---|---|
| 2 workers | 8-10 | 11.53 vs 11.63 | 9.4 -> 5.9 | 1.7 -> 5.3 |
| 1 worker | 15 | 17.59 vs 17.92 | 13.6 -> 9.0 | 3.0 -> 8.0 |
| 3 workers | 13-15 | 18.62 vs 19.41 | 15.4 -> 9.9 | 2.3 -> 8.4 |

Same answer as on the phone: recording gets 35-40% shorter and the frame does not, because the
render thread then waits for the game's next frame (idle goes up by what recording lost). At 0
workers the render thread already has 1-3 ms of slack per frame. So **workers stay off
on Linux too.** Still not measured: this machine with nothing else running (it never was), where
the game's own threads would be faster and the renderer could become the limit; the pad test in
"For the Mac / iOS / Windows sessions" answers that in one run.

Also on Linux, with 2 workers and the laptop at load average 20 (so only good for "no large
effect"): chunk size 96 against 32: frame 33.0 vs 32.9, 36.6 vs 36.5, 35.8 vs 36.8 ms;
`gtav_native_early_fence=false|true`: 31.3 vs 36.3, 34.6 vs 36.0, 38.6 vs 44.2 ms (not better).
Both stay as they were (96, off).

**S23 FE: the frame is not bound by recording.** This corrects the 11-22 perf handoff.

- `--gtav_native_record_pad_ms=0|8` (8 ms added to every frame's recording): frame 38.0 vs 38.0,
  47.4 vs 46.6, 46.5 vs 46.0 ms; idle 10-13 -> 1-4 ms. The render thread has that much slack.
- `--gtav_wait_profile=true` + simpleperf: the game's main thread waits ~47% of the time in ~80
  short waits a frame (for the game's job threads), and the game's own render thread waits on the
  main thread 32% of the time. Big cores (3 mid + 1 prime) carry the renderer (68% of one), the
  game's audio/update thread (59%), its render thread (50%), the command processor (43%), the main
  thread and four job threads (11-17% each).
- Workers therefore do not raise fps there, and cost a little by taking big-core time:

| phone A/B against 0 workers | record ms | frame ms |
|---|---|---|
| 1 worker, big cores | 25.3 -> 22.7, 30.3 -> 26.2, 31.1 -> 26.9 | 37.0 -> 37.6, 42.4 -> 43.3, 47.4 -> 47.7 |
| 2 workers, big cores | 29.2 -> 17.9, 31.3 -> 18.3, 32.4 -> 19.0 | 39.3 -> 41.1, 43.6 -> 45.1, 45.1 -> 47.2 |
| 3 workers, little cores | 19.8 -> 27.1, 24.3 -> 32.9, 20.8 -> 28.0 | 27.0 -> 31.6, 31.8 -> 37.6, 27.0 -> 32.8 |
| 3 workers, little cores, chunks of 24 | 25.3 -> 30.9, 29.7 -> 36.1, 32.2 -> 37.4 | 36.4 -> 36.1, 40.9 -> 42.1, 43.3 -> 43.9 |
| the same + `gtav_android_guest_big` | (renderer becomes the limit, idle ~1 ms) | 30.2 -> 32.3, 33.5 -> 36.1, 37.0 -> 38.6, 39.1 -> 40.0 |

  (Little cores: the render thread's CPU halves, 24-33 -> 12-16 ms, but one render pass is one
  chunk, the G-buffer pass alone holds a large share of the frame's draws, and a little core takes
  ~20 ms over it while the render thread waits.)
- Thread placement, also tested and left off: `gtav_android_prime_exclusive` (nothing but the main
  thread on the prime core): 42.4 vs 42.8, 41.0 vs 41.1, 46.2 vs 45.9, 46.9 vs 44.8 ms, neutral.
  `gtav_android_guest_big` (the game's threads off the little cores): 35.3 -> 36.9, 41.6 -> 41.9,
  44.8 -> 46.2, 52.4 -> 59.9 ms, worse (the big cores are already full).
- `--gtav_native_early_fence=false|true` (guest GPU thread released when the frame is recorded
  instead of after the present): frame 44.47 vs 44.50, 44.55 vs 44.17 ms; with 2 workers
  44.13 vs 44.75, 48.42 vs 48.31, 46.16 vs 47.27 ms. Nothing, as the pad test predicted. Left in
  as a test setting for machines where the renderer is the limit.
- Phone state for these: battery 33-41 C, thermal status 1-2, CPU caps from 1.79/2.50/3.00 GHz
  (cool) to 1.48/2.11/2.40 GHz (little/mid/prime) by the end of a run.

So: recording gets 40% shorter with two workers, as designed, and the phone does not care until
the game's own threads get faster. Workers stay off on Android.

## For the Mac / iOS / Windows sessions

1. Check out `mt-record`, apply `plume-gtav.patch` to `thirdparty/plume` again
   (`git -C thirdparty/plume checkout . && git -C thirdparty/plume apply ../../plume-gtav.patch`),
   build as usual. New code is plain C++20 threads; Android-only pinning is behind `__ANDROID__`.
2. First look at one line without workers: run with `--gtav_native_stats=true
   --gtav_native_ab='gtav_native_record_pad_ms=0|4'` in the open world. If the frame gets ~4 ms
   longer with the pad, recording sets the frame time on that machine and workers should pay off.
   If only `idle` shrinks, the game is the limit (as on the phone) and they will not.
3. Then `--gtav_native_ab='gtav_native_record_workers=0|2'` (then `0|3`), 3+ in-world lines (or,
   by eye: the menu's "Record threads" with the FPS counter on). Read
   frame, record, idle. macOS is the best candidate: capture.cpp notes its renderer runs frames
   behind the game (record 20-33 ms in the open world).
4. Picture check: same run with `GTAV_NATIVE_SHOT_DIR=<dir> GTAV_SHOT_FRAMES=15`, then
   `python3 tools/native/ab_shots_diff.py <dir>` (needs numpy): "same picture" expected. Once with
   `--gtav_native_record_chaos=1`.
5. Things that could differ per platform: MoltenVK with 10-40 command buffers per submit and
   command buffers recorded on several threads at once (allowed by Vulkan; untested there); iOS
   memory (one command pool per chunk list, per frame slot and pair); Windows has no per-thread
   CPU time in the A/B line.
6. Only then consider a default: owner play-test first (visible, with sound).

## Open

- Linux default: off by measurement (above). Worth one more A/B if this laptop is ever idle.
- The render thread's ~9 ms is the next limit where recording matters: texture hashing
  (`HashGuestTexture`, `SampledHash`) and the non-draw commands are still serial.
- Mac / iOS / Windows now run the two-half draw path by default (0 workers) like everything else:
  the first thing to check there is that the game looks and runs as before, then the workers.
- Phone fps: the lever is the game's own threads (main thread waits on job threads; big cores
  oversubscribed), not the renderer. Ideas not yet tried: fewer renderer CPU ms in total (vertex
  hashing without a write watch is ~10 ms a frame), the audio thread (59-94% of a big core), ADPF
  performance hints for the main and job threads.
