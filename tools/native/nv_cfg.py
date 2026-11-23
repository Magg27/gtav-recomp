#!/usr/bin/env python3
"""nv_cfg.py GENERATED_DIR [--known MANIFEST.toml]: non-volatile registers live into each
recompiled function, by control flow (nv_livein.py reads the text top to bottom and misses a read
on a path the text shows after a write on another).

For codegen's non_volatile_as_local: a function that reads r14-r31 / f14-f31 / v14-v31 / v64-v127
on some path before writing it takes the value from its caller and must be a share_registers
function. The generated code is read as it is without the option (registers as ctx.rN):
  - blocks start at the function and at every loc_XXXXXXXX: label; goto / return end them (inside
    an if or a switch they are conditional: the block also falls through)
  - a store of the register to the stack (address from r1, directly or through a register or
    `ea` computed from r1) is a prologue save, not a read; the __save*/__rest* helpers are skipped
  - a register the function saves itself (stack save, or its __save* helper call) is its own:
    a read of it before a write is a read of an uninitialised variable in the original program
    (common for vectors built piece by piece), not a value taken from the caller. Such registers
    are dropped from the result (--all keeps them)
  - a direct call to a function that needs registers from its caller reads them at the call
    (repeated until nothing changes), so thin wrappers around such functions are found too
--roots: only functions that read a register themselves (calls not followed).
--writes: instead, functions that write a non-volatile they never save (a value handed back to
the caller: live-out), again only the ones not marked yet with --known.
--nonarg: the same questions for non_argument_as_local's set (r0, r2, r11, r12, f0, v32-v63).
These are volatile, so nothing is "saved": a stack store counts as a read. What it finds are
fragments that continue with their parent's r11/r12 and SEH funclets that get the frame in r12;
vector hits are mostly `vrlimi128 vD,vS,7,0`-style merges with an undefined source, harmless.
--after-calls (with --nonarg): registers read after a call before being rewritten, i.e. values a
callee hands back. Kept only when the callee writes the register itself or through its direct
calls, or the call is indirect (the program is whole-program optimised: a caller keeps r11 or f0
live across a callee it knows does not touch them, which works with locals). Value-independent
idioms (`subfe rD,rX,rX`, `xor rD,rX,rX`, ...) are not reads. Prints "callee reg:n | from callers".
Prints "name reg reg ..." for every function with a register live at entry; with --known, only
the ones the manifest does not mark share_registers yet (and how many it already has)."""
import glob
import os
import re
import sys

KEEP_SAVED = "--all" in sys.argv
WRITES = "--writes" in sys.argv
ROOTS_ONLY = "--roots" in sys.argv  # without following calls: the functions that read one themselves
args = [a for a in sys.argv[1:] if not a.startswith("--")]
root = args[0]
known = set()
if "--known" in sys.argv:
    for line in open(sys.argv[sys.argv.index("--known") + 1]):
        m = re.match(r"\s*(0x[0-9A-Fa-f]+)\s*=\s*\{[^}]*share_registers\s*=\s*true", line)
        if m:
            known.add("sub_%08X" % int(m.group(1), 16))

NV = r"(?:r(?:1[4-9]|2[0-9]|3[01])|f(?:1[4-9]|2[0-9]|3[01])|v(?:1[4-9]|2[0-9]|3[01]|6[4-9]|[7-9][0-9]|1[01][0-9]|12[0-7]))"
# --nonarg: the same questions for non_argument_as_local's set instead (r0, r2, r11, r12, f0,
# v32-v63). Nothing saves these, so a store to the stack is a read like any other.
NONARG = "--nonarg" in sys.argv
if NONARG:
    NV = r"(?:r(?:0|2|11|12)|f0|v(?:3[2-9]|[45][0-9]|6[0-3]))"
MENTION = re.compile(r"\bctx\.(" + NV + r")\b")
ASSIGN = re.compile(r"^ctx\.(" + NV + r")\.\w+(?:\[\d+\])?\s*=[^=]")
VSTORE = re.compile(r"^simde_mm_store\w*\(\s*(?:\([^)]*\)\s*)?ctx\.(" + NV + r")\.\w+\s*,")
WHOLE = re.compile(r"^ctx\.(" + NV + r") = ")
STORE_SP = re.compile(r"^REX_STORE_\w+\(ctx\.(r\d+)\.u32 \+ -?\d+, ")
STORE_EA = re.compile(r"^(?:REX_STORE_\w+\(ea(?: \+ i)?, |simde_mm_store\w*\(\s*\([^)]*\)\s*REX_RAW_ADDR\(ea\))")
SET_REG = re.compile(r"^ctx\.(r\d+)\.\w+ = (.*);$")
SET_EA = re.compile(r"^ea = (.*);$")
REG = re.compile(r"\bctx\.(r\d+)\b")
LABEL = re.compile(r"^(loc_[0-9A-Fa-f]+):")
GOTO = re.compile(r"\bgoto (loc_[0-9A-Fa-f]+);")
CALL = re.compile(r"\b((?:sub_[0-9A-Fa-f]{8})|__(?:save|rest)\w+)\(ctx, base\);")
SAME = re.compile(r"// (?:subfe|subf|xor|andc|eqv|vxor|vxor128|vandc|vandc128|vcmpequb|vcmpequh|vcmpequw|vcmpequw128|vsubuwm|vsububm|vsubuhm) \w+,(\w+),(\w+)$")
INDIRECT = re.compile(r"\bREX_CALL_INDIRECT_FUNC\(")


