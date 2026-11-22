// gtav - frame-rate limit (--gtav_fps_limit).
//
// The game paces itself in sub_827B5878: while less than N/60 s (N = the int at 0x8391D9D4,
// 2 on the 360) passed since the last frame (timebase), it sleeps. N <= 0 skips the wait.
// --gtav_fps_limit=30 (default) leaves the game's limiter alone; any other value sets N = 0
// and, unless 0 (uncapped), paces frames here to that rate instead. The game itself is not
// the only limit: with the emulated GPU's vblank at 60 Hz (--vsync=true) other waits still
// tie it to the display, so uncapped play also wants --vsync=false.

#include "linux_threads.h"

#include <rex/chrono/clock.h>
#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/input/mnk/mnk_input_driver.h>

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstring>
#include <cstdint>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#include <timeapi.h>
#endif

REXCVAR_DEFINE_INT32(gtav_fps_limit, 30, "GTAV",
                     "Frame-rate limit: 30 = the game's own limiter, 0 = uncapped, other = that rate");
REXCVAR_DEFINE_BOOL(gtav_vehicle_30fps, false, "GTAV",
                    "In vehicles, run at the Xbox's 30 fps (the game's own limiter): the driving and flying "
                    "handling is tuned for it and too fast/twitchy above (issue #12). On foot the limit applies");
REXCVAR_DEFINE_BOOL(gtav_timestep_nudge, false, "GTAV",
                    "Keep the frame timestep out of a narrow band around 1/60 s: there the vehicle handling "
                    "runs ~50% faster (#12). Off: the drive test showed the real 60 Hz pacing, not the timestep value, is the trigger");
REXCVAR_DEFINE_INT32(gtav_vehicle_fps, 120, "GTAV",
                     "Frame rate in vehicles when gtav_vehicle_30fps is on: 30 = the game's own limiter (the Xbox). "
                     "Any rate the sleep limiter holds, away from ~60, drives like 30 (drive tests 11-21: 29-55 and "
                     "65-120 fps match; ~60 is ~50% faster, unlimited ~8%): 120 keeps the speed on foot");
REXCVAR_DEFINE_BOOL(gtav_audio_unsync, true, "GTAV",
                    "Above 30 fps: stop the audio update waiting for the next 16 ms audio frame "
                    "(otherwise the game cannot exceed 62.5 fps)");
REXCVAR_DEFINE_BOOL(gtav_no_power_throttling, true, "GTAV",
                    "Windows: opt out of power throttling (EcoQoS / efficiency cores, ignored timer "
                    "resolution) while the window is hidden, minimized or in the background");

namespace {

constexpr uint32_t kFrameIntervalVblanks = 0x8391D9D4;  // N: frame time in 1/60 s units
// The audio update (sub_831D8A68, run by the thread in sub_82488170 that the main loop waits on
// every frame) polls up to 1 s for the audio engine's next frame (768 samples at 48 kHz = 16 ms);
// the main loop therefore never exceeds 62.5 fps. When that poll times out the game sets this
// byte and stops waiting from then on: its own no-audio-sync fallback, set here instead.
constexpr uint32_t kAudioSyncTimedOut = 0x83D0D4CD;

void Store32(uint8_t* base, uint32_t addr, int32_t value) {
  uint8_t* p = base + addr;
  p[0] = uint8_t(uint32_t(value) >> 24), p[1] = uint8_t(value >> 16), p[2] = uint8_t(value >> 8);
  p[3] = uint8_t(value);
}

#ifdef _WIN32
// Windows rounds every host sleep (this limiter's and the guest's) up to the 15.6 ms system tick
// unless the process asks for 1 ms; at 60 fps that alone costs whole frames.
const bool g_timer_resolution = [] { return timeBeginPeriod(1) == TIMERR_NOERROR; }();

// Windows 11 power throttling: a process whose windows are hidden or minimized (or just not in
// front) may get EcoQoS (efficiency cores on hybrid CPUs, lower clocks) and has its timer
// resolution request ignored, so every guest sleep rounds up to 15.6 ms again. Opt out of both
// (--gtav_no_power_throttling; checked every frame so an A/B can flip it in one run).
void ApplyPowerThrottling() {
  static int applied = -1;
  int off = REXCVAR_GET(gtav_no_power_throttling) ? 1 : 0;
  if (off == applied) return;
  applied = off;
  PROCESS_POWER_THROTTLING_STATE state = {};
  state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
  state.ControlMask =
      off ? PROCESS_POWER_THROTTLING_EXECUTION_SPEED | PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION : 0;
  state.StateMask = 0;  // with ControlMask 0: the system decides again
  SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &state, sizeof(state));
}
#else
void ApplyPowerThrottling() {}
#endif

