// gtav - LD_PRELOAD sampling profiler for one thread (perf is unavailable: perf_event_paranoid=4,
// and gdb stops the whole process ~1 s per sample).
//
// SPROF_THREAD=<comm substring> (e.g. gtav-record, "Main XThread") or "busiest" (the thread with the
// most CPU over 2 s, excluding Main XThread), SPROF_START=<s after launch> (default 100),
// SPROF_SECS=<duration> (30), SPROF_HZ=<rate> (1000), SPROF_OUT=<file>. A timer thread sends SIGPROF
// to the target with tgkill; the handler records backtrace() frames into a preallocated buffer.
// The output lists the process's /proc/self/maps and then one stack per line (hex frames,
// innermost first); tools/native/sprof_report.py symbolizes and aggregates them.
//   cc -shared -fPIC -O2 -o sprof_preload.so sprof_preload.c
#define _GNU_SOURCE
#include <dirent.h>
#include <execinfo.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#define MAX_DEPTH 24
#define MAX_SAMPLES 200000
static uintptr_t* g_frames;           // MAX_SAMPLES * MAX_DEPTH
static uint8_t* g_depth;
static atomic_int g_count;

static void on_prof(int sig, siginfo_t* si, void* uc) {
  (void)sig; (void)si; (void)uc;
  int i = atomic_fetch_add(&g_count, 1);
  if (i >= MAX_SAMPLES) return;
  void* buf[MAX_DEPTH + 2];
  int n = backtrace(buf, MAX_DEPTH + 2);
  int skip = n > 2 ? 2 : 0;  // the handler and the signal trampoline
  int d = n - skip;
  for (int k = 0; k < d; k++) g_frames[(size_t)i * MAX_DEPTH + k] = (uintptr_t)buf[k + skip];
  g_depth[i] = (uint8_t)d;
}

static long tid_cpu(pid_t tid) {
  char p[64], buf[512]; snprintf(p, sizeof p, "/proc/self/task/%d/stat", tid);
  FILE* f = fopen(p, "r"); if (!f) return -1;
  if (!fgets(buf, sizeof buf, f)) { fclose(f); return -1; } fclose(f);
  char* r = strrchr(buf, ')'); if (!r) return -1;
  long ut = 0, st = 0; int fld = 3; char* tok = strtok(r + 2, " ");
  for (; tok; tok = strtok(NULL, " "), fld++) { if (fld == 14) ut = atol(tok); if (fld == 15) { st = atol(tok); break; } }
  return ut + st;
}

static pid_t find_thread(const char* want) {
  DIR* d = opendir("/proc/self/task"); if (!d) return 0;
  struct dirent* e; pid_t best = 0; long best_d = -1;
  pid_t tids[512]; long cpu0[512]; char names[512][32]; int n = 0;
  while ((e = readdir(d)) && n < 512) {
    if (e->d_name[0] == '.') continue;
    pid_t t = atoi(e->d_name); char p[80], nm[32] = "";
    snprintf(p, sizeof p, "/proc/self/task/%d/comm", t);
    FILE* f = fopen(p, "r"); if (f) { if (fgets(nm, sizeof nm, f)) nm[strcspn(nm, "\n")] = 0; fclose(f); }
    if (strcmp(want, "busiest") != 0) { if (strstr(nm, want)) { closedir(d); return t; } continue; }
    tids[n] = t; cpu0[n] = tid_cpu(t); strncpy(names[n], nm, 31); names[n][31] = 0; n++;
  }
  closedir(d);
  if (strcmp(want, "busiest") != 0) return 0;
  sleep(2);
  for (int i = 0; i < n; i++) {
    if (!strncmp(names[i], "Main XThread", 12)) continue;
    long dcpu = tid_cpu(tids[i]) - cpu0[i];
    if (dcpu > best_d) { best_d = dcpu; best = tids[i]; }
  }
  return best;
}

static void* timer_thread(void* arg) {
  (void)arg;
  const char* want = getenv("SPROF_THREAD"); if (!want) want = "busiest";
  const char* start = getenv("SPROF_START"); const char* secs = getenv("SPROF_SECS"); const char* hz = getenv("SPROF_HZ");
  const char* out = getenv("SPROF_OUT"); if (!out) out = "sprof.txt";
  int rate = hz ? atoi(hz) : 1000; double dur = secs ? atof(secs) : 30;
  sleep(start ? atoi(start) : 100);
  pid_t tid = find_thread(want);
  FILE* o = fopen(out, "w"); if (!o) return NULL;
  if (!tid) { fprintf(o, "# thread '%s' not found\n", want); fclose(o); return NULL; }
  char p[80], nm[32] = "?"; snprintf(p, sizeof p, "/proc/self/task/%d/comm", tid);
  FILE* f = fopen(p, "r"); if (f) { if (fgets(nm, sizeof nm, f)) nm[strcspn(nm, "\n")] = 0; fclose(f); }
  fprintf(o, "# sampling tid %d '%s' at %d Hz for %.0f s\n", tid, nm, rate, dur);
  g_frames = calloc((size_t)MAX_SAMPLES * MAX_DEPTH, sizeof *g_frames); g_depth = calloc(MAX_SAMPLES, 1);
  struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_sigaction = on_prof; sa.sa_flags = SA_SIGINFO | SA_RESTART;
  sigaction(SIGPROF, &sa, NULL);
  { void* warm[4]; backtrace(warm, 4); }  // libgcc init outside the handler
  struct timespec ts = {0, 1000000000L / rate};
  struct timespec t0, t; clock_gettime(CLOCK_MONOTONIC, &t0);
  for (;;) {
    syscall(SYS_tgkill, getpid(), tid, SIGPROF);
    nanosleep(&ts, NULL);
    clock_gettime(CLOCK_MONOTONIC, &t);
    if ((t.tv_sec - t0.tv_sec) + (t.tv_nsec - t0.tv_nsec) / 1e9 >= dur) break;
    if (atomic_load(&g_count) >= MAX_SAMPLES) break;
  }
  usleep(20000);
  int n = atomic_load(&g_count); if (n > MAX_SAMPLES) n = MAX_SAMPLES;
  fprintf(o, "# samples %d\n# maps\n", n);
  FILE* m = fopen("/proc/self/maps", "r"); char line[512];
  while (m && fgets(line, sizeof line, m)) fputs(line, o);
  if (m) fclose(m);
  fprintf(o, "# stacks\n");
  for (int i = 0; i < n; i++) {
    for (int k = 0; k < g_depth[i]; k++) fprintf(o, "%s%lx", k ? " " : "", (unsigned long)g_frames[(size_t)i * MAX_DEPTH + k]);
    fputc('\n', o);
  }
  fclose(o);
  return NULL;
}

__attribute__((constructor)) static void sprof_init(void) {
  const char* only = getenv("SPROF_EXE");  // only in the process whose /proc/self/exe basename matches
  if (only) { char exe[512]; ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1); if (n < 0) return; exe[n] = 0; const char* b = strrchr(exe, '/'); if (strcmp(b ? b + 1 : exe, only)) return; }
  pthread_t th; pthread_attr_t a; pthread_attr_init(&a); pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
  pthread_create(&th, &a, timer_thread, NULL);
}