HELPER = re.compile(r"^__(?:save|rest)(gprlr|fpr|vmx)_(\d+)$")


def helper_regs(name):
    m = HELPER.match(name)
    if not m:
        return ()
    kind, n = m.group(1), int(m.group(2))
    if kind == "vmx":
        return ["v%d" % i for i in range(n, 32 if n < 32 else 128)]
    return [("r" if kind == "gprlr" else "f") + str(i) for i in range(n, 32)]


saved = {}    # function -> registers it saves itself
written = {}  # function -> registers it writes


class Block:
    __slots__ = ("events", "succ", "falls")

    def __init__(self):
        self.events = []  # ("u", reg) / ("d", reg) / ("c", callee)
        self.succ = []    # label names
        self.falls = True


def parse(path, funcs):
    fn = None
    for raw in open(path, errors="replace"):
        if raw.startswith("DEFINE_REX_FUNC("):
            fn = raw[16:raw.index(")")]
            blocks = [Block()]
            labels = {}
            sp = set()
            funcs[fn] = (blocks, labels)
            sv = saved[fn] = set()
            wr = written[fn] = set()
            same = None
            continue
        if fn is None:
            continue
        if raw.startswith("}"):
            fn = None
            continue
        depth = len(raw) - len(raw.lstrip("\t"))
        s = raw.strip()
        if s.startswith("// "):
            # "subfe rD,rX,rX", "xor rD,rX,rX", "vxor vD,vX,vX"...: the result does not depend on
            # rX, and compilers use whatever register is at hand: not a read of its value.
            m = SAME.match(s)
            same = m.group(1) if m and m.group(1) == m.group(2) else None
            continue
        if not s:
            continue
        if same:
            s = s.replace("ctx." + same + ".", "ctx.same_.").replace("ctx." + same + ")", "ctx.same_)")
        m = LABEL.match(s)
        if m:
            labels[m.group(1)] = len(blocks)
            blocks.append(Block())
            sp = set()
            continue
        b = blocks[-1]
        # Registers (and `ea`) that hold an address made from the stack pointer.
        save = False
        m = STORE_SP.match(s)
        if m and (m.group(1) == "r1" or m.group(1) in sp):
            save = True
        elif STORE_EA.match(s) and "ea" in sp:
            save = True
        m = SET_EA.match(s)
        if m:
            (sp.add if any(r == "r1" or r in sp for r in REG.findall(m.group(1))) else sp.discard)("ea")
        m = SET_REG.match(s)
        if m and m.group(1) != "r1":
            from_sp = any(r == "r1" or r in sp for r in REG.findall(m.group(2))) and "REX_LOAD" not in m.group(2)
            (sp.add if from_sp else sp.discard)(m.group(1))
        if save and not NONARG:
            sv.update(MENTION.findall(s))
        else:
            d = ASSIGN.match(s) or VSTORE.match(s) or WHOLE.match(s)
            if d:
                wr.add(d.group(1))
            rest = s[d.end():] if d else s
            for r in MENTION.findall(rest):
                b.events.append(("u", r))
            if d:
                b.events.append(("d", d.group(1)))
        c = CALL.search(s)
        if c and not c.group(1).startswith("__"):
            b.events.append(("c", c.group(1)))
        elif c and c.group(1).startswith("__save") and not NONARG:
            sv.update(helper_regs(c.group(1)))
        if s.startswith("__builtin_trap()"):  # the end of a jump table: nothing falls out of it
            b.falls = False
            blocks.append(Block())
            continue
        if INDIRECT.search(s):
            b.events.append(("c", "(indirect)"))
        g = GOTO.search(s)
        conditional = depth > 1 or s.startswith("if ") or s.startswith("case ") or s.startswith("default")
        if g:
            b.succ.append(g.group(1))
            if not conditional:
                b.falls = False
            # A new block after every branch, taken or not: code after a conditional branch must
            # not inherit what is live at the branch target.
            blocks.append(Block())
        elif s == "return;" and not conditional:
            b.falls = False
            blocks.append(Block())


