#!/usr/bin/env python3
# Plan 5.26 (WSL): plants one bug at a time in a copy of ps3_jit_tcopy.h; run.sh must FAIL on each.
import os, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
HDR = 'ps3/ps3_jit_tcopy.h'
TMP = '/tmp/tcopymut/ps3_jit_tcopy.h'

MUTANTS = [
    ('ifb not a T writer', 'o.op == shop_ifb || o.op == shop_jdyn;', 'o.op == shop_jdyn;'),
    ('jdyn not a T writer', 'o.op == shop_ifb || o.op == shop_jdyn;', 'o.op == shop_ifb;'),
    ('rd2 ignored', ' || (o.rd2.is_r32i() && o.rd2._reg == reg_sr_T)', ''),
    ('rd ignored', '(o.rd.is_r32i() && o.rd._reg == reg_sr_T) || ', ''),
    ('first writer wins', 'for (size_t k = j; k-- > 0;)', 'for (size_t k = 0; k < j; k++)'),
    ('scan from the block end', 'for (size_t k = j; k-- > 0;)', 'for (size_t k = b.oplist.size(); k-- > 0;)'),
    ('any writer accepted', 'return tcopy_op_ok(b.oplist[k].op) ? (int)k : -1;', 'return (int)k;'),
    ('jcond operand not checked', ' || !b.oplist[j].rs1.is_r32i() || b.oplist[j].rs1._reg != reg_sr_T', ''),
    ('held kept after a later writer', '\tif (!s.armed && tcopy_may_write_t(o))\n\t\ts.held = false;\n', ''),
    ('every store copies', '\tif (s.armed)\n\t\ts.held = true;\n\treturn s.armed;', '\ts.held = true;\n\treturn true;'),
    ('copy without held', '\tif (s.armed)\n\t\ts.held = true;\n\treturn s.armed;', '\treturn s.armed;'),
    ('armed one op late', '(size_t)s.k == i', '(size_t)s.k + 1 == i'),
    ('held at block start', 's.armed = s.held = false;', 's.armed = false;\n\ts.held = true;'),
    ('option ignored', 's.k = b ? tcopy_writer(*b) : -1;', 's.k = b ? tcopy_writer(*b) : 0;'),
    ('tst not listed', 'case shop_test: case shop_seteq:', 'case shop_seteq:'),
    ('fcmp/gt not listed', 'case shop_fseteq: case shop_fsetgt:', 'case shop_fseteq:'),
    ('div1 not listed', 'case shop_subv: case shop_div1:', 'case shop_subv:'),
    ('readm listed', 'case shop_adc: case shop_sbc:', 'case shop_readm: case shop_adc: case shop_sbc:'),
]

def main():
    iters = sys.argv[1] if len(sys.argv) > 1 else '40000'
    src = open(os.path.join(ROOT, HDR), encoding='utf-8').read()
    caught = fuzz_caught = 0
    os.makedirs(os.path.dirname(TMP), exist_ok=True)
    for name, find, repl in MUTANTS:
        if src.count(find) != 1:
            print(f'BAD MUTANT (pattern found {src.count(find)} times): {name}')
            sys.exit(2)
        open(TMP, 'w', encoding='utf-8').write(src.replace(find, repl))
        env = dict(os.environ, TCOPY_HEADER=TMP)
        run = ['sh', os.path.join(ROOT, 'misc/tcopy_check/run.sh'), '1', iters]
        r = subprocess.run(run, capture_output=True, text=True, env=env)
        ok = r.returncode != 0
        caught += ok
        f = subprocess.run(run, capture_output=True, text=True, env=dict(env, TCOPY_FUZZ_ONLY='1'))
        fuzz_caught += f.returncode != 0
        out = [l for l in (r.stdout + r.stderr).splitlines() if l and not l.startswith('  op') and l != 'directed tests: ok']
        first = next((l for l in out if ':' in l and ('k ' in l or 'CHECK' in l or 'directed' in l)), out[0] if out else '')
        print(f"{'CAUGHT' if ok else 'MISSED'} fuzz:{'yes' if f.returncode else 'no '}  {name}: {first[:110]}")
    print(f'{caught}/{len(MUTANTS)} mutants caught, {fuzz_caught} by the fuzzer alone')
    sys.exit(0 if caught == len(MUTANTS) else 1)

main()
