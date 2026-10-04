#!/usr/bin/env python3
# flow.py FUNC: every executed word of FUNC in address order with cycles, mispredicts and source line (out/hot_all.txt)
import re, sys
dis = open('out/spe_aica_sim.dis').read().splitlines()
ins, src, cur = {}, {}, '?'
for l in dis:
    m = re.match(r'^(/\S+?):(\d+)', l)
    if m:
        cur = m.group(1).split('/')[-1] + ':' + m.group(2); continue
    m = re.match(r'^\s+([0-9a-f]+):\t(?:[0-9a-f]{2} ){4}\t(.*)$', l)
    if m:
        a = int(m.group(1), 16); ins[a] = re.sub(r'\s*<.*', '', m.group(2)); src[a] = cur
rows = [l.split() for l in open('out/hot.txt').read().splitlines()[1:]]
rows = sorted(rows, key=lambda f: int(f[0], 16))
for f in rows:
    a = int(f[0], 16)
    print('%s %9s %6s  %-30s %s' % (f[0], f[1], f[3], ins.get(a, '?')[:30], src.get(a, '?')))
