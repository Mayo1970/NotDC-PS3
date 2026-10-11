#!/usr/bin/env python3
# Plan section 7, bundle 8 (WSL): cuts the code under test out of the sources into /tmp/jit8_ext for the two check
# programs, and checks the lines of the files that do not build on the PC. JIT8_SRC: a tree of mutated files.
import os, re, sys

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
put('rm.inc', cut(dec, '\t\tcpu.FSZ64=fpu_cfg.SZ;\n', '\t\t//what about fp/fs ?', 'RM setup'))

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
    ('ps3/ps3_presets.cpp', 'JIT IDLE RULE defaults to X30 (Matt 2026-10-12)', 'int g_jit_idle_rule_preset = 0;'),
    ('ps3/ps3_presets.cpp', 'getter', 'int get_bcache_preset()        { return g_jit_bcache_preset; }'),
    ('ps3/ps3_presets.cpp', 'getter', 'int get_jit_pr_moves_preset()  { return g_jit_pr_moves_preset; }'),
    ('ps3/ps3_presets.cpp', 'getter', 'int get_jit_rm_any_preset()    { return g_jit_rm_any_preset; }'),
    ('ps3/ps3_presets.cpp', 'getter', 'int get_jit_idle_rule_preset() { return g_jit_idle_rule_preset; }'),
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
