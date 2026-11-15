#!/usr/bin/env python3
"""sprof_report.py SPROF.txt [--top N] [--under FUNC] [--lines FUNC] [--callers NAME] [--raw]

Symbolizes and aggregates the stacks sprof_preload.so wrote. Each frame is mapped through the
recorded /proc/self/maps to a file + offset, then:
  - files with DWARF (the game binary): llvm-symbolizer with inlining, so a frame is attributed to
    the outermost (non-inlined) function, and the innermost inlined function + source line is kept;
  - stripped files (libc, drivers): gdb `info symbol`, which reads the .gnu_debugdata mini symbol
    table that distro libraries ship (names like __memmove_avx_unaligned_erms); failing that,
    lib+offset.
Results are cached in SPROF.sym.json next to the profile, so re-running with other options is
instant.

Views: self time per function (innermost frame), inclusive per function (any frame, once per
stack); --under FUNC: self time and first callee of the stacks passing through FUNC; --lines FUNC:
self and inclusive time per source line of FUNC's own frame (what each line of FUNC costs,
including what it calls); --callers NAME: who calls the frames whose function matches NAME
(the next-outer frame's function and line). NAME/FUNC match as substrings. --raw also prints the
file+offset of each entry.
"""
import bisect, collections, json, os, re, subprocess, sys

args = sys.argv[1:]
def opt(name, default=None, conv=str):
    if name in args:
        i = args.index(name); v = conv(args[i + 1]); del args[i:i + 2]; return v
    return default
top = opt('--top', 30, int); under = opt('--under'); lines_of = opt('--lines'); callers = opt('--callers')
raw = '--raw' in args
if raw: args.remove('--raw')
path = args[0]

maps = []; stacks = []; header = []; mode = None
for line in open(path, errors='replace'):
    if line.startswith('# maps'): mode = 'maps'; continue
    if line.startswith('# stacks'): mode = 'stacks'; continue
    if line.startswith('#'): header.append(line.strip()); continue
    if mode == 'maps':
        f = line.split()
        if len(f) >= 6 and 'x' in f[1] and f[5].startswith('/'):
            lo, hi = (int(x, 16) for x in f[0].split('-')); maps.append((lo, hi, int(f[2], 16), f[5]))
    elif mode == 'stacks':
        stacks.append([int(x, 16) for x in line.split()])
maps.sort(); starts = [m[0] for m in maps]

def locate(addr):  # -> (file, file offset) or (None, addr)
    i = bisect.bisect_right(starts, addr) - 1
    if i >= 0 and maps[i][0] <= addr < maps[i][1]:
        lo, hi, off, file = maps[i]; return file, addr - lo + off
    return None, addr

def load_segments(file):
    segs = []
    try:
        for l in subprocess.run(['readelf', '-lW', file], capture_output=True, text=True).stdout.splitlines():
            f = l.split()
            if f and f[0] == 'LOAD': segs.append((int(f[1], 16), int(f[2], 16), int(f[4], 16)))  # off, vaddr, filesz
    except Exception: pass
    return segs
def off2va(segs, o):
    for off, va, sz in segs:
        if off <= o < off + sz: return o - off + va
    return o
def has_dwarf(file):
    try: return '.debug_info' in subprocess.run(['readelf', '-SW', file], capture_output=True, text=True).stdout
    except Exception: return False

# ---- symbolize every distinct address once, cached ----
cache_path = re.sub(r'\.txt$', '', path) + '.sym.json'
sym = {}
if os.path.exists(cache_path):
    try: sym = {int(k): v for k, v in json.load(open(cache_path)).items()}
    except Exception: sym = {}
addrs = set(a for s in stacks for a in s)
todo = collections.defaultdict(list)
for a in addrs:
    if a in sym: continue
    file, off = locate(a)
    todo[file].append((a, off))
for file, items in todo.items():
    base = os.path.basename(file) if file else '?'
    if file is None:
        for a, off in items: sym[a] = [[f'{a:#x}', '']]
        continue
    segs = load_segments(file)
    done = False
    if has_dwarf(file):
        inp = '\n'.join(f'{off2va(segs, off):#x}' for a, off in items)
        try:
            out = subprocess.run(['llvm-symbolizer-20', '-e', file, '-f', '-C', '-i'], input=inp, capture_output=True, text=True).stdout
            blocks = out.strip('\n').split('\n\n')
            if len(blocks) == len(items):
                for (a, off), b in zip(items, blocks):
                    ls = b.splitlines(); frames = []
                    for i in range(0, len(ls) - 1, 2):
                        fn = ls[i].strip(); loc = ls[i + 1].strip()
                        loc = loc.split('/')[-1]; loc = re.sub(r':\d+$', '', loc) if loc.count(':') >= 2 else loc  # drop column
                        if fn == '??' or not fn: fn = f'{base}+{off:#x}'
                        frames.append([fn, loc])
                    sym[a] = frames or [[f'{base}+{off:#x}', '']]
                done = True
        except FileNotFoundError: pass
    if not done:
        # stripped: gdb reads .gnu_debugdata; only the addresses seen often enough to matter
        counts = collections.Counter(a for s in stacks for a in s)
        items.sort(key=lambda t: -counts[t[0]])
        head = items[:400]
        try:
            cmds = ''.join(f'info symbol {off2va(segs, off):#x}\n' for a, off in head)
            cmd_file = cache_path + '.gdbcmds'; open(cmd_file, 'w').write(cmds)
            out = subprocess.run(['gdb', '-batch', '-x', cmd_file, file], capture_output=True, text=True, timeout=300).stdout
            os.remove(cmd_file)
            res = [l for l in out.splitlines() if ' in section ' in l or l.startswith('No symbol')]
            if len(res) == len(head):
                for (a, off), l in zip(head, res):
                    m = re.match(r'(.+?) \+ \d+ in section|(.+?) in section', l)
                    name = (m.group(1) or m.group(2)) if m else None
                    sym[a] = [[f'{base}:{name}' if name else f'{base}+{off:#x}', '']]
        except Exception: pass
        for a, off in items:
            if a not in sym: sym[a] = [[f'{base}+{off:#x}', '']]
