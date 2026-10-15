#!/usr/bin/env python3
# Bundle 21 (WSL): plants one bug at a time in a copy of ps3/ps3_aica_defer.h; run.sh must FAIL on each.
import os, shutil, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
TMP = '/tmp/defermut'
H = 'ps3/ps3_aica_defer.h'
MUTANTS = [
    ('a store goes out before its HOLD SYNC', 'if ((s32)(s.k - held) > 0)\n\t\t\tbreak;', 'if (0)\n\t\t\tbreak;'),
    ('the stores of the reached SYNC stay', 'if ((s32)(s.k - held) > 0)', 'if ((s32)(s.k - held) >= 0)'),
    ('covers misses the last byte', 'addr <= q.hi && addr + size - 1 >= q.lo', 'addr < q.hi && addr + size - 1 >= q.lo'),
    ('covers misses the first byte', 'addr <= q.hi && addr + size - 1 >= q.lo', 'addr <= q.hi && addr + size - 1 > q.lo'),
    ('the range grows only at the first store', '\tif (end > q.hi)\n\t\tq.hi = end;', ''),
    ('the range is kept once all went out', '\tif (q.tail == q.head)\n\t{\n\t\tq.lo = 1;\n\t\tq.hi = 0;\n\t}\n\treturn n;', '\treturn n;'),
    ('a store keeps the SYNC before', 's.k = q.k_pushed;', 's.k = q.k_pushed - 1;'),
    ('full one store late', 'return q.head - q.tail >= DEFER_MAX;', 'return q.head - q.tail > DEFER_MAX;'),
]

def main():
    src = open(os.path.join(ROOT, H), encoding='utf-8', newline='').read().replace('\r\n', '\n')
    caught = 0
    for name, find, repl in MUTANTS:
        if src.count(find) != 1:
            print(f'BAD MUTANT (pattern found {src.count(find)} times): {name}')
            sys.exit(2)
        shutil.rmtree(TMP, ignore_errors=True)
        os.makedirs(os.path.join(TMP, 'ps3'))
        open(os.path.join(TMP, H), 'w', encoding='utf-8', newline='').write(src.replace(find, repl))
        r = subprocess.run(['sh', os.path.join(ROOT, 'misc/defer_check/run.sh')], capture_output=True, text=True,
                           env=dict(os.environ, DEFER_SRC=TMP, SEEDS='40'))
        ok = r.returncode != 0
        caught += ok
        out = [l for l in (r.stdout + r.stderr).splitlines() if l.startswith('FAIL')]
        print(f"{'caught' if ok else 'MISSED'}: {name}: {(out[0] if out else '')[:100]}", flush=True)
    print(f'{caught} of {len(MUTANTS)} mutants fail run.sh')
    sys.exit(0 if caught == len(MUTANTS) else 1)

main()
