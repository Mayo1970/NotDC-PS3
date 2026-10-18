#!/usr/bin/env python3
# modem_check: each field of plugs/nullExtDev/modem_regs.h sits where Flycast's ref/modem_regs.h has it: host g++
# (little-endian, run) and ppu-gcc (big-endian, bytes read from the object). Args: work dir. Exit 1: a difference.
import os
import re
import subprocess
import sys

T = sys.argv[1]
OURS = os.environ.get('MODEM_REGS', 'plugs/nullExtDev/modem_regs.h')
REF = 'misc/modem_check/ref/modem_regs.h'
PPU = os.path.join(os.environ.get('PS3DEV', '/usr/local/ps3dev'), 'ppu/bin/ppu-')


def union_text(path):
    src = re.sub(r'//[^\n]*', '', open(path).read())
    body = src[src.index('union modemreg_t'):src.index('#pragma pack(pop)')]
    # BITS() as written: bit 0 first, the little-endian order.
    return re.sub(r'BITS\(([^)]*)\)', lambda m: '; '.join(x.strip() for x in m.group(1).split(',')) + ';', body)


def fields(path):
    """(path, width) of each named field, in order."""
    stack = [[]]
    for t in re.findall(r'\{|\}\s*\w*\s*;|[^;{}]+;', union_text(path)):
        t = t.strip()
        if t == '{':
            stack.append([])
        elif t.startswith('}'):
            name = t[1:].rstrip(';').strip()
            for p, w in stack.pop():
                stack[-1].append(((name + '.' + p) if name else p, w))
        else:
            m = re.match(r'(u8|u16)\s+(\w*)\s*(?::\s*(\d+))?\s*(\[[^\]]*\])?\s*;$', t)
            if not m:
                sys.exit('layout.py: cannot read "%s" in %s' % (t, path))
            typ, name, w, arr = m.groups()
            if name and not arr:
                stack[-1].append((name, int(w) if w else (16 if typ == 'u16' else 8)))
    if len(stack) != 1:
        sys.exit('layout.py: braces do not pair in ' + path)
    return stack[0]


def run(cmd):
    r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, universal_newlines=True)
    if r.returncode:
        sys.exit('layout.py: %s\n%s' % (' '.join(cmd), r.stdout))
    return r.stdout


def host_table(header, flist, flycast):
    src = ['#include "types.h"', '#include "%s"' % os.path.abspath(header), '#include <stdio.h>', '#include <string.h>',
           'static void dump(const char* n, const modemreg_t& r)',
           '{ for (int i = 0; i < 0x21; i++) if (r.ptr[i]) printf("%s %d %02X\\n", n, i, r.ptr[i]); }',
           'int main() {', 'printf("size %d\\n", (int)sizeof(modemreg_t));']
    for p, w in flist:
        if flycast and p in ('reg18', 'reg19'):
            src.append('{ modemreg_t r; memset(&r, 0, sizeof r); r.reg18_19 = %s; dump("%s", r); }'
                       % ('0x00FF' if p == 'reg18' else '0xFF00', p))
        else:
            src.append('{ modemreg_t r; memset(&r, 0, sizeof r); r.%s = %d; dump("%s", r); }' % (p, (1 << w) - 1, p))
    src.append('return 0; }')
    name = 'host_%s' % ('ref' if flycast else 'ours')
    open(os.path.join(T, name + '.cpp'), 'w').write('\n'.join(src) + '\n')
    run(['g++', '-std=c++17', '-w', '-Imisc/modem_check/fake_ref', '-o', os.path.join(T, name),
         os.path.join(T, name + '.cpp')])
    return parse(run([os.path.join(T, name)]).splitlines())


def parse(lines):
    size, tab = None, {}
    for ln in lines:
        a = ln.split()
        if a[0] == 'size':
            size = int(a[1])
        else:
            tab.setdefault(a[0], set()).add((int(a[1]), int(a[2], 16)))
    return size, tab


def init(p, w):
    parts = p.split('.')
    s = '.%s = %d' % (parts[-1], (1 << w) - 1)
    for q in reversed(parts[:-1]):
        s = '.%s = { %s }' % (q, s)
    return '{ %s }' % s


def ppu_table(flist):
    c = os.path.join(T, 'ppu_ours.c')
    src = ['#include "types.h"', '#include "%s"' % os.path.basename(OURS),
           '_Static_assert(sizeof(union modemreg_t) == 0x21, "size");',
           'const union modemreg_t layout_t[] __attribute__((section(".mlayout"))) = {']
    src += ['\t%s,' % init(p, w) for p, w in flist]
    src.append('};')
    open(c, 'w').write('\n'.join(src) + '\n')
    o, b = os.path.join(T, 'ppu_ours.o'), os.path.join(T, 'ppu_ours.bin')
    run([PPU + 'gcc', '-std=gnu11', '-O0', '-w', '-Imisc/modem_check/fake_c', '-I' + os.path.dirname(OURS), '-c', c,
         '-o', o])
    run([PPU + 'objcopy', '-O', 'binary', '-j', '.mlayout', o, b])
    data = open(b, 'rb').read()
    if len(data) != 0x21 * len(flist):
        sys.exit('layout.py: ppu-gcc gave %d bytes for %d fields' % (len(data), len(flist)))
    tab = {}
    for k, (p, w) in enumerate(flist):
        for i in range(0x21):
            if data[k * 0x21 + i]:
                tab.setdefault(p, set()).add((i, data[k * 0x21 + i]))
    return 0x21, tab


ours = fields(OURS)
ref = [(p, w) for p, w in fields(REF) if p != 'reg18_19'] + [('reg18', 8), ('reg19', 8)]
fail = 0
names_o, names_r = set(p for p, _ in ours), set(p for p, _ in ref)
if names_o != names_r:
    print('FAIL: fields only in ours: %s; only in Flycast: %s' % (sorted(names_o - names_r), sorted(names_r - names_o)))
    fail = 1
widths = dict(ref)
for p, w in ours:
    if p in widths and widths[p] != w:
        print('FAIL: %s is %d bits wide, Flycast %d' % (p, w, widths[p]))
        fail = 1
size_r, tab_r = host_table(REF, ref, True)
size_l, tab_l = host_table(OURS, ours, False)
size_b, tab_b = ppu_table(ours)
for what, size in (('Flycast', size_r), ('ours, host', size_l), ('ours, PPU', size_b)):
    if size != 0x21:
        print('FAIL: %s: sizeof(modemreg_t) = %s' % (what, size))
        fail = 1
for p, _ in ours:
    want = tab_r.get(p)
    for what, tab in (('host g++', tab_l), ('ppu-gcc', tab_b)):
        if tab.get(p) != want:
            print('FAIL: %s with %s at %s, Flycast at %s' % (p, what, sorted(tab.get(p, [])), sorted(want or [])))
            fail = 1
if not fail:
    print('PASS: layout: %d fields, each at Flycast\'s byte and bits with g++ (LE) and ppu-gcc (BE)' % len(ours))
sys.exit(fail)
