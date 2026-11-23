# Handoff: multithreaded frame recording (for the Mac, iOS and Windows sessions)

Written 2026-11-22. Owner's request: "build the multithreaded renderer for android and linux that
will also work mac ios pc too and ill test it there once we get android and linux dialed".
Branch **`mt-record`** (off `perf-linux` @ 6dbeaeb), commits f5a6eb3 and 6b5d508 plus the ones after
them. Nothing is on main or in a release. **The feature is off by default on every platform.**

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

The one-piece `DrawImpl` is still there and is still what runs by default (and always for dump
frames, the flicker inspector and convert threads). It was not deleted: it is the default path and
the only picture to compare against. `--gtav_native_split_draw=true` runs the two halves inline on
the render thread (no workers).

## Settings

| flag | default | |
|---|---|---|
| `--gtav_native_record_workers=N` | 0 | worker threads; 0 = the old path |
| `--gtav_native_record_chunk_draws=N` | 96 | draws per chunk before a cut (0: one chunk, in order) |
| `--gtav_native_record_chaos=1` | 0 | test: random cuts, workers pause at random draws |
| `--gtav_native_record_pad_ms=N` | 0 | test: sleep N ms inside every frame's recording |
| `--gtav_native_record_worker_cores=0/1/2` | 0 | Android: workers on the big cores / little cores / unpinned |
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
- No crash in any run on Linux or the phone.

## Does it help?

**Linux** (12 threads, load average 12-20 from other sessions all day, so wall times are noisy):

| A/B against 0 workers | record ms | render thread CPU ms |
|---|---|---|
| 1 worker | 38.5 -> 34.9, 39.6 -> 32.6 | 17.6 -> 7.0, 19.8 -> 8.0 |
| 2 workers | 19.2 -> 14.3, 34.7 -> 21.0, 35.3 -> 29.8 | 15.0 -> 8.1, 18.6 -> 10.0, 19.1 -> 9.5 |
| 3 workers + chaos | 21.7 -> 18.8, 22.2 -> 20.1, 28.0 -> 23.8 | 16.6 -> 9.4, 17.6 -> 9.4, 18.6 -> 9.6 |

The render thread's own part (~9 ms here: targets, textures, pipelines, the non-draw commands) is
the floor now. Frame time was not bound by recording on this loaded laptop (idle 5-15 ms a frame),
so fps moved within noise both ways. **Not measured: an idle desktop at 100+ fps, where recording
is most of the frame.** That is the case this should help; measure it before turning it on.

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

  (Little cores: the render thread's CPU halves, 24-33 -> 12-16 ms, but one render pass is one
  chunk, the G-buffer pass alone holds a large share of the frame's draws, and a little core takes
  ~20 ms over it while the render thread waits.)
- Thread placement, also tested and left off: `gtav_android_prime_exclusive` (nothing but the main
  thread on the prime core): 42.4 vs 42.8, 41.0 vs 41.1, 46.2 vs 45.9, 46.9 vs 44.8 ms, neutral.
  `gtav_android_guest_big` (the game's threads off the little cores): 35.3 -> 36.9, 41.6 -> 41.9,
  44.8 -> 46.2, 52.4 -> 59.9 ms, worse (the big cores are already full).
- Phone state for these: battery 33-41 C, thermal status 1-2, CPU caps from 1.79/2.50/3.00 GHz
  (cool) to 1.48/2.11/2.40 GHz (little/mid/prime) by the end of a run.

So: recording gets 40% shorter with two workers, as designed, and the phone does not care until
the game's own threads get faster. Workers stay off on Android.

## For the Mac / iOS / Windows sessions

1. Check out `mt-record`, apply `plume-gtav.patch` to `thirdparty/plume` again
   (`git -C thirdparty/plume checkout . && git -C thirdparty/plume apply ../../plume-gtav.patch`),
   build as usual. New code is plain C++20 threads; Android-only pinning is behind `__ANDROID__`.
2. First look at one line with the old path: run with `--gtav_native_stats=true
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

- Linux default: decide after an A/B on a quiet machine (see above).
- The render thread's ~9 ms is the next limit where recording matters: texture hashing
  (`HashGuestTexture`, `SampledHash`) and the non-draw commands are still serial.
- Deleting `DrawImpl` (two copies of the draw logic exist now: a fix to one must go into the
  other) needs dump frames, flicker notes and convert threads moved to the split path first.
- Calling `FenceCopiesRecorded` right after the workers join (plan item) was not tried: moot while
  the phone is not bound by recording.
- Phone fps: the lever is the game's own threads (main thread waits on job threads; big cores
  oversubscribed), not the renderer. Ideas not yet tried: fewer renderer CPU ms in total (vertex
  hashing without a write watch is ~10 ms a frame), the audio thread (59-94% of a big core), ADPF
  performance hints for the main and job threads.
