#!/usr/bin/env python3
# Mutants of ps3/ps3_jit_sched.h: each must make misc/sched_check fail. Run from WSL: python3 misc/sched_check/mutate.py
# [regions] [name part]. A mutant whose text is not in the header exactly once is reported NOT FOUND.
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HDR = os.path.join(ROOT, 'ps3', 'ps3_jit_sched.h')
WORK = '/tmp/schedmut'

MUTANTS = [
    ('no GPR read-after-write', '\t\t\tif (use && last_def[q] >= 0)\n', '\t\t\tif (use && last_def[q] >= 0 && q >= 32)\n'),
    ('no FPR read-after-write', '\t\t\tif (use && last_def[q] >= 0)\n', '\t\t\tif (use && last_def[q] >= 0 && (q < 32 || q >= 64))\n'),
    ('no CR/CA read-after-write', '\t\t\tif (use && last_def[q] >= 0)\n', '\t\t\tif (use && last_def[q] >= 0 && q < 64)\n'),
    ('no write-after-read', '\t\t\t\t\t\tif (i != j)\n\t\t\t\t\t\t\tsched_edge(g, i, j, 0);', '\t\t\t\t\t\t(void)i;'),
    ('no write-after-write', '\t\t\t\tif (last_def[q] >= 0)\n\t\t\t\t\tsched_edge(g, (u32)last_def[q], j, 1);', ''),
    ('memory never conflicts', '\t\t\tif (s.span[i] && s.span[j] && (s.hi[i] <= s.lo[j] || s.hi[j] <= s.lo[i]))', '\t\t\tif (true)'),
    ('memory disjoint when one span is known', 'if (s.span[i] && s.span[j] && (s.hi[i]', 'if ((s.span[i] || s.span[j]) && (s.hi[i]'),
    ('store after load unordered', '!(a.store || b.store)', '!(a.store && b.store)'),
    ('rlwinm reads rA', '\tcase 21:  // rlwinm\n\t\td.use_r = sched_gpr(rD);', '\tcase 21:  // rlwinm\n\t\td.use_r = sched_gpr(rA);'),
    ('rlwimi does not read rA', 'd.use_r = sched_gpr(rD) | sched_gpr(rA); d.def_r = sched_gpr(rA); sched_rc(d, w);',
     'd.use_r = sched_gpr(rD); d.def_r = sched_gpr(rA); sched_rc(d, w);'),
    ('D-form base not read', '\td.base = (u8)ra;\n\tif (ra)\n\t\td.use_r |= sched_gpr(ra);', '\td.base = (u8)ra;'),
    ('addic does not set CA', '\t\td.use_r = sched_gpr(rA); d.def_r = sched_gpr(rD); d.def_x = 1u << SX_CA;\n\t\tif (op == 13)',
     '\t\td.use_r = sched_gpr(rA); d.def_r = sched_gpr(rD);\n\t\tif (op == 13)'),
    ('adde does not read CA', 'if (xo == 136 || xo == 138) d.use_x |= 1u << SX_CA;', ''),
    ('record forms keep CR0', '\t\td.def_x |= 1u;\n\t\td.use_x |= 1u << SX_SO;', '\t\td.use_x |= 1u << SX_SO;'),
    ('cmpli writes CR0 only', '\t\td.use_r = sched_gpr(rA); d.use_x = 1u << SX_SO; d.def_x = 1u << (rD >> 2);\n\t\treturn true;\n\tcase 12:',
     '\t\td.use_r = sched_gpr(rA); d.use_x = 1u << SX_SO; d.def_x = 1u;\n\t\treturn true;\n\tcase 12:'),
    ('CR logic does not read BT', 'd.use_x = (1u << (rA >> 2)) | (1u << (rB >> 2)) | (1u << (rD >> 2));',
     'd.use_x = (1u << (rA >> 2)) | (1u << (rB >> 2));'),
    ('stfs does not read its FPR', '\t\td.use_r |= sched_fpr(rD); d.lat = 1; d.unit = SU_LS;\n\t\treturn true;\n\tcase 58:',
     '\t\td.lat = 1; d.unit = SU_LS;\n\t\treturn true;\n\tcase 58:'),
    ('mfocrf reads no field', '\t\t\t\t\tif (fxm & (0x80 >> f)) d.use_x = 1u << f;', ''),
    ('mtcrf writes no field', '\t\t\t\tif (fxm & (0x80 >> f)) d.def_x |= 1u << f;', ''),
    ('srawi keeps CA', '\t\t\td.use_r = sched_gpr(rS); d.def_r = sched_gpr(rA); d.def_x = 1u << SX_CA; sched_rc(d, w);',
     '\t\t\td.use_r = sched_gpr(rS); d.def_r = sched_gpr(rA); sched_rc(d, w);'),
    ('mfxer reads nothing', '\t\t\tif (xo == 339) { d.use_x = x; d.def_r = sched_gpr(rD); }', '\t\t\tif (xo == 339) { d.def_r = sched_gpr(rD); }'),
    ('lha is 4 bytes', 'd.size = op == 32 ? 4 : op == 34 ? 1 : 2;', 'd.size = op == 32 || op == 42 ? 4 : op == 34 ? 1 : 2;'),
    ('fmadd does not read C', '\tcase 28: case 29: case 30: case 31: c = true; break;', '\tcase 28: case 29: case 30: case 31: break;'),
    ('fmul reads B, not C', '\tcase 25: b = false; c = true; break;', '\tcase 25: break;'),
    ('RAM kind from any addis', '\t\telse if (op == 15 && p[rA].k == SP_RAMIDX && (w & 0xFFFF) == e.ram_hi)',
     '\t\telse if (op == 15 && p[rA].k == SP_RAMIDX)'),
    ('8C: RAM kind from any addis', '\t\telse if (op == 15 && e.bias_hi && (w & 0xFFFF) == e.bias_hi)',
     '\t\telse if (op == 15 && e.bias_hi)'),
    ('8C: an exact kind from the bias', '\t\telse if (op == 15 && e.bias_hi && (w & 0xFFFF) == e.bias_hi)   // 8C PREFIX: used only past its guard\n\t\t\tn = SchedPtr{ SP_RAM, 0, 0 };',
     '\t\telse if (op == 15 && e.bias_hi && (w & 0xFFFF) == e.bias_hi)   // 8C PREFIX: used only past its guard\n\t\t\tn = SchedPtr{ SP_ABS, e.ram_lo, e.ram_lo };'),
    ('RAM index from any mask', '\t\tif (mb == 8 && me == 31)', '\t\tif (me == 31)'),
    ('BASE: any indexed access in RAM', 'if (d.indexed && e.base_reg && d.index == e.base_reg)', 'if (d.indexed && e.base_reg)'),
    ('BASE: the base register as the base', 'if (d.indexed && e.base_reg && d.index == e.base_reg)',
     'if (d.indexed && e.base_reg && d.base == e.base_reg)'),
    ('ori adds, not ors', 'const s64 v = p[rD].lo | (s64)(w & 0xFFFF);', 'const s64 v = p[rD].lo + (s64)(w & 0xFFFF);'),
    ('xorism keeps a RAM kind', 'else if (op == 26 && p[rD].k == SP_RAM)', 'else if ((op == 26 || op == 27) && p[rD].k == SP_RAM)'),
    ('a defined register keeps its kind', '\tfor (u32 k = 0; k < 32; k++)\n\t\tif (d.def_r & sched_gpr(k))\n\t\t\tp[k].k = SP_UNK;\n', ''),
    ('a barrier keeps the kinds', '\tif (d.barrier)\n\t{\n\t\tfor (u32 k = 0; k < 32; k++)\n\t\t\tp[k].k = SP_UNK;\n\t}', '\tif (d.barrier)\n\t{\n\t}'),
    ('RAM span of 64 bytes', 'case SP_RAM: lo = e.ram_lo + (d.disp < 0 ? d.disp : 0); hi = e.ram_end + (d.disp > 0 ? d.disp : 0);',
     'case SP_RAM: lo = e.ram_lo; hi = e.ram_lo + 64;'),
    ('absolute span ignores the displacement', 'case SP_ABS: lo = b.lo + d.disp; hi = lo + d.size;', 'case SP_ABS: lo = b.lo; hi = lo + d.size;'),
    ('indexed access spans its base', '\tif (d.indexed)\n\t\treturn false;\n', ''),
    ('SQ range of 4 bytes', 'n = SchedPtr{ SP_RANGE, v, v + 64 };', 'n = SchedPtr{ SP_RANGE, v, v + 4 };'),
    ('joins ignored', '\t\tif (j >= lo && j < hi)\n\t\t\ts.start[j - lo] = 1;', '\t\t(void)j;'),
    ('branch targets ignored', '\t\tif (t && t >= lo && t < hi)\n\t\t\ts.start[t - lo] = 1;', '\t\t(void)t;'),
    ('a join keeps the kinds', '\t\tif (s.start[i])\n\t\t\tfor (u32 k = 0; k < 32; k++)\n\t\t\t\tif (k != 30)\n\t\t\t\t\tptr[k].k = SP_UNK;\n', ''),
    ('regions run over joins', '\t\twhile (j < n && j - i < SCHED_MAX && !s.start[j] && sched_decode(lo[j], d))',
     '\t\twhile (j < n && j - i < SCHED_MAX && sched_decode(lo[j], d))'),
    ('regions run over barriers', '\t\twhile (j < n && j - i < SCHED_MAX && !s.start[j] && sched_decode(lo[j], d))',
     '\t\twhile (j < n && j - i < SCHED_MAX && !s.start[j] && (sched_decode(lo[j], d) || (lo[j] >> 26) != 16))'),
    ('mtctr writes nothing', '\t\t\telse { d.use_r = sched_gpr(rS); d.def_x = x; }', '\t\t\telse { d.use_r = sched_gpr(rS); }'),
    ('divw is not busy... and reads only rA', 'case 457: case 459: case 489: case 491:   // divdu, divwu, divd, divw\n\t\t\tif (xo & 0x200) return sched_bar(d);\n\t\t\td.use_r = sched_gpr(rA) | sched_gpr(rB);',
     'case 457: case 459: case 489: case 491:   // divdu, divwu, divd, divw\n\t\t\tif (xo & 0x200) return sched_bar(d);\n\t\t\td.use_r = sched_gpr(rA);'),
    ('fcmpu writes CR0', '\t\t\td.use_r = sched_fpr(rA) | sched_fpr(rB); d.def_x = 1u << (rD >> 2);', '\t\t\td.use_r = sched_fpr(rA) | sched_fpr(rB); d.def_x = 1u;'),
    ('sraw keeps CA', '\t\t\td.use_r = sched_gpr(rS) | sched_gpr(rB); d.def_r = sched_gpr(rA); d.def_x = 1u << SX_CA; d.lat = 4;',
     '\t\t\td.use_r = sched_gpr(rS) | sched_gpr(rB); d.def_r = sched_gpr(rA); d.lat = 4;'),
]


