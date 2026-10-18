#!/usr/bin/env python3
# Plan section 7, bundle 10 (WSL): plants one bug at a time in a copy of the sources; run.sh must FAIL on each.
import os, shutil, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
TMP = '/tmp/padmut'
H, U, Q = 'ps3/ps3_padmap.h', 'dc/maple/maple_puru.h', 'ps3/ps3_padports.h'
I, D, C = 'ps3/ps3_input.cpp', 'dc/maple/maple_devs.cpp', 'dc/maple/maple_cfg.cpp'
M, P = 'ps3/ps3_menu.cpp', 'ps3/ps3_presets.cpp'
FILES = [H, U, Q, I, D, C, M, P, 'ps3/dc_pad_bits.h', 'misc/pad_check/pad_check.cpp', 'misc/pad_check/run.sh']

MUTANTS = [
    # The button map
    ('L1 is no trigger by default', H, 'PADMAP_LT, PADMAP_RT, PADMAP_LT, PADMAP_RT, PADMAP_NONE, PADMAP_NONE }',
     'PADMAP_NONE, PADMAP_RT, PADMAP_LT, PADMAP_RT, PADMAP_NONE, PADMAP_NONE }'),
    ('L3 is START by default', H, 'PADMAP_LT, PADMAP_RT, PADMAP_LT, PADMAP_RT, PADMAP_NONE, PADMAP_NONE }',
     'PADMAP_LT, PADMAP_RT, PADMAP_LT, PADMAP_RT, PADMAP_START, PADMAP_NONE }'),
    ('A and B swapped in the bit table', H, '0, key_CONT_A, key_CONT_B,', '0, key_CONT_B, key_CONT_A,'),
    ('the last button on a trigger wins', H, 'if (t == PADMAP_LT && ana[b] > *lt)', 'if (t == PADMAP_LT)'),
    ('RT raises LT', H, 'if (t == PADMAP_RT && ana[b] > *rt)\n\t\t\t*rt = ana[b];',
     'if (t == PADMAP_RT && ana[b] > *lt)\n\t\t\t*lt = ana[b];'),
    ('a value past the enum is not NONE', H, 'if (!ana[b] || t >= PADMAP_COUNT)', 'if (!ana[b])'),
    ('a button that is up acts', H, 'if (!ana[b] || t >= PADMAP_COUNT)', 'if (t >= PADMAP_COUNT)'),
    # The stick
    ('a dead zone of 20%', H, '#define PAD_DEADZONE 0.10f', '#define PAD_DEADZONE 0.20f'),
    ('no rescale of x', H, 'lroundf((nx - nx * k) / (1.0f - nx * k) * 128.0f)', 'lroundf(nx * 128.0f)'),
    ('no magnitude limit', H, 'if (mag > 128.0f * 128.0f)', 'if (mag > 1e9f)'),
    ('a limit of 100', H, 'mag = sqrtf(mag) / 128.0f;', 'mag = sqrtf(mag) / 100.0f;'),
    ('the sign of x is lost', H, 'ix = x < 0 ? -ix : ix > 127 ? 127 : ix;', 'ix = ix > 127 ? 127 : ix;'),
    ('the dead zone is per axis', H, 'const float r2 = nx * nx + ny * ny;', 'const float r2 = nx * nx;'),
    ('y takes the x value', H, '\t*oy = iy;', '\t*oy = ix;'),
    # The Puru Puru pack
    ('INH does not turn the slope', U, '\t\tinc = -inc;', '\t\tinc = +inc;'),
    ('the EXH test is the other way', U, 'else if (!(vibset & 0x0800))', 'else if (vibset & 0x0800)'),
    ('the power is the positive part only', U, '(pow_pos + pow_neg) / 7.0)', '(pow_pos) / 7.0)'),
    ('no cap at 1', U, '\tif (v.power > 1.0f)\n\t\tv.power = 1.0f;\n', ''),
    ('the length is not cut to the auto-stop time', U, '\t\tif (ms < (int)ast_ms)\n', '\t\tif (ms >= 0)\n'),
    ('FREQ from the wrong bits', U, 'freq = (vibset >> 16) & 0xFF;', 'freq = (vibset >> 17) & 0xFF;'),
    ('the slope without the power', U, '(float)(freq / (1000.0 * inc * pow_max))', '(float)(freq / (1000.0 * inc))'),
    ('CNT is not read', U, 'const bool cnt = (vibset & 1) != 0;', 'const bool cnt = false;'),
    # The ones below fail only run.sh's source lines: that code does not build on the PC.
    ('the standard pad reads the right stick', I, '\tif (get_controller_type() == 5)\n', ''),
    ('the dead zone row does nothing', I, '\tif (!g_stick_deadzone_preset)\n', '\tif (true)\n'),
    ('the triggers do not set C and D', I, '\tif (l) kcode[port] &= ~key_CONT_D;\n', ''),
    ('L2 without the digital bit rule', I, '\tif (d->BTN_L2 && l2 == 0) l2 = 255;\n', ''),
    ('the full controller keeps the old capability word', D, 'virtual u32 caps() { return 0xffff3f00; }',
     'virtual u32 caps() { return 0xfe060f00; }'),
    ('X2 is never sent', D, 'w8(second_stick() ? pjs.joy[PJAI_X2] : 0x80);', 'w8(0x80);'),
    ('the auto-stop time from the wrong byte', D, 'AST=(r32()>>16)&0xFF;', 'AST=(r32()>>8)&0xFF;'),
    ('the kcode mask is gone', C, 'pjs->kcode = kcode[dev->bus_id] | 0xF901;', 'pjs->kcode = kcode[dev->bus_id];'),
    ('a VMU slot holds the pack', C, 'mcfg_Create(dev == 1 ? MDT_SegaVMU : MDT_PuruPuru, bus, slot);',
     'mcfg_Create(dev == 1 ? MDT_PuruPuru : MDT_SegaVMU, bus, slot);'),
    ('the full controller has no VMU', C, '(ctrlType == MDT_SegaController || ctrlType == MDT_FullController);',
     '(ctrlType == MDT_SegaController);'),
    ('RADIAL by default', P, 'int g_stick_deadzone_preset = 0;', 'int g_stick_deadzone_preset = 1;'),
    ('a slot holds the pack by default', P, 'int g_slot_dev_preset[8]   = { 1, 0, 1, 0, 1, 0, 1, 0 };',
     'int g_slot_dev_preset[8]   = { 1, 2, 1, 0, 1, 0, 1, 0 };'),
    ('Players 4 by default', P, 'int g_player_count         = 1;', 'int g_player_count         = 4;'),
    ('slot 2 holds a VMU by default', P, 'int g_slot_dev_preset[8]   = { 1, 0, 1, 0, 1, 0, 1, 0 };',
     'int g_slot_dev_preset[8]   = { 1, 1, 1, 1, 1, 1, 1, 1 };'),
    ('the slot getter reads the other slot', P, 'return slot < 8 ? g_slot_dev_preset[slot] : 0;',
     'return slot < 8 ? g_slot_dev_preset[slot ^ 1] : 0;'),
    ('FULL by default', P, 'int g_controller_type      = 0;', 'int g_controller_type      = 1;'),
    ('the full controller is the light gun', P, 'return g_controller_type == 1 ? 5 : 0; }', 'return g_controller_type; }'),
    ('the menu does not stop the motors', M, '\tps3_rumble_stop();\n', ''),
    # G15: the slot rows
    ('RUMBLE comes before VMU', M, '{ "NONE", "VMU", "RUMBLE", NULL };', '{ "NONE", "RUMBLE", "VMU", NULL };'),
    ('Port B Slot 1 sets slot A1', M, '{ "Port B Slot 1",   &g_slot_dev_preset[2],', '{ "Port B Slot 1",   &g_slot_dev_preset[0],'),
    ('the slot rows are live', M, '\t&g_maple_frame_preset, &g_vmu_screen_preset,\n',
     '\t&g_maple_frame_preset, &g_vmu_screen_preset, &g_slot_dev_preset[0],\n'),
    ('every slot row shows', M, 'if (!s_controls[i].port1 || s_controls[i].port1 <= get_player_count())', 'if (true)'),
    ('the rows of one port too many show', M, 's_controls[i].port1 <= get_player_count())', 's_controls[i].port1 <= get_player_count() + 1)'),
    ('port B shows with port A', M, 'NULL, k_slot_help, 2 },\n\t{ "Port B Slot 2"', 'NULL, k_slot_help, 1 },\n\t{ "Port B Slot 2"'),
    ('UP goes over the hidden rows', M, 's_ctl_row = rows[pos > 0 ? pos - 1 : n - 1];', 's_ctl_row = s_ctl_row > 0 ? s_ctl_row - 1 : CONTROLS_ROWS - 1;'),
    ('the scroll leaves a gap when rows go', M, '\tif (s_ctl_top > n - vis)\n\t\ts_ctl_top = n > vis ? n - vis : 0;\n', ''),
    # G15: the ports follow the pads
    ('a port keeps a pad that left', Q, '(port >= cap || map[port] >= npads || !status[map[port]])',
     '(port >= cap || map[port] >= npads)'),
    ('a kept port is not held to Players', Q, '(port >= cap || map[port] >= npads ||', '(map[port] >= npads ||'),
    ('a new pad is not held to Players', Q, 'for (port = 0; port < cap && port < PADPORTS_DC; port++)',
     'for (port = 0; port < PADPORTS_DC; port++)'),
    ('a pad with a port takes one more', Q, '\t\tif (port < PADPORTS_DC)\n\t\t\tcontinue;\n', ''),
    ('a new pad takes every free port', Q, '\t\t\t\tmap[port] = pad;\n\t\t\t\tbreak;\n', '\t\t\t\tmap[port] = pad;\n'),
    ('the mask counts the empty ports', Q, 'if (map[port] >= 0)\n\t\t\tports |= 1u << port;', 'ports |= 1u << port;'),
    ('the pads take the ports from the last one', Q, 'for (int pad = 0; pad < npads; pad++)',
     'for (int pad = npads - 1; pad >= 0; pad--)'),
    ('a port that lost its pad keeps it', I, '\t\t\t\tps3_pad_of_dc[port] = -1;\n', ''),
    ('the motor stays on when its pad leaves', I,
     '\t\t\t\t// P13: the pad that leaves this port must not keep its motor on.\n\t\t\t\trumble_send(port, 0.0f);\n',
     '\t\t\t\t// P13: the pad that leaves this port must not keep its motor on.\n'),
    ('no new pads', I, '\t\tpadports_fill(ps3_pad_of_dc, info.status, MAX_PORT_NUM, cap);\n', ''),
]

def main():
    shutil.rmtree(TMP, ignore_errors=True)
    for f in FILES:
        os.makedirs(os.path.dirname(os.path.join(TMP, f)), exist_ok=True)
        shutil.copy(os.path.join(ROOT, f), os.path.join(TMP, f))
    caught = 0
    for name, f, find, repl in MUTANTS:
        # Some sources keep CRLF line ends; the patterns and the mutated copy use LF.
        text = open(os.path.join(ROOT, f), encoding='utf-8', newline='').read().replace('\r\n', '\n')
        if text.count(find) != 1:
            print(f'BAD MUTANT (pattern found {text.count(find)} times): {name}')
            sys.exit(2)
        open(os.path.join(TMP, f), 'w', encoding='utf-8', newline='').write(text.replace(find, repl))
        r = subprocess.run(['sh', os.path.join(TMP, 'misc/pad_check/run.sh')], capture_output=True, text=True,
                           env=dict(os.environ, PAD_SRC=TMP))
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
