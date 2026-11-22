# gdb: run the program; on each SIGSEGV, if the fault address is SEGV_ADDR print a backtrace and kill,
# otherwise pass the signal on (the runtime's own handler serves write watches and MMIO).
import gdb, os
target = int(os.environ['SEGV_ADDR'], 16)
gdb.execute('set pagination off')
gdb.execute('handle SIGSEGV stop nopass noprint')
gdb.execute('handle SIGBUS nostop noprint pass')
gdb.execute('run')
while True:
    try: addr = int(gdb.parse_and_eval('$_siginfo._sifields._sigfault.si_addr'))
    except gdb.error: break
    if addr == target:
        gdb.execute('bt 25'); gdb.execute('kill'); break
    gdb.execute('signal SIGSEGV')
