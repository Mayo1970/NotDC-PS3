// Plan section 7, bundle 13 (P58): the bit each MMUCR field has in the PS3 build. bitfield_ppu.sh compiles this
// with ppu-g++ -O2 -S; each function folds to a constant, which the script reads from the assembly.
#include "dc/sh4/ccn.h"

#define BF(name, word, field) \
	u32 bf_##name() { CCN_MMUCR_type t; t.reg_data = word; return t.field; }

extern "C" {
// The bitfield, one set bit per word: 1 = the field reads that bit.
BF(at_bit0, 0x00000001, AT)
BF(at_bit4, 0x00000010, AT)
BF(ti_bit2, 0x00000004, TI)
BF(ti_bit6, 0x00000040, TI)
BF(sv_bit8, 0x00000100, SV)
BF(sv_bit12, 0x00001000, SV)
BF(sqmd_bit9, 0x00000200, SQMD)
BF(sqmd_bit13, 0x00002000, SQMD)
// URC as a whole: the manual's bits 15-10, and the bits 19-14 that P58 names.
BF(urc_15_10, 0x0000FC00, URC)
BF(urc_19_14, 0x000FC000, URC)
BF(urb_23_18, 0x00FC0000, URB)
BF(urb_25_20, 0x03F00000, URB)
BF(lrui_31_26, 0xFC000000, LRUI)
// The self-test word of ccn.cpp, through the bitfield and through the masks.
BF(self_at, 0x0000A905, AT)
BF(self_ti, 0x0000A905, TI)
BF(self_urc, 0x0000A905, URC)
u32 mk_self_at()  { return MMUCR_AT(0x0000A905u); }
u32 mk_self_ti()  { return MMUCR_TI(0x0000A905u); }
u32 mk_self_urc() { return MMUCR_URC(0x0000A905u); }
}
