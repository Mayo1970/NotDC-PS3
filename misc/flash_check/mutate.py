#!/usr/bin/env python3
# Plan section 7, bundle 15 (WSL): plants one bug at a time in a copy of the sources; run.sh must FAIL on each.
# About 15 minutes: run it detached with a log file.
import os, shutil, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
TMP = '/tmp/flashmut'
F, D, N = 'dc/flash_fix.cpp', 'dc/dc.cpp', 'nullDC.cpp'
G, A, Z = 'dc/mem/sh4_mem.cpp', 'dc/aica/aica_if.cpp', 'ps3/ps3_main.cpp'
P, M = 'ps3/ps3_presets.cpp', 'ps3/ps3_menu.cpp'
FILES = [F, D, N, G, A, Z, P, M, 'dc/flash_fix.h', 'ps3/ps3_presets.h', 'misc/flash_check/flash_check.cpp',
         'misc/flash_check/run.sh', 'misc/flash_check/ref_flash_fix.inc', 'misc/flash_check/fake/types.h',
         'misc/flash_check/fake/dc/mem/sh4_mem.h']

MUTANTS = [
    # Broadcast
    ('the NTSC row writes PAL', F, 'set_factory(FACT_BROADCAST, g_broadcast_preset - 1);',
     'set_factory(FACT_BROADCAST, 2 - g_broadcast_preset);'),
    ('DISC writes a byte with no disc', F, 'if (g_broadcast_preset == 1 || g_broadcast_preset == 2)',
     'if (g_broadcast_preset >= 1)'),
    ('DISC: a disc for Japan and Europe is PAL', F, 'set_factory(FACT_BROADCAST, (j || u) ? 0 : 1);',
     'set_factory(FACT_BROADCAST, e ? 1 : 0);'),
    ('DISC: NTSC and PAL swapped', F, 'set_factory(FACT_BROADCAST, (j || u) ? 0 : 1);',
     'set_factory(FACT_BROADCAST, (j || u) ? 1 : 0);'),
    ('the disc rule acts at NTSC and PAL too', F, '\tif (g_broadcast_preset == 3)\n', '\tif (g_broadcast_preset)\n'),
    ('the disc rule acts at CONSOLE', F, '\tif (g_broadcast_preset == 3)\n', '\tif (g_broadcast_preset != 1 && g_broadcast_preset != 2)\n'),
    ('the broadcast byte is the language byte', F, 'FACT_BROADCAST = 4,', 'FACT_BROADCAST = 3,'),
    ('the second factory copy is not written', F, "\tflash_b[FLASH_FACTORY_B + at] = (u8)('0' + digit);\n", ''),
    ('the second factory copy at 0x1a080', F, '#define FLASH_FACTORY_B 0x1a0a0', '#define FLASH_FACTORY_B 0x1a080'),
    ('a factory byte is the value, not its digit', F, "\tflash_b[FLASH_FACTORY_A + at] = (u8)('0' + digit);\n",
     '\tflash_b[FLASH_FACTORY_A + at] = (u8)digit;\n'),
    ('the broadcast byte is not put back', F,
     'at <= FACT_BROADCAST; at++)\n\t{\n\t\tif (!(s_fact_patched & (1u << at)))\n\t\t\tcontinue;',
     'at < FACT_BROADCAST; at++)\n\t{\n\t\tif (!(s_fact_patched & (1u << at)))\n\t\t\tcontinue;'),
    ('the second copy gets the first copy\'s own byte', F, '\t\tflash_b[FLASH_FACTORY_B + at] = s_fact_orig[1][at];\n',
     '\t\tflash_b[FLASH_FACTORY_B + at] = s_fact_orig[0][at];\n'),
    ('a new load keeps the last load\'s patches', F, '\ts_fact_patched = 0;\n\ts_lang_forced = false;\n\tif (flash_b.data == 0)',
     '\tif (flash_b.data == 0)'),
    # BIOS Language
    ('the factory language is the row, not row - 1', F, 'set_factory(FACT_LANG, g_bios_lang_preset - 1);',
     'set_factory(FACT_LANG, g_bios_lang_preset);'),
    ('the syscfg language is the row, not row - 1', F, 's_lang_set = (u8)(g_bios_lang_preset - 1);',
     's_lang_set = (u8)g_bios_lang_preset;'),
    ('ITALIAN does nothing', F, 'g_bios_lang_preset >= 1 && g_bios_lang_preset <= 6', 'g_bios_lang_preset >= 1 && g_bios_lang_preset <= 5'),
    ('CONSOLE writes Japanese', F, 'g_bios_lang_preset >= 1 && g_bios_lang_preset <= 6', 'g_bios_lang_preset >= 0 && g_bios_lang_preset <= 6'),
    ('the language byte of the block is 8', F, '#define SYSCFG_LANG         7', '#define SYSCFG_LANG         8'),
    ('the row does not set the block', F, '\t\tblk[SYSCFG_LANG] = s_lang_set;\n', ''),
    ('the row\'s language reaches the file', F, '\tif (id && phys(id)[SYSCFG_LANG] == s_lang_set)\n', '\tif (false)\n'),
    ('a language the BIOS wrote is lost', F, '\tif (id && phys(id)[SYSCFG_LANG] == s_lang_set)\n', '\tif (id)\n'),
    ('no CRC after the language goes back', F, '\t\twr16(phys(id) + 62, crc_block(phys(id)));\n', ''),
    ('the language goes back at each save', F, '\tif (!s_lang_forced)\n\t\treturn;\n\ts_lang_forced = false;\n',
     '\tif (!s_lang_forced)\n\t\treturn;\n'),
    # Flash Defaults
    ('a new block is English at OLD', F, 'blk[SYSCFG_LANG] = g_flash_defaults_preset ? 1 : 0;', 'blk[SYSCFG_LANG] = 1;'),
    ('a new block is Japanese at FLYCAST', F, 'blk[SYSCFG_LANG] = g_flash_defaults_preset ? 1 : 0;', 'blk[SYSCFG_LANG] = 0;'),
    ('the built flash is Japan', F, 'memcpy(&flash_b[FLASH_FACTORY_A], "00110Dreamcast  ", 16);',
     'memcpy(&flash_b[FLASH_FACTORY_A], "00010Dreamcast  ", 16);'),
    ('the built flash is PAL in its second copy', F, 'memcpy(&flash_b[FLASH_FACTORY_B], "00110Dreamcast  ", 16);',
     'memcpy(&flash_b[FLASH_FACTORY_B], "00111Dreamcast  ", 16);'),
    ('the built flash has no game partition header', F, '{ FLASH_USER_OFFSET, 0x10000, 0x00000 }',
     '{ FLASH_USER_OFFSET, 0x10040, 0x00000 }'),
    ('the built flash numbers its partitions from 3', F, '(u8)(FLASH_PT_USER + p);', '(u8)(FLASH_PT_USER + p + 1);'),
    ('the built flash is zeros', F, 'memset(&flash_b[0], 0xFF, flash_b.Size);', 'memset(&flash_b[0], 0x00, flash_b.Size);'),
    ('the built flash has no header version', F, '\t\tflash_b[part[p] + 17] = 0;\n', ''),
    ('a flash is built at OLD', D, 'if (!flash_ok && g_flash_defaults_preset)', 'if (!flash_ok)'),
    ('a built flash is not a loaded flash', D, '\t\t\tdc_flash_build();\n\t\t\tflash_ok = true;\n', '\t\t\tdc_flash_build();\n'),
    ('OLD does not exit with no flash file', N, 'if (!fr && !get_flash_defaults_preset())',
     'if (!fr && get_flash_defaults_preset())'),
    # Console ID
    ('Console ID acts at OLD', F, '\tif (g_console_id_preset)\n\t\tfix_console_id(seed);', '\tfix_console_id(seed);'),
    ('five 0xFF bytes are a blank ID', F, '\tfor (int i = 0; i < 6; i++)\n\t\tblank = blank && id[i] == 0xFF;',
     '\tfor (int i = 0; i < 5; i++)\n\t\tblank = blank && id[i] == 0xFF;'),
    ('a known ID is replaced', F, '\tif (blank)\n', '\tif (true)\n'),
    ('the new ID is not in the second copy', F, 'id[i] = id[i + FLASH_COPY_STEP] = (u8)(x >> 8);', 'id[i] = (u8)(x >> 8);'),
    ('the new ID does not come from the seed', F, 'u32 x = seed ? seed : 1;', 'u32 x = 1;'),
    ('the checksum misses the last byte', F, '\tfor (int i = 0; i < 6; i++)\n\t\tsum += id[i];',
     '\tfor (int i = 0; i < 5; i++)\n\t\tsum += id[i];'),
    ('the sum byte holds the inverted sum', F, 'id[-1] = id[FLASH_COPY_STEP - 1] = sum;', 'id[-1] = id[FLASH_COPY_STEP - 1] = (u8)~sum;'),
    ('the inverted sum byte holds the sum', F, 'id[-2] = id[FLASH_COPY_STEP - 2] = (u8)~sum;', 'id[-2] = id[FLASH_COPY_STEP - 2] = sum;'),
    ('the machine version stays', F, '\tid[7] = id[FLASH_COPY_STEP + 7] = 0xFE;\n', ''),
    ('the machine version is 0xFF', F, 'id[7] = id[FLASH_COPY_STEP + 7] = 0xFE;', 'id[7] = id[FLASH_COPY_STEP + 7] = 0xFF;'),
    ('the second copy is 0x80 further', F, '#define FLASH_COPY_STEP  0xA0', '#define FLASH_COPY_STEP  0x80'),
    ('the console ID at 0x1a050', F, '#define FLASH_CONSOLE_ID 0x1a058', '#define FLASH_CONSOLE_ID 0x1a050'),
    # The region switch of before, and the report
    ('a region switch reaches the file', F, '\tset_factory(FACT_REGION, want);\n',
     "\tflash_b[FLASH_FACTORY_A + FACT_REGION] = flash_b[FLASH_FACTORY_B + FACT_REGION] = (u8)('0' + want);\n"),
    ('the report names the row\'s bytes as the file\'s', F, 'digit((s_fact_patched & (1u << at)) ? s_fact_orig[0][at] : b);',
     'digit(b);'),
    ('the report names the file\'s language as the row\'s', F, 's_lang_forced ? (int)s_lang_orig : lang,', 'lang,'),
    ('the report counts one block more', F, '\treturn id - 1;\n', '\treturn id;\n'),
    ('the report tests half of the checksum', F, 'cid[-1] == sum && cid[-2] == (u8)~sum;', 'cid[-1] == sum;'),
    ('no report line', D, '\t\t\tdc_flash_report();\n', ''),
    ('no fix-up call', D, '\t\t\tdc_flash_fixup(settings.dreamcast.RTC);\n', ''),
    # Exit Flash Save
    ('the forced save does not ask if a flash is loaded', G, 'if (!g_flash_loaded || flash_b.data == 0)', 'if (flash_b.data == 0)'),
    ('the forced save gives the lock back', G, '\tif (!forced)\n\t\t__sync_lock_release(&s_flash_saving);', '\t__sync_lock_release(&s_flash_saving);'),
    ('the plain save keeps the lock', G, '\tif (!forced)\n\t\t__sync_lock_release(&s_flash_saving);\n', ''),
    ('the plain save waits for the lock', G, 'if (!forced || i >= 20)', 'if (i >= 20)'),
    ('the forced save waits one turn more', G, 'if (!forced || i >= 20)', 'if (!forced || i >= 21)'),
    ('no restore before the write', G, '\tdc_flash_restore_region();\n\tchar* temp_path', '\tchar* temp_path'),
    ('the save goes to dc_flash.bin', G, 'strcat(temp_path, "dc_flash_wb.bin");', 'strcat(temp_path, "dc_flash.bin");'),
    ('a failed write reads as saved', G, '\treturn ok;\n', '\treturn true;\n'),
    ('the flag stays set over a zeroed array', G, '\t\tg_flash_loaded = false;\n', ''),
    ('the flag is set before the load', D, '\t\tg_flash_loaded = flash_ok;\n', '\t\tg_flash_loaded = true;\n'),
    ('the forced exit saves at OFF', Z, '\t\t\t\tif (g_exit_save_preset)\n', ''),
    ('the forced exit does not save', Z, '\t\t\t\t\tmem_SaveFlashForced();\n', ''),
    # RTC
    ('P12: one day under summer time', A, 'delta += 3600;', 'delta += 24 * 3600;'),
    ('RTC Local Time adds no zone', A, 'return (u32)(DC_EPOCH_1970 + time(NULL) + zone);', 'return (u32)(DC_EPOCH_1970 + time(NULL));'),
    ('1950 to 1970 without the leap days', A, '(20 * 365 + 5) * 24 * 60 * 60;', '(20 * 365) * 24 * 60 * 60;'),
    ('RTC Local Time is never read', A, '    if (get_rtc_local_preset())\n', '    if (false)\n'),
    ('RTC Local Time acts at OFF', A, '    if (get_rtc_local_preset())\n', '    if (true)\n'),
    ('no summer time in the offset', Z, 'return (zone + summer) * 60;', 'return zone * 60;'),
    ('the offset is minutes, not seconds', Z, 'return (zone + summer) * 60;', 'return zone + summer;'),
    ('a refused zone counts', Z, 'if (rz != 0 || zone < -12 * 60 || zone > 14 * 60)', 'if (zone < -12 * 60 || zone > 14 * 60)'),
    ('zones stop at +12 h', Z, 'zone > 14 * 60)', 'zone > 12 * 60)'),
    ('zones stop at -11 h', Z, 'zone < -12 * 60 ||', 'zone < -11 * 60 ||'),
    ('summer time 1 is one minute', Z, '\tif (summer == 1)\n\t\tsummer = 60;\n', ''),
    ('a refused summer time counts', Z, 'if (rs != 0 || summer < 0 || summer > 120)', 'if (summer < 0 || summer > 120)'),
    ('a summer time of 3 h counts', Z, 'summer > 120)', 'summer > 180)'),
    # The rows
    ('Broadcast NTSC by default', P, 'int g_broadcast_preset     = 3;', 'int g_broadcast_preset     = 1;'),
    ('BIOS Language CONSOLE by default', P, 'int g_bios_lang_preset     = 2;', 'int g_bios_lang_preset     = 0;'),
    ('Flash Defaults OLD by default', P, 'int g_flash_defaults_preset = 1;', 'int g_flash_defaults_preset = 0;'),
    ('Console ID OLD by default', P, 'int g_console_id_preset    = 1;', 'int g_console_id_preset    = 0;'),
    ('Exit Flash Save ON by default', P, 'int g_exit_save_preset     = 0;', 'int g_exit_save_preset     = 1;'),
    ('RTC Local Time OFF by default', P, 'int g_rtc_local_preset     = 1;', 'int g_rtc_local_preset     = 0;'),
    ('RTC Local Time is not saved', P, '\t{ "RtcLocal",      &g_rtc_local_preset },\n\t{ "FpsOverlay"', '\t{ "FpsOverlay"'),
    ('Broadcast is not saved', P, '\t{ "MapR3",         &g_pad_map[PADBTN_R3] },\n\t{ "Broadcast",     &g_broadcast_preset },\n',
     '\t{ "MapR3",         &g_pad_map[PADBTN_R3] },\n'),
    ('the RTC getter reads another row', P, 'int get_rtc_local_preset()     { return g_rtc_local_preset; }',
     'int get_rtc_local_preset()     { return g_exit_save_preset; }'),
    ('Exit Flash Save is not live', M, '\t&g_exit_save_preset,\n', ''),
    ('Broadcast is live', M, '\t&g_exit_save_preset,\n', '\t&g_exit_save_preset, &g_broadcast_preset,\n'),
    ('PAL before NTSC in the list', M, '{ "CONSOLE", "NTSC", "PAL", "DISC", NULL }', '{ "CONSOLE", "PAL", "NTSC", "DISC", NULL }'),
    ('GERMAN before ENGLISH in the list', M, '"JAPANESE", "ENGLISH", "GERMAN",', '"JAPANESE", "GERMAN", "ENGLISH",'),
    ('the FLASH / CLOCK page is not in the menu', M,
     '\t{ "FLASH / CLOCK", s_flash, (int)(sizeof(s_flash) / sizeof(s_flash[0])) },\n', ''),
    ('no [flashcfg] line at boot', M, '\tps3_flashcfg_print("boot", true);\n', ''),
    ('Console ID row has ON / OFF', M, '{ "Console ID",      &g_console_id_preset,    OPT_CHOICE, k_oldfly,',
     '{ "Console ID",      &g_console_id_preset,    OPT_CHOICE, k_onoff, '),
]

