#!/usr/bin/env python3
"""Do the two sides of a --gtav_native_ab run draw the same picture?

usage: ab_shots_diff.py SHOT_DIR [--gap FRAMES] [--worst N] [--save OUT_DIR]
  (needs numpy; on this laptop: ~/.venvs/reskate/bin/python3 tools/native/ab_shots_diff.py ...)

SHOT_DIR holds the GTAV_NATIVE_SHOT_DIR captures of an A/B run, named native_<frame>_ab0.ppm /
native_<frame>_ab1.ppm after the side each frame was recorded with. Capture with
GTAV_SHOT_FRAMES=15: the A/B flips every 60 frames and the first 8 after a flip carry no side, so
that gives three captures per side per flip. There is no replay of a frame, so two captures are
never of the same frame: the scene moves between them (traffic, peds, clouds). The test is
therefore statistical. Captures the same number of frames apart are paired (30 with that cadence:
the first and third of a side, or the third of one side and the first of the next); a pair on
one side measures how much the scene moves by itself, a pair across the flip that plus whatever
the setting changes. A setting that draws the same picture gives the same distribution for both
kinds of pairs; a broken one shows as cross-side pairs above the same-side range.

Per pair: mean absolute difference (levels, 0-255) and the share of pixels differing by more than
24 levels in some channel (missing or misplaced geometry, as opposed to shimmer). --save writes
the worst cross-side pairs as amplified difference images.
"""
import glob
import os
import re
import sys

import numpy as np


def load(path):
    with open(path, 'rb') as f:
        data = f.read()
    # P6\n<w> <h>\n255\n<pixels>
    m = re.match(rb'P6\s+(\d+)\s+(\d+)\s+255\s', data)
    if not m:
        raise ValueError(f'{path}: not a binary PPM')
    w, h = int(m.group(1)), int(m.group(2))
    px = np.frombuffer(data, np.uint8, w * h * 3, m.end())
    return px.reshape(h, w, 3).astype(np.int16)


def stats(values):
    v = np.sort(np.array(values, np.float64))
    if not len(v):
        return 'none'
    return (f'n {len(v):3d}  median {np.median(v):7.3f}  p90 {v[int(0.9 * (len(v) - 1))]:7.3f}  '
            f'max {v[-1]:7.3f}')


def main():
    args = sys.argv[1:]
    opts = {'--gap': 0, '--worst': 3, '--save': None}
    for name in list(opts):
        if name in args:
            i = args.index(name)
            opts[name] = args[i + 1] if name == '--save' else int(args[i + 1])
            del args[i:i + 2]
    if len(args) != 1:
        sys.exit(__doc__)
    shots = []
    for path in glob.glob(os.path.join(args[0], 'native_*_ab[01].ppm')):
        m = re.search(r'native_(\d+)_ab([01])\.ppm$', path)
        shots.append((int(m.group(1)), int(m.group(2)), path))
    shots.sort()
    # Pairs the same number of frames apart, on one side and across the flip: the smallest gap
    # that has enough of both kinds (captures in the first frames after a flip carry no side).
    count = {}
    for i, (frame, side, _) in enumerate(shots):
        for other, other_side, _ in shots[i + 1:i + 12]:
            c = count.setdefault(other - frame, [0, 0])
            c[side != other_side] += 1
    gaps = sorted(g for g, c in count.items() if min(c) >= 8)
    gap = opts['--gap'] or (gaps[0] if gaps else 0)
    if not gap or gap not in count:
        sys.exit(f'{args[0]}: {len(shots)} tagged captures, no gap with same-side and cross-side pairs '
                 f'(gaps seen: { {g: tuple(c) for g, c in sorted(count.items())[:8]} }); capture with '
                 f'GTAV_SHOT_FRAMES=15')
    pairs = {'same': [], 'cross': []}
    recent = []  # (frame, side, image, path) within `gap` frames
    for frame, side, path in shots:
        image = load(path)
        recent = [r for r in recent if frame - r[0] <= gap]
        for prev in recent:
            if frame - prev[0] != gap or image.shape != prev[2].shape:
                continue
            d = np.abs(image - prev[2])
            kind = 'same' if side == prev[1] else 'cross'
            keep = d if kind == 'cross' and opts['--save'] else None
            pairs[kind].append((float(d.mean()), float((d.max(axis=2) > 24).mean()) * 100, prev[3], path, keep))
        recent.append((frame, side, image, path))
    print(f'{len(shots)} captures, pairs {gap} frames apart')
    for kind in ('same', 'cross'):
        print(f'{kind:5s}-side  mean abs diff: {stats([p[0] for p in pairs[kind]])}')
        print(f'{"":5s}       % pixels >24:  {stats([p[1] for p in pairs[kind]])}')
    if not pairs['same'] or not pairs['cross']:
        sys.exit('not enough pairs of both kinds')
    same_max = max(p[0] for p in pairs['same'])
    same_big = max(p[1] for p in pairs['same'])
    over = [p for p in pairs['cross'] if p[0] > same_max * 1.25 + 0.05 or p[1] > same_big * 1.25 + 0.05]
    worst = sorted(pairs['cross'], key=lambda p: -p[1])[:opts['--worst']]
    for mean, big, a, b, d in worst:
        print(f'  cross pair {os.path.basename(a)} -> {os.path.basename(b)}: mean {mean:.3f}, {big:.2f}% >24')
        if opts['--save'] and d is not None:
            from PIL import Image
            os.makedirs(opts['--save'], exist_ok=True)
            out = os.path.join(opts['--save'], os.path.basename(b).replace('.ppm', '_diff.png'))
            Image.fromarray(np.clip(d * 4, 0, 255).astype(np.uint8)).save(out)
    med = lambda k, i: float(np.median([p[i] for p in pairs[k]]))
    print(f'median ratio cross/same: mean {med("cross", 0) / max(med("same", 0), 1e-9):.2f}, '
          f'>24 {med("cross", 1) / max(med("same", 1), 1e-9):.2f}')
    print(f'RESULT: {len(over)} of {len(pairs["cross"])} cross-side pairs above the same-side range'
          f' ({"same picture" if not over else "LOOK AT THESE"})')
    return 1 if over else 0


if __name__ == '__main__':
    sys.exit(main())
