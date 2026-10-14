// sys20_check: dc/mem/sh4_internal_reg.cpp whole (a copy with LF ends), and entry points into its inline parts.
#include "sh4_internal_reg.cpp"

static u32 s_r32;
static u16 s_r16;
static u8 s_r8;
static Array<RegisterStruct> s_t(3, true);

// Three registers of 32, 16 and 8 bits for RegSRead / RegSWrite.
void s20_regs_init()
{
	s_t[0].flags = REG_32BIT_READWRITE | REG_READ_DATA | REG_WRITE_DATA;
	s_t[0].data32 = &s_r32;
	s_t[1].flags = REG_16BIT_READWRITE | REG_READ_DATA | REG_WRITE_DATA;
	s_t[1].data16 = &s_r16;
	s_t[2].flags = REG_8BIT_READWRITE | REG_READ_DATA | REG_WRITE_DATA;
	s_t[2].data8 = &s_r8;
}
u32 s20_regs_get(u32 i) { return i == 0 ? s_r32 : i == 1 ? s_r16 : s_r8; }
void s20_regs_set(u32 i, u32 v)
{
	if (i == 0)
		s_r32 = v;
	else if (i == 1)
		s_r16 = (u16)v;
	else
		s_r8 = (u8)v;
}
u32 s20_regs_read(u32 i, u32 sz)
{
	return sz == 1 ? RegSRead<1>(s_t, i * 4) : sz == 2 ? RegSRead<2>(s_t, i * 4) : RegSRead<4>(s_t, i * 4);
}
void s20_regs_write(u32 i, u32 sz, u32 v)
{
	if (sz == 1)
		RegSWrite<1>(s_t, i * 4, v);
	else if (sz == 2)
		RegSWrite<2>(s_t, i * 4, v);
	else
		RegSWrite<4>(s_t, i * 4, v);
}
u32 s20_sci_read(u32 addr) { return RegSRead<1>(SCI, addr & 0xFF); }
void s20_sci_write(u32 addr, u32 v) { RegSWrite<1>(SCI, addr & 0xFF, v); }
void s20_area7_init() { map_area7_init(); }
void s20_area7(u32 base) { map_area7(base); }
u32 s20_area7_handler() { return area7_handler; }
u32 s20_ocr_handler() { return area7_orc_handler; }
