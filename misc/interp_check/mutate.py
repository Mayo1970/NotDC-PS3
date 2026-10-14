#!/usr/bin/env python3
# Plan section 7, bundle 20 (WSL): plants one bug at a time in a copy of an op body file (IC_OVL goes before the tree);
# run.sh must FAIL on each. A mutant that does not build is a bad mutant. Args: a text the names hold.
import os, shutil, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
OVL = '/tmp/icmut'
FPU = 'dc/sh4/sh4_fpu_ops.h'
MISC = 'dc/sh4/sh4_cpu_misc.h'
MOVS = 'dc/sh4/sh4_cpu_movs.h'
REGS = 'dc/sh4/sh4_registers.cpp'
SELF = 'dc/sh4/sh4_selftest.cpp'

MUTANTS = [
    (FPU, 'fneg FIXED takes FR(n & 0xE) with PR=1', '  if (fpscr.PR == 0 || g_interp_fix_preset)', '  if (fpscr.PR == 0)'),
    (FPU, 'fneg flips the low bit', '    fr_hex[n] ^= 0x80000000;\n  else', '    fr_hex[n] ^= 0x00000001;\n  else'),
    (MISC, 'mul.l FIXED adds', '\t\tmacl = r[n] * r[m];', '\t\tmacl = r[n] + r[m];'),
    (MISC, 'mac.w S=1 skipped with the row', '\tif (sr.S != 0 && !g_interp_fix_preset)', '\tif (sr.S != 0)'),
    (MISC, 'mac.w S=1 with MACH and MACL swapped', 'sh4_mac_w((u32)rm, (u32)rn, macl, mach, sr.status);',
     'sh4_mac_w((u32)rm, (u32)rn, mach, macl, sr.status);'),
    (MISC, 'mac.w S=1 without saturation', '\t\tif (sr.S != 0)\n\t\t\tsh4_mac_w(', '\t\tif (false)\n\t\t\tsh4_mac_w('),
    (MISC, 'mac.w same register read twice at Rn', 'ReadMem16(r[m] + (n == m ? 2 : 0))', 'ReadMem16(r[m])'),
    (MISC, 'mac.l same register read twice at Rn', 'ReadMemS32(rn, r[n] + (n == m ? 4 : 0));', 'ReadMemS32(rn, r[n]);'),
    (MISC, 'mac.l S=1 without saturation', '\tif (sr.S != 0)\n\t\tsh4_mac_l(', '\tif (false)\n\t\tsh4_mac_l('),
    (MISC, 'mac.l Rn not moved', '\tr[m] += 4;\n\tr[n] += 4;\n', '\tr[m] += 4;\n'),
    (MISC, 'addv FIXED T from either sign', 'sr.T = ((r[n] ^ ans) & (r[m] ^ ans)) >> 31;', 'sr.T = ((r[n] ^ ans) | (r[m] ^ ans)) >> 31;'),
    (MISC, 'subv FIXED T from Rm', 'sr.T = ((r[n] ^ r[m]) & (r[n] ^ ans)) >> 31;', 'sr.T = ((r[n] ^ r[m]) & (r[m] ^ ans)) >> 31;'),
    (MISC, 'sts.l FPSCR stores at Rn', '\tWriteMemU32(r[n] - 4, fpscr.full);', '\tWriteMemU32(r[n], fpscr.full);'),
    (MISC, 'stc.l SR moves Rn twice', '\tWriteMemU32(r[n] - 4, sr.GetFull());\n\tr[n] -= 4;',
     '\tr[n] -= 4;\n\tWriteMemU32(r[n] - 4, sr.GetFull());\n\tr[n] -= 4;'),
    (MOVS, 'mov.l Rm,@-Rn stores at Rn', '\t\tWriteMemU32(r[n] - 4, r[m]);\n\t\tr[n] -= 4;', '\t\tWriteMemU32(r[n], r[m]);\n\t\tr[n] -= 4;'),
    (MOVS, 'mov.w Rm,@-Rn moves Rn by 4', '\t\tWriteMemU16(r[n] - 2, r[m]);\n\t\tr[n] -= 2;', '\t\tWriteMemU16(r[n] - 2, r[m]);\n\t\tr[n] -= 4;'),
    (MOVS, 'sts.l MACL stores MACH', '\tWriteMemU32(r[n] - 4, macl);', '\tWriteMemU32(r[n] - 4, mach);'),
    (REGS, 'FPSCR cause mask one bit short', 'fpscr.full &= ~0x0003F000u;', 'fpscr.full &= ~0x0001F000u;'),
    (REGS, 'FPSCR cause cleared without the row', '\tif (g_fpscr_cause_preset)\n\t\tfpscr.full &=', '\tif (true)\n\t\tfpscr.full &='),
    (SELF, 'self-test keeps the test context', '\tSh4cntx = saved;\n', ''),
]

def run(ovl):
    env = dict(os.environ)
    env.pop('IC_OVL', None)
    if ovl:
        env['IC_OVL'] = ovl
    try:
        r = subprocess.run(['sh', os.path.join(ROOT, 'misc/interp_check/run.sh')], capture_output=True, text=True,
                           env=env, timeout=600)
    except subprocess.TimeoutExpired:
        return 1, 'TIMEOUT'
    out = [l for l in (r.stdout + r.stderr).splitlines() if 'FAIL' in l or 'error' in l]
    return r.returncode, out[0] if out else ''

def main():
    todo = [m for m in MUTANTS if len(sys.argv) < 2 or sys.argv[1] in m[1]]
    rc, line = run(None)
    if rc != 0:
        print(f'BASELINE FAILS: {line}')
        sys.exit(2)
    caught = 0
    for path, name, find, repl in todo:
        src = open(os.path.join(ROOT, path), encoding='utf-8', newline='').read().replace('\r\n', '\n')
        if src.count(find) != 1:
            print(f'BAD MUTANT (pattern found {src.count(find)} times): {name}')
            sys.exit(2)
        shutil.rmtree(OVL, ignore_errors=True)
        os.makedirs(os.path.dirname(os.path.join(OVL, path)), exist_ok=True)
        open(os.path.join(OVL, path), 'w', encoding='utf-8', newline='').write(src.replace(find, repl))
        rc, line = run(OVL)
        if rc == 2:
            print(f'BAD MUTANT (does not build): {name}: {line[:100]}')
            sys.exit(2)
        caught += rc != 0
        print(f"{'caught' if rc else 'MISSED'}: {name}: {line[:100]}", flush=True)
    print(f'{caught} of {len(todo)} mutants fail run.sh')
    sys.exit(0 if caught == len(todo) else 1)

main()
