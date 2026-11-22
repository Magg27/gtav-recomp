# gdb: break at FRAG (host symbol), print the guest ctx r14-r31, cr0/cr6, ctr on the first N hits, then kill.
import gdb, os
N = int(os.environ.get('FRAG_N', '3')); hits = [0]
gdb.execute('set pagination off')
gdb.execute('handle SIGSEGV nostop noprint pass'); gdb.execute('handle SIGBUS nostop noprint pass')
class B(gdb.Breakpoint):
    def stop(self):
        hits[0] += 1
        c = gdb.parse_and_eval('*(PPCContext*)$rdi')
        regs = ' '.join(f'r{i}={int(c["r%d" % i]["u32"]):08x}' for i in [3,4,5] + list(range(14, 32)))
        print(f'HIT {hits[0]}: {regs} ctr={int(c["ctr"]["u32"]):08x}', flush=True)
        return hits[0] >= N
B(os.environ['FRAG'])
gdb.execute('run')
gdb.execute('kill')
