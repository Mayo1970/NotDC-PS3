#!/usr/bin/env python3
# Plan 5.44 + P74 + P87 + P98 (WSL): plants one bug at a time in a copy of ps3_rend.cpp; run.sh must FAIL on each.
# Args: flushes per run (default 600000), a name part to run only some mutants.
import os, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
TMP = '/tmp/ramsqmut'
F = 'ps3/ps3_rend.cpp'

MMU_ADDR = '\tconst u32 addr = mmu_TranslateSQW(dest & 0xFFFFFFE0);\n\tif (!sqw_mmu_ta(addr))'
# do_sqw_flat and do_sqw_slot_t share lines; these tails tell them apart.
FLAT_END = '\n\t}\n\tsqw_ta_flat(dest, peak);'
FLAT_RAM = '\n\t\tCCALL(CC_SQW);\n\t\treturn sqw_ram_copy(dest, dest);' + FLAT_END
FLAT_NEXT = "\n\n// The JIT's PREF target without the MMU"
SLOT_RAM = '\n\t}\n\t// g_ta_slot is the spare'
SLOT_HEAD = 'void do_sqw_slot_t(u32 dest, u32 slot, u32 pcw, u32 mask)\n{\n'
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
    ('no census count on the direct path', '\t\tCCALL(CC_SQW);\n\t\treturn sqw_ram_copy(dest, dest);' + FLAT_END,
     '\t\treturn sqw_ram_copy(dest, dest);' + FLAT_END),
    ('direct path also runs the chain', '\t\tCCALL(CC_SQW);\n\t\treturn sqw_ram_copy(dest, dest);' + FLAT_END,
     '\t\tsqw_ram_copy(dest, dest);\n\t\treturn sqw_chain(dest);' + FLAT_END),
    ('sampled RAM flush not copied', '\t\tg_ta_split.t_store = ta_stamp();\n\t\tsqw_ram_copy(dest, dest);\n',
     '\t\tg_ta_split.t_store = ta_stamp();\n'),
    ('sampled RAM flush takes the chain', '\telse if (sqw_ram_target(dest))\n\t{\n\t\tg_ta_split.t_store = ta_stamp();',
     '\telse if (false)\n\t{\n\t\tg_ta_split.t_store = ta_stamp();'),
    ('sampled RAM flush not marked', 'sqw_ram_copy(dest, dest);\n\t\tg_ta_split.t_copy = ta_stamp();\n\t\tg_ta_split.ram = 1;\n',
     'sqw_ram_copy(dest, dest);\n\t\tg_ta_split.t_copy = ta_stamp();\n'),
    ('a TA flush copied into RAM', '\t\tif (!sqw_ram_target(dest))\n\t\t\treturn sqw_chain(dest);' + FLAT_RAM,
     '\t\tif (!sqw_ram_target(dest) && CCN_QACR0.Area != 4)\n\t\t\treturn sqw_chain(dest);' + FLAT_RAM),
    ('flat TA: TaCalls not counted', '\ts_fsm_state = (u8)e;\n\tTaCalls++;\n}' + FLAT_NEXT, '\ts_fsm_state = (u8)e;\n}' + FLAT_NEXT),
    ('flat TA: no slot move', '\tmemcpy(dst, data, 32);\n\tc->raw_used += 32;\n\tta_slot_sync();\n',
     '\tmemcpy(dst, data, 32);\n\tc->raw_used += 32;\n'),
    # P87 JIT TA SLOT: do_sqw_slot.
    ('slot: RAM flush not copied', '\t\tCCALL(CC_SQW);\n\t\treturn sqw_ram_copy(dest, dest);' + SLOT_RAM,
     '\t\tCCALL(CC_SQW);\n\t\treturn;' + SLOT_RAM),
    ('slot: no sample', SLOT_HEAD + '\tif (++g_ta_split.calls == g_ta_split.next)\n\t\treturn sqw_sampled(dest);\n',
     SLOT_HEAD),
    ('slot: no slot test', 'if (slot != g_ta_slot || slot != (u32)(uintptr_t)(c->raw + c->raw_used) || (dest & 0x1F))',
     'if (dest & 0x1F)'),
    ('slot: an unaligned PREF from the slot', 'slot != (u32)(uintptr_t)(c->raw + c->raw_used) || (dest & 0x1F))',
     'slot != (u32)(uintptr_t)(c->raw + c->raw_used))'),
    ('slot: the spare taken as the record', 'slot != g_ta_slot || slot != (u32)(uintptr_t)(c->raw + c->raw_used) ||',
     'slot != g_ta_slot ||'),
    ('slot: the queue words not copied', '\t\t\t\tw[i] = q[i];', '\t\t\t\t(void)q;'),
    ('slot: the mask read inverted', '\t\t\tif (!(mask & (1u << i)))', '\t\t\tif (mask & (1u << i))'),
    ('slot: word 0 from r12 when the record did not store it', '\t\tif (!(mask & 1))\n\t\t\tpcw = q[0];\n', ''),
    ('slot: raw_used not moved', '\t\t\t\tw[i] = q[i];\n\t}\n\tc->raw_used += 32;\n', '\t\t\t\tw[i] = q[i];\n\t}\n'),
    ('slot: no slot move', '\t\t\t\tw[i] = q[i];\n\t}\n\tc->raw_used += 32;\n\tta_slot_sync();\n',
     '\t\t\t\tw[i] = q[i];\n\t}\n\tc->raw_used += 32;\n'),
    ('slot: the frame variant notes the peak', 'do_sqw_slot_t<false>(dest, slot, pcw, mask);',
     'do_sqw_slot_t<true>(dest, slot, pcw, mask);'),
    ('slot: TaCalls not counted', '\ts_fsm_state = (u8)e;\n\tTaCalls++;\n}\n\nvoid FASTCALL do_sqw_slot(',
     '\ts_fsm_state = (u8)e;\n}\n\nvoid FASTCALL do_sqw_slot('),
    ('slot: the spare is a buffer above 2 GB too', '(uintptr_t)c->raw < 0x7FE00000u', '(uintptr_t)c->raw < 0x100u'),
    ('slot: no room test', ' && c->raw_used + 32 <= TA_RAW_SIZE;\n\tg_ta_slot', ';\n\tg_ta_slot'),
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
    # P98 TA Line Zero: ta_line_zero and its calls.
    ('zero: row ignored', 'if (g_ta_line_zero_preset && !(c->raw_used & 127)', 'if (!(c->raw_used & 127)'),
    ('zero: not only at a line start', 'if (g_ta_line_zero_preset && !(c->raw_used & 127) && ',
     'if (g_ta_line_zero_preset && '),
    ('zero: the end test a line short', 'c->raw_used + 256 <= TA_RAW_SIZE)', 'c->raw_used + 128 <= TA_RAW_SIZE)'),
    ('zero: the line of the record', 'TA_DCBZ(c->raw + c->raw_used + 128);', 'TA_DCBZ(c->raw + c->raw_used);'),
    ('zero: the line before', 'TA_DCBZ(c->raw + c->raw_used + 128);', 'TA_DCBZ(c->raw + c->raw_used - 128);'),
    ('zero: two lines ahead', 'TA_DCBZ(c->raw + c->raw_used + 128);', 'TA_DCBZ(c->raw + c->raw_used + 256);'),
    ('zero: no dcbz', '\t\tTA_DCBZ(c->raw + c->raw_used + 128);\n', ''),
    ('zero: not counted', '\t\ts_stat_ta_zero++;\n', ''),
    ('zero: flat body without it', '\tta_line_zero(c);\n\tmemcpy(dst, data, 32);', '\tmemcpy(dst, data, 32);'),
    ('zero: slot path without it', '\tta_line_zero(c);\n\tif (mask != 0xFF)', '\tif (mask != 0xFF)'),
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
