// SCI, the SH4's serial port without FIFO; the Dreamcast does not use it. On-chip Regs FLYCAST (P67, Flycast
// 4d815570d): the 7 registers exist with their reset values; OLD has none, so a read prints a line and returns 0.

#include "types.h"
#include "dc/mem/sh4_internal_reg.h"
#include "ps3/ps3_presets.h"
#include "sci.h"

static u8 SCI_SCSMR1, SCI_SCBRR1, SCI_SCSCR1, SCI_SCTDR1, SCI_SCSSR1, SCI_SCRDR1, SCI_SCSPTR1;

static void sci_reg(u32 addr, u8* data, u32 flags, RegWriteFP* wr)
{
	RegisterStruct& rs = SCI[(addr & 0xFF) >> 2];
	rs.flags         = REG_8BIT_READWRITE | flags;
	rs.readFunction  = 0;
	rs.writeFunction = wr;
	rs.data8         = data;
}

// Flycast's write masks: SCSSR1's TEND and MPB are read-only; SCRDR1 is read-only.
static void write_SCSSR1(u32 data)  { SCI_SCSSR1 = (u8)(data & 0xF9); }
static void write_SCSPTR1(u32 data) { SCI_SCSPTR1 = (u8)(data & 0x8F); }
static void write_SCRDR1(u32)       {}

void sci_Init()
{
	if (!g_onchip_regs_preset)
		return;
	sci_reg(SCI_SCSMR1_addr,  &SCI_SCSMR1,  REG_READ_DATA | REG_WRITE_DATA, 0);
	sci_reg(SCI_SCBRR1_addr,  &SCI_SCBRR1,  REG_READ_DATA | REG_WRITE_DATA, 0);
	sci_reg(SCI_SCSCR1_addr,  &SCI_SCSCR1,  REG_READ_DATA | REG_WRITE_DATA, 0);
	sci_reg(SCI_SCTDR1_addr,  &SCI_SCTDR1,  REG_READ_DATA | REG_WRITE_DATA, 0);
	sci_reg(SCI_SCSSR1_addr,  &SCI_SCSSR1,  REG_READ_DATA, write_SCSSR1);
	sci_reg(SCI_SCRDR1_addr,  &SCI_SCRDR1,  REG_READ_DATA, write_SCRDR1);
	sci_reg(SCI_SCSPTR1_addr, &SCI_SCSPTR1, REG_READ_DATA, write_SCSPTR1);
}

// The manual's reset values: SCBRR1 and SCTDR1 0xFF, SCSSR1 0x84 (TDRE, TEND).
void sci_Reset(bool Manual)
{
	(void)Manual;
	SCI_SCSMR1  = 0;
	SCI_SCBRR1  = 0xFF;
	SCI_SCSCR1  = 0;
	SCI_SCTDR1  = 0xFF;
	SCI_SCSSR1  = 0x84;
	SCI_SCRDR1  = 0;
	SCI_SCSPTR1 = 0;
}

void sci_Term()
{
}
