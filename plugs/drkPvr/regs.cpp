#include "regs.h"
#include "Renderer_if.h"
#include "ta.h"
#include "spg.h"
#include "dc/sh4/sys_gate.h"
#include "dc/pvr/pvr_vram.h"

// Basic PVR register emulation for the PowerVR CLX2 ("Holly"): a flat 32KB byte array, accessed via PvrReg().
// TODO: full TA emulation is still needed for accurate rendering.

// PVR X-scaler preset (ps3/ps3_presets.cpp).
extern "C" int get_x_scaler_preset();

u8 regs[RegSize];

u32 FASTCALL libPvr_ReadReg(u32 addr, u32 size)
{
	// The PVR only supports 32-bit aligned register accesses.
	// Sub-word reads are undefined on hardware; assert in debug builds.
	verify(size == 4);
	if (size != 4)
		return 0;

	if (g_sys_gate_on && (addr & RegMask) == SPG_STATUS_addr)
		return spg_read_status();

	return PvrReg(addr, u32);
}

// Plan 5.36: registers the SYS GATE's line plan reads (CalculateSync and the event lines).
static bool spg_plan_reg(u32 addr)
{
	switch (addr)
	{
		case FB_R_CTRL_addr:
		case SPG_VBLANK_INT_addr:
		case SPG_CONTROL_addr:
		case SPG_LOAD_addr:
		case SPG_VBLANK_addr:
		case SCALER_CTL_addr:
			return true;
		default:
			return false;
	}
}

static void pvr_write_reg(u32 addr, u32 data);

// P26: the shown field's bytes (the second field when interlaced), as Flycast's check_framebuffer_write.
void fb_watch_update()
{
	const u32 size = (((FB_R_SIZE >> 10) & 0x3FF) + 1) * ((FB_R_SIZE & 0x3FF) + ((FB_R_SIZE >> 20) & 0x3FF)) * 4;
	fb_watch_set(SPG_CONTROL.interlace ? FB_R_SOF2 : FB_R_SOF1, size);
}

void FASTCALL libPvr_WriteReg(u32 paddr, u32 data, u32 size)
{
	// Same constraint as reads -- PVR registers are 32-bit only.
	verify(size == 4);
	if (size != 4)
		return;

	const u32 addr = paddr & RegMask;

	// With the SYS GATE, the skipped slices run at the old timing; the next slice end plans again.
	if (g_sys_gate_on && spg_plan_reg(addr))
	{
		sys_gate_sync_slow();
		pvr_write_reg(addr, data);
		sys_gate_kick_slow();
		return;
	}
	pvr_write_reg(addr, data);
}

static void pvr_write_reg(u32 addr, u32 data)
{
	switch (addr)
	{
		// ---- Read-only registers: ignore writes ----
		case ID_addr:
		case REVISION_addr:
			return;

		// ---- Trigger: start the ISP/TSP rendering pipeline ----
		case STARTRENDER_addr:
			rend_start_render();
			return;

		// ---- TA_LIST_INIT: bit 31 triggers TA initialisation ----
		case TA_LIST_INIT_addr:
			if (data >> 31)
			{
				rend_list_init();
				data = 0;  // hardware clears the register after triggering
			}
			break;

		// ---- SOFTRESET: bits 0-2 control individual reset lines ----
		// bit 0 = TA soft reset, bit 1 = core (ISP/TSP) soft reset, bit 2 = SDR interface reset (ignored in emulation)
		case SOFTRESET_addr:
			if (data != 0)
			{
				if (data & 1)
					rend_list_srst();
				// bits 1 and 2 accepted but not currently emulated
				data = 0;  // hardware self-clears
			}
			break;

		// ---- TA_LIST_CONT: a write (any value) resumes TA list processing ----
		// The value is not stored; the write is purely a trigger.
		case TA_LIST_CONT_addr:
			rend_list_cont();
			return;  // do NOT write to register array

		// ---- Registers that require video-sync recalculation ----
		case FB_R_CTRL_addr:
		case SPG_CONTROL_addr:
		case SPG_LOAD_addr:
			PvrReg(addr, u32) = data;
			CalculateSync();
			return;

		// ---- SPG_VBLANK: vblank fires when the scanline counter (wraps at SPG_LOAD.vcount+1) equals vbstart ----
		// That event drives the FPS stats line, speed limiter and rend_vblank, so a vbstart past the wrap never fires.
		case SPG_VBLANK_addr:
			PvrReg(addr, u32) = data;
			{
				// Diagnostic hook: log writes here to catch a game setting vbstart past the wrap point.
			}
			return;

		// ---- FB_R_SIZE: P26, a new size clears the dirty mark and moves the watch (Flycast) ----
		case FB_R_SIZE_addr:
			if (PvrReg(addr, u32) != data)
			{
				PvrReg(addr, u32) = data;
				g_fb_dirty = false;
				fb_watch_update();
			}
			return;

		// ---- SCALER_CTL: hscale feeds the canvas width (Omicron / Wacky Races: 1280-wide 2:1 SSAA, SPG.cpp) ----
		// Only with the x_scaler preset on: legacy never resynced on this write; preset-off must stay bit-identical.
		case SCALER_CTL_addr:
			PvrReg(addr, u32) = data;
			if (get_x_scaler_preset())
				CalculateSync();
			return;

		default:
			// Palette RAM writes: track which palette entries have changed
			// so the renderer can invalidate cached palette textures.
			if (addr >= PALETTE_RAM_START_addr && addr <= PALETTE_RAM_END_addr)
			{
				if (PvrReg(addr, u32) != data)
				{
					// TODO: palette dirty tracking (bump the revisions of this entry's 16- and 256-entry banks).
				}
			}
			break;
	}

	PvrReg(addr, u32) = data;
}

bool Regs_Init()
{
	// Zero all registers so we start from a clean, deterministic state
	// before Regs_Reset() applies the hardware power-on defaults.
	memset(regs, 0, sizeof(regs));
	return true;
}

void Regs_Term()
{
	// Nothing to free -- regs[] is a static array.
}

void Regs_Reset(bool Manual)
{
	// Power-on default values taken from the Dreamcast hardware manual.
	ID                  = 0x17FD11DB;
	REVISION            = 0x00000011;
	SOFTRESET           = 0x00000007;
	SPG_HBLANK_INT.full = 0x031D0000;
	SPG_VBLANK_INT.full = 0x01500104;
	FPU_PARAM_CFG       = 0x0007DF77;
	HALF_OFFSET         = 0x00000007;
	ISP_FEED_CFG        = 0x00402000;
	SDRAM_REFRESH       = 0x00000020;
	SDRAM_ARB_CFG       = 0x0000001F;
	SDRAM_CFG           = 0x15F28997;
	SPG_HBLANK.full     = 0x007E0345;
	SPG_LOAD.full       = 0x01060359;
	SPG_VBLANK.full     = 0x01500104;
	SPG_WIDTH.full      = 0x07F1933F;
	VO_CONTROL          = 0x00000108;
	VO_STARTX           = 0x0000009D;
	VO_STARTY           = 0x00000015;
	SCALER_CTL.full     = 0x00000400;
	FB_BURSTCTRL        = 0x00090639;
	PT_ALPHA_REF        = 0x000000FF;
}
