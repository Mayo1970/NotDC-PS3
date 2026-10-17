#include "types.h"
#include "dc/asic/asic.h"
#include "dc/sh4/intc.h"
#include "dc/mem/sb.h"
#include "dc/sh4/dmac.h"
#include "dc/mem/sh4_mem.h"
#include "pvr_sb_regs.h"
#include "plugins/plugin_manager.h"
#include "dc/sh4/sh4_sched.h"   // SCHED preset: deadline-ordered PVR-DMA completion
#include "dc/sh4/rec_v2/jit_dma.h"

extern "C" int get_dma_fix_preset();

// SCHED preset: the PVR-DMA end IRQ goes through the deadline scheduler, in order with the
// ch2/GD-ROM/AICA completions (Rez's 89 KB texture upload uses this channel).
extern "C" int get_sched_preset();

static int pvr_dma_schid = -1;

// Small transfer-finish latency (SH4 cycles); only needs to be >0 so the
// completion passes through the ordered queue instead of firing inline.
#define PVR_DMA_IRQ_DELAY 200

static int pvr_dma_end_cb(int tag, int sch_cycl, int jitter)
{
	asic_RaiseInterrupt(holly_PVR_DMA);
	return 0;   // single-shot
}

// --- Constants ---

// DMA alignment requirements
static const u32 DMA_ALIGNMENT_MASK = 0x1F;
static const u32 DMA_ALIGNMENT_SIZE = 32;

// Expected DMAOR configuration
static const u32 EXPECTED_DMAOR = 0x8201;

// Link list termination values
static const u32 LINK_ADDR_END = 1;
static const u32 LINK_ADDR_RESTART = 2;

// Link address offsets in list entry
static const u32 LINK_NEXT_OFFSET = 0x1C;
static const u32 LINK_SIZE_OFFSET = 0x18;

// --- Forward Declarations ---

static void do_pvr_dma();
static void pvr_do_sort_dma();
static u32 calculate_start_link_addr();
static inline bool validate_dma_config(u32 dmaor, u32 len);
static inline void complete_dma_transfer(u32 src, u32 len);

// --- Ch2 DMA Handler ---

void RegWrite_SB_C2DST(u32 data)
{
	if (data & 1)
	{
		SB_C2DST = 1;
		DMAC_Ch2St();
	}
}

// --- PVR DMA Implementation ---

// DMA_FIX: PVR-DMA only checks DMAOR, as in the PSP port; the legacy 32-byte length check
// (copied from ch2) silently dropped unaligned transfers and left SB_PDST stuck at 1.
static inline bool validate_dma_config(u32 dmaor, u32 len)
{
	// Check DMAOR settings
	if ((dmaor & DMAOR_MASK) != EXPECTED_DMAOR)
	{
		printf("\n!\tDMAC: DMAOR has invalid settings (0x%X) !\n", dmaor);
		return false;
	}

	// Legacy (pre-fix) bogus alignment check, kept for A/B comparison.
	if (!get_dma_fix_preset() && (len & DMA_ALIGNMENT_MASK))
	{
		printf("\n!\tDMAC: SB_C2DLEN not %u-byte aligned (0x%X) !\n", DMA_ALIGNMENT_SIZE, len);
		return false;
	}

	return true;
}

static inline void complete_dma_transfer(u32 src, u32 len)
{
	// DMA_FIX: set TE and keep DE (real SH4 does not clear DE), as in the PSP port; the legacy
	// path cleared DE and never set TE, so a guest checking CHCR0 never saw the end.
	if (get_dma_fix_preset())
	{
		DMAC_SAR[0]     = src + len;
		DMAC_CHCR[0].TE = 1;
		DMAC_DMATCR[0]  = 0;
	}
	else
	{
		DMAC_SAR[0]        = src + len;
		DMAC_CHCR[0].full &= 0xFFFFFFFEu;   // legacy bug: clear DE, never set TE
		DMAC_DMATCR[0]     = 0;
	}

	// Clear pending status. Kept immediate (like SB_GDST / SB_C2DST): a guest
	// polling SB_PDST for completion is unaffected; only the IRQ is reordered.
	SB_PDST = 0;

	// SCHED preset: the completion IRQ goes through the deadline queue; otherwise it is raised inline.
	if (get_sched_preset() && pvr_dma_schid != -1)
		sh4_sched_request(pvr_dma_schid, PVR_DMA_IRQ_DELAY);
	else
		asic_RaiseInterrupt(holly_PVR_DMA);
}

// PVR DMA between system RAM and PVR memory; SB_PDDIR selects the direction.
static void do_pvr_dma()
{
	const u32 dmaor = DMAC_DMAOR.full;
	const u32 src = SB_PDSTAR;
	const u32 dst = SB_PDSTAP;
	const u32 len = SB_PDLEN;

	// Validate configuration
	if (!validate_dma_config(dmaor, len))
	{
		return;
	}

	// Perform transfer based on direction
	if (SB_PDDIR)
	{
		// PVR -> System RAM
		// Must read from PVR memory with proper synchronization
		for (u32 offset = 0; offset < len; offset += 4)
		{
			u32 value = ReadMem32_nommu(dst + offset);
			WriteMem32_nommu(src + offset, value);
		}
		rdv_DmaWrite(src, (len + 3) & ~3u, JDMA_PVR);   // the loop writes whole words
	}
	else
	{
		// System RAM -> PVR
		// Optimized block write when possible
		u32* src_ptr = (u32*)GetMemPtr(src, len);
		if (src_ptr != NULL)
		{
			WriteMemBlock_nommu_ptr(dst, src_ptr, len);
		}
		else
		{
			// Fallback to word-by-word transfer
			for (u32 offset = 0; offset < len; offset += 4)
			{
				u32 value = ReadMem32_nommu(src + offset);
				WriteMem32_nommu(dst + offset, value);
			}
		}
	}

	// Complete the transfer
	complete_dma_transfer(src, len);
}

