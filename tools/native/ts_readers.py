# gdb -p PID -batch -ex "py BASE=0x..." -x ts_readers.py: read-watch the timer fields 0x839C7838-0x839C7857
# (4 x 8 bytes, the x86 debug registers), skip the script VM's known readers, record up to N other stacks
# or until TS_SECS pass. Output: logs/ts_readers.txt ("<field> <stack>").
import gdb, os, time
N = int(os.environ.get('TS_N', '2000')); SECS = float(os.environ.get('TS_SECS', '60'))
SKIP = ('sub_83525E00', 'sub_82927BE0', 'sub_83525A90')
hits = [0]; t_end = time.time() + SECS
out = open(os.path.expanduser('~/gtav-linux/logs/ts_readers.txt'), 'w')
class W(gdb.Breakpoint):
    def __init__(self, spec, label):
        super().__init__(spec, type=gdb.BP_WATCHPOINT, wp_class=gdb.WP_READ); self.label = label
    def stop(self):
        f = gdb.newest_frame(); names = []
        for _ in range(12):
            if f is None: break
            names.append(f.name() or hex(f.pc())); f = f.older()
        if any(s in n for n in names[:3] for s in SKIP): return time.time() > t_end
        hits[0] += 1
        out.write(self.label + ' ' + ' <- '.join(names) + '\n')
        return hits[0] >= N or time.time() > t_end
gdb.execute('set pagination off')
for off in (0x7838, 0x7840, 0x7848, 0x7850):
    W(f'*(long*){BASE + 0x839C0000 + off:#x}', f'{0x839C0000 + off:08X}')
gdb.execute('continue')
out.close()
