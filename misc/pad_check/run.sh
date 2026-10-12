#!/bin/sh
# Plan section 7, bundle 10, PC check (WSL): pad_check on the real ps3_padmap.h and maple_puru.h, then the lines
# that do not build on the PC. Exit 2: no build. PAD_SRC: a mutated copy of the tree (mutate.py).
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
I=ps3/ps3_input.cpp
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
has $C 1 'MapleDeviceType slot2 = g_rumble_pack_preset ? MDT_PuruPuru : MDT_SegaVMU;'
has $C 1 'bool isStandard = (ctrlType == MDT_SegaController || ctrlType == MDT_FullController);'
P=ps3/ps3_presets.cpp
# The defaults: every row at its old path until its HW run.
has $P 1 'int g_controller_type      = 0;'
has $P 1 'int g_stick_deadzone_preset = 0;'
has $P 1 'int g_rumble_pack_preset   = 0;'
has $P 1 'int g_pad_map[PADBTN_COUNT] = PADMAP_DEFAULTS;'
has $P 1 'int get_controller_type()      { return g_controller_type == 1 ? 5 : 0; }'
M=ps3/ps3_menu.cpp
has $M 1 '{ "Controller",      &g_controller_type,      OPT_CHOICE, k_pad_type,  0, 0,0,0, NULL,'
has $M 1 '{ "Stick Dead Zone", &g_stick_deadzone_preset, OPT_CHOICE, k_deadzone, 0, 0,0,0, NULL,'
has $M 1 '{ "Rumble Pack",     &g_rumble_pack_preset,   OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,'
has $M 15 'OPT_CHOICE, k_padmap, 0, 0,0,0, NULL, k_map_help },'
has $M 1 'static const char* k_padmap[]    = { "NONE", "A", "B", "X", "Y", "START", "D-PAD UP", "D-PAD DOWN", "D-PAD LEFT",'
has $M 1 '                                     "D-PAD RIGHT", "L TRIGGER", "R TRIGGER", NULL };'
has $M 1 'ps3_rumble_stop();'
[ $r = 0 ] && echo "PASS: 45 source line checks"
exit $r