// fwTimer::Update (sub_8352BEC8) turns the timebase delta since the last frame (u64 at 0x83DDDF90,
// times the float at 0x820C9A00) into the frame timestep. Measured with the drivetest command: every
// sleep-limited rate from 29 to 120 fps accelerates like 30 fps except within ~0.4 ms of 16.67 ms
// (60 fps: +50%; 59 / 61 fps, frames partly in the band: +5-8%; uncapped: +8%). Before the update
// runs, a delta in the band is moved just outside it by shifting the stored last timebase; the
// difference is paid back on later frames, so the game clock loses nothing.
constexpr uint32_t kTimerLastTicks = 0x83DDDF90, kTimerTickScale = 0x820C9A00;
uint64_t Load64(const uint8_t* base, uint32_t a) { uint64_t v; std::memcpy(&v, base + a, 8); return __builtin_bswap64(v); }
void Store64(uint8_t* base, uint32_t a, uint64_t v) { v = __builtin_bswap64(v); std::memcpy(base + a, &v, 8); }
float LoadF32(const uint8_t* base, uint32_t a) { uint32_t v; std::memcpy(&v, base + a, 4); return std::bit_cast<float>(__builtin_bswap32(v)); }
double g_timestep_debt = 0;  // seconds the game clock is behind real time (+) from nudges
uint64_t g_timestep_nudged = 0;

void NudgeTimestep(uint8_t* base) {
  constexpr double kSixty = 1.0 / 60.0, kLo = kSixty - 0.00045, kHi = kSixty + 0.00045;
  const uint64_t last = Load64(base, kTimerLastTicks);
  const double scale = LoadF32(base, kTimerTickScale);
  if (!last || scale <= 0) return;
  const uint64_t now = rex::chrono::Clock::QueryGuestTickCount();
  if (now <= last) return;
  const double dt = double(now - last) * scale;
  if (dt > 0.1) return;  // loading / pause: leave it
  double target = dt;
  if (dt > kLo && dt < kHi) {
    target = g_timestep_debt > 0 ? kHi + 0.00005 : kLo - 0.00005;
    ++g_timestep_nudged;
  } else if (g_timestep_debt != 0) {  // pay back, up to 1 ms a frame, without entering the band
    double t = dt + std::clamp(g_timestep_debt, -0.001, 0.001);
    if (!(t > kLo && t < kHi) && t > 0.001) target = t;
  }
  if (target == dt) return;
  g_timestep_debt += dt - target;
  // The update reads mftb itself a moment later: shifting "last" moves its delta by the same amount.
  const int64_t shift = int64_t((dt - target) / scale);
  Store64(base, kTimerLastTicks, last + uint64_t(shift));
}

}  // namespace

REX_EXTERN(__imp__sub_827B5878);
REX_HOOK_RAW(sub_827B5878) {
  gtav::linux_threads::Note(gtav::linux_threads::kMain);
  ApplyPowerThrottling();
  int32_t limit = REXCVAR_GET(gtav_fps_limit);
  if (REXCVAR_GET(gtav_vehicle_30fps)) {
    // The player's context comes from the game (pc_controls.cpp, also without keyboard/mouse).
    using C = rex::input::mnk::InputContext;
    switch (rex::input::mnk::GetGameplayContext()) {
      case C::kCar: case C::kBike: case C::kBicycle: case C::kBoat: case C::kAircraft: case C::kSub:
        // (never 56-64: at ~60 fps the handling runs ~50% fast; see gtav_vehicle_fps)
        limit = REXCVAR_GET(gtav_vehicle_fps);
        if (limit <= 0 || (limit > 55 && limit < 65)) limit = 30;
        if (REXCVAR_GET(gtav_fps_limit) > 0 && REXCVAR_GET(gtav_fps_limit) < limit) limit = REXCVAR_GET(gtav_fps_limit);
        break;
      default: break;
    }
  }
  if (limit == 30) {
    Store32(base, kFrameIntervalVblanks, 2);  // the 360's value (also after a switch from another limit)
    __imp__sub_827B5878(ctx, base);
    return;
  }
  Store32(base, kFrameIntervalVblanks, 0);
  if (REXCVAR_GET(gtav_audio_unsync)) base[kAudioSyncTimedOut] = 1;
  __imp__sub_827B5878(ctx, base);
  if (limit <= 0) return;
  using clock = std::chrono::steady_clock;
  static clock::time_point next = clock::now();
  auto period = std::chrono::nanoseconds(1000000000 / limit);
  auto now = clock::now();
  if (next > now) std::this_thread::sleep_until(next);
  next = std::max(next + period, now);  // no catch-up after a long frame
}

REX_EXTERN(__imp__sub_8352BEC8);
REX_HOOK_RAW(sub_8352BEC8) {
  if (REXCVAR_GET(gtav_timestep_nudge)) NudgeTimestep(base);
  __imp__sub_8352BEC8(ctx, base);
}