try: json.dump({str(k): v for k, v in sym.items()}, open(cache_path, 'w'))
except Exception: pass

def short(fn):
    fn = re.sub(r'\(.*', '', fn)  # drop the parameter list
    fn = fn.replace('gtav::native::Backend::Impl::', '').replace('gtav::native::', '')
    return fn[:80]
def outer(a):  # the non-inlined function a frame belongs to
    return short(sym[a][-1][0])
def inner(a):  # innermost inlined function + line
    f = sym[a][0]; return f'{short(f[0])} [{f[1]}]' if f[1] else short(f[0])
def own_line(a, fn):  # the source line in FUNC's own frame (the frame whose function matches fn)
    for f in sym[a]:
        if fn in f[0]: return f[1]
    return None
def pct(c): return f'{100.0 * c / len(stacks):6.1f}%'
def rawtag(a):
    if not raw: return ''
    file, off = locate(a); return f'  ({os.path.basename(file) if file else "?"}+{off:#x})'

print('\n'.join(header)); print(f'{len(stacks)} stacks')
if not stacks: sys.exit()

selfc = collections.Counter(); self_ex = {}
for s in stacks:
    if not s: continue
    k = outer(s[0]); selfc[k] += 1; self_ex.setdefault(k, collections.Counter())[inner(s[0]) + rawtag(s[0])] += 1
print(f'\n== self time by function (innermost frame), top {top}; the hottest inlined site of each in brackets')
for n, c in selfc.most_common(top):
    site, sc = self_ex[n].most_common(1)[0]
    print(f'{pct(c)}  {n}    <{site} {100.0 * sc / c:.0f}%>')

incl = collections.Counter()
for s in stacks:
    for n in set(outer(a) for a in s): incl[n] += 1
print(f'\n== inclusive by function (any frame, once per stack), top {top}')
for n, c in incl.most_common(top): print(f'{pct(c)}  {n}')

if under:
    sub = [s for s in stacks if any(under in outer(a) for a in s)]
    print(f'\n== self time of the {len(sub)} stacks ({100.0 * len(sub) / len(stacks):.1f}%) through "{under}", top {top}')
    c2 = collections.Counter(); ex = {}
    for s in sub:
        k = outer(s[0]); c2[k] += 1; ex.setdefault(k, collections.Counter())[inner(s[0])] += 1
    for n, c in c2.most_common(top):
        site, sc = ex[n].most_common(1)[0]; print(f'{pct(c)}  {n}    <{site} {100.0 * sc / c:.0f}%>')
    print(f'\n== first callee below "{under}" (what it calls), top {top}')
    c3 = collections.Counter()
    for s in sub:
        idx = max(i for i, a in enumerate(s) if under in outer(a))
        c3[outer(s[idx - 1]) if idx > 0 else '(self)'] += 1
    for n, c in c3.most_common(top): print(f'{pct(c)}  {n}')

if lines_of:
    self_l = collections.Counter(); incl_l = collections.Counter()
    for s in stacks:
        idxs = [i for i, a in enumerate(s) if any(lines_of in f[0] for f in sym[a])]
        if not idxs: continue
        i = max(idxs)  # the outermost frame that is in FUNC (its own line, not an inlined callee's)
        ln = own_line(s[i], lines_of) or '?'
        incl_l[ln] += 1
        if i == 0 and lines_of in sym[s[0]][-1][0]: self_l[ln] += 1
    print(f'\n== "{lines_of}" by source line: inclusive (the line and everything it calls) / self, top {top}')
    for ln, c in incl_l.most_common(top): print(f'{pct(c)} / {pct(self_l[ln])}  {ln}')

if callers:
    cc = collections.Counter()
    for s in stacks:
        for i, a in enumerate(s):
            if any(callers in f[0] for f in sym[a]):
                if i + 1 < len(s): cc[f'{outer(s[i + 1])} [{sym[s[i + 1]][0][1]}]'] += 1
                else: cc['(root)'] += 1
                break
    print(f'\n== callers of "{callers}" (next-outer frame), top {top}')
    for n, c in cc.most_common(top): print(f'{pct(c)}  {n}')
