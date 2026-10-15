#!/usr/bin/env python3
# Plan section 7, bundle 8 (WSL): cuts the code under test out of the sources into /tmp/jit8_ext for the check
# programs, and checks the lines of the files that do not build on the PC. JIT8_SRC: a tree of mutated files.
import hashlib, os, re, struct, sys

SRC = os.environ.get('JIT8_SRC', '')
OUT = '/tmp/jit8_ext'

def read(path):
    p = os.path.join(SRC, path) if SRC and os.path.exists(os.path.join(SRC, path)) else path
    return open(p, encoding='utf-8', newline='').read().replace('\r\n', '\n')

def cut(s, start, end, what, keep_end=False):
    if s.count(start) != 1:
        sys.exit('FAIL cut: %s: start found %d times' % (what, s.count(start)))
    a = s.index(start)
    b = s.find(end, a + len(start))
    if b < 0:
        sys.exit('FAIL cut: %s: no end' % what)
    return s[a:b + len(end) if keep_end else b]

def put(name, text):
    open(os.path.join(OUT, name), 'w', encoding='utf-8', newline='').write(text)

os.makedirs(OUT, exist_ok=True)

dec = read('dc/sh4/rec_v2/decoder.cpp')
put('pr_free.inc', cut(dec, 'static bool dec_is_pr_free(u32 op)\n', '\n}\n', 'dec_is_pr_free', True))
put('pr_gate.inc', cut(dec, '\t\tif (state.cpu.FPR64 /*|| state.cpu.FSZ64*/)\n', '\n\t\tif (state.cpu.FSZ64 && (d==PRM_FRN_SZ',
                       'PR gate'))
put('cycle.inc', cut(dec, '\t//cycle tricks\n', '\n\tstatic int cc=0;', 'cycle block'))
put('bill.inc', cut(dec, 'static u32 dec_bill(int idle_rule,u32 cycles,u32 max_cycles)\n', '\n}\n', 'dec_bill', True))
put('rm.inc', cut(dec, '\t\tcpu.FSZ64=fpu_cfg.SZ;\n', '\t\t//what about fp/fs ?', 'RM setup'))
put('hash_dec.inc', cut(dec, 'static bool dec_idle_hash(int level)\n', '\n}\n', 'dec_idle_hash', True))

# hash8_check's references. SHA-1 from hashlib, over the first n bytes of a fixed stream.
stream = bytes((i * 167 + 13) & 0xFF for i in range(128))
put('sha_ref.inc', ''.join('{ %s },\n' % ', '.join('0x%08Xu' % x for x in struct.unpack('>5I', hashlib.sha1(stream[:n]).digest()))
                           for n in range(129)))

# Reicast's hash(full, relocable=true) as it runs on x86: the first len(words) code bytes, each as a u16.
def reicast_hash(addr, words, full):
    code = b''.join(struct.pack('<H', w) for w in words)
    msg = b''.join(struct.pack('<H', code[i]) for i in range(len(words)))
    tail = ':'.join('%08X' % x for x in struct.unpack('>5I', hashlib.sha1(msg).digest()))
    return '>:1:%08X:%02X:%s' % (addr, len(words), tail) if full else '>:1:%02X:%s' % (len(words), tail)