void RegWrite_SB_PDST(u32 data)
{
	if (data & 1)
	{
		SB_PDST = 1;
		do_pvr_dma();
	}
}

// --- Sort DMA Implementation ---

// Next start link from the sort table, or a termination value.
static u32 calculate_start_link_addr()
{
	const u8* base = &mem_b[SB_SDSTAW & RAM_MASK];
	u32 link_addr;

	if (SB_SDWLT == 0)
	{
		// 16-bit width mode; RAM holds native 32-bit words, so big-endian hosts swap the halves
		const u16* table = (const u16*)base;
		link_addr = *host_ptr_xor(&table[SB_SDDIV]);
	}
	else
	{
		// 32-bit width mode
		const u32* table = (const u32*)base;
		link_addr = table[SB_SDDIV];
	}

	// Increment index for next read
	SB_SDDIV++;

	return link_addr;
}

// Sort DMA: walks the linked display lists and feeds them to the TA (mostly WinCE games).
// DMA_FIX (PSP port): 2 ends the chain and 1 fetches the next start link (legacy had them swapped).
static void pvr_do_sort_dma()
{
	// Reset table index
	SB_SDDIV = 0;

	// Get initial link address
	u32 link_addr = calculate_start_link_addr();
	const u32 link_base = SB_SDBAAW;

	const bool dma_fix = get_dma_fix_preset() != 0;
	// Fixed: terminate on LINK_ADDR_RESTART, continue-fetch on LINK_ADDR_END.
	// Legacy (swapped): terminate on LINK_ADDR_END, continue-fetch on LINK_ADDR_RESTART.
	const u32 loop_term = dma_fix ? LINK_ADDR_RESTART : LINK_ADDR_END;
	const u32 loop_next = dma_fix ? LINK_ADDR_END : LINK_ADDR_RESTART;

	// Process linked list
	while (link_addr != loop_term)
	{
		if (SB_SDLAS == 1)
			link_addr <<= 5;
		else if (dma_fix)
			link_addr &= ~31u;   // 32-byte align (PSP: link_addr &= ~31)

		// Calculate effective address and get pointer
		const u32 ea = (link_base + link_addr) & RAM_MASK;
		u32* ea_ptr = (u32*)&mem_b[ea];

		// Get next link address
		link_addr = ea_ptr[LINK_NEXT_OFFSET >> 2];

		// Submit display list to Tile Accelerator
		const u32 transfer_size = ea_ptr[LINK_SIZE_OFFSET >> 2];
		libPvr_TaDMA(ea_ptr, transfer_size);

		// Fetch the next start link and continue
		if (link_addr == loop_next)
		{
			link_addr = calculate_start_link_addr();
		}
	}

	// Mark DMA as complete
	SB_SDST = 0;
	if (dma_fix)
		SB_SDSTAW += 32;

	// Raise interrupt
	asic_RaiseInterrupt(holly_PVR_SortDMA);
}

void RegWrite_SB_SDST(u32 data)
{
	if (data & 1)
	{
		pvr_do_sort_dma();
	}
}

// --- Initialization & Cleanup ---

// Registers a DMA control register write handler.
static inline void register_dma_control(u32 addr, 
                                        void (*write_func)(u32), 
                                        u32* data_ptr)
{
	const u32 index = (addr - SB_BASE) >> 2;
	
	sb_regs[index].flags = REG_32BIT_READWRITE | REG_READ_DATA;
	sb_regs[index].readFunction = NULL;
	sb_regs[index].writeFunction = write_func;
	sb_regs[index].data32 = data_ptr;
}

void pvr_sb_Init()
{
	// Register PVR DMA start control (0x005F7C18)
	register_dma_control(SB_PDST_addr, RegWrite_SB_PDST, &SB_PDST);

	// Register Ch2 DMA start control (0x005F6808)
	register_dma_control(SB_C2DST_addr, RegWrite_SB_C2DST, &SB_C2DST);

	// Register Sort DMA start control (0x005F6820)
	register_dma_control(SB_SDST_addr, RegWrite_SB_SDST, &SB_SDST);

	// SCHED preset: register the deferred PVR-DMA end IRQ callback once.
	if (pvr_dma_schid == -1)
		pvr_dma_schid = sh4_sched_register(0, pvr_dma_end_cb);
}

void pvr_sb_Term()
{
	// Currently no resources to free
	// Placeholder for future cleanup if needed
}

void pvr_sb_Reset(bool Manual)
{
	// Reset DMA state registers
	SB_PDST = 0;
	SB_C2DST = 0;
	SB_SDST = 0;
	SB_SDDIV = 0;
	
	// Note: Manual parameter currently unused but reserved
	// for future functionality (e.g., different reset behaviors)
}

// --- Register Read/Write Stubs ---

u32 pvr_sb_readreg_Pvr(u32 addr, u32 sz)
{
	// Currently handled by generic register system
	// Placeholder for future direct register reads if needed
	return 0;
}

void pvr_sb_writereg_Pvr(u32 addr, u32 data, u32 sz)
{
	// Currently handled by generic register system
	// Placeholder for future direct register writes if needed
}
