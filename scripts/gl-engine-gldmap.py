#!/usr/bin/env python3
"""Where Leopard's GLEngine keeps its gld* function table and who calls it.

  gl-engine-gldmap.py <GLEngine.ppc> <GLEngine.asm>

GLEngine.ppc: the ppc slice (lipo -thin ppc) of GLEngine.bundle/GLEngine from
Mac OS X 10.5.9; GLEngine.asm: otool -arch ppc -tV of it. Reads the table of
name pointers in __DATA,__const and counts, for a window of 65 consecutive
word offsets, the loads that are followed by an indirect call (mtspr ctr).
The window with the most hits is taken as the driver table: its offset in
the renderer's record and each name's slot. A heuristic: the printed call
site counts are what supports it.

Copyright (c) 2026 kouta-kun and Claude
SPDX-License-Identifier: MIT
"""
import collections
import re
import struct
import sys

ppc, asm = sys.argv[1:3]
d = open(ppc, 'rb').read()
L = open(asm, errors='replace').read().split('\n')

# the table of name pointers: consecutive words that point into __cstring
names = []
tab = None
for a in range(0, len(d) - 4, 4):
    p, = struct.unpack('>I', d[a:a + 4])
    if 0 < p < len(d) and d[p:p + 3] == b'gld' and d[p:p + 12].isascii():
        run = []
        b = a
        while True:
            q, = struct.unpack('>I', d[b:b + 4])
            if 0 < q < len(d) and d[q:q + 3] == b'gld':
                run.append(d[q:d.index(b'\0', q)].decode())
                b += 4
            else:
                break
        if len(run) > 40:
            tab, names = a, run
            break
print('name table at %#x, %d names' % (tab, len(names)))

pat = re.compile(r'\tlwz\tr(\d+),(0x[0-9a-f]+)\(r(\d+)\)')
disp = collections.defaultdict(list)
for i, l in enumerate(L):
    m = pat.search(l)
    if m and any(re.search(r'mtspr\tctr,r%s$' % m.group(1), x) for x in L[i + 1:i + 14]):
        disp[int(m.group(2), 16)].append(i)
best = (0, 0)
for b in sorted(disp):
    hits = sum(1 for k in range(len(names)) if (b + 4 * k) in disp)
    best = max(best, (hits, b))
print('table offset %#x: %d of %d slots have an indirect call' % (best[1], best[0], len(names)))
for k, n in enumerate(names):
    print('%-28s +%#05x  %3d call sites' % (n, best[1] + 4 * k, len(disp.get(best[1] + 4 * k, []))))
