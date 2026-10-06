#!/usr/bin/env python3
# P16 (WSL): plants one bug at a time in a copy of ps3_tex_slot.h; run.sh must FAIL on each.
import os, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
SRC = 'ps3/ps3_tex_slot.h'
TMP = '/tmp/texslotmut/ps3_tex_slot.h'

MUTANTS = [
    ('low bits of the product', '* 0x85EBCA6Bu) >> (32 - TEX_SLOT_BITS)', '* 0x85EBCA6Bu) % (1u << TEX_SLOT_BITS)'),
    ('multiply dropped', ' * 0x85EBCA6Bu)', ')'),
    ('key1 dropped', '(key0 ^ (key1 * 0x9E3779B1u))', 'key0'),
    ('TexAddr bits only', '(key0 ^ (key1', '((key0 & 0x1FFFFFu) ^ (key1'),
    ('slot out of range', '>> (32 - TEX_SLOT_BITS)', '>> (31 - TEX_SLOT_BITS)'),
    ('HASH runs OLD', 'if (!hash)', 'if (true)'),
    ('modes swapped', 'if (!hash)', 'if (hash)'),
    ('OLD changed', '2654435761u', '2654435769u'),
]

def main():
    sets = sys.argv[1] if len(sys.argv) > 1 else '50'
    src = open(os.path.join(ROOT, SRC), encoding='utf-8').read()
    caught = 0
    os.makedirs(os.path.dirname(TMP), exist_ok=True)
    for name, find, repl in MUTANTS:
        if src.count(find) != 1:
            print(f'BAD MUTANT (pattern found {src.count(find)} times): {name}')
            sys.exit(2)
        open(TMP, 'w', encoding='utf-8').write(src.replace(find, repl))
        r = subprocess.run(['sh', os.path.join(ROOT, 'misc/texslot_check/run.sh'), sets], capture_output=True,
                           text=True, env=dict(os.environ, SLOT_SOURCE=TMP))
        out = [l for l in (r.stdout + r.stderr).splitlines() if l.startswith('FAIL:') or 'error' in l]
        ok = r.returncode != 0
        caught += ok
        print(f"{'caught' if ok else 'MISSED'}: {name}: {(out[0] if out else '')[:100]}")
    print(f'{caught} of {len(MUTANTS)} mutants fail run.sh')
    sys.exit(0 if caught == len(MUTANTS) else 1)

main()