def main():
    regions = sys.argv[1] if len(sys.argv) > 1 else '3000'
    part = sys.argv[2] if len(sys.argv) > 2 else ''
    src = open(HDR, encoding='utf-8').read().replace('\r\n', '\n')
    os.makedirs(WORK, exist_ok=True)
    caught = missed = notfound = 0
    for name, find, repl in MUTANTS:
        if part and part not in name:
            continue
        if src.count(find) != 1:
            print('NOT FOUND (%d): %s' % (src.count(find), name))
            notfound += 1
            continue
        path = os.path.join(WORK, 'ps3_jit_sched.h')
        with open(path, 'w', encoding='utf-8') as f:
            f.write(src.replace(find, repl))
        env = dict(os.environ, SCHED_HEADER=path)
        try:
            r = subprocess.run(['sh', 'misc/sched_check/run.sh', regions, '7'], cwd=ROOT, env=env,
                               capture_output=True, text=True, timeout=600)
            ok = r.returncode != 0
        except subprocess.TimeoutExpired:
            ok = True
        print(('CAUGHT  ' if ok else 'MISSED  ') + name)
        caught += ok
        missed += not ok
    print('%d caught, %d missed, %d not found' % (caught, missed, notfound))
    sys.exit(1 if missed or notfound else 0)


main()
