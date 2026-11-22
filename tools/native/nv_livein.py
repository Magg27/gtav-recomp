#!/usr/bin/env python3
"""nv_livein.py GENERATED_DIR: functions that read a non-volatile register (r14-r31, f14-f31) before
writing it in their own body (linear scan), i.e. take a value from the caller in a non-volatile:
alternate entry points and fragments. With non_volatile_as_local those must be share_registers
functions (callers sync their locals through ctx around the call). Saves of the register to the
stack (REX_STORE_* ... ctx.r1 ... , ctx.rN) do not count as uses."""
import re, sys, glob, os
NV = re.compile(r'ctx\.([rf](?:1[4-9]|2[0-9]|3[01]))\b')
out = []
for path in sorted(glob.glob(os.path.join(sys.argv[1], 'gtav_recomp.*.cpp'))):
    fn = None
    for line in open(path, errors='replace'):
        if line.startswith('DEFINE_REX_FUNC('):
            fn = line[16:line.index(')')]; written = set(); hit = None; continue
        if fn is None or hit: 
            if line.startswith('}'): fn = None
            continue
        s = line.strip()
        if not s or s.startswith('//'): continue
        if line.startswith('}'):
            fn = None; continue
        lhs, rhs = None, s
        m = re.match(r'(ctx\.[rf]\d+)\.[a-z0-9]+\s*=\s*(.*)', s)
        if m: lhs, rhs = m.group(1)[4:], m.group(2)
        if s.startswith('REX_STORE') and 'ctx.r1.' in s: rhs = ''   # saving it to the stack
        for r in NV.findall(rhs):
            if r not in written:
                hit = r; out.append(f'{fn} {r}'); break
        if lhs and NV.fullmatch('ctx.' + lhs): written.add(lhs)
print('\n'.join(out))
