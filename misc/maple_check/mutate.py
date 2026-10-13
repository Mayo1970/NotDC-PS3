#!/usr/bin/env python3
# Plan section 7, bundle 16 (WSL): plants one bug at a time in a copy of the sources; run.sh must FAIL on each.
# About 15 minutes: run it detached with a log file.
import os, shutil, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
TMP = '/tmp/maplemut'
F, D, C = 'dc/maple/maple_if.cpp', 'dc/maple/maple_devs.cpp', 'dc/maple/maple_cfg.cpp'
N, L = 'dc/maple/maple_vmu_name.h', 'ps3/ps3_vmulcd.h'
R, V, X = 'ps3/ps3_rend.cpp', 'ps3/ps3_vmulcd.cpp', 'dc/dc.cpp'
P, M, PH = 'ps3/ps3_presets.cpp', 'ps3/ps3_menu.cpp', 'plugins/plugin_header.h'
FILES = [F, D, C, N, L, R, V, X, P, M, PH, 'dc/maple/maple_if.h', 'dc/maple/maple_devs.h',
         'dc/maple/maple_cfg.h', 'dc/maple/maple_helper.h', 'dc/maple/maple_helper.cpp', 'dc/maple/maple_puru.h',
         'misc/maple_check/maple_check.cpp', 'misc/maple_check/run.sh', 'misc/maple_check/ref_maple_if.inc',
         'misc/maple_check/fake/types.h', 'misc/maple_check/fake/config/config.h',
         'misc/maple_check/fake/dc/sh4/intc.h', 'misc/maple_check/fake/dc/mem/sb.h',
         'misc/maple_check/fake/dc/mem/sh4_mem.h', 'misc/maple_check/fake/dc/asic/asic.h',
         'misc/maple_check/fake/plugins/plugin_manager.h', 'misc/maple_check/fake/ps3/ps3_stall.h']

PAD = "\t\t// P29: with the row, the pad answers the reset, kill and all-status commands as Flycast's does.\n"
VMU = "\t\t// P29: with the row, the VMU answers the reset, kill and all-status commands as Flycast's does.\n"
ALL = '\t\tconst bool all=g_maple_cmds_preset && cmd==MDC_AllStatusReq;\n'
RST = '\t\tif (g_maple_cmds_preset && (cmd==MDC_DeviceReset || cmd==MDC_DeviceKill))\n'

