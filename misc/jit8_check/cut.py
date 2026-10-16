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
put('bill.inc', cut(dec, 'static u32 dec_bill(int idle_rule,u32 cycles,u32 max_cycles,bool fwd_plain)\n', '\n}\n',
                     'dec_bill', True))
put('rm.inc', cut(dec, '\t\tcpu.FSZ64=fpu_cfg.SZ;\n', '\t\t//what about fp/fs ?', 'RM setup'))
put('hash_dec.inc', cut(dec, 'static u32 dec_idle_hash(int level,int task)\n', '\n}\n', 'dec_idle_hash', True))

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
    ('CvS2 vsync wait, tail (the Kamui rule, not a list, since P89)', 0x8C21A2EE, '61F2 6312 2338 8BF1', '-'),
    ('CvS2 vsync wait, counter block (Reicast meant it: its entry lost a colon)', 0x8C21A2DE,
     '63F2 6232 72FF 2322 53F1 6232 32E0 8B03', '-'),
    ('CvS2 vsync wait, the hook call', 0x8C21A2DA, '4D0B E407', '-'),
    ('CvS2 hook dispatcher', 0x8C217FE0, '4F22 7FF8 6243 D30E 4208 2F42 4200 323C 6423 1F21 6122 410B 5441', 'R'),
    ('CvS2 hook dispatcher, its return', 0x8C217FFA, '7F08 4F26 000B E000', 'R'),
    ('Sonic Shuffle task dispatch, loop test (the task rule, not a list, since P94)', 0x8C0158A0, 'E310 51F1 3133 8BE1', '-'),
    ('Sonic Shuffle task dispatch, the turn tail', 0x8C01589A, '52F1 7201 1F21 E310 51F1 3133 8BE1', '-'),
    ('Border Down task dispatch, loop test (the task rule since P94)', 0x8C04838C, 'E310 51F1 3133 8BE1', '-'),
]
LISTS = {'R': 'IDLE_LIST_REICAST', 'O': 'IDLE_LIST_OWN', 'T': 'IDLE_LIST_TASK', '-': 'IDLE_LIST_NONE'}
rows = ''
for name, addr, text, lst in KNOWN:
    words = [int(w, 16) for w in text.split()]
    rows += '{ "%s", 0x%08Xu, %d, { %s }, "%s", "%s", %s },\n' % (
        name, addr, len(words), ', '.join('0x%04X' % w for w in words), reicast_hash(addr, words, False),
        reicast_hash(addr, words, True), LISTS[lst])
put('hash_known.inc', rows)

# P89: Kamui's vsync wait in the [idle] dumps of the HW logs: tail block, form (0 hook 7, 1 hooks 7 and 12, 2 the same
# with other registers), the loop from its jsr.
K7 = '4D0B E407 63F2 6232 72FF 2322 53F1 6232 32E0 8B03 61F2 6312 2338 8BF1'
K12 = '4E0B E407 4E0B E40C 63F2 6232 72FF 2322 53F1 6232 32D0 8B03 61F2 6312 2338 8BEF'
KQ3 = '4E0B E407 4E0B E40C 62F2 6322 73FF 2232 52F1 6322 33D0 8B03 61F2 6312 2338 8BEF'
KAMUI = [
    ('Capcom vs. SNK 2', 0x8C21A2EE, 0, K7), ('Spider-Man', 0x8C016CAE, 0, K7), ('Project Justice', 0x0C2DB70E, 0, K7),
    ('Power Stone 2', 0x0C2CBF2E, 0, K7), ('Donald Duck', 0x8C0E13AE, 0, K7), ('Rayman 2', 0x8C0DFF8E, 0, K7),
    ('Re-Volt', 0x8C0261EE, 0, K7), ('Ikaruga', 0x8C0370D2, 1, K12), ('Rez', 0x8C046052, 1, K12),
    ('Quake III Arena', 0x8C0CB69E, 2, KQ3),
]
put('kamui_known.inc', ''.join('{ "%s", 0x%08Xu, %d, %d, { %s } },\n' % (name, addr, form, len(text.split()),
                               ', '.join('0x' + w for w in text.split())) for name, addr, form, text in KAMUI))

