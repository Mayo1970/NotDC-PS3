// SH4 Bus State Controller registers: init/reset/term, GPIO port A (PCTRA/PDTRA) with the
// cable-type detection, and a port B (PDTRB) stub that Dreamcast software never uses.

#include "types.h"
#include "dc/mem/sh4_internal_reg.h"
#include "ps3/ps3_presets.h"
#include "bsc.h"

// -- Register instances (definitions – declarations are in bsc.h via extern) ---

BCR1_type   BSC_BCR1;   // 32-bit bus control 1
BCR2_type   BSC_BCR2;   // 16-bit bus control 2  (A0SZ0_inp/A0SZ1_inp read-only)
WCR1_type   BSC_WCR1;   // 32-bit wait control 1
WCR2_type   BSC_WCR2;   // 32-bit wait control 2
WCR3_type   BSC_WCR3;   // 32-bit wait control 3
MCR_type    BSC_MCR;    // 32-bit memory control
PCR_type    BSC_PCR;    // 16-bit PCMCIA control
RTCSR_type  BSC_RTCSR;  // 16-bit refresh timer control/status
RTCNT_type  BSC_RTCNT;  // 16-bit refresh timer counter
RTCOR_type  BSC_RTCOR;  // 16-bit refresh timer constant
RFCR_type   BSC_RFCR;   // 16-bit refresh count
PCTRA_type  BSC_PCTRA;  // 32-bit port A control
PDTRA_type  BSC_PDTRA;  // 16-bit port A data
PCTRB_type  BSC_PCTRB;  // 32-bit port B control
PDTRB_type  BSC_PDTRB;  // 16-bit port B data
GPIOIC_type BSC_GPIOIC; // 16-bit GPIO interrupt control

// GPIO port A: vreg_2 = latch of the driven output pins; mask_read / mask_pull_up / mask_write =
// pins set as input / input with pull-up / output.
static u32 vreg_2      = 0;
static u32 mask_read   = 0;
static u32 mask_pull_up = 0;
static u32 mask_write  = 0;

// PCTRA write: pin i has bit 2i = output (1) / input (0) and bit 2i+1 = no pull-up (1). Rebuilds
// the masks and keeps vreg_2 so output pins hold the last PDTRA value.
void write_BSC_PCTRA(u32 data)
{
  //printf("C:BSC_PCTRA = %08X\n",data);
  
    mask_read    = 0;
    mask_pull_up = 0;
    mask_write   = 0;

    for (int i = 0; i < 16; i++)
    {
        const u32 pin_bit  = (1u << i);
        const u32 mode     = (data >> (i << 1)) & 3;
        const bool is_out  = (mode & 1) != 0;   // IO bit set  → output
        const bool pull_up = (mode & 2) == 0;   // PUP bit clr → pull-up

        if (is_out)
        {
            mask_write   |= pin_bit;
            // Force PUP bit high in register value for output pins
            // (SH4 manual: output pins ignore pull-up; mark as no pull-up)
            data |= (2u << (i << 1));
        }
        else
        {
            mask_read |= pin_bit;
            if (pull_up)
                mask_pull_up |= pin_bit;
        }
    }

    BSC_PCTRA.full = data;

    // Sync output latch: clear output bits then apply current PDTRA value
    vreg_2 = (vreg_2 & ~mask_write) | (BSC_PDTRA.full & mask_write);
}

// PDTRA write: stores the low 16 bits and updates the latch of the output pins.
void write_BSC_PDTRA(u32 data)
{
    BSC_PDTRA.full = (u16)data;
    vreg_2 = (vreg_2 & ~mask_write) | (BSC_PDTRA.full & mask_write);
}

extern int g_cable_override; // dc.cpp apply_disc_meta

