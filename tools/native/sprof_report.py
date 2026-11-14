#!/usr/bin/env python3
"""sprof_report.py SPROF.txt [--top N] [--under FUNC]

Symbolizes and aggregates the stacks sprof_preload.so wrote (PIE binary + shared libraries: each
frame is mapped through /proc/self/maps to a file + offset and looked up in that file's symbol table
with nm). Prints: self time per function (innermost frame), inclusive time per function (any frame,
counted once per stack), and, with --under FUNC, the self-time histogram of the stacks that pass
through FUNC (what that function spends its time on)."""
import bisect, collections, os, re, subprocess, sys

args = sys.argv[1:]
top = 30; under = None
if '--top' in args: i = args.index('--top'); top = int(args[i + 1]); del args[i:i + 2]
if '--under' in args: i = args.index('--under'); under = args[i + 1]; del args[i:i + 2]
path = args[0]
maps = []; stacks = []; header = []
mode = None
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
maps.sort()
starts = [m[0] for m in maps]
symtabs = {}
def symtab(file):
    if file in symtabs: return symtabs[file]
    syms = []
    try:
        for flags in (['-C', '-S', '--defined-only', '-n'], ['-D', '-C', '-S', '--defined-only', '-n']):  # stripped libs: dynamic symbols
            out = subprocess.run(['nm', *flags, file], capture_output=True, text=True, errors='replace').stdout
            for l in out.splitlines():
                p = l.split(' ', 3)
                if len(p) == 4 and p[2] in 'tTwWiI': syms.append((int(p[0], 16), p[3], int(p[1], 16)))
                elif len(p) >= 3 and p[1] in 'tTwWiI': syms.append((int(p[0], 16), ' '.join(p[2:]), 0))
            if syms: break
        # PLT stubs would otherwise resolve to the symbol before them (often _init): label them
        out = subprocess.run(['readelf', '-SW', file], capture_output=True, text=True, errors='replace').stdout
        for l in out.splitlines():
            f = l.replace('[', ' ').replace(']', ' ').split()
            for k, t in enumerate(f):
                if t.startswith('.plt') and k + 3 < len(f):
                    try: syms.append((int(f[k + 2], 16), f'{os.path.basename(file)}:{t}', int(f[k + 4], 16) if k + 4 < len(f) else 0))
                    except ValueError: pass
    except Exception: pass
    syms.sort()
    symtabs[file] = (syms, [s[0] for s in syms])
    return symtabs[file]
def vaddr_base(file):
    # ELF: file offset -> virtual address of the first PT_LOAD (nm addresses are virtual)
    try:
        out = subprocess.run(['readelf', '-lW', file], capture_output=True, text=True).stdout
        for l in out.splitlines():
            f = l.split()
            if f and f[0] == 'LOAD': return int(f[1], 16), int(f[2], 16)  # offset, vaddr
    except Exception: pass
    return 0, 0
bases = {}
cache = {}
def sym(addr):
    if addr in cache: return cache[addr]
    i = bisect.bisect_right(starts, addr) - 1
    name = f'{addr:#x}'
    if i >= 0 and maps[i][0] <= addr < maps[i][1]:
        lo, hi, off, file = maps[i]
        if file not in bases: bases[file] = vaddr_base(file)
        foff, fvaddr = bases[file]
        va = addr - lo + off - foff + fvaddr
        syms, keys = symtab(file)
        j = bisect.bisect_right(keys, va) - 1
        if j >= 0 and (syms[j][2] == 0 or va < syms[j][0] + syms[j][2] + 16): name = syms[j][1]
        else: name = f'{os.path.basename(file)}+{va:#x}'
        name = re.sub(r'\(.*', '', name)[:90]
        if name.startswith('gtav::native::') and len(name) < 20: name = f'{os.path.basename(file)}+{va:#x}'
    cache[addr] = name
    return name
print('\n'.join(header)); print(f'{len(stacks)} stacks')
if not stacks: sys.exit()
selfc = collections.Counter(sym(s[0]) for s in stacks if s)
print(f'\n== self time (innermost frame), top {top}')
for n, c in selfc.most_common(top): print(f'{100.0 * c / len(stacks):6.1f}%  {n}')
incl = collections.Counter()
for s in stacks:
    for n in set(sym(a) for a in s): incl[n] += 1
print(f'\n== inclusive (any frame), top {top}')
for n, c in incl.most_common(top): print(f'{100.0 * c / len(stacks):6.1f}%  {n}')
if under:
    sub = [s for s in stacks if any(under in sym(a) for a in s)]
    print(f'\n== self time of the {len(sub)} stacks ({100.0 * len(sub) / len(stacks):.1f}%) through "{under}", top {top}')
    c2 = collections.Counter(sym(s[0]) for s in sub)
    for n, c in c2.most_common(top): print(f'{100.0 * c / len(stacks):6.1f}%  {n}')
    print(f'\n== first callee below "{under}" (what it calls), top {top}')
    c3 = collections.Counter()
    for s in sub:
        names = [sym(a) for a in s]
        idx = max(i for i, n in enumerate(names) if under in n)
        c3[names[idx - 1] if idx > 0 else '(self)'] += 1
    for n, c in c3.most_common(top): print(f'{100.0 * c / len(stacks):6.1f}%  {n}')