# P94: the Shinobi task dispatch in the [idle] dumps (its loop test, the loop from the bf target); words 12-13 differ.
T1, T2 = '51F1 4108 4100 D304 313C 1F12 6212 2228 8B04 A00C 0009 0000', '53F2 6232 54F2 5441 420B 0009 63F2 230B 2F32 52F1 7201 1F21 E310 51F1 3133 8BE1'
TASKS = [('Sonic Shuffle', 0x8C0158A0, T1 + ' 2890 8C11 ' + T2), ('Border Down', 0x8C04838C, T1 + ' 5410 8C1A ' + T2),
         ('Garou', 0x8C015E70, T1 + ' E0AC 8C18 ' + T2)]
put('task_known.inc', ''.join('{ "%s", 0x%08Xu, { %s } },\n' % (name, addr, ', '.join('0x' + w for w in text.split()))
                              for name, addr, text in TASKS))

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
    ('ps3/ps3_presets.cpp', 'JIT IDLE HASH defaults to ON (Matt 2026-10-15)', 'int g_jit_idle_hash_preset = 1;'),
    ('ps3/ps3_presets.cpp', 'getter', 'int get_jit_idle_hash_preset() { return g_jit_idle_hash_preset; }'),
    ('ps3/ps3_presets.cpp', 'JIT TASK IDLE defaults to LONG (Matt 2026-10-15)', 'int g_jit_task_idle_preset = 2;'),
    ('ps3/ps3_presets.cpp', 'JIT SQ MISS defaults to + LA 16 OPS (Matt 2026-10-15)', 'int g_jit_sq_miss_preset   = 4;'),
    ('ps3/ps3_presets.cpp', 'getter', 'int get_jit_task_idle_preset() { return g_jit_task_idle_preset; }'),
    ('ps3/ps3_presets.cpp', 'the key', '\t{ "JitTaskIdle",   &g_jit_task_idle_preset },\n'),
    ('ps3/ps3_menu.cpp', 'the row', '\t{ "JIT TASK IDLE",   &g_jit_task_idle_preset, OPT_CHOICE, k_taskidle,'),
    ('ps3/ps3_menu.cpp', 'the levels: LONG is 2', 'static const char* k_taskidle[]  = { "OFF", "ON", "LONG", NULL };'),
    ('dc/sh4/rec_v2/decoder.cpp', 'idle hash: under its rows, with the levels',
     '\tconst int idle_hash_level=get_jit_idle_hash_preset();\n'
     '\tconst int task_idle=get_jit_task_idle_preset();\n'
     '\tblock.idle_hash=(u8)((idle_hash_level || task_idle) && !g_jit_mmu ? dec_idle_hash(idle_hash_level,task_idle) : 0);\n'),
    ('dc/sh4/rec_v2/decoder.cpp', 'idle hash: a new block starts unlisted', '\tidle_loop=false;\n\tidle_hash=false;\n'),
    ('ps3/ps3_driver.cpp', 'idle hash: a listed block drops the rest of the slice, a task entry more',
     '\tif (block->idle_hash)\n\t{\n\t\tconst s32 slice=sh4_GetCpuTimeslice();\n'
     '\t\ts32 debt=(1-(s32)block->idle_hash)*slice;\n\t\tif (debt<-32768)\n\t\t\tdebt=-(32768/slice)*slice;'
     '\t// TASK IDLE LONG above SH4 Clock 200: a sign-extended li holds 16 bits\n\t\tppc_li(ppc_cycles,debt);\n\t}\n'),
    ('ps3/ps3_driver.cpp', 'task idle LONG: an interrupt drops the rest of the debt, at LONG only',
     '\t\t\t\treg_reload_all();\n\t\t\t\tif (get_jit_task_idle_preset()>=2)\n\t\t\t\t{\n'
     "\t\t\t\t\t// JIT TASK IDLE LONG: an interrupt ends an idle turn's skip, so the slices left of it are dropped.\n"
     '\t\t\t\t\tppc_cmpi(ppc_cr0,ppc_cycles,0,0);\n\t\t\t\t\tppc_label* paid=ppc_CreateLabel();\n'
     '\t\t\t\t\tppc_bcx(BO_FALSE,BI_CR0_LT,0,0,0);\t// bge paid\n\t\t\t\t\tppc_li(ppc_cycles,0);\n'
     '\t\t\t\t\tpaid->MarkLabel();\n\t\t\t\t}\n\t\t\t}\n\t\t\tno_intr->MarkLabel();\n'),
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