# Blocks from the HW logs ([HOT] and [idle] dumps) and the list that holds each: R Reicast's, O ours, - none.
KNOWN = [
    ('BIOS wait', 0x8C0BA526, 'D31E 6232 2228 8BEB', 'R'),
    ('Crazy Taxi wait, head', 0x0C15A658, 'D321 6232 420B 5431', 'R'),
    ('Crazy Taxi wait, middle', 0x0C15A660, 'D120 6312 D020 6202 D11B 323C 7201 6312 3326 8B01', 'R'),
    ('Crazy Taxi wait, tail', 0x0C15A678, 'D318 6232 2228 8BEB', 'R'),
    ('Looney Tunes, a function entry', 0x8C1ADD60, '4F22 7FF8 6243 D30E 4208 2F42 4200 323C 6423 1F21 6122 410B 5441', 'R'),
    ('Looney Tunes, its return', 0x8C1ADD7A, '7F08 4F26 000B E000', 'R'),
    ('Looney Tunes, the return of another function', 0x8C090918, '7F08 4F26 000B 0009', 'R'),
    ('Looney Tunes frame wait, tail', 0x8C014544, 'F03D 015A D008 6002 3017 89F6', 'O'),
    ('Looney Tunes frame wait, head', 0x8C01453E, 'D00A 400B E406', '-'),
    ('Looney Tunes frame wait, call', 0x8C0713D0, 'D002 4408 340C 000B F048', '-'),
    ('Looney Tunes event wait, tail', 0x8C19D168, '88FF 8DF9 6D03', 'O'),
    ('Looney Tunes event wait, head', 0x8C19D160, '4A0B E407', '-'),
    ('Looney Tunes event wait, the scan call', 0x8C19D164, 'BD53 0009', '-'),
]
LISTS = {'R': 'IDLE_LIST_REICAST', 'O': 'IDLE_LIST_OWN', '-': 'IDLE_LIST_NONE'}
rows = ''
for name, addr, text, lst in KNOWN:
    words = [int(w, 16) for w in text.split()]
    rows += '{ "%s", 0x%08Xu, %d, { %s }, "%s", "%s", %s },\n' % (
        name, addr, len(words), ', '.join('0x%04X' % w for w in words), reicast_hash(addr, words, False),
        reicast_hash(addr, words, True), LISTS[lst])
put('hash_known.inc', rows)

put('mac_ref.inc', cut(read('dc/sh4/rec_v2/shil.cpp'), 'extern "C" void sh4_mac_l(u32 a, u32 b, u32 oldl, u32 oldh, u32 srs)\n',
                       '// C call-out census', 'sh4_mac_l/w'))
put('bcache_struct.inc', cut(read('dc/sh4/rec_v2/blockmanager.h'), 'struct BlockCacheFlatEntry\n',
                             '"the JIT\'s lookup reads 8-byte entries");\n', 'BlockCacheFlatEntry', True))
put('bcache_mirror.inc', cut(read('dc/sh4/rec_v2/blockmanager.cpp'), 'static inline void bcache_mirror(',
                             '\n}\n', 'bcache_mirror', True))

# SetFloatStatusReg()'s PS3 branch, each mtfsfi as a call the check counts. 5.40 is closed: no NI form is left.
CELL = '#elif HOST_CPU==CPU_PPC_CELL\n'
host = cut(read('dc/sh4/sh4_registers.cpp'), CELL, '#endif', 'SetFloatStatusReg')[len(CELL):]
host, n = re.subn(r'__asm__ volatile\("mtfsfi 7,(\d+)" ::: "memory"\);', r'host_field7(\1);', host)
if n != 2 or '__asm__' in host:
    sys.exit('FAIL cut: SetFloatStatusReg: %d mtfsfi forms' % n)
put('fpscr_host.inc', host)

