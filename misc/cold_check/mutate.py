#!/usr/bin/env python3
# Mutants of dc/sh4/rec_v2/driver.cpp's cold allocation: each must make misc/cold_check fail. Run from WSL:
# python3 misc/cold_check/mutate.py [rounds]. A text not in the file exactly once is reported NOT FOUND.
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, 'dc', 'sh4', 'rec_v2', 'driver.cpp')
WORK = '/tmp/coldmut'

# Left out as equal in effect: the 16 KB gap, the room test and the size's rounding (every bound is a multiple of
# 128; ngen_Compile's 64 KB start check keeps the room), and emit_ColdEnd's verify (a fragment's own comes first).
MUTANTS = [
    ('free space ignores the chunks', '\treturn s_cold.lo - LastAddr;', '\treturn s_code_size - LastAddr;'),
    ('hot code bounded by the array', 'verify(LastAddr + 4 <= s_cold.lo);', 'verify(LastAddr + 4 <= s_code_size);'),
    ('the cursor does not move', '\ts_compiled_bytes += at - s_cold.at;\n\ts_cold.at = at;', '\ts_compiled_bytes += at - s_cold.at;'),
    ('the room test inverted', '\tif (s_cold.end - s_cold.at < bound)', '\tif (s_cold.end - s_cold.at > bound)'),
    ('a new chunk overlaps the last', '\t\ts_cold.lo -= size;', '\t\ts_cold.lo -= size / 2;'),
    ('a reset keeps the chunk cursor', '\ts_cold.lo = s_cold.at = s_cold.end = s_code_size;', '\ts_cold.lo = s_code_size;'),
]


def main():
    rounds = sys.argv[1] if len(sys.argv) > 1 else '12'
    src = open(SRC, encoding='utf-8').read().replace('\r\n', '\n')
    os.makedirs(WORK, exist_ok=True)
    caught = missed = notfound = 0
    for name, find, repl in MUTANTS:
        if src.count(find) != 1:
            print('NOT FOUND (%d): %s' % (src.count(find), name))
            notfound += 1
            continue
        path = os.path.join(WORK, 'driver.cpp')
        with open(path, 'w', encoding='utf-8') as f:
            f.write(src.replace(find, repl))
        env = dict(os.environ, COLD_SRC=path)
        try:
            r = subprocess.run(['sh', 'misc/cold_check/run.sh', rounds], cwd=ROOT, env=env,
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
