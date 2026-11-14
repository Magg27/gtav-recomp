#!/usr/bin/env python3
"""thp_collapse.py PID [--min-fill F] [--dry]

Collapse the running game's guest memory into 2 MB transparent huge pages from outside.

The guest address space is a /dev/shm object mapped as several views (xmemory.cpp map_info); shmem
only gets huge pages when /sys/kernel/mm/transparent_hugepage/shmem_enabled allows it (root), but
MADV_COLLAPSE ignores that setting ("never" is not "deny"), and process_madvise() lets another
process of the same user apply it. Only 2 MB chunks whose pages are at least F present (pagemap,
default 0.5) are collapsed: an empty chunk would become a zero-filled huge page (RSS bloat).
Prints what was collapsed and the process's ShmemPmdMapped before/after. Linux 6.1+, x86-64."""
import ctypes, os, struct, sys, time

MADV_COLLAPSE = 25
NR_PIDFD_OPEN, NR_PROCESS_MADVISE = 434, 440
CHUNK = 2 << 20
libc = ctypes.CDLL(None, use_errno=True)
libc.syscall.restype = ctypes.c_long

class IOVec(ctypes.Structure):
    _fields_ = [("base", ctypes.c_void_p), ("len", ctypes.c_size_t)]

def rollup(pid, key):
    for line in open(f"/proc/{pid}/smaps_rollup"):
        if line.startswith(key): return int(line.split()[1])
    return -1

def guest_mappings(pid):
    out = []
    for line in open(f"/proc/{pid}/maps"):
        f = line.split()
        if len(f) >= 6 and ('/dev/shm/' in f[5] or 'memfd:' in f[5]):
            lo, hi = (int(x, 16) for x in f[0].split('-'))
            if hi - lo < 64 << 20: continue
            if (lo - int(f[2], 16)) % CHUNK:  # the 0xE0000000 view: file offset +4 KB, never PMD-mappable
                print(f"skip {f[5]} {lo:#x}-{hi:#x}: file offset not 2 MB aligned"); continue
            out.append((lo, hi, f[5]))
    return out

def main():
    args = sys.argv[1:]
    min_fill, dry = 0.5, False
    if '--min-fill' in args:
        i = args.index('--min-fill'); min_fill = float(args[i + 1]); del args[i:i + 2]
    if '--dry' in args: dry = True; args.remove('--dry')
    pid = int(args[0])
    maps = guest_mappings(pid)
    if not maps: sys.exit("no guest memory mapping found (/dev/shm or memfd, >= 64 MB)")
    before = rollup(pid, "ShmemPmdMapped")
    pagemap = os.open(f"/proc/{pid}/pagemap", os.O_RDONLY)
    chunks = []
    for lo, hi, name in maps:
        lo2 = (lo + CHUNK - 1) & ~(CHUNK - 1)
        present_total = 0
        for a in range(lo2, hi - CHUNK + 1, CHUNK):
            data = os.pread(pagemap, 512 * 8, (a // 4096) * 8)
            present = sum(1 for (v,) in struct.iter_unpack('<Q', data) if v >> 63 & 1)
            present_total += present
            if present >= min_fill * 512: chunks.append(a)
        print(f"{name} {lo:#x}-{hi:#x}: {present_total * 4 // 1024} MB present, "
              f"{sum(1 for c in chunks if lo <= c < hi)} chunks of 2 MB to collapse")
    os.close(pagemap)
    if dry or not chunks: return
    pidfd = libc.syscall(NR_PIDFD_OPEN, pid, 0)
    if pidfd < 0: sys.exit(f"pidfd_open: {os.strerror(ctypes.get_errno())}")
    # Merge runs of adjacent chunks into iovecs (max 1024 per call).
    runs = []
    for c in sorted(chunks):
        if runs and runs[-1][0] + runs[-1][1] == c: runs[-1][1] += CHUNK
        else: runs.append([c, CHUNK])
    t0 = time.time(); done = 0; errs = 0
    for i in range(0, len(runs), 1024):
        batch = runs[i:i + 1024]
        vec = (IOVec * len(batch))(*[IOVec(b, n) for b, n in batch])
        r = libc.syscall(NR_PROCESS_MADVISE, pidfd, vec, len(batch), MADV_COLLAPSE, 0)
        if r < 0:
            errs += 1
            print(f"process_madvise: {os.strerror(ctypes.get_errno())} (batch {i // 1024})")
        else: done += r
    after = rollup(pid, "ShmemPmdMapped")
    print(f"collapsed {done >> 20} MB in {len(runs)} runs ({errs} failed batches) in {time.time() - t0:.1f} s; "
          f"ShmemPmdMapped {before // 1024} -> {after // 1024} MB")

main()