def main():
    shutil.rmtree(TMP, ignore_errors=True)
    for f in FILES:
        os.makedirs(os.path.dirname(os.path.join(TMP, f)), exist_ok=True)
        shutil.copy(os.path.join(ROOT, f), os.path.join(TMP, f))
    env = dict(os.environ, FLASH_SRC=TMP, FLASH_TMP='/tmp/flashmut_build')
    caught = 0
    for name, f, find, repl in MUTANTS:
        # Some sources keep CRLF line ends; the patterns and the mutated copy use LF.
        text = open(os.path.join(ROOT, f), encoding='utf-8', newline='').read().replace('\r\n', '\n')
        if text.count(find) != 1:
            print(f'BAD MUTANT (pattern found {text.count(find)} times): {name}')
            sys.exit(2)
        open(os.path.join(TMP, f), 'w', encoding='utf-8', newline='').write(text.replace(find, repl))
        r = subprocess.run(['sh', os.path.join(TMP, 'misc/flash_check/run.sh'), '120'], capture_output=True, text=True,
                           env=env)
        shutil.copy(os.path.join(ROOT, f), os.path.join(TMP, f))
        out = [l for l in (r.stdout + r.stderr).splitlines() if l.startswith(('FAIL', 'ERROR')) or 'error' in l]
        if r.returncode == 2:
            print(f'NO BUILD: {name}: {(out[0] if out else "")[:110]}')
            sys.exit(2)
        ok = r.returncode == 1
        caught += ok
        print(f"{'caught' if ok else 'MISSED'}: {name}: {(out[0] if out else '')[:110]}", flush=True)
    print(f'{caught} of {len(MUTANTS)} mutants fail run.sh')
    sys.exit(0 if caught == len(MUTANTS) else 1)

main()
