// Android: Bionic gives threads ~1 MB of stack by default, glibc 8 MB. The native renderer (and
// the rest of the game's host code) was written against the 8 MB default, and an overflow on
// Android kills the process without a crash report. Every pthread_create in libmain.so (std::thread
// included) is linked to this wrapper (-Wl,--wrap=pthread_create) and gets 8 MB unless the caller
// chose a size.
#include <pthread.h>
#include <sched.h>
#include <unistd.h>

#include <dirent.h>

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
    int tid = 0, pinned = -1;
    bool policy = false;
    for (;;) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      if (!tid) tid = FindMainThread();
      if (!tid) continue;
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
