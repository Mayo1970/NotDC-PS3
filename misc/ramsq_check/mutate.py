#!/usr/bin/env python3
# Plan 5.44 + P74 (WSL): plants one bug at a time in a copy of ps3_rend.cpp; run.sh must FAIL on each.
# Args: flushes per run (default 600000), a name part to run only some mutants.
import os, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
TMP = '/tmp/ramsqmut'
F = 'ps3/ps3_rend.cpp'

MMU_ADDR = '\tconst u32 addr = mmu_TranslateSQW(dest & 0xFFFFFFE0);\n\tif (!sqw_mmu_ta(addr))'
MMU_TA = 'return g_ta_sq_direct_preset && s_flat_ok && ((addr >> 26) & 7) == 4'
MMU_RAM = '((addr >> 26) & 7) == 3 && g_ram_sq_direct_preset && !g_sh4_cached_live;'

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
    ('always the first queue', 'memcpy(&mem_b[addr & (RAM_MASK & ~31u)], &sq_both[dest & 0x20], 32);',
     'memcpy(&mem_b[addr & (RAM_MASK & ~31u)], &sq_both[0], 32);'),
    ('offset keeps the low bits', 'memcpy(&mem_b[addr & (RAM_MASK & ~31u)]', 'memcpy(&mem_b[addr & (RAM_MASK & ~3u)]'),
    ('offset loses bit 5', 'memcpy(&mem_b[addr & (RAM_MASK & ~31u)]', 'memcpy(&mem_b[addr & (RAM_MASK & ~63u)]'),
    ('offset loses the top RAM bit', 'memcpy(&mem_b[addr & (RAM_MASK & ~31u)]',
     'memcpy(&mem_b[addr & (RAM_MASK & 0x7FFFE0u)]'),
    ('28 bytes copied', '&sq_both[dest & 0x20], 32);\n}', '&sq_both[dest & 0x20], 28);\n}'),
    ('flush not counted in sq_mem', '\tg_vram_split.sq_mem++;\n\tmemcpy(&mem_b', '\tmemcpy(&mem_b'),
    ('no census count on the direct path', '\t\tCCALL(CC_SQW);\n\t\treturn sqw_ram_copy(dest, dest);',
     '\t\treturn sqw_ram_copy(dest, dest);'),
    ('direct path also runs the chain', '\t\tCCALL(CC_SQW);\n\t\treturn sqw_ram_copy(dest, dest);',
     '\t\tsqw_ram_copy(dest, dest);\n\t\treturn sqw_chain(dest);'),
    ('sampled RAM flush not copied', '\t\tg_ta_split.t_store = ta_stamp();\n\t\tsqw_ram_copy(dest, dest);\n',
     '\t\tg_ta_split.t_store = ta_stamp();\n'),
    ('sampled RAM flush takes the chain', '\telse if (sqw_ram_target(dest))\n\t{\n\t\tg_ta_split.t_store = ta_stamp();',
     '\telse if (false)\n\t{\n\t\tg_ta_split.t_store = ta_stamp();'),
    ('sampled RAM flush not marked', 'sqw_ram_copy(dest, dest);\n\t\tg_ta_split.t_copy = ta_stamp();\n\t\tg_ta_split.ram = 1;\n',
     'sqw_ram_copy(dest, dest);\n\t\tg_ta_split.t_copy = ta_stamp();\n'),
    ('a TA flush copied into RAM', '\t\tif (!sqw_ram_target(dest))\n\t\t\treturn sqw_chain(dest);',
     '\t\tif (!sqw_ram_target(dest) && CCN_QACR0.Area != 4)\n\t\t\treturn sqw_chain(dest);'),
    ('flat TA: TaCalls not counted', '\ts_fsm_state = (u8)e;\n\tTaCalls++;\n}', '\ts_fsm_state = (u8)e;\n}'),
    ('flat TA: the other queue', '\tCCALL(CC_SQW);\n\tu32* data = (u32*)&sq_both[dest & 0x20];',
     '\tCCALL(CC_SQW);\n\tu32* data = (u32*)&sq_both[(dest & 0x20) ^ 0x20];'),
    ('flat TA: no census', '\tCCALL(CC_SQW);\n\tu32* data = (u32*)&sq_both[dest & 0x20];',
     '\tu32* data = (u32*)&sq_both[dest & 0x20];'),
    ('MMU: row ignored', '\tif (!g_mmu_sq_direct_preset)\n\t\treturn sqw_mmu_old(dest);\n', ''),
    ('MMU: row inverted', 'if (!g_mmu_sq_direct_preset)', 'if (g_mmu_sq_direct_preset)'),
    ('MMU: row OFF without the probe count', '\t\treturn sqw_mmu_old(dest);', '\t\treturn sqw_mmu_chain(dest);'),
    ('MMU: a sample before the row test',
     '\tif (!g_mmu_sq_direct_preset)\n\t\treturn sqw_mmu_old(dest);\n\tif (++g_ta_split.calls == g_ta_split.next)\n'
     '\t\treturn sqw_mmu_sampled(dest);\n',
     '\tif (++g_ta_split.calls == g_ta_split.next)\n\t\treturn sqw_mmu_sampled(dest);\n'
     '\tif (!g_mmu_sq_direct_preset)\n\t\treturn sqw_mmu_chain(dest);\n'),
    ('MMU: target from QACR', MMU_ADDR,
     '\tconst u32 addr = (dest & 0x03FFFFE0) | (((dest & 0x20) ? CCN_QACR1.Area : CCN_QACR0.Area) << 26);\n'
     '\tif (!sqw_mmu_ta(addr))'),
    ('MMU: target without the page offset', MMU_ADDR,
     '\tconst u32 addr = sq_remap[(dest >> 20) & 0x3F];\n\tif (!sqw_mmu_ta(addr))'),
    ('MMU: target from the next slot', MMU_ADDR,
     '\tconst u32 addr = mmu_TranslateSQW((dest & 0xFFFFFFE0) + 0x100000);\n\tif (!sqw_mmu_ta(addr))'),
    ('MMU: TA test without TA SQ Direct', MMU_TA, 'return s_flat_ok && ((addr >> 26) & 7) == 4'),
    ('MMU: TA test without the gate', MMU_TA, 'return g_ta_sq_direct_preset && ((addr >> 26) & 7) == 4'),
    ('MMU: YUV and VRAM taken flat', '((addr >> 26) & 7) == 4 && (addr & 0x01800000) == 0;',
     '((addr >> 26) & 7) == 4;'),
    ('MMU: TA test for area 0 too', '((addr >> 26) & 7) == 4 && (addr & 0x01800000) == 0;',
     '((addr >> 26) & 3) == 0 && (addr & 0x01800000) == 0;'),
    ('MMU: RAM test for area 4 too', 'return ((addr >> 26) & 7) == 3 && g_ram_sq_direct_preset',
     'return ((addr >> 26) & 7) >= 3 && g_ram_sq_direct_preset'),
    ('MMU: RAM direct with RAM SQ Direct OFF', MMU_RAM, '((addr >> 26) & 7) == 3 && !g_sh4_cached_live;'),
    ('MMU: RAM direct under the cached interpreter', MMU_RAM, '((addr >> 26) & 7) == 3 && g_ram_sq_direct_preset;'),
    ('MMU: RAM copy at the store-queue address', '\t\treturn sqw_ram_copy(addr, dest);',
     '\t\treturn sqw_ram_copy(dest, dest);'),
    ('MMU: no census on the RAM path', '\t\tCCALL(CC_SQW);\n\t\treturn sqw_ram_copy(addr, dest);',
     '\t\treturn sqw_ram_copy(addr, dest);'),
    ('MMU: chain without census', 'void sqw_mmu_chain(u32 dest)\n{\n\tCCALL(CC_SQW);\n', 'void sqw_mmu_chain(u32 dest)\n{\n'),
    ('MMU: peak noted with TA Peak Frame', 'sqw_ta_flat(dest, !s_peak_frame);', 'sqw_ta_flat(dest, true);'),
    ('MMU: peak never noted', 'sqw_ta_flat(dest, !s_peak_frame);', 'sqw_ta_flat(dest, false);'),
    ('MMU: sampled target is the store-queue address',
     '\tCCALL(CC_SQW);\n\tconst u32 addr = mmu_TranslateSQW(dest & 0xFFFFFFE0);\n\tif (sqw_mmu_ta(addr))',
     '\tCCALL(CC_SQW);\n\tconst u32 addr = dest;\n\tif (sqw_mmu_ta(addr))'),
    ('MMU: sampled TA takes the chain', '\tif (sqw_mmu_ta(addr))\n\t{\n\t\tstore_sq<true, true>',
     '\tif (false)\n\t{\n\t\tstore_sq<true, true>'),
    ('MMU: sampled TA without TaCalls',
     '\t\tstore_sq<true, true>((u32*)&sq_both[dest & 0x20]);\n\t\tTaCalls++;\n\t}\n\telse if (sqw_mmu_ram(addr))',
     '\t\tstore_sq<true, true>((u32*)&sq_both[dest & 0x20]);\n\t}\n\telse if (sqw_mmu_ram(addr))'),
    ('MMU: sampled RAM not copied', '\t\tg_ta_split.t_store = ta_stamp();\n\t\tsqw_ram_copy(addr, dest);\n',
     '\t\tg_ta_split.t_store = ta_stamp();\n'),
    ('MMU: sampled chain not armed', '\t\tg_ta_split.armed = 1;\n\t\tdo_sqw_target<true>(dest);',
     '\t\tdo_sqw_target<true>(dest);'),
]

