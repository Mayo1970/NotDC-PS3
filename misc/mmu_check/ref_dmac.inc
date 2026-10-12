// mmu_check: dc/sh4/dmac.cpp of commit 27f2a65 (before bundle 13), the lines that the "dmac" section replaces.
// Address range helpers (makes intent explicit, avoids magic numbers)
static inline bool isDstTA(u32 dst)         { return dst >= 0x10000000u && dst <= 0x10FFFFFFu; }
static inline bool isDstVRAM_LM0(u32 dst)  { return dst >= 0x11000000u && dst <= 0x11FFFFE0u; }
static inline bool isDstVRAM_LM1(u32 dst)  { return dst >= 0x13000000u && dst <= 0x13FFFFE0u; }

// Disproven Rez experiment: a deferred ch2 IRQ (CH2_IRQ_DELAY_CYCLES, even 65536) changed nothing, so the
// IRQ is synchronous again as in the PSP port; bring the delay back only if a timing hang shows up.

// Channel 2 DMA to the TA / VRAM; started by a write to SB_C2DST.
void DMAC_Ch2St()
{
	StallScope st_(ST_DMA);
	const u32 dmaor  = DMAC_DMAOR.full;
	const u32 dst    = SB_C2DSTAT;
	u32       src    = DMAC_SAR[2];
	u32       len    = SB_C2DLEN;

	// Validate DMAOR: must have DME set and DDT mode, no error flags
	if ((dmaor & DMAOR_MASK) != 0x8201u)
	{
		printf("DMAC: Ch2 abort - DMAOR invalid (0x%08X)\n", dmaor);
		return;
	}

	// SB_C2DLEN must be a multiple of 32 bytes
	if (len & 0x1Fu)
	{
		printf("DMAC: Ch2 abort - SB_C2DLEN not 32-byte aligned (0x%X)\n", len);
		return;
	}

	// Guard against zero-length transfer
	if (len == 0)
	{
		printf("DMAC: Ch2 abort - SB_C2DLEN is zero\n");
		return;
	}

	//printf("DMAC: Ch2 DMA  SRC=0x%08X  DST=0x%08X  LEN=0x%X\n", src, dst, len);

	// --- Transfer to TA (display list) address space ---
	if (isDstTA(dst))
	{
		while (len)
		{
			const u32 p_addr  = src & RAM_MASK;
			const u32 avail   = RAM_SIZE - p_addr;

			if (avail < len)
			{
				// Transfer up to the RAM wrap boundary, then loop
				u32 *sys_buf = (u32 *)GetMemPtr(src, avail);
				if (sys_buf)
					TAWrite(dst, sys_buf, avail / 32);
				len -= avail;
				src += avail;
			}
			else
			{
				u32 *sys_buf = (u32 *)GetMemPtr(src, len);
				if (sys_buf)
					TAWrite(dst, sys_buf, len / 32);
				src += len;
				break;
			}
		}
	}
	// --- P7 LMMODE: 0x11 and 0x13 take the bus SB_LMMODE0/1 selects (Flycast), in RAM runs as above ---
	else if (g_lmmode_preset && (isDstVRAM_LM0(dst) || isDstVRAM_LM1(dst)))
	{
		u32 vram_dst = dst;
		while (len)
		{
			const u32 avail = RAM_SIZE - (src & RAM_MASK);
			const u32 n     = avail < len ? avail : len;
			u32 *sys_buf = (u32 *)GetMemPtr(src, n);
			if (sys_buf)
				pvr_vram_dma(vram_dst, sys_buf, n);
			len      -= n;
			src      += n;
			vram_dst += n;
		}
	}
	// --- Transfer to VRAM via LMMODE0 (0x11xxxxxx -> VRAM bank 0) ---
	else if (isDstVRAM_LM0(dst))
	{
		u32 vram_dst = (dst & 0x00FFFFFFu) | 0xA4000000u;
		g_vram_split.dma_v64 += len;

		while (len)
		{
			const u32 p_addr  = src & RAM_MASK;
			const u32 avail   = RAM_SIZE - p_addr;

			if (avail < len)
			{
				u32 *sys_buf = (u32 *)GetMemPtr(src, avail);
				if (sys_buf)
					WriteMemBlock_nommu_ptr(vram_dst, sys_buf, avail);
				len      -= avail;
				src      += avail;
				vram_dst += avail;
			}
			else
			{
				u32 *sys_buf = (u32 *)GetMemPtr(src, len);
				if (sys_buf)
					WriteMemBlock_nommu_ptr(vram_dst, sys_buf, len);
				src += len;
				break;
			}
		}
	}
	// --- Transfer to VRAM via LMMODE1 (0x13xxxxxx -> VRAM bank 1) ---
	else if (isDstVRAM_LM1(dst))
	{
		// LMMODE1 path not yet fully implemented; advance src to keep state consistent
		printf("DMAC: Ch2 LMMODE1 transfer (stub) DST=0x%08X LEN=0x%X\n", dst, len);
		g_vram_split.dma_stub += len;
		src += len;
	}
	else
	{
		printf("DMAC: Ch2 invalid SB_C2DSTAT address (0x%08X)\n", dst);
		src += len;
	}

	// DMA_FIX (PSP port writeback): set TE, keep DE (legacy DE=0 stalled Rez's chained batch),
	// leave DMAC_SAR[2] as programmed and SB_C2DSTAT untouched on the TA path.
	if (get_dma_fix_preset())
	{
		DMAC_CHCR[2].TE = 1;
		DMAC_DMATCR[2]  = 0x00000000u;
		SB_C2DST        = 0x00000000u;
		SB_C2DLEN       = 0x00000000u;
	}
	else
	{
		// Legacy (pre-fix) writeback, kept for A/B comparison.
		DMAC_CHCR[2].full &= 0xFFFFFFFEu;   // clear DE, never set TE
		DMAC_DMATCR[2]     = 0x00000000u;
		SB_C2DST           = 0x00000000u;
		SB_C2DLEN          = 0x00000000u;
		SB_C2DSTAT         = src;           // bug: clobbers destination reg
		DMAC_SAR[2]        = src;
	}

	// End IRQ: synchronous as in the PSP port; the SCHED preset orders it with the other
	// completions through the deadline queue (the ordering matters, not the delay).
	if (get_sched_preset() && ch2_dma_schid != -1)
		sh4_sched_request(ch2_dma_schid, CH2_DMA_IRQ_DELAY);
	else
		asic_RaiseInterrupt(holly_CH2_DMA);
	(void)src;
}

// On-demand data transfer stubs (not yet implemented)
void dmac_ddt_ch0_ddt(u32 src, u32 dst, u32 count)
{
	// TODO: Ch0 on-demand data transfer
	(void)src; (void)dst; (void)count;
}

void dmac_ddt_ch2_direct(u32 dst, u32 count)
{
	// TODO: Ch2 direct data transfer (DDT mode)
	(void)dst; (void)count;
}

// UpdateDMA - cycle-accurate DMA tick (not yet implemented)
void UpdateDMA()
{
	// TODO: timesliced DMA for active channels (DE=1, TE=0) when DMAOR.DME=1, AE=0, DDT=1,
	// in 22 KB chunks (704 x 32 bytes).
}

// CHCR / DMAOR write handlers (used as callbacks in the register map)
template<u32 ch>
static void WriteCHCR(u32 data)
{
	DMAC_CHCR[ch].full = data;
	//printf("DMAC: Write CHCR%u = 0x%08X\n", ch, data);
}

static void WriteDMAOR(u32 data)
{
	DMAC_DMAOR.full = data;
	//printf("DMAC: Write DMAOR = 0x%08X\n", data);
}