# Lines of sources that do not build here.
LINES = [
    ('ps3/ps3_driver.cpp', 'mac: the form, then MACL and MACH',
     '\t\t\t\temit_mac_acc(ppc_rarg6,ppc_rarg7,ppc_rarg0,ppc_rarg1,ppc_rarg2,ppc_rarg3,ppc_rarg5);\n'
     '\t\t\t\tppc_sh_store(ppc_rarg6,op->rd);\t\t\t// -> MACL\n'
     '\t\t\t\tppc_sh_store(ppc_rarg7,op->rd2);\t\t// -> MACH\n'),
    ('ps3/ps3_driver.cpp', 'mac: the sources as the S==1 call takes them',
     '\t\t\t\tppc_sh_load(ppc_rarg0,op->rs1);\t\t\t// a\n'
     '\t\t\t\tppc_sh_load(ppc_rarg1,op->rs2);\t\t\t// b\n'
     '\t\t\t\tppc_sh_load(ppc_rarg2,reg_macl);\t\t// old MACL\n'
     '\t\t\t\tppc_sh_load(ppc_rarg3,reg_mach);\t\t// old MACH\n'
     '\t\t\t\tppc_sh_load(ppc_rarg4,op->rs3);\t\t\t// SR.status\n'),
    ('ps3/ps3_driver.cpp', 'bcache: the lookup on the target in rarg0, under its row',
     '\t\tif (get_bcache_preset())\n\t\t{\n'),
    ('ps3/ps3_driver.cpp', 'bcache: the table bound and the lookup',
     '\t\t\tverify(BM_BLOCKLIST_COUNT==16384 && (uintptr_t)&bm_bcache[BM_BLOCKLIST_COUNT] < 0x7FFF0000u);\n'
     '\t\t\temit_bcache_lookup(ppc_rarg0,&bm_bcache[0],ppc_rarg1,ppc_rarg2,ppc_rarg3);\n'),
    ('ps3/ps3_presets.cpp', 'JIT BCACHE defaults to ON (Matt 2026-10-11, HW)',
     'int g_jit_bcache_preset    = 1;'),
    ('ps3/ps3_presets.cpp', 'rows default to the old path', 'int g_jit_pr_moves_preset  = 0;'),
    ('ps3/ps3_presets.cpp', 'rows default to the old path', 'int g_jit_rm_any_preset    = 0;'),
    ('ps3/ps3_presets.cpp', 'JIT IDLE RULE defaults to AUTO (Matt 2026-10-15)', 'int g_jit_idle_rule_preset = 4;'),
    ('ps3/ps3_presets.cpp', 'getter', 'int get_bcache_preset()        { return g_jit_bcache_preset; }'),
    ('ps3/ps3_presets.cpp', 'getter', 'int get_jit_pr_moves_preset()  { return g_jit_pr_moves_preset; }'),
    ('ps3/ps3_presets.cpp', 'getter (the MMU JIT forces ACCEPT)',
     'int get_jit_rm_any_preset()    { return g_jit_rm_any_preset || g_jit_mmu; }'),
    ('ps3/ps3_presets.cpp', 'getter', 'int get_jit_idle_rule_preset() { return g_jit_idle_rule_preset; }'),
    ('ps3/ps3_presets.cpp', 'JIT IDLE HASH defaults to OFF', 'int g_jit_idle_hash_preset = 0;'),
    ('ps3/ps3_presets.cpp', 'getter', 'int get_jit_idle_hash_preset() { return g_jit_idle_hash_preset; }'),
    ('dc/sh4/rec_v2/decoder.cpp', 'idle hash: under its row, with the level',
     '\tconst int idle_hash_level=get_jit_idle_hash_preset();\n'
     '\tblock.idle_hash=idle_hash_level && !g_jit_mmu && dec_idle_hash(idle_hash_level);\n'),
    ('dc/sh4/rec_v2/decoder.cpp', 'idle hash: a new block starts unlisted', '\tidle_loop=false;\n\tidle_hash=false;\n'),
    ('ps3/ps3_driver.cpp', 'idle hash: a listed block drops the rest of the slice',
     '\tif (block->idle_hash)\n\t\tppc_li(ppc_cycles,0);\n'),
    ('dc/sh4/sh4_registers.cpp', 'the host FPSCR follows RM and DN',
     '\tif ((old_rm!=fpscr.RM) || (old_dn!=fpscr.DN) || (old_rm==0xFF )|| (old_dn==0xFF))\n'),
]
fails = 0
for path, what, text in LINES:
    if read(path).count(text) != 1:
        print('FAIL source: %s: %s' % (path, what))
        fails += 1
print('%s: source: %d lines, %d failures' % ('FAIL' if fails else 'PASS', len(LINES), fails))
sys.exit(1 if fails else 0)
