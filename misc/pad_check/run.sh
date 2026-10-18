#!/bin/sh
# Plan section 7, bundle 10, and G15, PC check (WSL): pad_check on the real ps3_padmap.h, ps3_padports.h and
# maple_puru.h, then the lines that do not build on the PC. Exit 2: no build. PAD_SRC: a mutated copy (mutate.py).
cd "${PAD_SRC:-$(dirname "$0")/../..}" || exit 2
g++ -std=c++14 -O2 -Wall -fsanitize=undefined -fno-sanitize-recover=undefined -I. \
	-o /tmp/pad_check misc/pad_check/pad_check.cpp || exit 2
r=0
/tmp/pad_check || r=1
# The count of lines that hold the text, with CR line ends or without.
has() {
	if [ "$(tr -d '\r' < "$1" | grep -cF -- "$3")" != "$2" ]; then
		echo "FAIL: $1 does not have $2 line(s): $3"
		r=1
	fi
}
# The count of lines that hold A and whose next line holds B.
pair() {
	n=$(tr -d '\r' < "$1" | awk -v a="$3" -v b="$4" 'p && index($0, b) { n++ } { p = index($0, a) } END { print n + 0 }')
	if [ "$n" != "$2" ]; then
		echo "FAIL: $1 does not have $2 time(s) the line / the next line: $3 / $4"
		r=1
	fi
}
I=ps3/ps3_input.cpp
# G15: the ports follow the pads (ps3_padports.h); a port that loses its pad stops the motor first.
has $I 1 'ioPadGetInfo('
has $I 0 'ps3_find_pad'
has $I 1 'if (padports_lost(ps3_pad_of_dc, info.status, MAX_PORT_NUM, cap, port))'
pair $I 1 '// P13: the pad that leaves this port must not keep its motor on.' 'rumble_send(port, 0.0f);'
pair $I 1 'rumble_send(port, 0.0f);' 'ps3_pad_of_dc[port] = -1;'
has $I 1 'padports_fill(ps3_pad_of_dc, info.status, MAX_PORT_NUM, cap);'
has $I 1 'return padports_mask(ps3_pad_of_dc);'
has $I 1 'const int pad = ps3_pad_ok ? ps3_pad_of_dc[port] : -1;'
has $I 1 'padmap_apply(g_pad_map, ana, &kcode[port], &l, &r);'
has $I 1 'if (d->BTN_L2 && l2 == 0) l2 = 255;'
has $I 1 'if (d->BTN_R2 && r2 == 0) r2 = 255;'
has $I 1 'ana[PADBTN_L2] = l2;'
has $I 1 'ana[PADBTN_R2] = r2;'
has $I 1 'if (l) kcode[port] &= ~key_CONT_D;'
has $I 1 'if (r) kcode[port] &= ~key_CONT_C;'
has $I 1 'if (!g_stick_deadzone_preset)'
has $I 1 '*x = ClampAnalogValue(h);'
has $I 1 'pad_stick_radial(h, v, &ox, &oy);'
has $I 1 'ps3_stick(d->ANA_L_H, d->ANA_L_V, &joyx[port], &joyy[port]);'
has $I 1 'if (get_controller_type() == 5)'
has $I 1 'ps3_stick(d->ANA_R_H, d->ANA_R_V, &joyrx[port], &joyry[port]);'
has $I 1 'joyrx[port] = joyry[port] = 0;'
has $I 1 'act.large_motor = level;'
has $I 3 'rumble_send(port, 0.0f);'
has $I 1 'rumble_update(port);'
has $I 1 'if (left_ms <= 0.0)'
D=dc/maple/maple_devs.cpp
has $D 1 'virtual u32 caps() { return 0xfe060f00; }'
has $D 1 'virtual u32 caps() { return 0xffff3f00; }'
has $D 1 'const char* maple_full_controller_name = "Dreamcast PantherDC Controller";'
has $D 1 'w8(second_stick() ? pjs.joy[PJAI_X2] : 0x80);'
has $D 1 'w8(second_stick() ? pjs.joy[PJAI_Y2] : 0x80);'
has $D 1 'AST=(r32()>>16)&0xFF;'
has $D 1 'VIBSET=r32();'
has $D 1 'w32(0x3B07E010);	// vibration specs'
has $D 1 'wptr(extra,80);'
C=dc/maple/maple_cfg.cpp
has $C 1 'pjs->kcode = kcode[dev->bus_id] | 0xF901;'
has $C 1 'pjs->joy[PJAI_X2] = SignedToByte(joyrx[dev->bus_id]);'
has $C 1 'pjs->joy[PJAI_Y2] = SignedToByte(joyry[dev->bus_id]);'
has $C 1 'case 5:  return MDT_FullController;'
has $C 1 'bool isStandard = (ctrlType == MDT_SegaController || ctrlType == MDT_FullController);'
# G15: each port's two slot rows, 1 = a VMU, 2 = the pack; the old Rumble Pack row is gone.
has $C 1 'const int dev = get_slot_dev_preset(bus * 2 + slot);'
has $C 1 'mcfg_Create(dev == 1 ? MDT_SegaVMU : MDT_PuruPuru, bus, slot);'
has $C 0 'g_rumble_pack_preset'
P=ps3/ps3_presets.cpp
# The defaults: every row at its old path until its HW run. G15: a VMU in every slot.
has $P 1 'int g_controller_type      = 0;'
has $P 1 'int g_stick_deadzone_preset = 0;'
has $P 1 'int g_slot_dev_preset[8]   = { 1, 0, 1, 0, 1, 0, 1, 0 };'
has $P 1 'int g_player_count         = 1;'
has $P 0 'RumblePack'
has $P 1 'int g_pad_map[PADBTN_COUNT] = PADMAP_DEFAULTS;'
has $P 1 'int get_controller_type()      { return g_controller_type == 1 ? 5 : 0; }'
has $P 1 'int get_slot_dev_preset(u32 slot) { return slot < 8 ? g_slot_dev_preset[slot] : 0; }'
has $P 2 '{ "SlotA1",        &g_slot_dev_preset[0] },'
has $P 2 '{ "SlotA2",        &g_slot_dev_preset[1] },'
has $P 2 '{ "SlotB1",        &g_slot_dev_preset[2] },'
has $P 2 '{ "SlotB2",        &g_slot_dev_preset[3] },'
has $P 2 '{ "SlotC1",        &g_slot_dev_preset[4] },'
has $P 2 '{ "SlotC2",        &g_slot_dev_preset[5] },'
has $P 2 '{ "SlotD1",        &g_slot_dev_preset[6] },'
has $P 2 '{ "SlotD2",        &g_slot_dev_preset[7] },'
M=ps3/ps3_menu.cpp
has $M 1 '{ "Controller",      &g_controller_type,      OPT_CHOICE, k_pad_type,  0, 0,0,0, NULL,'
has $M 1 '{ "Stick Dead Zone", &g_stick_deadzone_preset, OPT_CHOICE, k_deadzone, 0, 0,0,0, NULL,'
has $M 0 'Rumble Pack'
has $M 1 'static const char* k_slotdev[]   = { "NONE", "VMU", "RUMBLE", NULL };'
has $M 1 '{ "Port A Slot 1",   &g_slot_dev_preset[0],   OPT_CHOICE, k_slotdev,   0, 0,0,0, NULL, k_slot_help, 1 },'
has $M 1 '{ "Port A Slot 2",   &g_slot_dev_preset[1],   OPT_CHOICE, k_slotdev,   0, 0,0,0, NULL, k_slot_help, 1 },'
has $M 1 '{ "Port B Slot 1",   &g_slot_dev_preset[2],   OPT_CHOICE, k_slotdev,   0, 0,0,0, NULL, k_slot_help, 2 },'
has $M 1 '{ "Port B Slot 2",   &g_slot_dev_preset[3],   OPT_CHOICE, k_slotdev,   0, 0,0,0, NULL, k_slot_help, 2 },'
has $M 1 '{ "Port C Slot 1",   &g_slot_dev_preset[4],   OPT_CHOICE, k_slotdev,   0, 0,0,0, NULL, k_slot_help, 3 },'
has $M 1 '{ "Port C Slot 2",   &g_slot_dev_preset[5],   OPT_CHOICE, k_slotdev,   0, 0,0,0, NULL, k_slot_help, 3 },'
has $M 1 '{ "Port D Slot 1",   &g_slot_dev_preset[6],   OPT_CHOICE, k_slotdev,   0, 0,0,0, NULL, k_slot_help, 4 },'
has $M 1 '{ "Port D Slot 2",   &g_slot_dev_preset[7],   OPT_CHOICE, k_slotdev,   0, 0,0,0, NULL, k_slot_help, 4 },'
# The slot rows are set before boot: in the CONTROLS rows only, not in the live list.
has $M 8 '&g_slot_dev_preset'
# A port's rows show only while Players reaches it; the cursor and the scroll go over the rows shown.
has $M 1 'if (!s_controls[i].port1 || s_controls[i].port1 <= get_player_count())'
has $M 1 's_ctl_row = rows[pos > 0 ? pos - 1 : n - 1];'
has $M 1 's_ctl_row = rows[pos < n - 1 ? pos + 1 : 0];'
has $M 1 'draw_row(k - s_ctl_top, rows[k] == s_ctl_row, COL_TEXT, o->label, val);'
pair $M 1 'if (s_ctl_top > n - vis)' 's_ctl_top = n > vis ? n - vis : 0;'
has $M 15 'OPT_CHOICE, k_padmap, 0, 0,0,0, NULL, k_map_help },'
has $M 1 'static const char* k_padmap[]    = { "NONE", "A", "B", "X", "Y", "START", "D-PAD UP", "D-PAD DOWN", "D-PAD LEFT",'
has $M 1 '                                     "D-PAD RIGHT", "L TRIGGER", "R TRIGGER", NULL };'
has $M 1 'ps3_rumble_stop();'
[ $r = 0 ] && echo "PASS: 81 source line checks"
exit $r
