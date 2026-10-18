#!/usr/bin/env python3
# G11 stage 1: plants one bug at a time in a copy of the sources; run.sh must FAIL on each. In the ps3dev image, from
# Git Bash: MSYS_NO_PATHCONV=1 docker run --rm --security-opt seccomp=unconfined -v ... sh -c 'python3 misc/modem_check/mutate.py'
import os, shutil, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
TMP = '/tmp/modemmut'
E, R = 'plugs/nullExtDev/nullExtDev.cpp', 'plugs/nullExtDev/modem_regs.h'
D, I = 'ps3/ps3_dcnet.cpp', 'dc/sh4/sh4_interpreter.cpp'
P, M = 'ps3/ps3_presets.cpp', 'ps3/ps3_menu.cpp'
FILES = [E, R, D, I, P, M, 'Makefile', 'dc/flash_fix.cpp', 'ps3/ps3_dcnet.h', 'ps3/ps3_presets.h', 'types.h', 'config.h',
         'os_base.h', 'stdclass.h', 'plugins/plugin_header.h', 'plugins/plugin_types.h',
         'misc/tr4_check/fake/linux/typedefs.h'] + \
        [os.path.join(d, f)[len(ROOT) + 1:] for d, _, fs in os.walk(os.path.join(ROOT, 'misc/modem_check')) for f in fs
         if f != 'mutate.py']

MUTANTS = [
    # The scheduler of the one callback
    ('the jitter is not taken off the next request', E, 's_sched_left = next > jitter ? next - jitter : 0;',
     's_sched_left = next;'),
    ('a due callback waits one more update', E, '\tif (s_sched_left > 0)\n\t\treturn;\n', '\tif (s_sched_left >= 0)\n\t\treturn;\n'),
    ('the row OFF takes writes', E, '\tif (s_on)\n\t\tModemWriteMem_A0_006(addr, data, size);\n',
     '\tModemWriteMem_A0_006(addr, data, size);\n'),
    ('a second is 150M cycles', E, 's_cps = SH4_CLOCK_EFF;', 's_cps = 150000000;'),
    ('a byte every 144 us', E, 'callback_cycles = s_cps / 1000000 * 143;', 'callback_cycles = s_cps / 1000000 * 144;'),
    ('the dial wait leaves out the jitter', E, 's_now - last_dial_time >= (u64)(s_cps + (u32)jitter)',
     's_now - last_dial_time >= (u64)s_cps'),
    # Registers and the DSP RAM
    ('MEDA reads from MEDAM first', E, 'static u32 meda() { return modem_regs.reg18 | (modem_regs.reg19 << 8); }',
     'static u32 meda() { return modem_regs.reg19 | (modem_regs.reg18 << 8); }'),
    ('a DSP RAM word read in the other byte order', E,
     'set_meda(dspram[dspram_addr] | (dspram[dspram_addr + 1] << 8));',
     'set_meda(dspram[dspram_addr + 1] | (dspram[dspram_addr] << 8));'),
    ('a DSP RAM word written in the other byte order', E,
     'dspram[dspram_addr] = meda() & 0xFF;\n\t\t\t\t\tdspram[dspram_addr + 1] = (meda() >> 8) & 0xFF;',
     'dspram[dspram_addr + 1] = meda() & 0xFF;\n\t\t\t\t\tdspram[dspram_addr] = (meda() >> 8) & 0xFF;'),
    ('the DSP RAM keeps its values over a reset', E, '\tmemcpy(dspram, por_dspram, sizeof(por_dspram));\n', ''),
    ('a write ignores the write mask', E,
     'modem_regs.ptr[reg] = (old & ~regs_write_mask[reg]) | (data & regs_write_mask[reg]);', 'modem_regs.ptr[reg] = data;'),
    ('the maker is not Rockwell', E, '(MODEM_MAKER_ROCKWELL<<4) | (MODEM_DEVICE_TYPE_336K),', '(MODEM_MAKER_ROCKWELL<<3),'),
    ('an RBUFFER read does not update the interrupt', E,
     'SET_STATUS_BIT(0x01, modem_regs.reg01.RXHF, 0);\n\t\t\t\t\tupdate_interrupt();',
     'SET_STATUS_BIT(0x01, modem_regs.reg01.RXHF, 0);'),
    ('a received byte does not set RXHF', E, '\t\t\t\t\tSET_STATUS_BIT(0x01, modem_regs.reg01.RXHF, 1);\n', ''),
    # The call and the link
    ('bytes come in before the game sent one', E, 'if (!modem_regs.reg1e.RDBF && data_sent)', 'if (!modem_regs.reg1e.RDBF)'),
    ('no link at the connection', E, '\t\t\tdcnet_start();\n', ''),
    ('a hard reset keeps the link', E, 'state = MS_RESET;\n\t\tdcnet_stop();', 'state = MS_RESET;'),
    ('the row OFF still has a modem', E, 'return s_on ? ModemReadMem_A0_006(addr, size) : 0;',
     'return ModemReadMem_A0_006(addr, size);'),
    # Big-endian layout
    ('CTS and DSR swapped', R, 'u8 DSR:1, u8 CTS:1,', 'u8 CTS:1, u8 DSR:1,'),
    ('reg02 v0 does not fill its byte', R, 'u8 S511:1, u8 :2) } v0;', 'u8 S511:1) } v0;'),
    ('4 fields are not reversed on the PPU', R, '#define MB_R4(a, b, c, d) d; c; b; a;', '#define MB_R4(a, b, c, d) a; b; c; d;'),
    ('MEADDH upside down', R, 'BITS(u8 MEMADD_h:4, u8 MEMCR:1, u8 MEMW:1, u8 :1, u8 MEACC:1)',
     'BITS(u8 MEACC:1, u8 :1, u8 MEMW:1, u8 MEMCR:1, u8 MEMADD_h:4)'),
    ('MEDAM before MEDAL', R, '\t\tu8 reg18;\n\t\tu8 reg19;\n', '\t\tu8 reg19;\n\t\tu8 reg18;\n'),
    ('the RDBF bit can be written', R, '0x24,\t//1E', '0x25,\t//1E'),
    ('the 0F interrupt mask byte', R, '0x242, 0x241,', '0x242, 0x240,'),
    ('a power-on DSP RAM byte', R, '/* 0x2E0 */ 0x00, 0x00, 0x00, 0x00, 0x0D,', '/* 0x2E0 */ 0x00, 0x00, 0x00, 0x00, 0x0C,'),
    # The DCNet rings and thread
    ('the RX ring is not masked', D, 'const u32 at = rw & (RX_SIZE - 1);', 'const u32 at = rw;'),
    ('the TX ring takes one byte too many', D, 'if (w - ACQ(s_dc.tx_r) >= TX_SIZE)', 'if (w - ACQ(s_dc.tx_r) > TX_SIZE)'),
    ('a send runs past the ring end', D, '\t\t\tif (n > TX_SIZE - at)\n\t\t\t\tn = TX_SIZE - at;\n', ''),
    ('a full ring reads 0 bytes', D, 'if (rw - rr < RX_SIZE)', 'if (true)'),
    ('a new call keeps the old bytes', D, '\tREL(s_dc.rx_r, ACQ(s_dc.rx_w));\n\tu32 call', '\tu32 call'),
    ('bytes from before the link go out', D, '\t\tREL(s_dc.tx_r, ACQ(s_dc.tx_w));\n\t\tserve(fd, call);',
     '\t\tserve(fd, call);'),
    ('a closed link reads as an error', D, 'else if (k == 0)\n', 'else if (k == 0 && false)\n'),
    ('a new call does not end the old one', D, 'while (ACQ(s_dc.want) == call)', 'while (ACQ(s_dc.want) != 0)'),
    # Lines that do not run on the PC
    ('no sin_len', D, '\tsa.sin_len = sizeof(sa);   // PSL1GHT: left at 0, the call fails\n', ''),
    ('DCNet port 7655', D, 'DCNET_PORT = 7654', 'DCNET_PORT = 7655'),
    ('the modem gets no time', I, '\tlibExtDevice_Update(s_timeslice * s_medium_period);\n', ''),
    ('Modem OFF by default', P, 'int g_modem_preset         = 1;', 'int g_modem_preset         = 0;'),
    ('the row is not saved', P, '\t{ "Modem",         &g_modem_preset },\n', ''),
    ('the row is live', M, '\t&g_fb_reads_preset, &g_keep_frame_preset,\n',
     '\t&g_modem_preset,\n\t&g_fb_reads_preset, &g_keep_frame_preset,\n'),
    ('the NETWORK page is not in the menu', M, '\t{ "NETWORK",       s_net,   (int)(sizeof(s_net) / sizeof(s_net[0])) },\n', ''),
    ('the modem takes TOC entries', 'Makefile', '$(BUILD)/plugs/nullExtDev/nullExtDev.o: CXXFLAGS += -mminimal-toc\n', ''),
]