funcs = {}
for path in sorted(glob.glob(os.path.join(root, "gtav_recomp.*.cpp"))):
    parse(path, funcs)

livein = {name: frozenset() for name in funcs}


def analyse(name):
    blocks, labels = funcs[name]
    n = len(blocks)
    use, define = [], []
    for b in blocks:
        u, d = set(), set()
        for kind, x in b.events:
            if kind == "u":
                if x not in d:
                    u.add(x)
            elif kind == "d":
                d.add(x)
            elif not ROOTS_ONLY:
                for r in livein.get(x, ()):
                    if r not in d:
                        u.add(r)
        use.append(u)
        define.append(d)
    succ = []
    for i, b in enumerate(blocks):
        s = [labels[t] for t in b.succ if t in labels]
        if b.falls and i + 1 < n:
            s.append(i + 1)
        succ.append(s)
    live = [set(u) for u in use]
    changed = True
    while changed:
        changed = False
        for i in range(n - 1, -1, -1):
            out = set()
            for s in succ[i]:
                out |= live[s]
            new = use[i] | (out - define[i])
            if new != live[i]:
                live[i] = new
                changed = True
    return frozenset(live[0] if KEEP_SAVED else live[0] - saved[name])


def order(r):
    return (r[0], int(r[1:]))


callers = {}
for name, (blocks, _) in funcs.items():
    for b in blocks:
        for kind, x in b.events:
            if kind == "c":
                callers.setdefault(x, set()).add(name)

work = set(funcs)
rounds = 0
while work:
    rounds += 1
    nxt = set()
    for name in work:
        new = analyse(name)
        if new != livein[name]:
            livein[name] = new
            nxt |= callers.get(name, set())
    work = nxt

if "--after-calls" in sys.argv:
    # Registers read after a call before the function writes them again: values the callee hands
    # back. Per callee, the registers and how many call sites.
    back = {}
    for name, (blocks, labels) in funcs.items():
        n = len(blocks)
        use, define = [], []
        for b in blocks:
            u, d = set(), set()
            for kind, x in b.events:
                if kind == "u":
                    if x not in d:
                        u.add(x)
                elif kind == "d":
                    d.add(x)
            use.append(u)
            define.append(d)
        succ = []
        for i, b in enumerate(blocks):
            t = [labels[x] for x in b.succ if x in labels]
            if b.falls and i + 1 < n:
                t.append(i + 1)
            succ.append(t)
        live = [set(u) for u in use]
        changed = True
        while changed:
            changed = False
            for i in range(n - 1, -1, -1):
                out = set()
                for t in succ[i]:
                    out |= live[t]
                new = use[i] | (out - define[i])
                if new != live[i]:
                    live[i] = new
                    changed = True
        for i, b in enumerate(blocks):
            cur = set()
            for t in succ[i]:
                cur |= live[t]
            for kind, x in reversed(b.events):
                if kind == "u":
                    cur.add(x)
                elif kind == "d":
                    cur.discard(x)
                elif cur:
                    e = back.setdefault(x, {})
                    for r in cur:
                        e.setdefault(r, set()).add(name)
    # A register can stay live across a call the compiler knows not to touch it (whole-program
    # builds do that for volatile registers): that works with locals too. Only a callee that
    # writes the register, itself or through its own direct calls, hands a value back.
    wstar = {n: set(w) for n, w in written.items()}
    calls = {n: {x for b in blocks for k, x in b.events if k == "c"} for n, (blocks, _) in funcs.items()}
    changed = True
    while changed:
        changed = False
        for n, cs in calls.items():
            w = wstar[n]
            before = len(w)
            for c in cs:
                w |= wstar.get(c, set())
            if len(w) != before:
                changed = True
    for callee in sorted(back):
        regs = {r: v for r, v in back[callee].items() if callee == "(indirect)" or r in wstar.get(callee, ())}
        if regs:
            print(callee, " ".join(f"{r}:{len(v)}" for r, v in sorted(regs.items(), key=lambda kv: order(kv[0]))),
                  "| from", " ".join(sorted(set().union(*regs.values()))[:6]))
    sys.exit(0)

if WRITES:
    livein = {n: frozenset(written[n] - saved[n]) for n in funcs}
hits = {n: v for n, v in livein.items() if v and not n.startswith("__")}
new = {n: v for n, v in hits.items() if n not in known}



print(f"# {len(funcs)} functions, {len(hits)} with a non-volatile {'written but never saved' if WRITES else 'live at entry'} ({rounds} rounds)"
      + (f", {len(hits) - len(new)} of them already share_registers, {len(new)} not" if known else ""), file=sys.stderr)
for n in sorted(new):
    print(n, " ".join(sorted(new[n], key=order)))