// Port A read: the BIOS probes PCTRA/PDTRA pin setups for the cable (Chankast's findings);
// bits [9:8] carry the cable type (0 VGA, 2 RGB, 3 composite).
u32 read_BSC_PDTRA()
{
    const u32 pctra_lo = BSC_PCTRA.full & 0xF;
    const u32 pdtra_lo = BSC_PDTRA.full & 0xF;

    u32 result = 0;

    if (pctra_lo == 0x8)
    {
        // PB1: input, no-pull-up — PB0: input, pull-up
        // Both cable-sense lines read high
        result = 3;
    }
    else if (pctra_lo == 0xB)
    {
        // PB1: input, no-pull-up — PB0: output, no-pull-up
        // Lines read high unless PB0 is being driven low (pdtra bit 1 set)
        result = (pdtra_lo == 2) ? 0 : 3;
    }
    else if (pctra_lo == 0xC && pdtra_lo == 2)
    {
        // PB1: output, no-pull-up driven high — PB0: input, no-pull-up
        result = 3;
    }
    // else result stays 0

    // Overlay the configured cable type into bits [9:8]
    result |= (u32)(g_cable_override >= 0 ? g_cable_override : (int)settings.dreamcast.cable) << 8;

    return result;
}

// -- read_BSC_PDTRB – Port B data register read (not used on Dreamcast) ------
u32 read_BSC_PDTRB()
{
    die("read_BSC_PDTRB: unexpected read – port B not used on Dreamcast");
    return 0;
}

// On-chip Regs FLYCAST (P78, Flycast bsc.cpp): RFCR is a fixed 17 that drops writes, so a wait on a refresh count
// ends; OLD keeps a plain register.
static void write_BSC_RFCR_dropped(u32)
{
}

// bsc_Init helper: one register-table entry for a plain data-backed register.
#define BSC_REG_INIT_DATA(addr, bits, dataptr)                                  \
    do {                                                                         \
        const u32 _idx = ((addr) & 0xFF) >> 2;                                  \
        BSC[_idx].flags         = REG_##bits##BIT_READWRITE | REG_READ_DATA | REG_WRITE_DATA; \
        BSC[_idx].readFunction  = 0;                                             \
        BSC[_idx].writeFunction = 0;                                             \
        BSC[_idx].data##bits    = (dataptr);                                     \
    } while (0)