def main():
    shutil.rmtree(TMP, ignore_errors=True)
    for f in FILES:
        os.makedirs(os.path.dirname(os.path.join(TMP, f)), exist_ok=True)
        shutil.copy(os.path.join(ROOT, f), os.path.join(TMP, f))
    env = dict(os.environ, MODEM_SRC=TMP, MODEM_TMP='/tmp/modemmut_build')
    # The copy with no mutant must pass with these arguments, or a mutant counts as caught for nothing.
    r = subprocess.run(['sh', os.path.join(TMP, 'misc/modem_check/run.sh'), '150', '1'], stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, universal_newlines=True, env=env)
    if r.returncode:
        print('BASELINE FAILS:\n' + r.stdout[-2000:])
        sys.exit(2)
    names = sys.argv[1].split(',') if len(sys.argv) > 1 else None
    caught = tried = 0
    for name, f, find, repl in MUTANTS:
        if names and not any(n in name for n in names):
            continue
        tried += 1
        # Some sources keep CRLF line ends; the patterns and the mutated copy use LF.
        text = open(os.path.join(ROOT, f), encoding='utf-8', newline='').read().replace('\r\n', '\n')
        if text.count(find) != 1:
            print(f'BAD MUTANT (pattern found {text.count(find)} times): {name}')
            sys.exit(2)
        open(os.path.join(TMP, f), 'w', encoding='utf-8', newline='').write(text.replace(find, repl))
        r = subprocess.run(['sh', os.path.join(TMP, 'misc/modem_check/run.sh'), '150', '1'], stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, universal_newlines=True, env=env)
        shutil.copy(os.path.join(ROOT, f), os.path.join(TMP, f))
        out = [l for l in r.stdout.splitlines() if l.startswith(('FAIL', 'ERROR', '==')) or 'error' in l.lower()]
        if r.returncode == 2:
            print(f'NO BUILD: {name}: {(out[0] if out else "")[:110]}')
            sys.exit(2)
        ok = r.returncode == 1
        caught += ok
        print(f"{'caught' if ok else 'MISSED'}: {name}: {(out[0] if out else '')[:110]}", flush=True)
    print(f'{caught} of {tried} mutants fail run.sh')
    sys.exit(0 if caught == tried else 1)


main()
