#!/usr/bin/env python3
# hot.py: annotate out/hot.txt (address, cycles, mispredicts) with the instruction and its source line from the
# objdump -dl listing of the sim image.
import re, sys
dis = open('out/spe_aica_sim.dis').read().splitlines()
ins, src = {}, {}
cur = '?'
for l in dis:
    m = re.match(r'^(/\S+?):(\d+)', l)
    if m:
        cur = m.group(1).split('/')[-1] + ':' + m.group(2)
        continue
    m = re.match(r'^\s+([0-9a-f]+):\t(?:[0-9a-f]{2} ){4}\t(.*)$', l)
    if m:
        a = int(m.group(1), 16)
        ins[a] = re.sub(r'\s*<.*', '', m.group(2))
        src[a] = cur
lines = open('out/hot.txt').read().splitlines()
print(lines[0])
for l in lines[1:]:
    f = l.split()
    a = int(f[0], 16)
    print('%s %10s cyc %7s miss  %-34s %s' % (f[0], f[1], f[3], ins.get(a, '?')[:34], src.get(a, '?')))
