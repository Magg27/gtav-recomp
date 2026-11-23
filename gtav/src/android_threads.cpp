// Android: Bionic gives threads ~1 MB of stack by default, glibc 8 MB. The native renderer (and
// the rest of the game's host code) was written against the 8 MB default, and an overflow on
// Android kills the process without a crash report. Every pthread_create in libmain.so (std::thread
// included) is linked to this wrapper (-Wl,--wrap=pthread_create) and gets 8 MB unless the caller
// chose a size.
#include <pthread.h>
#include <sched.h>
#include <sys/resource.h>
#include <unistd.h>

#include <dirent.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <unordered_map>

#include <rex/cvar.h>
#include <rex/logging.h>

// The game's main thread does most of a frame's work (it waits ~20% of the time on an S23 FE;
// the renderer and the other game threads wait far more), but the scheduler ran it on the little
// cores ~45% of the time and never on the prime core. The renderer's threads keep to the middle
// cluster (renderer.cpp PinToBigCores), and this pins the main thread to the prime core (the last
// CPU). Checked every 250 ms, so it is live (--gtav_native_ab=gtav_android_main_prime=true|false).
REXCVAR_DEFINE_BOOL(gtav_android_main_prime, true, "GTAV",
                    "Android: run the game's main thread on the prime core (the last CPU)");
// The main thread sets the frame time, and it waits and wakes ~80 times a frame (for the game's
// job threads): pinned to one core, every wake-up that finds another thread on that core waits for
// the scheduler to move it away. Test: nothing else of the game may run there.
REXCVAR_DEFINE_BOOL(gtav_android_prime_exclusive, false, "GTAV",
                    "Android, with gtav_android_main_prime: keep the game's other threads off the prime core");
// The job threads the main thread waits for are busy 10-17% each, in short bursts: the scheduler
// keeps such threads on the little cores, where a job takes ~3x as long. Test: the game's own
// threads (jobs, its render and audio threads) only on the upper half of the CPUs.
REXCVAR_DEFINE_BOOL(gtav_android_guest_big, false, "GTAV",
                    "Android: keep the game's own threads (XThread...) off the little cores");

// S23 FE probe (tools/android/cpu_probe.sh, 11-22): the main thread ran 40% of the time and sat
// runnable, waiting for its core, another 39% (it sleeps and wakes ~2,400 times a second for the
// game's job threads, and each time finds another busy thread on the prime core). A negative nice
// value gives it the core back at once when it wakes. Same-run A/B, 0 | -10: 24.7 | 25.7,
// 22.9 | 23.9, 19.6 | 22.2 fps (the render thread's wait for the game 9-12 -> 7-8 ms a frame).
REXCVAR_DEFINE_INT32(gtav_android_main_nice, -10, "GTAV",
                     "Android: nice value of the game's main thread (negative: it takes its core back "
                     "from other threads as soon as it wakes; live)");
// Helper threads that only prepare things ahead (the SDK's "xma-fresh" thread opens audio decoders
// for sounds about to start) took time on the fast cores from the threads a frame waits for.
REXCVAR_DEFINE_BOOL(gtav_android_background_little, true, "GTAV",
                    "Android: background helper threads run on the little cores only, at low priority (live)");

// The SDK puts the game's own choice into the name of each game thread it did not name,
// "XThread<hardware thread>p<priority><id>". The game raises its job threads one step above normal
// (two pools of four, on hardware threads 1, 2, 3 and 5): the main thread hands them ~5,000 small
// jobs a second and waits for each batch. S23 FE probe: they ran 55-65% of the time on the little
// cores (the scheduler puts threads that are busy in short bursts there) and waited for a core
// 40-75% as long as they ran. Same-run A/B with the main thread at -10: nice 0 | -8: 28.1 | 29.3,
// 28.4 | 28.2, 23.8 | 24.3, 21.5 | 22.5, 24.3 | 24.8 fps. Off the little cores (job_cores 1): 2-9%
// slower, they take the middle cores from the renderer's record thread and the game's render thread.
REXCVAR_DEFINE_INT32(gtav_android_job_nice, -8, "GTAV",
                     "Android: nice value of the game's job threads, the ones the game itself raised "
                     "above normal (live)");
