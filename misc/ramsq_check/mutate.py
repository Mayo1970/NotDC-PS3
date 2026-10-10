#!/usr/bin/env python3
# Plan 5.44 (WSL): plants one bug at a time in a copy of ps3_rend.cpp; run.sh must FAIL on each.
# Arg: flushes per run (default 600000).
import os, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
TMP = '/tmp/ramsqmut'
F = 'ps3/ps3_rend.cpp'

MUTANTS = [
    ('direct for every area', 'return area == 3 && g_ram_sq_direct_preset && !g_sh4_cached_live;',
     'return g_ram_sq_direct_preset && !g_sh4_cached_live;'),
    ('direct for area 4', 'return area == 3 && g_ram_sq_direct_preset', 'return area >= 3 && g_ram_sq_direct_preset'),
    ('direct with the toggle off', 'return area == 3 && g_ram_sq_direct_preset && !g_sh4_cached_live;',
     'return area == 3 && !g_sh4_cached_live;'),
    ('direct under the cached interpreter', 'return area == 3 && g_ram_sq_direct_preset && !g_sh4_cached_live;',
     'return area == 3 && g_ram_sq_direct_preset;'),
    ('QACR picked by the wrong half',
     'static inline bool sqw_ram_target(u32 dest)\n{\n\tconst u32 area = (dest & 0x20) ? CCN_QACR1.Area : CCN_QACR0.Area;',
     'static inline bool sqw_ram_target(u32 dest)\n{\n\tconst u32 area = (dest & 0x20) ? CCN_QACR0.Area : CCN_QACR1.Area;'),
    ('QACR0 for both queues',
     'static inline bool sqw_ram_target(u32 dest)\n{\n\tconst u32 area = (dest & 0x20) ? CCN_QACR1.Area : CCN_QACR0.Area;',
     'static inline bool sqw_ram_target(u32 dest)\n{\n\tconst u32 area = CCN_QACR0.Area;'),
    ('always the first queue', 'memcpy(&mem_b[dest & (RAM_MASK & ~31u)], &sq_both[dest & 0x20], 32);',
     'memcpy(&mem_b[dest & (RAM_MASK & ~31u)], &sq_both[0], 32);'),
    ('offset keeps the low bits', 'memcpy(&mem_b[dest & (RAM_MASK & ~31u)]', 'memcpy(&mem_b[dest & (RAM_MASK & ~3u)]'),
    ('offset loses bit 5', 'memcpy(&mem_b[dest & (RAM_MASK & ~31u)]', 'memcpy(&mem_b[dest & (RAM_MASK & ~63u)]'),
    ('offset loses the top RAM bit', 'memcpy(&mem_b[dest & (RAM_MASK & ~31u)]',
     'memcpy(&mem_b[dest & (RAM_MASK & 0x7FFFE0u)]'),
    ('28 bytes copied', '&sq_both[dest & 0x20], 32);\n}', '&sq_both[dest & 0x20], 28);\n}'),
    ('flush not counted in sq_mem', '\tg_vram_split.sq_mem++;\n\tmemcpy(&mem_b', '\tmemcpy(&mem_b'),
    ('no census count on the direct path', '\t\tCCALL(CC_SQW);\n\t\treturn sqw_ram_copy(dest);',
     '\t\treturn sqw_ram_copy(dest);'),
    ('direct path also runs the chain', '\t\tCCALL(CC_SQW);\n\t\treturn sqw_ram_copy(dest);',
     '\t\tsqw_ram_copy(dest);\n\t\treturn sqw_chain(dest);'),
    ('sampled RAM flush not copied', '\t\tg_ta_split.t_store = ta_stamp();\n\t\tsqw_ram_copy(dest);\n',
     '\t\tg_ta_split.t_store = ta_stamp();\n'),
    ('sampled RAM flush takes the chain', '\telse if (sqw_ram_target(dest))\n\t{\n\t\tg_ta_split.t_store = ta_stamp();',
     '\telse if (false)\n\t{\n\t\tg_ta_split.t_store = ta_stamp();'),
    ('sampled RAM flush not marked', '\t\tg_ta_split.t_copy = ta_stamp();\n\t\tg_ta_split.ram = 1;\n\t}\n\telse\n\t{\n',
     '\t\tg_ta_split.t_copy = ta_stamp();\n\t}\n\telse\n\t{\n'),
    ('a TA flush copied into RAM', '\t\tif (!sqw_ram_target(dest))\n\t\t\treturn sqw_chain(dest);',
     '\t\tif (!sqw_ram_target(dest) && CCN_QACR0.Area != 4)\n\t\t\treturn sqw_chain(dest);'),
]

def main():
    n = sys.argv[1] if len(sys.argv) > 1 else '600000'
    src = open(os.path.join(ROOT, F), encoding='utf-8', newline='').read().replace('\r\n', '\n')
    os.makedirs(os.path.join(TMP, 'ps3'), exist_ok=True)
    caught = 0
    for name, find, repl in MUTANTS:
        if src.count(find) != 1:
            print(f'BAD MUTANT (pattern found {src.count(find)} times): {name}')
            sys.exit(2)
        open(os.path.join(TMP, F), 'w', encoding='utf-8', newline='').write(src.replace(find, repl))
        r = subprocess.run(['sh', os.path.join(ROOT, 'misc/ramsq_check/run.sh'), n], capture_output=True, text=True,
                           env=dict(os.environ, RAMSQ_SRC=TMP))
        out = [l for l in (r.stdout + r.stderr).splitlines() if l.startswith(('FAIL', 'ERROR', '==')) or 'error' in l]
        ok = r.returncode != 0
        caught += ok
        print(f"{'caught' if ok else 'MISSED'}: {name}: {(out[0] if out else '')[:110]}", flush=True)
    print(f'{caught} of {len(MUTANTS)} mutants fail run.sh')
    sys.exit(0 if caught == len(MUTANTS) else 1)

main()
