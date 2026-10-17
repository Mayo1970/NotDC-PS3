#!/bin/sh
# Plan 5.44 + P74 + P87 + P98 (WSL): do_sqw_nommu, do_sqw_mmu and do_sqw_slot against the old flush chains, cut from
# the live sources. Args: flushes (default 4000000), seed. RAMSQ_SRC: a folder with a mutated ps3/ps3_rend.cpp.
set -e
cd "$(dirname "$0")/../.."
S="${RAMSQ_SRC:-.}" python3 - << 'PY'
import os
def cut(path, a, b, incl_b=False):
    s = open(path, encoding='utf-8', newline='').read().replace('\r\n', '\n')
    i = s.index(a)
    j = s.index(b, i)
    return s[i:j + (len(b) if incl_b else 0)]
src = os.environ['S']
os.makedirs('/tmp/ramsq_ext', exist_ok=True)
parts = {
    'rend.inc': cut(src + '/ps3/ps3_rend.cpp', "// The flat path's flushes: TA SQ Direct", 'void ps3ta_store_dma('),
    'slot.inc': cut(src + '/ps3/ps3_rend.cpp', '// P87 JIT TA SLOT: where', '\n}\n', True),
    'zero.inc': cut(src + '/ps3/ps3_rend.cpp', '// P98 TA Line Zero: a record', '\n}\n', True),
    'sqw.inc': cut('dc/sh4/sh4_sqw.h', '// The flush itself', "// The JIT's PREF targets."),
    'mmu.inc': cut('dc/mem/mmu.h', '#define mmu_TranslateSQW(', '\n', True),
    'wmb.inc': cut('dc/mem/sh4_mem.cpp', 'void MEMCALL WriteMemBlock_nommu_ptr(', 'void MEMCALL WriteMemBlock_ptr('),
    'vmem.inc': cut('dc/mem/_vmem.cpp', 'static u32 FindMask(u32 msk)', '// Core read dispatcher') +
                cut('dc/mem/_vmem.cpp', 'u8* fastcall _vmem_GetBlockPtr(', '    return &((u8*)ptr)[base];\n}\n', True) +
                cut('dc/mem/_vmem.cpp', 'void _vmem_map_block(void* base', 'void _vmem_mirror_mapping('),
    'map.inc': cut('dc/mem/_vmem.h', '#define _vmem_map_block_mirror(', '// ---- Public read/write accessors'),
}
for name, text in parts.items():
    open('/tmp/ramsq_ext/' + name, 'w', encoding='utf-8', newline='').write(text)
PY
# -no-pie: the slot is a 32-bit address, so the TA buffer must lie below 2 GB, as on the PS3.
g++ -std=c++14 -O1 -g -fno-pie -no-pie -fsanitize=address,undefined -fno-sanitize-recover=undefined -Wall \
	-Wno-unused-function -I/tmp/ramsq_ext -o /tmp/ramsq_check misc/ramsq_check/ramsq_check.cpp
/tmp/ramsq_check "${1:-4000000}" "${2:-1}"
