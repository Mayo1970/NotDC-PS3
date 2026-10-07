# 5.12 #2: the disassembly of one function with sim_vb's HOT cycles and mispredicts per instruction.
# Usage: python3 annot.py hot.txt image.dis function
import re
import sys

hot = {}
for line in open(sys.argv[1]):
    m = re.match(r'\s+([0-9A-F]{5})\s+(\d+) cyc\s+(\d+) miss', line)
    if m:
        hot[int(m.group(1), 16)] = (int(m.group(2)), int(m.group(3)))
fn = '<' + sys.argv[3] + '>'
inside = False
for line in open(sys.argv[2]):
    if re.match(r'^[0-9a-f]{8} <', line):
        inside = fn in line
        if inside:
            print(line.rstrip())
        continue
    if not inside:
        continue
    m = re.match(r'\s+([0-9a-f]+):\s+(?:[0-9a-f]{2} ){4}\s*(.*)', line)
    if m:
        a = int(m.group(1), 16)
        c, mi = hot.get(a, (0, 0))
        print('%6x %8d %6d  %s' % (a, c, mi, m.group(2)[:64]))
    elif line.startswith('/src'):
        print('    ' + line.strip()[len('/src/'):])
