#!/usr/bin/env python3
"""gpu_hold.py [MiB=1024]: holds that much GPU memory (CUDA driver API via ctypes, no compute)
until killed, printing "held" once it has it. bench.sh starts it before the game boots: another
session's render queue (~/nts-cycles datagen/jobs/gen_frames.sh) launches a Blender render
whenever the GPU has under 700 MiB in use, polling every 30 s, and squeezed renders into the
game's boot on 11-15, spoiling most bench windows."""
import ctypes, signal, sys
mib = int(sys.argv[1]) if len(sys.argv) > 1 else 1024
cuda = ctypes.CDLL('libcuda.so.1')
assert cuda.cuInit(0) == 0, 'cuInit'
dev = ctypes.c_int()
assert cuda.cuDeviceGet(ctypes.byref(dev), 0) == 0, 'cuDeviceGet'
ctx = ctypes.c_void_p()
assert cuda.cuCtxCreate_v2(ctypes.byref(ctx), 0, dev) == 0, 'cuCtxCreate'
ptr = ctypes.c_uint64()
r = cuda.cuMemAlloc_v2(ctypes.byref(ptr), ctypes.c_size_t(mib << 20))
assert r == 0, f'cuMemAlloc {r}'
print('held', flush=True)
signal.pause()