// -- bsc_Init – wire up every BSC register into the register dispatch table ---
void bsc_Init()
{
    // --- Simple data-backed registers (read and write go straight to the var) ---

    // BCR1  0xFF800000  32-bit  reset=0x00000000
    BSC_REG_INIT_DATA(BSC_BCR1_addr, 32, &BSC_BCR1.full);

    // BCR2  0xFF800004  16-bit  reset=0x3FFC
    BSC_REG_INIT_DATA(BSC_BCR2_addr, 16, &BSC_BCR2.full);

    // WCR1  0xFF800008  32-bit  reset=0x77777777
    BSC_REG_INIT_DATA(BSC_WCR1_addr, 32, &BSC_WCR1.full);

    // WCR2  0xFF80000C  32-bit  reset=0xFFFEEFFF
    BSC_REG_INIT_DATA(BSC_WCR2_addr, 32, &BSC_WCR2.full);

    // WCR3  0xFF800010  32-bit  reset=0x07777777
    BSC_REG_INIT_DATA(BSC_WCR3_addr, 32, &BSC_WCR3.full);

    // MCR   0xFF800014  32-bit  reset=0x00000000
    BSC_REG_INIT_DATA(BSC_MCR_addr, 32, &BSC_MCR.full);

    // PCR   0xFF800018  16-bit  reset=0x0000
    BSC_REG_INIT_DATA(BSC_PCR_addr, 16, &BSC_PCR.full);

    // RTCSR 0xFF80001C  16-bit  reset=0x0000
    BSC_REG_INIT_DATA(BSC_RTCSR_addr, 16, &BSC_RTCSR.full);

    // RTCNT 0xFF800020  16-bit  reset=0x0000
    BSC_REG_INIT_DATA(BSC_RTCNT_addr, 16, &BSC_RTCNT.full);

    // RTCOR 0xFF800024  16-bit  reset=0x0000
    BSC_REG_INIT_DATA(BSC_RTCOR_addr, 16, &BSC_RTCOR.full);

    // RFCR  0xFF800028  16-bit  reset=0x0000
    BSC_REG_INIT_DATA(BSC_RFCR_addr, 16, &BSC_RFCR.full);
    if (g_onchip_regs_preset)
    {
        BSC[(BSC_RFCR_addr & 0xFF) >> 2].flags         = REG_16BIT_READWRITE | REG_READ_DATA;
        BSC[(BSC_RFCR_addr & 0xFF) >> 2].writeFunction = write_BSC_RFCR_dropped;
    }

    // PCTRB 0xFF800040  32-bit  reset=0x00000000
    BSC_REG_INIT_DATA(BSC_PCTRB_addr, 32, &BSC_PCTRB.full);

    // GPIOIC 0xFF800048  16-bit  reset=0x0000
    BSC_REG_INIT_DATA(BSC_GPIOIC_addr, 16, &BSC_GPIOIC.full);

    // --- Registers with custom handlers ---

    // PCTRA 0xFF80002C  32-bit  – read from register, write via handler
    {
        const u32 idx = (BSC_PCTRA_addr & 0xFF) >> 2;
        BSC[idx].flags         = REG_32BIT_READWRITE | REG_READ_DATA;
        BSC[idx].readFunction  = 0;
        BSC[idx].writeFunction = write_BSC_PCTRA;
        BSC[idx].data32        = &BSC_PCTRA.full;
    }

    // PDTRA 0xFF800030  16-bit  – fully custom read/write (cable detection)
    // data pointer is null: handlers manage the register directly
    {
        const u32 idx = (BSC_PDTRA_addr & 0xFF) >> 2;
        BSC[idx].flags         = REG_16BIT_READWRITE;
        BSC[idx].readFunction  = read_BSC_PDTRA;
        BSC[idx].writeFunction = write_BSC_PDTRA;
        BSC[idx].data16        = 0;
    }

    // PDTRB 0xFF800044  16-bit  – read causes fatal (not used on Dreamcast)
    {
        const u32 idx = (BSC_PDTRB_addr & 0xFF) >> 2;
        BSC[idx].flags         = REG_16BIT_READWRITE | REG_READ_DATA | REG_WRITE_DATA;
        BSC[idx].readFunction  = read_BSC_PDTRB;
        BSC[idx].writeFunction = 0;
        BSC[idx].data16        = &BSC_PDTRB.full;
    }
}

#undef BSC_REG_INIT_DATA

// Hardware reset values. PDTRA and PDTRB are "undefined" after reset, so they are left as-is.
void bsc_Reset(bool /*Manual*/)
{
    BSC_BCR1.full  = 0x00000000;
    BSC_BCR2.full  = 0x3FFC;
    BSC_WCR1.full  = 0x77777777;
    BSC_WCR2.full  = 0xFFFEEFFF;
    BSC_WCR3.full  = 0x07777777;

    BSC_MCR.full   = 0x00000000;
    BSC_PCR.full   = 0x0000;
    BSC_RTCSR.full = 0x0000;
    BSC_RTCNT.full = 0x0000;
    BSC_RTCOR.full = 0x0000;
    BSC_RFCR.full  = g_onchip_regs_preset ? 17 : 0x0000;

    BSC_PCTRA.full = 0x00000000;
    // BSC_PDTRA – undefined after reset; do not assign

    BSC_PCTRB.full = 0x00000000;
    // BSC_PDTRB – undefined after reset; do not assign

    BSC_GPIOIC.full = 0x0000;

    // Also reset the GPIO port A state derived from PCTRA
    vreg_2       = 0;
    mask_read    = 0;
    mask_pull_up = 0;
    mask_write   = 0;
}

// -- bsc_Term – nothing to release; kept for symmetry with Init/Reset --------
void bsc_Term()
{
}