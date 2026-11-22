#!/usr/bin/env python3
"""nv_liveout.py GENERATED_DIR: functions that write a non-volatile register (r14-r31, f14-f31) they
never save (no __savegprlr_N / __savefpr_N covering it, no store of it to the stack): by the ABI
they must preserve it, so the write is an output for the caller (outlined fragments, shared
epilogues). With non_volatile_as_local the caller would never see it: share_registers."""
import re, sys, glob, os
W = re.compile(r'^\s*ctx\.([rf](?:1[4-9]|2[0-9]|3[01]))\.[a-z0-9]+\s*=')
SAVE = re.compile(r'__save(gprlr|fpr)_(\d+)\(ctx')
STK = re.compile(r'REX_STORE_\w+\(ctx\.r1\..*ctx\.([rf]\d+)\.')
out = []
for path in sorted(glob.glob(os.path.join(sys.argv[1], 'gtav_recomp.*.cpp'))):
    fn = None
    for line in open(path, errors='replace'):
        if line.startswith('DEFINE_REX_FUNC('):
            fn = line[16:line.index(')')]; written = set(); saved = set(); continue
        if fn is None: continue
        if line.startswith('}'):
            bad = sorted(written - saved)
            if bad and fn.startswith('sub_'): out.append(f'{fn} {",".join(bad)}')
            fn = None; continue
        m = SAVE.search(line)
        if m:
            k = 'r' if m.group(1) == 'gprlr' else 'f'
            saved |= {f'{k}{i}' for i in range(int(m.group(2)), 32)}
        m = STK.search(line)
        if m: saved.add(m.group(1))
        m = W.match(line)
        if m: written.add(m.group(1))
print('\n'.join(out))