MUTANTS = [
    # Maple Reply Delay
    ('the reply is written at once with the row on', F, 'const bool direct=!delay && !drop && !swap_msb;', 'const bool direct=!drop && !swap_msb;'),
    ('the replies wait with the row off', F, 'const bool delay=g_maple_delay_preset!=0,', 'const bool delay=true,'),
    ('the kept replies never reach RAM', F, '\t\tmaple_copy_replies();\n\t\tif (s_overrun)', '\t\tif (s_overrun)'),
    ('an aborted DMA writes its replies', F, '\t\t\tif (maple_dma_pending<=0)\n\t\t\t\tmaple_end_replies(false);',
     '\t\t\tif (maple_dma_pending<=0)\n\t\t\t\tmaple_end_replies(true);'),
    ('an abort drops the replies before the end time', F, '\t\t\tif (maple_dma_pending<=0)\n\t\t\t\tmaple_end_replies(false);',
     '\t\t\tmaple_end_replies(false);'),
    ('a full pool is not written out', F, '\t\t\t\t\tmaple_copy_replies();\n\t\t\t\t}\n', '\t\t\t\t}\n'),
    ('a kept reply is one word short', F, 's_out[s_out_n].words=words;', 's_out[s_out_n].words=words-1;'),
    ('a kept reply goes one word too far', F, 's_out[s_out_n].addr=header_2;', 's_out[s_out_n].addr=header_2+4;'),
    ('a kept reply starts at the pool\'s start', F, 's_out[s_out_n].first=s_out_words;', 's_out[s_out_n].first=0;'),
    ('the end keeps the pool', F, '\t\tmemcpy(GetMemPtr(s_out[i].addr,s_out[i].words*4),&s_out_buf[s_out[i].first],s_out[i].words*4);\n\ts_out_n=s_out_words=0;\n',
     '\t\tmemcpy(GetMemPtr(s_out[i].addr,s_out[i].words*4),&s_out_buf[s_out[i].first],s_out[i].words*4);\n'),
    ('a reset keeps the waiting replies', F, '\tmaple_dma_pending=0;\n\tmaple_end_replies(false);\n', '\tmaple_dma_pending=0;\n'),
    # Maple Bus Speed
    ('REAL is 2 Mb/s both ways', F, '(u64)SH4_CLOCK_EFF*xfer_out/(740000/8)', '(u64)SH4_CLOCK_EFF*xfer_out/(2000000/8)'),
    ('REAL is 740 Kb/s both ways', F, '(u64)SH4_CLOCK_EFF*xfer_in/(2000000/8)', '(u64)SH4_CLOCK_EFF*xfer_in/(740000/8)'),
    ('a frame out has no start, parity and stop bytes', F, 'xfer_in+=plen*4+3;', 'xfer_in+=plen*4;'),
    ('a reply has no start, parity and stop bytes', F, 'xfer_out+=outlen+4+3;', 'xfer_out+=outlen+4;'),
    ('a reply counts without its header', F, 'xfer_out+=outlen+4+3;', 'xfer_out+=outlen+3;'),
    ('REAL with the row off', F, '\tif (g_maple_speed_preset)\n\t{', '\tif (true)\n\t{'),
    ('OLD with the row on', F, '\tif (g_maple_speed_preset)\n\t{', '\tif (false)\n\t{'),
    ('a DMA can last more than a second', F, '\t\tif (cycles>SH4_CLOCK_EFF)\n\t\t\tcycles=SH4_CLOCK_EFF;\n', ''),
    ('a DMA of no bytes never ends', F, 'maple_dma_pending=cycles ? (s32)cycles : 1;', 'maple_dma_pending=(s32)cycles;'),
    ('a reset pattern is no byte', F, 'if (maple_op==3 || (maple_op==2 &&', 'if (false || (maple_op==2 &&'),
    ('an occupy to an empty bus is a byte', F, '(maple_op==2 && MapleDevices[(header_1>>16)&3][5])', '(maple_op==2)'),
    # Maple VBlank DMA
    ('FIXED clears SB_MDST at once', F, '\t\t\t\tif (!g_maple_ddt_preset)\n\t\t\t\t\tSB_MDST = 0;', '\t\t\t\tSB_MDST = 0;'),
    ('OLD sets SB_MDST', F, '\t\t\t\tif (g_maple_ddt_preset)\n\t\t\t\t\tSB_MDST = 1;', '\t\t\t\tSB_MDST = 1;'),
    ('FIXED does not set SB_MDST', F, '\t\t\t\tif (g_maple_ddt_preset)\n\t\t\t\t\tSB_MDST = 1;', ''),
    ('the vblank DMA runs without the trigger select', F, '\t\tif (SB_MDTSEL&1)\n', '\t\tif (true)\n'),
    # Maple Guards
    ('a reply outside RAM is dropped with the row off', F, 'const bool drop=!on_ram && guard;', 'const bool drop=!on_ram;'),
    ('a reply outside RAM is written with the row on', F, 'const bool drop=!on_ram && guard;', 'const bool drop=false;'),
    ('no overrun interrupt', F, '\t\tif (s_overrun)\n\t\t\tasic_RaiseInterrupt(holly_MAPLE_OVERRUN);\n', ''),
    ('the overrun interrupt is the end interrupt', F, 'asic_RaiseInterrupt(holly_MAPLE_OVERRUN);', 'asic_RaiseInterrupt(holly_MAPLE_DMA);'),
    ('an overrun is not noted', F, '\t\t\t\tif (drop)\n\t\t\t\t\ts_overrun=true;\n\t\t\t\telse if (delay)', '\t\t\t\tif (drop)\n\t\t\t\t\t;\n\t\t\t\telse if (delay)'),
    ('an overrun stays for the next DMA', F, '\ts_out_n=s_out_words=0;\n\ts_overrun=false;\n}', '\ts_out_n=s_out_words=0;\n}'),
    ('the overrun interrupt comes after the end interrupt', F,
     '\t\t\t\tmaple_end_replies(true);\n\t\t\t\tSB_MDST=0;\n\t\t\t\tasic_RaiseInterrupt(holly_MAPLE_DMA);',
     '\t\t\t\tSB_MDST=0;\n\t\t\t\tasic_RaiseInterrupt(holly_MAPLE_DMA);\n\t\t\t\tmaple_end_replies(true);'),
    ('a table outside RAM is read with the row on', F, '\t\t\tif (!p_data && guard)', '\t\t\tif (!p_data && !guard)'),
    ('a stopped DMA stays busy', F, '\t\t\t\tSB_MDST=0;\n\t\t\t\tmaple_end_replies(false);\n\t\t\t\treturn;', '\t\t\t\tmaple_end_replies(false);\n\t\t\t\treturn;'),
    ('a stopped DMA keeps its replies', F, '\t\t\t\tSB_MDST=0;\n\t\t\t\tmaple_end_replies(false);\n\t\t\t\treturn;', '\t\t\t\tSB_MDST=0;\n\t\t\t\treturn;'),
    # Maple Frame Rules
    ('SB_MMSEL 0 swaps with the row off', F, 'const bool swap_msb=frame_rules && SB_MMSEL==0;', 'const bool swap_msb=SB_MMSEL==0;'),
    ('SB_MMSEL 1 swaps', F, 'const bool swap_msb=frame_rules && SB_MMSEL==0;', 'const bool swap_msb=frame_rules && SB_MMSEL==1;'),
    ('the frame goes to the device as it is', F, '\t\t\t\tp_data=s_in_buf;\n', ''),
    ('the reply is not swapped', F, '\t\t\t\tif (swap_msb)\n\t\t\t\t\tfor (u32 i=0;i<words;i++)\n\t\t\t\t\t\tout[i]=maple_swap32(out[i]);\n', ''),
    ('the reply\'s header word is not swapped', F, '\t\t\t\t\tfor (u32 i=0;i<words;i++)\n\t\t\t\t\t\tout[i]=maple_swap32(out[i]);', '\t\t\t\t\tfor (u32 i=1;i<words;i++)\n\t\t\t\t\t\tout[i]=maple_swap32(out[i]);'),
    ('the swap is of the two halves', F, 'return (v<<24) | ((v&0xFF00)<<8) | ((v>>8)&0xFF00) | (v>>24);', 'return (v<<16) | (v>>16);'),
    ('no unit bit is the main unit with the row off', F, 'if (frame_rules && !(reci&0x3F))', 'if (!(reci&0x3F))'),
    ('no unit bit stays sub-unit 1', F, '\t\t\t\tport=5;\n', '\t\t\t\tport=0;\n'),
    ('a sub-unit address is the main unit', F, 'if (frame_rules && !(reci&0x3F))', 'if (frame_rules && !(reci&0x20))'),
    # Maple Commands
    ('the pad answers all-status with the row off', D, PAD + ALL, PAD + '\t\tconst bool all=cmd==MDC_AllStatusReq;\n'),
    ('the pad answers reset with the row off', D, PAD + ALL + RST, PAD + ALL + '\t\tif ((cmd==MDC_DeviceReset || cmd==MDC_DeviceKill))\n'),
    ('the pad does not answer kill', D, PAD + ALL + RST, PAD + ALL + '\t\tif (g_maple_cmds_preset && (cmd==MDC_DeviceReset))\n'),
    ('the pad\'s all-status has no text', D, '\t\t\t\twptr(maple_pad_extra,80);\n', ''),
    ('the pad\'s all-status has the status code', D, '\t\t\t\twptr(maple_pad_extra,80);\n\t\t\t\treturn MDRS_DeviceStatusAll;', '\t\t\t\twptr(maple_pad_extra,80);\n\t\t\t\treturn MDRS_DeviceStatus;'),
    ('an unknown pad command is UnknownCmd with the row off', D, 'return g_maple_cmds_preset ? MDRE_UnknownCmd : MDRE_UnknownFunction;', 'return MDRE_UnknownCmd;'),
    ('an unknown pad command is UnknownFunction with the row on', D, 'return g_maple_cmds_preset ? MDRE_UnknownCmd : MDRE_UnknownFunction;', 'return MDRE_UnknownFunction;'),
    ('the VMU answers all-status with the row off', D, VMU + ALL, VMU + '\t\tconst bool all=cmd==MDC_AllStatusReq;\n'),
    ('the VMU answers reset with the row off', D, VMU + ALL + RST, VMU + ALL + '\t\tif ((cmd==MDC_DeviceReset || cmd==MDC_DeviceKill))\n'),
    ('the VMU does not answer reset', D, VMU + ALL + RST, VMU + ALL + '\t\tif (g_maple_cmds_preset && (cmd==MDC_DeviceKill))\n'),
    ('the VMU\'s all-status has the pad\'s text', D, 'wptr(maple_vmu_extra,80);', 'wptr(maple_pad_extra,80);'),
    ('the LCD media-info byte is 2 with the row off', D, 'w8(g_maple_cmds_preset ? 2 : 0);', 'w8(2);'),
    ('the LCD media-info byte is 0 with the row on', D, 'w8(g_maple_cmds_preset ? 2 : 0);', 'w8(0);'),
    # VMU Per Game
    ('the card stays when the disc is known', D, '\tvirtual void OnGame()\n\t{\n\t\topen_card();\n\t}', '\tvirtual void OnGame()\n\t{\n\t}'),
    ('a new card starts with the old card\'s bytes', D, '\t\tstrcpy(file_name,name);\n\t\tmemset(flash_data,0,sizeof(flash_data));\n', '\t\tstrcpy(file_name,name);\n'),
    ('the disc ID is not kept', D, '\tvmu_game_id(s_vmu_game, id10);\n', ''),
    ('the file name has no folder rule: the old name', D, '\t\tvmu_file_name(name,sizeof(name),logical_port,s_vmu_game,g_vmu_per_game_preset);\n',
     '\t\tvmu_file_name(name,sizeof(name),logical_port,s_vmu_game,0);\n'),
    ('SLOT A1 acts on every slot', N, '(level == 1 && !strcmp(port, "A1"))', '(level == 1)'),
    ('ALL SLOTS acts on no slot', N, 'if (n > 0 && (level == 2 || (level == 1', 'if (n > 0 && ((level == 1'),
    ('no disc ID gives a file per game', N, 'if (n > 0 && (level == 2', 'if ((level == 2'),
    ('the ID keeps its trailing blanks', N, "while (n > 0 && (out[n - 1] == ' ' ||", "while (false && (out[n - 1] == ' ' ||"),
    ('a slash stays in the file name', N, 'if (strchr(" /\\\\:*?|<>\\"", out[i]) ||', 'if (strchr(" \\\\:*?|<>\\"", out[i]) ||'),
    ('a control byte stays in the file name', N, '(unsigned char)out[i] < 0x20 || ', ''),
    ('the ID is 11 bytes', N, 'for (; id10 && n < 10 && id10[n]; n++)', 'for (; id10 && n < 11 && id10[n]; n++)'),
    # VMU Screen
    ('the picture has the bus and the slot swapped', C, 'ps3_vmulcd_push(dev->bus_id, dev->bus_port, (const u8*)img);', 'ps3_vmulcd_push(dev->bus_port, dev->bus_id, (const u8*)img);'),
    ('an LCD write gives no picture', C, '\t\tif (dev)\n\t\t\tps3_vmulcd_push(dev->bus_id, dev->bus_port, (const u8*)img);\n', ''),
    ('the screen\'s rows are top-down', L, 'return (31 - screen_y) * 48 + screen_x;', 'return screen_y * 48 + screen_x;'),
    ('the screen is mirrored', L, 'return (31 - screen_y) * 48 + screen_x;', 'return (31 - screen_y) * 48 + (47 - screen_x);'),
    ('the screens are drawn with the row off', V, '\tif (!g_vmu_screen_preset)\n\t\treturn 0;\n', ''),
    ('the lit dots are the white ones', V, '\t\t\t\tif (row[x])\n\t\t\t\t\tcontinue;', '\t\t\t\tif (!row[x])\n\t\t\t\t\tcontinue;'),
    ('slot 2 draws over slot 1', V, 'const u32 id = bus * 2 + port;', 'const u32 id = bus * 2;'),
    ('the screens are not drawn', R, '\tif (ps3_vmulcd_draw(sw, sh))\n\t\treset_param_cache();\n', ''),
    ('the GL state cache is stale after the screens', R, '\tif (ps3_vmulcd_draw(sw, sh))\n\t\treset_param_cache();\n', '\tps3_vmulcd_draw(sw, sh);\n'),
    ('the UI is not ready at boot', R, '\t\tps3_vmulcd_init();\n', ''),
    ('the disc ID is the area text', X, 'maple_vmu_game((const char*)&ip[0x40]);', 'maple_vmu_game((const char*)&ip[0x30]);'),
    ('the overrun interrupt is bit 8', PH, 'holly_MAPLE_OVERRUN = holly_err | 0x09,', 'holly_MAPLE_OVERRUN = holly_err | 0x08,'),
    # The rows
    ('Maple Reply Delay OFF by default', P, 'int g_maple_delay_preset   = 1;', 'int g_maple_delay_preset   = 0;'),
    ('Maple Bus Speed REAL by default', P, 'int g_maple_speed_preset   = 0;', 'int g_maple_speed_preset   = 1;'),
    ('Maple Commands OLD by default', P, 'int g_maple_cmds_preset    = 1;', 'int g_maple_cmds_preset    = 0;'),
    ('Maple VBlank DMA OLD by default', P, 'int g_maple_ddt_preset     = 1;', 'int g_maple_ddt_preset     = 0;'),
    ('Maple Guards OFF by default', P, 'int g_maple_guard_preset   = 1;', 'int g_maple_guard_preset   = 0;'),
    ('Maple Frame Rules OLD by default', P, 'int g_maple_frame_preset   = 1;', 'int g_maple_frame_preset   = 0;'),
    ('VMU Per Game SLOT A1 by default', P, 'int g_vmu_per_game_preset  = 0;', 'int g_vmu_per_game_preset  = 1;'),
    ('VMU Screen OFF by default', P, 'int g_vmu_screen_preset    = 1;', 'int g_vmu_screen_preset    = 0;'),
    ('Maple Reply Delay is not saved', P, '\t{ "JitSqCopy",     &g_jit_sq_copy_preset },\n\t{ "MapleDelay",    &g_maple_delay_preset },\n',
     '\t{ "JitSqCopy",     &g_jit_sq_copy_preset },\n'),
    ('VMU Screen is not saved', P, '\t{ "VmuScreen",     &g_vmu_screen_preset },\n\t{ "Controller",    &g_controller_type },\n',
     '\t{ "Controller",    &g_controller_type },\n'),
    ('VMU Screen is saved as VMU Per Game', P, '\t{ "VmuScreen",     &g_vmu_screen_preset },\n\t{ "Controller"',
     '\t{ "VmuScreen",     &g_vmu_per_game_preset },\n\t{ "Controller"'),
    ('the MAPLE / VMU page is not in the menu', M, '\t{ "MAPLE / VMU",   s_maple, (int)(sizeof(s_maple) / sizeof(s_maple[0])) },\n', ''),
    ('no [maplecfg] line at boot', M, '\tps3_maplecfg_print("boot", true);\n', ''),
    ('Maple Reply Delay is not live', M, '\t&g_maple_delay_preset, &g_maple_speed_preset,', '\t&g_maple_speed_preset,'),
    ('VMU Per Game is live', M, '\t&g_maple_frame_preset, &g_vmu_screen_preset,\n', '\t&g_maple_frame_preset, &g_vmu_screen_preset, &g_vmu_per_game_preset,\n'),
    ('ALL SLOTS comes before SLOT A1', M, '{ "OFF", "SLOT A1", "ALL SLOTS", NULL }', '{ "OFF", "ALL SLOTS", "SLOT A1", NULL }'),
    ('Maple VBlank DMA shows OFF / ON', M, '{ "Maple VBlank DMA", &g_maple_ddt_preset,    OPT_CHOICE, k_div32,', '{ "Maple VBlank DMA", &g_maple_ddt_preset,    OPT_CHOICE, k_onoff,'),
    ('the Maple Guards row sets Maple Frame Rules', M, '{ "Maple Guards",    &g_maple_guard_preset,', '{ "Maple Guards",    &g_maple_frame_preset,'),
]

