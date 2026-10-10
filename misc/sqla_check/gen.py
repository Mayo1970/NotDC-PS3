#!/usr/bin/env python3
# Plans 5.27 and 5.43 (WSL): sqla_check's reference table, from dc/sh4/sh4_opcode_list.cpp as BuildOpcodeTables fills
# OpDesc. Per opcode: flags, pref register, GPRs written, GPRs only moved by a constant (@-Rn, add #imm). Arg: out file.
import re, struct, sys, os

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
src = open(os.path.join(ROOT, 'dc/sh4/sh4_opcode_list.cpp'), encoding='utf-8').read()
src = re.sub(r'/\*.*?\*/', '', src, flags=re.S)
src = '\n'.join(l.split('//')[0] for l in src.splitlines())

masks = {m: int(v, 16) for m, v in re.findall(r'#define\s+(Mask_\w+)\s+(0x[0-9A-Fa-f]+)', open(
    os.path.join(ROOT, 'dc/sh4/sh4_opcode_list.cpp'), encoding='utf-8').read())}

ENTRY = re.compile(r'\{\s*(\w+)\s*,\s*(\w+)\s*,\s*(Mask_\w+)\s*,\s*(0x[0-9A-Fa-f]+)\s*,\s*(\w+)\s*,\s*"([^"]*)"')
entries = [(masks[m.group(3)], int(m.group(4), 16), m.group(5), m.group(6)) for m in ENTRY.finditer(src)]
assert len(entries) > 190, len(entries)

PC_WRITERS = {'ReadWritePC', 'WritesSRRWPC', 'Branch_dir', 'Branch_rel', 'Branch_dir_d', 'Branch_rel_d'}
DELAYED = {'Branch_dir_d', 'Branch_rel_d'}
# Mnemonics whose last register operand is read, not written.
NO_DST = {'cmp/eq', 'cmp/ge', 'cmp/gt', 'cmp/hi', 'cmp/hs', 'cmp/str', 'cmp/pl', 'cmp/pz', 'tst', 'div0s', 'mul.l',
          'muls.w', 'mulu.w', 'dmuls.l', 'dmulu.l', 'braf', 'bsrf'}

def split_ops(s):
    out, depth, cur = [], 0, ''
    for ch in s:
        if ch == ',' and depth == 0:
            out.append(cur.strip()); cur = ''
            continue
        depth += ch == '('
        depth -= ch == ')'
        cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out

def analyze(op, typ, diss):
    mnem, _, rest = diss.strip().partition(' ')
    mnem = mnem.lower()
    ops = split_ops(rest)
    field = {'N': (op >> 8) & 15, 'M': (op >> 4) & 15}
    steps = other = 0
    for o in ops:
        m = re.fullmatch(r'@<REG_([NM])>\+', o)
        if m:
            other |= 1 << field[m.group(1)]
        m = re.fullmatch(r'@-<REG_([NM])>', o)
        if m:
            steps |= 1 << field[m.group(1)]
    last = ops[-1] if ops else ''
    m = re.fullmatch(r'<REG_([NM])>', last)
    if m and mnem not in NO_DST:
        if mnem == 'add' and ops[0].startswith('#'):
            steps |= 1 << field[m.group(1)]
        else:
            other |= 1 << field[m.group(1)]
    if last == 'R0' and mnem not in ('tst', 'cmp/eq'):
        other |= 1
    writes = steps | other
    flags = 1
    flags |= 2 if typ in PC_WRITERS else 0
    flags |= 4 if typ in DELAYED else 0
    flags |= 8 if (mnem in ('ldc', 'ldc.l') and last == 'SR') or mnem == 'rte' else 0
    flags |= 16 if mnem == 'pref' else 0
    return flags, field['N'], writes, steps & ~other

desc = [None] * 65536
for mask, key, typ, diss in entries:
    assert key & ~mask & 0xFFFF == 0, hex(key)
    free = ~mask & 0xFFFF
    sub = free
    while True:
        desc[key | sub] = (typ, diss)
        if sub == 0:
            break
        sub = (sub - 1) & free

with open(sys.argv[1], 'wb') as f:
    for op in range(65536):
        if desc[op] is None:
            f.write(struct.pack('<BBHH', 0, 0, 0, 0))
        else:
            fl, pr, wr, st = analyze(op, *desc[op])
            f.write(struct.pack('<BBHH', fl, pr, wr, st))
print(f'gen.py: {len(entries)} opcode entries, {sum(d is not None for d in desc)} defined opcodes')
