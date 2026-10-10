#!/usr/bin/env python3
# Plan 5.37 (WSL): plants one bug at a time in a copy of decoder.cpp; run.sh must FAIL on each.
import os, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
TMP = '/tmp/fschgmut'
F = 'dc/sh4/rec_v2/decoder.cpp'
HEAD = '//fschg\nsh4dec(i1111_0011_1111_1101)\n{\n\t//fpscr.SZ is bit 20\n'
XOR = '\tblock.Emit(shop_xor,reg_fpscr,reg_fpscr,mk_imm(1<<20));\n'
FLIP = '\tstate.cpu.FSZ64=!state.cpu.FSZ64;\n'
LEVEL = '\tif (get_jit_fschg_preset()>=2)\n\t\treturn;\n'
SYNC = '\tblock.Emit(shop_sync_fpscr,shil_param(),shil_param(),shil_param(),SYNC_FPSCR_SZ_ONLY);\n'
END = '\tif (!state.cpu.is_delayslot)\n\t\tdec_End(state.cpu.rpc + 2, BET_StaticJump, false);\n}\n\n//frchg\n'

MUTANTS = [
    ('level 1 also drops the sync', LEVEL, '\tif (get_jit_fschg_preset()>=1)\n\t\treturn;\n'),
    ('level 2 keeps the old path', LEVEL, '\tif (get_jit_fschg_preset()>=3)\n\t\treturn;\n'),
    ('level 2 skips the SZ flip', HEAD + XOR + FLIP, HEAD + XOR + LEVEL + FLIP),
    ('level 2 skips the xor', HEAD + XOR, HEAD + LEVEL + XOR),
    ('no SZ flip at all', HEAD + XOR + FLIP, HEAD + XOR),
    ('FR bit flipped, not SZ', HEAD + XOR, HEAD + '\tblock.Emit(shop_xor,reg_fpscr,reg_fpscr,mk_imm(1<<21));\n'),
    ('level 2 keeps the block end', LEVEL, '\tif (get_jit_fschg_preset()>=2)\n\t{\n\t\tif (!state.cpu.is_delayslot)\n'
     '\t\t\tdec_End(state.cpu.rpc + 2, BET_StaticJump, false);\n\t\treturn;\n\t}\n'),
    ('level 2 keeps the sync', LEVEL, '\tif (get_jit_fschg_preset()>=2)\n\t{\n' + '\t' + SYNC + '\t\treturn;\n\t}\n'),
    ('old path loses the SZ-only flag', SYNC, '\tblock.Emit(shop_sync_fpscr);\n'),
    ('old path ends in a delay slot too', END, '\tdec_End(state.cpu.rpc + 2, BET_StaticJump, false);\n}\n\n//frchg\n'),
    ('old path ends at the fschg itself', END,
     '\tif (!state.cpu.is_delayslot)\n\t\tdec_End(state.cpu.rpc, BET_StaticJump, false);\n}\n\n//frchg\n'),
]

def main():
    src = open(os.path.join(ROOT, F), encoding='utf-8', newline='').read().replace('\r\n', '\n')
    os.makedirs(os.path.dirname(os.path.join(TMP, F)), exist_ok=True)
    caught = 0
    for name, find, repl in MUTANTS:
        if src.count(find) != 1:
            print(f'BAD MUTANT (pattern found {src.count(find)} times): {name}')
            sys.exit(2)
        open(os.path.join(TMP, F), 'w', encoding='utf-8', newline='').write(src.replace(find, repl))
        r = subprocess.run(['sh', os.path.join(ROOT, 'misc/fschg_check/run.sh')], capture_output=True, text=True,
                           env=dict(os.environ, FSCHG_SRC=TMP))
        out = [l for l in (r.stdout + r.stderr).splitlines() if l.startswith('FAIL') or 'error' in l]
        ok = r.returncode != 0
        caught += ok
        print(f"{'caught' if ok else 'MISSED'}: {name}: {(out[0] if out else '')[:100]}", flush=True)
    print(f'{caught} of {len(MUTANTS)} mutants fail run.sh')
    sys.exit(0 if caught == len(MUTANTS) else 1)

main()
