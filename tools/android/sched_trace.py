#!/usr/bin/env python3
"""sched_trace.py TRACE.txt [PID]: who waits for whom, from an atrace scheduler trace.

tools/android/sched_trace.sh records it (atrace sched freq idle, text). For the game's process:
  - per thread: time running / runnable (waiting for a core) / asleep, wake-ups a second, and the
    delay from wake-up to running (median, 90th percentile, total), split by whether the core it was
    sent to was idle (then the delay is the core waking up) or busy (then it queued behind a thread)
  - the main thread's not-running time, by how many of the game's job threads were running then
  - who wakes the main thread and the job threads
Thread roles come from the names the SDK gives ("Main XThread", "XThread<hw>p<priority><id>")."""
import collections
import re
import sys

path = sys.argv[1]
want_pid = int(sys.argv[2]) if len(sys.argv) > 2 else None

LINE = re.compile(r"^\s*(.{1,16})-(\d+)\s+\(\s*([\d-]+)\)\s+\[(\d+)\]\s+\S+\s+([\d.]+):\s+(\w+):\s+(.*)$")
SWITCH = re.compile(r"prev_comm=(.*?) prev_pid=(\d+) prev_prio=\d+ prev_state=(\S+) ==> next_comm=(.*?) next_pid=(\d+)")
WAKE = re.compile(r"comm=(.*?) pid=(\d+) prio=\d+ target_cpu=(\d+)")
IDLE = re.compile(r"state=(\d+) cpu_id=(\d+)")

events = []
tgid_of = {}
name_of = {}
for line in open(path, errors="replace"):
    m = LINE.match(line)
    if not m:
        continue
    comm, tid, tgid, cpu, ts, ev, rest = m.groups()
    tid, cpu, ts = int(tid), int(cpu), float(ts)
    if tgid != "-----" and tgid.isdigit():
        tgid_of[tid] = int(tgid)
    if ev == "sched_switch":
        s = SWITCH.search(rest)
        if s:
            name_of[int(s.group(2))] = s.group(1)
            name_of[int(s.group(5))] = s.group(4)
            events.append((ts, "S", cpu, int(s.group(2)), s.group(3), int(s.group(5))))
    elif ev == "sched_waking":
        w = WAKE.search(rest)
        if w:
            events.append((ts, "W", cpu, tid, int(w.group(2)), int(w.group(3))))
    elif ev == "cpu_idle":
        i = IDLE.search(rest)
        if i:
            events.append((ts, "I", int(i.group(2)), int(i.group(1)), 0, 0))
events.sort(key=lambda e: e[0])
if not events:
    sys.exit("no scheduler events in " + path)
t_first, t_last = events[0][0], events[-1][0]
wall = t_last - t_first

if want_pid is None:  # the process of the thread called "Main XThread"
    for tid, n in name_of.items():
        if n.startswith("Main XThread") and tid in tgid_of:
            want_pid = tgid_of[tid]
            break
if want_pid is None:
    sys.exit("no 'Main XThread' in the trace; give the pid")
ours = {tid for tid, g in tgid_of.items() if g == want_pid}


def role(tid):
    n = name_of.get(tid, "?")
    m = re.match(r"XThread(\d)p([+-]\d\d)", n)
    if n.startswith("Main XThread"):
        return "main"
    if m and 1 <= int(m.group(2)) <= 15:
        return "job"
    return "other"


main = next((t for t in ours if role(t) == "main"), None)
jobs = sorted(t for t in ours if role(t) == "job")

# per thread: state machine. state: 'R' running, 'W' runnable, 'S' asleep
state = {}
since = {}
acc = collections.defaultdict(lambda: collections.Counter())
woken_at = {}
woken_idle = {}
lat = collections.defaultdict(lambda: {"idle": [], "busy": []})
wakes = collections.Counter()
wakers = collections.defaultdict(collections.Counter)
cpu_idle = collections.defaultdict(lambda: False)
cpu_cur = {}
job_running = 0
main_not_running = collections.Counter()  # by number of job threads running
main_state_since = None
jobs_set = set(jobs)
big_jobs = set(sorted(jobs, key=lambda t: 0)[:0])