REXCVAR_DEFINE_INT32(gtav_android_audio_nice, 0, "GTAV",
                     "Android: nice value of the game's audio mixer thread (live)");
REXCVAR_DEFINE_INT32(gtav_android_render_nice, 0, "GTAV",
                     "Android: nice value of the game's render thread (live)");
REXCVAR_DEFINE_INT32(gtav_android_other_nice, 0, "GTAV",
                     "Android: nice value of the game's other threads and the renderer's own (live)");
REXCVAR_DEFINE_INT32(gtav_android_job_cores, 0, "GTAV",
                     "Android: where the game's job threads may run: 0 anywhere, 1 off the little cores, "
                     "2 the middle cores only (live)");

std::atomic<int> gtav_android_render_tid{0};  // set by the game's render thread (capture.cpp)

namespace {

bool SetAffinity(int tid, int first, int last) {
  cpu_set_t set;
  CPU_ZERO(&set);
  for (int i = first; i <= last; i++) CPU_SET(i, &set);
  return sched_setaffinity(tid, sizeof(set), &set) == 0;
}

// Where the threads of the process other than `main` may run: the game's own threads on the
// upper half of the CPUs (guest_big), nothing on the last one (exclusive). Only threads whose mask
// was wide when first seen are managed, and only while their mask is still the one set here (the
// renderer and the hash workers pin themselves).
struct Managed {
  uint32_t mask;
  bool guest;
};
std::unordered_map<int, Managed> g_managed;  // pinner thread only

void ApplyThreadPolicy(int main, int n, bool exclusive, bool guest_big) {
  const uint32_t all = n >= 32 ? ~0u : (1u << n) - 1, prime = 1u << (n - 1);
  const uint32_t upper = all & ~((1u << (n / 2)) - 1);
  DIR* dir = opendir("/proc/self/task");
  if (!dir) return;
  while (dirent* e = readdir(dir)) {
    if (e->d_name[0] == '.') continue;
    const int tid = std::atoi(e->d_name);
    if (!tid || tid == main) continue;
    cpu_set_t set;
    CPU_ZERO(&set);
    if (sched_getaffinity(tid, sizeof(set), &set) != 0) continue;
    uint32_t now = 0;
    for (int i = 0; i < n && i < 32; i++)
      if (CPU_ISSET(i, &set)) now |= 1u << i;
    auto it = g_managed.find(tid);
    if (it == g_managed.end()) {
      if (now != all) continue;  // pinned by its owner
      char path[64], name[32] = {};
      std::snprintf(path, sizeof(path), "/proc/self/task/%d/comm", tid);
      if (FILE* f = std::fopen(path, "r")) {
        size_t got = std::fread(name, 1, sizeof(name) - 1, f);
        name[got] = 0;
        std::fclose(f);
      }
      it = g_managed.emplace(tid, Managed{all, std::strncmp(name, "XThread", 7) == 0}).first;
    } else if (now != it->second.mask) {
      continue;  // changed by someone else since
    }
    uint32_t want = guest_big && it->second.guest ? upper : all;
    if (exclusive) want &= ~prime;
    if (want == now || !want) continue;
    CPU_ZERO(&set);
    for (int i = 0; i < n && i < 32; i++)
      if (want & (1u << i)) CPU_SET(i, &set);
    if (sched_setaffinity(tid, sizeof(set), &set) == 0) it->second.mask = want;
  }
  closedir(dir);
}

// Threads named `name`: onto the little cores at nice 10 (on), or back to every core at nice 0.
void PlaceBackgroundThreads(const char* name, int n, bool on) {
  DIR* dir = opendir("/proc/self/task");
  if (!dir) return;
  while (dirent* e = readdir(dir)) {
    if (e->d_name[0] == '.') continue;
    char path[64], comm[32] = {};
    std::snprintf(path, sizeof(path), "/proc/self/task/%s/comm", e->d_name);
    if (FILE* f = std::fopen(path, "r")) {
      size_t got = std::fread(comm, 1, sizeof(comm) - 1, f);
      comm[got] = 0;
      std::fclose(f);
    }
    if (std::strncmp(comm, name, std::strlen(name)) != 0) continue;
    const int tid = std::atoi(e->d_name);
    SetAffinity(tid, 0, on ? n / 2 - 1 : n - 1);
    setpriority(PRIO_PROCESS, id_t(tid), on ? 10 : 0);
  }
  closedir(dir);
}

// The game's own priority from an SDK thread name "XThread<digit>p<+/-><two digits>...", or -100.
int GuestPriority(const char* name) {
  if (std::strncmp(name, "XThread", 7) != 0 || name[7] < '0' || name[7] > '9' || name[8] != 'p') return -100;
  if ((name[9] != '+' && name[9] != '-') || name[10] < '0' || name[10] > '9' || name[11] < '0' || name[11] > '9')
    return -100;
  const int v = (name[10] - '0') * 10 + (name[11] - '0');
  return name[9] == '-' ? -v : v;
}

// Nice values and cores by role. A thread starts with its creator's nice value, and the main
// thread creates most of the game's threads after it was raised here: the game's render and audio
// threads and the renderer's own threads all ran at the main thread's -10 (scheduler trace 11-23:
// the main thread then queued 167 ms a second behind the game's render thread on its own core).
// So every game ("XThread...") and renderer ("gtav-...") thread gets the nice value of its role:
// job threads (game priority 1 to 15), the game's audio mixer (it asks for 16), the game's render
// thread (the one that records draws, capture.cpp), the rest. SDK threads keep theirs (-4).
struct Roles {
  int job, audio, render, other, job_cores;
  bool operator==(const Roles&) const = default;
};
struct Placed {
  int nice = 100;  // applied (100: none yet)
  int cores = 0;   // applied job_cores
};
std::unordered_map<int, Placed> g_placed;  // pinner thread only

void PlaceThreads(int main, int n, const Roles& r) {
  DIR* dir = opendir("/proc/self/task");
  if (!dir) return;
  const int render = gtav_android_render_tid.load(std::memory_order_relaxed);
  while (dirent* e = readdir(dir)) {
    if (e->d_name[0] == '.') continue;
    const int tid = std::atoi(e->d_name);
    if (!tid || tid == main) continue;
    char path[64], name[32] = {};
    std::snprintf(path, sizeof(path), "/proc/self/task/%d/comm", tid);
    if (FILE* f = std::fopen(path, "r")) {
      size_t got = std::fread(name, 1, sizeof(name) - 1, f);
      name[got] = 0;
      std::fclose(f);
    }
    // (A new thread carries its creator's name until it names itself: left for the next pass.)
    if (std::strncmp(name, "XThread", 7) != 0 && std::strncmp(name, "gtav-", 5) != 0) continue;
    const int priority = GuestPriority(name);
    const bool job = priority >= 1 && priority <= 15;
    const int nice = job ? r.job : priority >= 16 ? r.audio : tid == render ? r.render : r.other;
    const int cores = job ? r.job_cores : 0;
    Placed& p = g_placed[tid];
    if (p.nice != nice && setpriority(PRIO_PROCESS, id_t(tid), nice) == 0) p.nice = nice;
    if (p.cores != cores && SetAffinity(tid, cores ? n / 2 : 0, cores == 2 ? n - 2 : n - 1)) p.cores = cores;
  }
  closedir(dir);
}

// The SDK names guest threads after their entry ("Main XThread (F8000...)").
int FindMainThread() {
  DIR* dir = opendir("/proc/self/task");
  if (!dir) return 0;
  int found = 0;
  while (dirent* e = readdir(dir)) {
    if (e->d_name[0] == '.') continue;
    char path[64], name[32] = {};
    std::snprintf(path, sizeof(path), "/proc/self/task/%s/comm", e->d_name);
    if (FILE* f = std::fopen(path, "r")) {
      size_t n = std::fread(name, 1, sizeof(name) - 1, f);
      name[n] = 0;
      std::fclose(f);
    }
    if (std::strncmp(name, "Main XThread", 12) == 0) {
      found = std::atoi(e->d_name);
      break;
    }
  }
  closedir(dir);
  return found;
}

const bool g_main_pinner = [] {
  std::thread([] {
    const int n = int(sysconf(_SC_NPROCESSORS_CONF));
    if (n < 4) return;
    int tid = 0, pinned = -1, nice_now = 0, background = -1;
    Roles roles{};
    uint32_t pass = 0;
    bool policy = false;
    for (;;) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      // Every 2 s (such threads start late), and at once when the setting changes.
      if (const int want = REXCVAR_GET(gtav_android_background_little) ? 1 : 0;
          want != background || (want && pass % 20 == 0)) {
        if (want || background == 1) PlaceBackgroundThreads("xma-fresh", n, want != 0);
        background = want;
      }
      pass++;
      if (!tid) {
        tid = FindMainThread();
        if (tid) nice_now = getpriority(PRIO_PROCESS, id_t(tid));
      }
      if (!tid) continue;
      if (const int nice_want = std::clamp(REXCVAR_GET(gtav_android_main_nice), -20, 19); nice_want != nice_now) {
        if (setpriority(PRIO_PROCESS, id_t(tid), nice_want) != 0)
          REXLOG_INFO("[android] main thread nice {} refused (errno {})", nice_want, errno);
        nice_now = nice_want;
      }
      // Roles: at once when a setting changes, else twice a second (the game starts and
      // re-prioritises threads as it goes).
      const auto nice_of = [](int v) { return std::clamp(v, -20, 19); };
      if (const Roles want_roles{nice_of(REXCVAR_GET(gtav_android_job_nice)),
                                 nice_of(REXCVAR_GET(gtav_android_audio_nice)),
                                 nice_of(REXCVAR_GET(gtav_android_render_nice)),
                                 nice_of(REXCVAR_GET(gtav_android_other_nice)),
                                 std::clamp(REXCVAR_GET(gtav_android_job_cores), 0, 2)};
          !(want_roles == roles) || pass % 5 == 0) {
        PlaceThreads(tid, n, want_roles);
        roles = want_roles;
      }
      const int want = REXCVAR_GET(gtav_android_main_prime) ? 1 : 0;
      // (Every pass while one is on: threads come and go, and new ones inherit their creator's
      // mask. One more pass after both went off puts the masks back.)
      const bool exclusive = want && pinned == 1 && REXCVAR_GET(gtav_android_prime_exclusive);
      const bool guest_big = REXCVAR_GET(gtav_android_guest_big);
      if (exclusive || guest_big || policy) ApplyThreadPolicy(tid, n, exclusive, guest_big);
      policy = exclusive || guest_big;
      if (want == pinned) continue;
      if (!(want ? SetAffinity(tid, n - 1, n - 1) : SetAffinity(tid, 0, n - 1))) {
        tid = 0;  // gone (or a new run of it): look it up again
        continue;
      }
      if (pinned < 0) REXLOG_INFO("[android] main thread {} {}", tid, want ? "pinned to the prime core" : "unpinned");
      pinned = want;
    }
  }).detach();
  return true;
}();

}  // namespace

extern "C" int __real_pthread_create(pthread_t* thread, const pthread_attr_t* attr,
                                     void* (*start)(void*), void* arg);

extern "C" int __wrap_pthread_create(pthread_t* thread, const pthread_attr_t* attr,
                                     void* (*start)(void*), void* arg) {
  constexpr size_t kStackSize = 8u << 20;
  if (attr) {
    size_t size = 0;
    if (pthread_attr_getstacksize(attr, &size) == 0 && size >= kStackSize) {
      return __real_pthread_create(thread, attr, start, arg);
    }
    pthread_attr_t copy = *attr;
    pthread_attr_setstacksize(&copy, kStackSize);
    return __real_pthread_create(thread, &copy, start, arg);
  }
  pthread_attr_t own;
  pthread_attr_init(&own);
  pthread_attr_setstacksize(&own, kStackSize);
  int result = __real_pthread_create(thread, &own, start, arg);
  pthread_attr_destroy(&own);
  return result;
}
