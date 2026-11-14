// gtav - LD_PRELOAD helper: collapse the guest memory into 2 MB transparent huge pages in-process.
//
// The guest address space is a /dev/shm object mapped as several views; shmem gets huge pages only
// when /sys/kernel/mm/transparent_hugepage/shmem_enabled allows it (root), but madvise(MADV_COLLAPSE)
// on our own mappings ignores that setting ("never" is not "deny"). process_madvise from outside
// needs CAP_SYS_NICE, hence this preload. THP_COLLAPSE_AT seconds after load (default 90) a thread
// scans /proc/self/pagemap and collapses every 2 MB chunk of a guest view that is >= half present
// (an empty chunk would become a zero-filled huge page). THP_ALL_PHYS=1 collapses every chunk of the
// physical views (file offset >= 4 GB: the 512 MB of guest physical memory is nearly all in use, and
// the raw view's own PTEs say nothing about pages mapped through the other views of the same file).
// THP_LOG names the report file.
//   cc -shared -fPIC -O2 -o thp_preload.so thp_preload.c
#define _GNU_SOURCE
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#ifndef MADV_COLLAPSE
#define MADV_COLLAPSE 25
#endif
#define CHUNK (2u << 20)

static long rollup(const char* key) {
  FILE* f = fopen("/proc/self/smaps_rollup", "r");
  if (!f) return -1;
  char line[256]; long v = -1;
  while (fgets(line, sizeof line, f))
    if (!strncmp(line, key, strlen(key))) { v = atol(line + strlen(key)); break; }
  fclose(f);
  return v;
}

static void* collapse_thread(void* arg) {
  (void)arg;
  const char* at = getenv("THP_COLLAPSE_AT");
  const char* logname = getenv("THP_LOG");
  sleep(at ? atoi(at) : 90);
  FILE* log = fopen(logname ? logname : "/dev/stderr", "a");
  if (!log) return NULL;
  time_t t = time(NULL); struct tm tm; localtime_r(&t, &tm);
  fprintf(log, "thp_preload: collapse at %02d:%02d:%02d, ShmemPmdMapped %ld MB before\n", tm.tm_hour, tm.tm_min, tm.tm_sec,
          rollup("ShmemPmdMapped:") / 1024);
  const char* all_phys_env = getenv("THP_ALL_PHYS");
  int all_phys = all_phys_env && atoi(all_phys_env);
  FILE* maps = fopen("/proc/self/maps", "r");
  int pm = open("/proc/self/pagemap", O_RDONLY);
  if (!maps || pm < 0) { fprintf(log, "thp_preload: maps/pagemap unavailable\n"); fclose(log); return NULL; }
  char line[512]; long total_mb = 0, done_mb = 0, fail = 0; double t0 = clock() / (double)CLOCKS_PER_SEC;
  struct timespec ts0, ts1; clock_gettime(CLOCK_MONOTONIC, &ts0);
  while (fgets(line, sizeof line, maps)) {
    unsigned long lo, hi, off; char perms[8], path[256] = "";
    if (sscanf(line, "%lx-%lx %7s %lx %*s %*s %255s", &lo, &hi, perms, &off, path) < 4) continue;
    if (!strstr(path, "/dev/shm/") && !strstr(path, "memfd:")) continue;
    if (hi - lo < (64u << 20)) continue;
    if ((lo - off) % CHUNK) { fprintf(log, "thp_preload: skip %lx-%lx (file offset not 2 MB aligned)\n", lo, hi); continue; }
    unsigned long a = (lo + CHUNK - 1) & ~(unsigned long)(CHUNK - 1);
    long present_mb = 0, chunks = 0, ok = 0;
    for (; a + CHUNK <= hi; a += CHUNK) {
      uint64_t ent[512];
      if (pread(pm, ent, sizeof ent, (off_t)(a / 4096) * 8) != (ssize_t)sizeof ent) break;
      int present = 0;
      for (int i = 0; i < 512; i++) present += (ent[i] >> 63) & 1;
      present_mb += present * 4 / 1024;
      if (present < 256 && !(all_phys && off >= 0x100000000ul)) continue;
      chunks++;
      if (madvise((void*)a, CHUNK, MADV_COLLAPSE) == 0) ok++; else fail++;
    }
    total_mb += present_mb; done_mb += ok * 2;
    fprintf(log, "thp_preload: %lx-%lx: %ld MB present, %ld chunks, %ld collapsed\n", lo, hi, present_mb, chunks, ok);
  }
  clock_gettime(CLOCK_MONOTONIC, &ts1);
  fclose(maps); close(pm);
  t = time(NULL); localtime_r(&t, &tm);
  fprintf(log, "thp_preload: done %02d:%02d:%02d: %ld MB collapsed of %ld MB present, %ld failed, %.2f s wall; "
          "ShmemPmdMapped %ld MB after\n", tm.tm_hour, tm.tm_min, tm.tm_sec, done_mb, total_mb, fail,
          (ts1.tv_sec - ts0.tv_sec) + (ts1.tv_nsec - ts0.tv_nsec) / 1e9, rollup("ShmemPmdMapped:") / 1024);
  (void)t0;
  fclose(log);
  return NULL;
}

__attribute__((constructor)) static void thp_preload_init(void) {
  pthread_t th;
  pthread_attr_t attr; pthread_attr_init(&attr); pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  pthread_create(&th, &attr, collapse_thread, NULL);
}
