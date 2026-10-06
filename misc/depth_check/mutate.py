#!/usr/bin/env python3
# P18 (WSL): plants one bug at a time in a copy of ps3_depth.h; run.sh must FAIL on each.
import os, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
SRC = 'ps3/ps3_depth.h'
TMP = '/tmp/depthmut/ps3_depth.h'

MUTANTS = [
    ('FLOAT less and greater swapped', '{ 0, 1, 2, 3, 4, 5, 6, 7 }', '{ 0, 4, 2, 3, 1, 5, 6, 7 }'),
    ('FLOAT gequal as greater', '{ 0, 1, 2, 3, 4, 5, 6, 7 }', '{ 0, 1, 2, 3, 4, 5, 4, 7 }'),
    ('FLOAT equal as lequal', '{ 0, 1, 2, 3, 4, 5, 6, 7 }', '{ 0, 1, 3, 3, 4, 5, 6, 7 }'),
    ('FIXED lequal and gequal kept', '{ 0, 4, 2, 6, 1, 5, 3, 7 }', '{ 0, 4, 2, 3, 1, 5, 6, 7 }'),
    ('FIXED equal and notequal swapped', '{ 0, 4, 2, 6, 1, 5, 3, 7 }', '{ 0, 4, 5, 6, 1, 2, 3, 7 }'),
    ('tables swapped', 'k_depth_cmp[fz ? 1 : 0]', 'k_depth_cmp[fz ? 0 : 1]'),
    ('mode mask 3 bits -> 2', '[mode & 7]', '[mode & 3]'),
    ('FLOAT m10 = 1', '*m10 = fz ? 0.0f : 1.0f;', '*m10 = fz ? 1.0f : 1.0f;'),
    ('FLOAT m10 = -1', '*m10 = fz ? 0.0f : 1.0f;', '*m10 = fz ? -1.0f : 1.0f;'),
    ('FLOAT m14 = 2n', '*m14 = fz ? n :', '*m14 = fz ? 2.0f * n :'),
    ('FLOAT m14 = -n', '*m14 = fz ? n :', '*m14 = fz ? -n :'),
    ('FLOAT z = n/(2W)', '*m14 = fz ? n :', '*m14 = fz ? 0.5f * n :'),
    ('FIXED m14 = -n', ': -2.0f * n;', ': -n;'),
    ('FIXED m10 = 0', '*m10 = fz ? 0.0f : 1.0f;', '*m10 = fz ? 0.0f : 0.0f;'),
    ('FLOAT through the GL remap', '*range_near = fz ? -1.0f : 0.0f;', '*range_near = fz ? 0.0f : 0.0f;'),
    ('FIXED depth range -1', '*range_near = fz ? -1.0f : 0.0f;', '*range_near = fz ? -1.0f : -1.0f;'),
    ('FLOAT clear 1', 'return fz ? 0.0f : 1.0f;', 'return fz ? 1.0f : 1.0f;'),
    ('FIXED clear 0', 'return fz ? 0.0f : 1.0f;', 'return fz ? 0.0f : 0.0f;'),
    ('FLOAT clear denormal', 'return fz ? 0.0f : 1.0f;', 'return fz ? 1e-38f : 1.0f;'),
]

def main():
    samples = sys.argv[1] if len(sys.argv) > 1 else '40000'
    src = open(os.path.join(ROOT, SRC), encoding='utf-8').read().replace('\r\n', '\n')
    caught = 0
    os.makedirs(os.path.dirname(TMP), exist_ok=True)
    for name, find, repl in MUTANTS:
        if src.count(find) != 1:
            print(f'BAD MUTANT (pattern found {src.count(find)} times): {name}')
            sys.exit(2)
        open(TMP, 'w', encoding='utf-8').write(src.replace(find, repl))
        r = subprocess.run(['sh', os.path.join(ROOT, 'misc/depth_check/run.sh'), samples], capture_output=True,
                           text=True, env=dict(os.environ, DEPTH_SOURCE=TMP))
        out = [l for l in (r.stdout + r.stderr).splitlines() if l.startswith('FAIL:') or 'error' in l]
        ok = r.returncode != 0
        caught += ok
        print(f"{'caught' if ok else 'MISSED'}: {name}: {(out[0] if out else '')[:110]}")
    print(f'{caught} of {len(MUTANTS)} mutants fail run.sh')
    sys.exit(0 if caught == len(MUTANTS) else 1)

main()
