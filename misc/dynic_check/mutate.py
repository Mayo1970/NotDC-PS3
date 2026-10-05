#!/usr/bin/env python3
# Plan 5.3 (WSL): plants one bug at a time in a copy of ps3_jit_dynic.h; run.sh must FAIL on each.
import os, re, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
TMP = '/tmp/dynicmut'
HDR = 'ps3/ps3_jit_dynic.h'

MUTANTS = [
    ('slot 0 is a plain b', 'return 0x48000001u |', 'return 0x48000000u |'),
    ('slot 0 lands past mflr', '(const u8*)stub - (const u8*)site)', '(const u8*)stub - (const u8*)site + 4)'),
    ('slot 1 misses site+16', 'ppc_bx(3, 0, 0);', 'ppc_bx(4, 0, 0);'),
    ('slot 3 misses site+16', 'ppc_bx(1, 0, 0);', 'ppc_bx(2, 0, 0);'),
    ('every site live', 'return *site == dynic_call_word(site, stub);', 'return true;'),
    ('guard xori', 'ppc_xoris(t, djump, pc >> 16);', 'ppc_xori(t, djump, pc >> 16);'),
    ('guard high half', 'ppc_xoris(t, djump, pc >> 16);', 'ppc_xoris(t, djump, pc >> 15);'),
    ('guard writes djump', 'ppc_xoris(t, djump, pc >> 16);', 'ppc_xoris(djump, djump, pc >> 16);'),
    ('guard 64-bit compare', 'ppc_cmpli(ppc_cr0, t, pc & 0xFFFF, 0);', 'ppc_cmpli(ppc_cr0, t, pc & 0xFFFF, 1);'),
    ('guard compares into cr1', 'ppc_cmpli(ppc_cr0, t,', 'ppc_cmpli(ppc_cr1, t,'),
    ('guard low half', 'ppc_cmpli(ppc_cr0, t, pc & 0xFFFF, 0);', 'ppc_cmpli(ppc_cr0, t, pc & 0x7FFF, 0);'),
    ('guard beq', 'ppc_bcx(BO_FALSE, BI_CR0_EQ, 2, 0, 0);', 'ppc_bcx(BO_TRUE, BI_CR0_EQ, 2, 0, 0);'),
    ('guard bne +12', 'ppc_bcx(BO_FALSE, BI_CR0_EQ, 2, 0, 0);', 'ppc_bcx(BO_FALSE, BI_CR0_EQ, 3, 0, 0);'),
    ('guard bne +4', 'ppc_bcx(BO_FALSE, BI_CR0_EQ, 2, 0, 0);', 'ppc_bcx(BO_FALSE, BI_CR0_EQ, 1, 0, 0);'),
    ('guard target off by 4', '(const u8*)target - (const u8*)emit_GetCCPtr())',
     '(const u8*)target - (const u8*)emit_GetCCPtr() + 4)'),
    ('guard b links', 'ppc_emit(0x48000000u | ((u32)((const u8*)target', 'ppc_emit(0x48000001u | ((u32)((const u8*)target'),
    ('count high half not adjusted', 'ppc_addis(t1, 0, (a - (u32)lo) >> 16);', 'ppc_addis(t1, 0, a >> 16);'),
    ('count adds 2', 'ppc_addi(t2, t2, 1);', 'ppc_addi(t2, t2, 2);'),
    ('count stores the address', 'ppc_stw(t2, t1, (u32)lo);', 'ppc_stw(t1, t1, (u32)lo);'),
    ('count reads the next word', 'ppc_lwz(t2, t1, (u32)lo);', 'ppc_lwz(t2, t1, (u32)lo + 4);'),
    ('stub site off by 4', 'ppc_addi(a1, a1, (u32)-4);', 'ppc_addi(a1, a1, (u32)-8);'),
    ('stub reads CTR', 'ppc_mfspr(a1, ppc_spr_lr);', 'ppc_mfspr(a1, ppc_spr_ctr);'),
    ('stub keeps the high word', '\tppc_emit(0x78000020u | (a0 << 21) | (a0 << 16));\t// clrldi a0,a0,32\n', ''),
    ('stub clrldi mask 0', '0x78000020u | (a0 << 21)', '0x78000000u | (a0 << 21)'),
    ('stub clrldi into a1', '(a0 << 21) | (a0 << 16)', '(a0 << 21) | (a1 << 16)'),
]

# Slots 1-3 never run (slot 0's call does not return), so only the ppu-as words catch these.
WORDS_ONLY = {'slot 1 misses site+16', 'slot 3 misses site+16'}

def run():
    env = dict(os.environ, DYNIC_INC=os.path.join(TMP, 'inc'))
    r = subprocess.run(['sh', os.path.join(ROOT, 'misc/dynic_check/run.sh')], capture_output=True, text=True,
                       env=env)
    out = (r.stdout + r.stderr).splitlines()
    summary = next((l for l in out if re.match(r'^(PASS|FAIL): \d+ checks', l)), '')
    m = re.search(r'\(words (\d+), model (\d+)\)', summary)
    words_only = bool(m) and int(m.group(2)) == 0
    first = next((l for l in out if l.startswith(('FAIL', 'error'))), summary or (out[-1] if out else ''))
    return r.returncode != 0, words_only, (first + ' | ' + summary) if summary and first != summary else first

def main():
    caught = model_caught = 0
    src = open(os.path.join(ROOT, HDR), encoding='utf-8').read()
    for name, find, repl in MUTANTS:
        if src.count(find) != 1:
            print(f'BAD MUTANT (pattern found {src.count(find)} times): {name}')
            sys.exit(2)
        hdr = os.path.join(TMP, 'inc', HDR)
        os.makedirs(os.path.dirname(hdr), exist_ok=True)
        open(hdr, 'w', encoding='utf-8').write(src.replace(find, repl))
        ok, words_only, line = run()
        if '| ' not in line and not line.startswith(('PASS', 'FAIL:')):
            print(f'BAD MUTANT (no test result, likely a compile error): {name}: {line}')
            sys.exit(2)
        on_model = ok and not words_only
        caught += ok
        model_caught += on_model
        print(f"{'caught' if ok else 'MISSED'}: {name}: {line}" + ('' if on_model or not ok else ' (words only)'))
        if ok and not on_model and name not in WORDS_ONLY:
            print(f'    expected the model to catch it too: {name}')
            caught -= 1
    print(f'{caught} of {len(MUTANTS)} mutants fail run.sh; {model_caught} also on the model')
    sys.exit(0 if caught == len(MUTANTS) else 1)

main()