def main():
    shutil.rmtree(TMP, ignore_errors=True)
    for f in FILES:
        os.makedirs(os.path.dirname(os.path.join(TMP, f)), exist_ok=True)
        shutil.copy(os.path.join(ROOT, f), os.path.join(TMP, f))
    env = dict(os.environ, MAPLE_SRC=TMP, MAPLE_TMP='/tmp/maplemut_build')
    # Every pattern first: a text that is not there once would only show after minutes.
    bad = 0
    for name, f, find, repl in MUTANTS:
        text = open(os.path.join(ROOT, f), encoding='utf-8', newline='').read().replace('\r\n', '\n')
        if text.count(find) != 1:
            print(f'BAD MUTANT (pattern found {text.count(find)} times): {name}')
            bad += 1
    if bad:
        sys.exit(2)
    caught = 0
    for name, f, find, repl in MUTANTS:
        # Some sources keep CRLF line ends; the patterns and the mutated copy use LF.
        text = open(os.path.join(ROOT, f), encoding='utf-8', newline='').read().replace('\r\n', '\n')
        open(os.path.join(TMP, f), 'w', encoding='utf-8', newline='').write(text.replace(find, repl))
        r = subprocess.run(['sh', os.path.join(TMP, 'misc/maple_check/run.sh'), '50'], capture_output=True, text=True,
                           env=env)
        shutil.copy(os.path.join(ROOT, f), os.path.join(TMP, f))
        out = [l for l in (r.stdout + r.stderr).splitlines() if l.startswith(('FAIL', 'ERROR')) or 'error' in l]
        if r.returncode == 2:
            print(f'NO BUILD: {name}: {(out[0] if out else "")[:110]}', flush=True)
            continue
        ok = r.returncode == 1
        caught += ok
        print(f"{'caught' if ok else 'MISSED'}: {name}: {(out[0] if out else '')[:110]}", flush=True)
    print(f'{caught} of {len(MUTANTS)} mutants fail run.sh')
    sys.exit(0 if caught == len(MUTANTS) else 1)

main()