def main():
    n = sys.argv[1] if len(sys.argv) > 1 else '600000'
    part = sys.argv[2] if len(sys.argv) > 2 else ''
    src = open(os.path.join(ROOT, F), encoding='utf-8', newline='').read().replace('\r\n', '\n')
    os.makedirs(os.path.join(TMP, 'ps3'), exist_ok=True)
    caught = runs = 0
    for name, find, repl in MUTANTS:
        if part not in name:
            continue
        if src.count(find) != 1:
            print(f'BAD MUTANT (pattern found {src.count(find)} times): {name}')
            sys.exit(2)
        open(os.path.join(TMP, F), 'w', encoding='utf-8', newline='').write(src.replace(find, repl))
        r = subprocess.run(['sh', os.path.join(ROOT, 'misc/ramsq_check/run.sh'), n], capture_output=True, text=True,
                           env=dict(os.environ, RAMSQ_SRC=TMP))
        out = [l for l in (r.stdout + r.stderr).splitlines() if l.startswith(('FAIL', 'ERROR', '==', 'verify')) or 'error' in l]
        ok = r.returncode != 0
        caught += ok
        runs += 1
        print(f"{'caught' if ok else 'MISSED'}: {name}: {(out[0] if out else '')[:110]}", flush=True)
    print(f'{caught} of {runs} mutants fail run.sh')
    sys.exit(0 if caught == runs else 1)

main()