def set_state(tid, new, ts):
    old = state.get(tid)
    if old is not None:
        acc[tid][old] += ts - since[tid]
    state[tid] = new
    since[tid] = ts


last_ts = t_first
for e in events:
    ts = e[0]
    # main thread not running: charge the interval to the current count of running job threads
    if main is not None and state.get(main) in ("W", "S"):
        main_not_running[(state[main], job_running)] += ts - last_ts
    last_ts = ts
    if e[1] == "I":
        cpu_idle[e[2]] = e[3] != 4294967295
        continue
    if e[1] == "W":
        _, _, cpu, waker, tid, target = e
        if tid in ours:
            woken_at[tid] = ts
            woken_idle[tid] = cpu_idle[target] or cpu_cur.get(target, 0) == 0
            wakes[tid] += 1
            wakers[tid][name_of.get(waker, str(waker)) if waker in ours else "(other process / kernel)"] += 1
            if state.get(tid) != "R":
                set_state(tid, "W", ts)
        continue
    _, _, cpu, prev, prev_state, nxt = e
    cpu_cur[cpu] = nxt
    if prev in ours:
        if prev in jobs_set and state.get(prev) == "R":
            job_running -= 1
        set_state(prev, "W" if prev_state.startswith("R") else "S", ts)
    if nxt in ours:
        if nxt in woken_at:
            lat[nxt]["idle" if woken_idle[nxt] else "busy"].append(ts - woken_at.pop(nxt))
        if nxt in jobs_set and state.get(nxt) != "R":
            job_running += 1
        set_state(nxt, "R", ts)
for tid in list(state):
    set_state(tid, state[tid], t_last)


def pct(v, p):
    if not v:
        return 0.0
    v = sorted(v)
    return v[min(len(v) - 1, int(len(v) * p))]


print(f"trace {wall:.2f} s, process {want_pid}, {len(ours)} threads, {len(jobs)} job threads")
print(f"{'tid':>6} {'thread':<16} {'run%':>5} {'queue%':>6} {'sleep%':>6} {'wakes/s':>7} | to an idle core: n/s, median, p90 us, ms/s | to a busy core: n/s, median, p90 us, ms/s")
rows = sorted(ours, key=lambda t: -(acc[t]["R"] + acc[t]["W"]))
for tid in rows:
    a = acc[tid]
    if (a["R"] + a["W"]) / wall < 0.02 and wakes[tid] / wall < 200:
        continue
    li, lb = lat[tid]["idle"], lat[tid]["busy"]
    print(f"{tid:>6} {name_of.get(tid, '?'):<16} {100 * a['R'] / wall:5.1f} {100 * a['W'] / wall:6.1f} {100 * a['S'] / wall:6.1f} {wakes[tid] / wall:7.0f} | "
          f"{len(li) / wall:5.0f} {1e6 * pct(li, .5):6.0f} {1e6 * pct(li, .9):6.0f} {1e3 * sum(li) / wall:6.1f} | "
          f"{len(lb) / wall:5.0f} {1e6 * pct(lb, .5):6.0f} {1e6 * pct(lb, .9):6.0f} {1e3 * sum(lb) / wall:6.1f}")

if main is not None:
    tot = sum(main_not_running.values()) or 1
    print(f"\nmain thread not running: {100 * tot / wall:.1f}% of the time; then, job threads running at that moment:")
    for st, label in (("S", "asleep"), ("W", "queued for its core")):
        parts = sorted((k[1], v) for k, v in main_not_running.items() if k[0] == st)
        s = sum(v for _, v in parts)
        print(f"  {label:<20} {100 * s / wall:5.1f}% of the time: " + ", ".join(f"{n} running {100 * v / wall:.1f}%" for n, v in parts))
    for tid, label in [(main, "main thread")] + [(j, name_of.get(j, "?")) for j in jobs[:8]]:
        top = wakers[tid].most_common(5)
        n = sum(wakers[tid].values()) or 1
        print(f"  {label:<16} woken by: " + ", ".join(f"{k} {100 * v / n:.0f}%" for k, v in top))
