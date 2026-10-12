// Plan section 7, bundle 13, PC check (WSL): the "mmu_check" sections of ccn.cpp, mmu.cpp, sh4_internal_reg.cpp and
// dmac.cpp against a model, each row OFF or ON. -DWORLD_OLD: the code of before the bundle (ref_*.inc), for run.sh.
#include "types.h"
#include "dc/sh4/ccn.h"
#include "dc/mem/mmu.h"
#include "dc/sh4/dmac.h"
#include "dc/mem/sh4_internal_reg.h"
#include <cstdlib>
#include <vector>

// ---- what the sections link against ----
static u32 g_pc = 0x8C010000;
#define curr_pc g_pc
static u32 g_clears, g_itlb_syncs;
static void count_clear() { g_clears++; }
static struct { void (*ResetCache)(); } sh4_cpu = { count_clear };
int g_mmu_bits_preset, g_p4_mirror_preset, g_ocr_banks_preset, g_ch2_dma_fix_preset, g_lmmode_preset;
CCN_PTEH_type CCN_PTEH;
CCN_MMUCR_type CCN_MMUCR;
CCN_CCR_type CCN_CCR;
void ITLB_Sync(u32) { g_itlb_syncs++; }
static u8 OnChipRAM[OnChipRAM_SIZE + 8];   // slack: a word access at the last byte, as the real array allows
template<u32 sz> static inline u32 host_to_le(u32 v) { return v; }

struct StallScope { StallScope(int) {} };
enum { ST_DMA = 4, holly_CH2_DMA = 19 };
#define RAM_SIZE (16u * 1024 * 1024)
#define RAM_MASK (RAM_SIZE - 1)
#define CH2_DMA_IRQ_DELAY 200
u32 SB_C2DSTAT, SB_C2DLEN, SB_C2DST;
u32 DMAC_SAR[4], DMAC_DAR[4], DMAC_DMATCR[4];
DMAC_CHCR_type DMAC_CHCR[4];
DMAC_DMAOR_type DMAC_DMAOR;
static int ch2_dma_schid = -1;
static int g_dmafix, g_sched;
static int get_dma_fix_preset() { return g_dmafix; }
static int get_sched_preset() { return g_sched; }
static struct { u32 dma_v64, dma_stub; } g_vram_split;

// What a ch2 DMA does to the rest of the emulator: 1 TA, 2 VRAM by LMMODE, 3 VRAM 64-bit, 4 interrupt, 5 scheduler.
struct Ev { u32 kind, a, b, c; };
static std::vector<Ev> g_ev;
static void ev(std::vector<Ev>& v, u32 k, u32 a, u32 b, u32 c) { v.push_back({ k, a, b, c }); }
#define FAKE_RAM 0x40000000u   // GetMemPtr's pointers are never read; they only carry the RAM offset
static u8* GetMemPtr(u32 src, u32) { return (u8*)(uintptr_t)(FAKE_RAM + (src & RAM_MASK)); }
static u32 ram_off(const u32* p) { return (u32)((uintptr_t)p - FAKE_RAM); }
static void TAWrite(u32 dst, u32* p, u32 count) { ev(g_ev, 1, dst, ram_off(p), count); }
static void pvr_vram_dma(u32 dst, u32* p, u32 bytes) { ev(g_ev, 2, dst, ram_off(p), bytes); }
static void WriteMemBlock_nommu_ptr(u32 dst, u32* p, u32 bytes) { ev(g_ev, 3, dst, ram_off(p), bytes); }
static void asic_RaiseInterrupt(int id) { ev(g_ev, 4, (u32)id, 0, 0); }
static void sh4_sched_request(int id, int cycles) { ev(g_ev, 5, (u32)id, (u32)cycles, 0); }

#define printf(...) ((void)0)
#ifdef WORLD_OLD
#include "ref_mmu.inc"
#include "ref_ccn.inc"
#include "ref_p4.inc"
#include "ref_ocr.inc"
#include "ref_dmac.inc"
#else
#include "sec_mmu.inc"
#include "sec_ccn.inc"
#include "sec_p4.inc"
#include "sec_ocr.inc"
#include "sec_dmac.inc"
#endif
#undef printf

// ---- the model ----
// The OLD paths are the old code as it compiles on this little-endian PC: there the 28-bit bitfield has AT at bit 0
// and TI at bit 2. bitfield_ppu.sh shows the bits of the PS3 build.
struct Tlb { u32 a, d, asst; };
struct Model
{
	int mmu, ocrb, ch2, lm, dmafix, sched;   // the rows
	u32 reg, pteh, ccr, clears, itlb_syncs;
	Tlb utlb[64], itlb[4];
	u32 sq[64];
	u8 ocr[OnChipRAM_SIZE + 8];
	u32 dstat, dlen, dst, sar2, chcr2, tcr2, dmaor, v64, stub;
	std::vector<Ev> ev;
};
static Model M;

// How often the model took each new path: a run that never took one proves nothing about it.
static struct
{
	u32 at_edge, ti, masked, v0_skip, v0_clear, array2, ocr_diff, ch2_ta, ch2_mirror, ch2_odd, ch2_zero, ch2_refused,
	    ch2_dmaor, dmaor_masked, ch2_vram;
} Cov;

static const u32 k_mask2[4] = { 0x003FFFFF, 0x003FFFFC, 0x003FFFC0, 0x003FFC00 };

static void m_sync(u32 e)
{
	const Tlb& t = M.utlb[e];
	const u32 vpn = t.a >> 10;
	if ((vpn & (0xFC000000u >> 10)) != (0xE0000000u >> 10))
		return;
	if (M.mmu && !(t.d & 0x100))
	{
		Cov.v0_skip++;
		return;
	}
	if (!(t.d & 0x100))
		Cov.v0_clear++;
	M.sq[(vpn >> 10) & 0x3F] = t.d & 0x1FFFFC00;
}

static void m_mmucr_write(u32 value)
{
	u32 v = M.mmu ? value & 0xFCFCFF05u : value;
	if (M.mmu && v != value)
		Cov.masked++;
	const u32 at = v & 1, ti = (v >> 2) & 1;
	const bool edge = at != (M.reg & 1);
	if (ti)
	{
		for (Tlb& t : M.utlb)
			t.d &= ~0x100u;
		for (Tlb& t : M.itlb)
			t.d &= ~0x100u;
		if (!M.mmu)
			memset(M.sq, 0, sizeof(M.sq));
		v &= ~4u;
		Cov.ti += M.mmu;
	}
	M.reg = v;
	if (M.mmu && edge)
	{
		M.clears++;
		Cov.at_edge++;
	}
}

static void m_set_vd(Tlb& t, u32 data)
{
	t.d = (t.d & ~0x104u) | (data & 0x100) | ((data >> 9 & 1) << 2);
}

static void m_p4_write(u32 addr, u32 data)
{
	switch (addr >> 24)
	{
	case 0xF2:
		{
			Tlb& t = M.itlb[(addr >> 8) & 3];
			t.a = data & 0xFFFFFEFF;
			t.d = (t.d & ~0x100u) | (data & 0x100);
			M.itlb_syncs++;
			return;
		}
	case 0xF3:
	case 0xF7:
		{
			const bool utlb = (addr >> 24) == 0xF7;
			const u32 e = utlb ? (addr >> 8) & 63 : (addr >> 8) & 3;
			Tlb& t = utlb ? M.utlb[e] : M.itlb[e];
			if (M.mmu && (addr & 0x800000))
			{
				t.asst = data & 0xF;
				Cov.array2++;
			}
			else
				t.d = data;
			if (utlb)
				m_sync(e);
			else
				M.itlb_syncs++;
			return;
		}
	case 0xF6:
		if (addr & 0x80)
		{
			for (int k = 0; k < 68; k++)
			{
				Tlb& t = k < 64 ? M.utlb[k] : M.itlb[k - 64];
				const u32 mask = k_mask2[((t.d >> 7) & 1) * 2 + ((t.d >> 4) & 1)];
				if (((t.a >> 10) & mask) == ((data >> 10) & mask) && (t.a & 0xFF) == (M.pteh & 0xFF))
					m_set_vd(t, data);
			}
		}
		else
		{
			const u32 e = (addr >> 8) & 63;
			M.utlb[e].a = data & 0xFFFFFCFF;
			m_set_vd(M.utlb[e], data);
			m_sync(e);
		}
		return;
	}
}

static u32 m_p4_read(u32 addr)
{
	switch (addr >> 24)
	{
	case 0xF2: { const Tlb& t = M.itlb[(addr >> 8) & 3]; return t.a | (t.d & 0x100); }
	case 0xF3: return M.itlb[(addr >> 8) & 3].d;
	case 0xF6: { const Tlb& t = M.utlb[(addr >> 8) & 63]; return t.a | ((t.d >> 2 & 1) << 9) | (t.d & 0x100); }
	case 0xF7: return M.utlb[(addr >> 8) & 63].d;
	}
	return 0;
}

static u32 m_ocr_ofs(u32 addr)
{
	const u32 old_ofs = addr & 0x1FFF;
	const u32 new_ofs = ((addr >> ((M.ccr & 0x80) ? 13 : 1)) & 0x1000) | (addr & 0xFFF);
	if (M.ocrb && old_ofs != new_ofs)
		Cov.ocr_diff++;
	return M.ocrb ? new_ofs : old_ofs;
}

static u32 m_ocr_read(u32 addr, u32 sz)
{
	if (!(M.ccr & 0x20))
		return 0xDE;
	u32 v = 0;
	memcpy(&v, &M.ocr[m_ocr_ofs(addr)], sz);
	return v;
}

static void m_ocr_write(u32 addr, u32 sz, u32 data)
{
	if (M.ccr & 0x20)
		memcpy(&M.ocr[m_ocr_ofs(addr)], &data, sz);
}

static void m_ch2()
{
	const bool fix = M.ch2 != 0;
	const u32 dst = fix ? (M.dstat & 0x03FFFFE0u) | 0x10000000u : M.dstat;
	u32 len = fix ? M.dlen & 0x00FFFFE0u : M.dlen;
	u32 src = M.sar2;
	if ((M.dmaor & 0xFFFF8201u) != 0x8201u)
	{
		Cov.ch2_dmaor++;
		return;
	}
	if ((M.dlen & 0x1F) || len == 0)
	{
		if (!fix)
		{
			Cov.ch2_refused++;
			return;
		}
		if (M.dlen & 0x1F)
			Cov.ch2_odd++;
		if (len == 0)
			Cov.ch2_zero++;
	}
	const bool ta = fix ? (dst & 0x01000000u) == 0 : (dst >= 0x10000000u && dst <= 0x10FFFFFFu);
	const bool lm0 = dst >= 0x11000000u && dst <= 0x11FFFFE0u, lm1 = dst >= 0x13000000u && dst <= 0x13FFFFE0u;
	if (ta || (M.lm && (lm0 || lm1)) || lm0)
	{
		// One call per run of RAM: the source wraps at the end of main RAM.
		const u32 kind = ta ? 1 : (M.lm ? 2 : 3);
		u32 vdst = kind == 1 ? dst : kind == 2 ? dst : (dst & 0x00FFFFFFu) | 0xA4000000u;
		if (kind == 3)
			M.v64 += len;
		Cov.ch2_ta += ta && fix;
		Cov.ch2_mirror += ta && fix && (dst & 0x02000000u) != 0;
		Cov.ch2_vram += !ta;
		while (len)
		{
			const u32 avail = RAM_SIZE - (src & RAM_MASK);
			const u32 n = avail < len ? avail : len;
			ev(M.ev, kind, vdst, src & RAM_MASK, kind == 1 ? n / 32 : n);
			len -= n;
			src += n;
			if (kind != 1)
				vdst += n;
		}
	}
	else
	{
		if (lm1)
			M.stub += len;
		src += len;
	}
	if (M.dmafix)
	{
		M.chcr2 |= 2;
		M.tcr2 = M.dst = M.dlen = 0;
	}
	else
	{
		M.chcr2 &= 0xFFFFFFFEu;
		M.tcr2 = M.dst = M.dlen = 0;
		M.dstat = src;
		M.sar2 = src;
	}
	ev(M.ev, 4, holly_CH2_DMA, 0, 0);
}

static void m_dmaor_write(u32 data)
{
	if (M.ch2 && (data & ~0x8307u))
		Cov.dmaor_masked++;
	M.dmaor = M.ch2 ? data & 0x8307u : data;
}

// ---- the driver ----
static u64 s_rng;
static u32 rnd()
{
	s_rng ^= s_rng << 13;
	s_rng ^= s_rng >> 7;
	s_rng ^= s_rng << 17;
	return (u32)(s_rng >> 16);
}

static u32 s_case, s_op;
static const char* s_what = "";
#define CHECK(cond, ...) do { if (!(cond)) { fprintf(stdout, "FAIL case %u op %u (%s): ", s_case, s_op, s_what); \
	fprintf(stdout, __VA_ARGS__); fprintf(stdout, "\n"); exit(1); } } while (0)

static void set_rows()
{
	g_mmu_bits_preset = M.mmu;
	g_ocr_banks_preset = M.ocrb;
	g_ch2_dma_fix_preset = M.ch2;
	g_lmmode_preset = M.lm;
	g_dmafix = M.dmafix;
	g_sched = M.sched;
}

static void reset_all()
{
	memset(UTLB, 0, sizeof(UTLB));
	memset(ITLB, 0, sizeof(ITLB));
	memset(sq_remap, 0, sizeof(sq_remap));
	memset(OnChipRAM, 0, sizeof(OnChipRAM));
	CCN_MMUCR.reg_data = CCN_PTEH.reg_data = CCN_CCR.reg_data = 0;
	g_clears = g_itlb_syncs = 0;
	SB_C2DSTAT = SB_C2DLEN = SB_C2DST = 0;
	memset(DMAC_SAR, 0, sizeof(DMAC_SAR));
	memset(DMAC_DMATCR, 0, sizeof(DMAC_DMATCR));
	memset(DMAC_CHCR, 0, sizeof(DMAC_CHCR));
	DMAC_DMAOR.full = 0;
	g_vram_split.dma_v64 = g_vram_split.dma_stub = 0;
	g_ev.clear();

	const Model rows = M;
	M = Model();
	M.mmu = rows.mmu, M.ocrb = rows.ocrb, M.ch2 = rows.ch2, M.lm = rows.lm, M.dmafix = rows.dmafix, M.sched = rows.sched;
}

static void compare()
{
	CHECK(CCN_MMUCR.reg_data == M.reg, "MMUCR %08X, model %08X", CCN_MMUCR.reg_data, M.reg);
	// On this PC the bitfield has AT and URC at the manual's bits, and URB two bits low.
	CHECK(mmucr_at() == (M.reg & 1) && mmucr_urc() == (M.reg >> 10 & 63) &&
	      mmucr_urb() == (M.reg >> (M.mmu ? 18 : 16) & 63), "ccn.h's helpers read AT %u URC %u URB %u of %08X",
	      mmucr_at(), mmucr_urc(), mmucr_urb(), M.reg);
	CHECK(g_clears == M.clears, "%u JIT cache clears, model %u", g_clears, M.clears);
	CHECK(g_itlb_syncs == M.itlb_syncs, "%u ITLB syncs, model %u", g_itlb_syncs, M.itlb_syncs);
	for (int k = 0; k < 68; k++)
	{
		const TLB_Entry& e = k < 64 ? UTLB[k] : ITLB[k - 64];
		const Tlb& t = k < 64 ? M.utlb[k] : M.itlb[k - 64];
		CHECK(e.Address.reg_data == t.a && e.Data.reg_data == t.d && e.Assistance.reg_data == t.asst,
		      "TLB %d: %08X %08X %X, model %08X %08X %X", k, e.Address.reg_data, e.Data.reg_data,
		      e.Assistance.reg_data, t.a, t.d, t.asst);
	}
	for (int k = 0; k < 64; k++)
		CHECK(sq_remap[k] == M.sq[k], "sq_remap[%d] %08X, model %08X", k, sq_remap[k], M.sq[k]);
	CHECK(!memcmp(OnChipRAM, M.ocr, sizeof(OnChipRAM)), "on-chip RAM differs");
	CHECK(SB_C2DSTAT == M.dstat && SB_C2DLEN == M.dlen && SB_C2DST == M.dst, "SB_C2D* %08X %08X %X, model %08X %08X %X",
	      SB_C2DSTAT, SB_C2DLEN, SB_C2DST, M.dstat, M.dlen, M.dst);
	CHECK(DMAC_SAR[2] == M.sar2 && DMAC_CHCR[2].full == M.chcr2 && DMAC_DMATCR[2] == M.tcr2,
	      "SAR2 %08X CHCR2 %08X DMATCR2 %X, model %08X %08X %X", DMAC_SAR[2], DMAC_CHCR[2].full, DMAC_DMATCR[2],
	      M.sar2, M.chcr2, M.tcr2);
	CHECK(DMAC_DMAOR.full == M.dmaor, "DMAOR %08X, model %08X", DMAC_DMAOR.full, M.dmaor);
	CHECK(g_vram_split.dma_v64 == M.v64 && g_vram_split.dma_stub == M.stub, "DMA byte counts %u %u, model %u %u",
	      g_vram_split.dma_v64, g_vram_split.dma_stub, M.v64, M.stub);
	CHECK(g_ev.size() == M.ev.size(), "%u DMA events, model %u", (u32)g_ev.size(), (u32)M.ev.size());
	for (size_t k = 0; k < g_ev.size(); k++)
		CHECK(!memcmp(&g_ev[k], &M.ev[k], sizeof(Ev)), "event %u: %u %08X %08X %X, model %u %08X %08X %X", (u32)k,
		      g_ev[k].kind, g_ev[k].a, g_ev[k].b, g_ev[k].c, M.ev[k].kind, M.ev[k].a, M.ev[k].b, M.ev[k].c);
}

static u64 s_hash;
static void fold(u32 v) { s_hash = (s_hash ^ v) * 0x100000001B3ull; }

// Every value the old and the new code must agree on, with every row OFF.
static void fold_state(u32 read_back)
{
	fold(read_back);
	fold(CCN_MMUCR.reg_data);
	fold(g_clears);
	fold(g_itlb_syncs);
	for (int k = 0; k < 68; k++)
	{
		const TLB_Entry& e = k < 64 ? UTLB[k] : ITLB[k - 64];
		fold(e.Address.reg_data);
		fold(e.Data.reg_data);
	}
	for (int k = 0; k < 64; k++)
		fold(sq_remap[k]);
	for (u32 k = 0; k < OnChipRAM_SIZE; k += 4)
	{
		u32 w;
		memcpy(&w, &OnChipRAM[k], 4);
		fold(w);
	}
	fold(SB_C2DSTAT), fold(SB_C2DLEN), fold(SB_C2DST), fold(DMAC_SAR[2]), fold(DMAC_CHCR[2].full), fold(DMAC_DMATCR[2]);
	fold(DMAC_DMAOR.full), fold(g_vram_split.dma_v64), fold(g_vram_split.dma_stub), fold((u32)g_ev.size());
	if (!g_ev.empty())
		fold(g_ev.back().kind), fold(g_ev.back().a), fold(g_ev.back().b), fold(g_ev.back().c);
}

static u32 pick_mmucr()
{
	static const u32 k[8] = { 0, 1, 4, 5, 0x105, 0x10, 0x40, 0x50 };
	const u32 r = rnd() % 10;
	return r < 8 ? k[r] | (rnd() & 0xFC000000u & -(rnd() & 1)) : rnd();
}

// A TLB word: the store queue area most of the time, valid most of the time.
static u32 pick_vpn_word()
{
	const u32 area = rnd() % 5 ? 0xE0000000u | (rnd() & 0x03F00000u) : rnd() & 0xFFFFFC00u;
	return area | (rnd() & 0x3FF & -(rnd() & 1)) | (rnd() % 4 ? 0x100 : 0);
}

static u32 pick_ppn_word()
{
	static const u32 k[4] = { 0x0C000000, 0x10000000, 0x04000000, 0x00000000 };
	const u32 base = rnd() % 6 ? k[rnd() % 4] | (rnd() & 0x00F00000u) : rnd() & 0x1FFFFC00u;
	return base | (rnd() & 0xFF & -(rnd() & 1)) | (rnd() % 5 ? 0x100 : 0);
}

static void one_op(bool mix)
{
	const u32 r = rnd() % 100;
	u32 back = 0;
	// The live rows change in play; MMUCR Bits and Cache RAM Banks are set before boot.
	const u32 flip = rnd();
	if (mix && (flip & 0xFF) < 6)
	{
		M.ch2 ^= (flip >> 8) & 1, M.lm ^= (flip >> 9) & 1, M.dmafix ^= (flip >> 10) & 1;
		set_rows();
	}
	if (r < 16)
	{
		s_what = "MMUCR write";
		const u32 v = pick_mmucr();
		CCN_MMUCR_write(v);
		m_mmucr_write(v);
	}
	else if (r < 46)
	{
		s_what = "UTLB array write";
		const u32 e = rnd() % (rnd() & 1 ? 64 : 26);
		const u32 kind = rnd() % 8;
		u32 addr, data;
		if (kind < 3)
			addr = 0xF6000000u | (e << 8) | (rnd() & 0x7C), data = pick_vpn_word() | (rnd() & 0x200);
		else if (kind < 4)
			addr = 0xF6000080u | (rnd() & 0x3F00), data = pick_vpn_word() | (rnd() & 0x200);
		else
			addr = 0xF7000000u | (e << 8) | (rnd() % 4 ? 0 : 0x800000), data = pick_ppn_word();
		WriteMem_P4<4, u32>(addr, data);
		m_p4_write(addr, data);
	}
	else if (r < 54)
	{
		s_what = "ITLB array write";
		const u32 addr = (rnd() & 1 ? 0xF2000000u : 0xF3000000u | (rnd() % 3 ? 0 : 0x800000)) | ((rnd() & 3) << 8);
		const u32 data = (addr >> 24) == 0xF2 ? pick_vpn_word() : pick_ppn_word();
		WriteMem_P4<4, u32>(addr, data);
		m_p4_write(addr, data);
	}
	else if (r < 64)
	{
		s_what = "array read";
		static const u32 k[8] = { 0xF2, 0xF3, 0xF6, 0xF7, 0xF6, 0xF7, 0xF0, 0xF5 };
		const u32 addr = (k[rnd() & 7] << 24) | (rnd() & 0x00803F00u);
		back = ReadMem_P4<4, u32>(addr);
		const u32 want = m_p4_read(addr);
		CHECK(back == want, "read %08X gives %08X, model %08X", addr, back, want);
		const u16 back16 = ReadMem_P4<2, u16>(addr);
		CHECK(back16 == (u16)want, "16-bit read %08X", addr);
	}
	else if (r < 68)
	{
		s_what = "PTEH write";
		CCN_PTEH.reg_data = M.pteh = rnd() % 3 ? rnd() & 3 : rnd();
	}
	else if (r < 74)
	{
		s_what = "store queue address";
		const u32 adr = 0xE0000000u | (rnd() & 0x03FFFFE0u);
		back = mmu_TranslateSQW(adr);
		CHECK(back == (M.sq[(adr >> 20) & 0x3F] | (adr & 0xFFFFF)), "SQ %08X goes to %08X", adr, back);
	}
	else if (r < 77)
	{
		s_what = "P4 page with no array";
		static const u32 k[6] = { 0xE4, 0xE5, 0xEC, 0xEF, 0xF8, 0xFE };
		const u32 addr = (k[rnd() % 6] << 24) | (rnd() & 0x00FFFFFCu);
		WriteMem_P4<4, u32>(addr, rnd());
		back = ReadMem_P4<4, u32>(addr);
		CHECK(back == 0, "read %08X gives %08X", addr, back);
	}
	else if (r < 89)
	{
		s_what = "on-chip RAM";
		if (rnd() % 8 == 0)
			CCN_CCR.reg_data = M.ccr = (rnd() & 0x80) | (rnd() % 6 ? 0x20 : 0) | (rnd() & 0x10F);
		const u32 sz = 1u << (rnd() % 3);
		const u32 addr = (0x7C000000u | (rnd() & 0x03FFFFFFu & -(rnd() & 1)) | (rnd() & 0x3FFF)) & ~(sz - 1);
		const u32 data = rnd();
		if (rnd() & 1)
		{
			if (sz == 1)
				WriteMem_area7_OCR_T<1, u8>(addr, (u8)data);
			else if (sz == 2)
				WriteMem_area7_OCR_T<2, u16>(addr, (u16)data);
			else
				WriteMem_area7_OCR_T<4, u32>(addr, data);
			m_ocr_write(addr, sz, data);
		}
		else
		{
			back = sz == 1 ? ReadMem_area7_OCR_T<1, u8>(addr) : sz == 2 ? ReadMem_area7_OCR_T<2, u16>(addr)
			                                                           : ReadMem_area7_OCR_T<4, u32>(addr);
			const u32 want = m_ocr_read(addr, sz);
			CHECK(back == (sz == 4 ? want : want & ((1u << (8 * sz)) - 1)), "OCR read%u %08X gives %X, model %X", sz * 8,
			      addr, back, want);
		}
	}
	else
	{
		s_what = "ch2 DMA";
		static const u32 k_dst[8] = { 0x10000000, 0x10800000, 0x11000000, 0x12000000, 0x12800000, 0x13000000,
		                              0x11FFFFE0, 0x10FFFFE0 };
		static const u32 k_or[6] = { 0x8201, 0x8201, 0x8201, 0x8203, 0x0201, 0x8001 };
		const u32 or_v = k_or[rnd() % 6] | (rnd() % 5 ? 0 : rnd() & 0xFFFF0104u);
		WriteDMAOR(or_v);
		m_dmaor_write(or_v);
		SB_C2DSTAT = M.dstat = rnd() % 6 ? k_dst[rnd() & 7] | (rnd() & 0x000FFFE0u & -(rnd() & 1)) | (rnd() % 7 ? 0 : rnd() & 0xEC00001Fu)
		                               : rnd();
		const u32 blocks = rnd() % 4 ? 1 + rnd() % 64 : rnd() % 3 ? 0 : 1 + rnd() % 4096;
		SB_C2DLEN = M.dlen = blocks * 32 + (rnd() % 6 ? 0 : 1 + rnd() % 31) + (rnd() % 9 ? 0 : rnd() & 0xFF000000u);
		DMAC_SAR[2] = M.sar2 = rnd() % 4 ? 0x0C000000u | (rnd() & 0x00FFFFE0u) : 0x0D000000u - 32 * (rnd() % 40);
		DMAC_CHCR[2].full = M.chcr2 = rnd() & 0x000052C1u;
		DMAC_DMATCR[2] = M.tcr2 = rnd() & 0xFFFF;
		SB_C2DST = M.dst = 1;
		DMAC_Ch2St();
		m_ch2();
	}
	compare();
	fold_state(back);
}

int main(int argc, char** argv)
{
	const bool mix = argc > 1 && !strcmp(argv[1], "mix");
	const u32 cases = argc > 2 ? (u32)atoi(argv[2]) : 300;
#ifdef WORLD_OLD
	if (mix)
	{
		fprintf(stdout, "the old code has no rows\n");
		return 2;
	}
#endif
	for (s_case = 0; s_case < cases; s_case++)
	{
		s_rng = 0x9E3779B97F4A7C15ull * (s_case + 1);
		const u32 rows = rnd();
		M.mmu = mix ? rows & 1 : 0, M.ocrb = mix ? rows >> 1 & 1 : 0, M.ch2 = mix ? rows >> 2 & 1 : 0;
		M.lm = rows >> 3 & 1, M.dmafix = rows >> 4 & 1, M.sched = 0;
		reset_all();
		set_rows();
		s_hash = 0xCBF29CE484222325ull;
		for (s_op = 0; s_op < 400; s_op++)
			one_op(mix);
		if (!mix)
			fprintf(stdout, "%u %016llx\n", s_case, (unsigned long long)s_hash);
	}
	if (mix)
	{
		const u32 c[15] = { Cov.at_edge, Cov.ti, Cov.masked, Cov.v0_skip, Cov.array2, Cov.ocr_diff, Cov.ch2_ta,
		                    Cov.ch2_mirror, Cov.ch2_odd, Cov.ch2_zero, Cov.ch2_dmaor, Cov.dmaor_masked, Cov.ch2_vram,
		                    Cov.ch2_refused, Cov.v0_clear };
		fprintf(stdout, "model: %u cases x 400 ops | AT edges %u, TI %u, masked MMUCR %u, V=0 kept %u (cleared with OLD %u),"
		        " data array 2 %u, OCR bank moved %u | ch2: TA %u, 0x12 %u, odd length %u, zero %u, VRAM %u, refused with"
		        " OLD %u, DMAOR abort %u, DMAOR masked %u\n", cases, c[0], c[1], c[2], c[3], c[14], c[4], c[5], c[6], c[7],
		        c[8], c[9], c[12], c[13], c[10], c[11]);
		for (int k = 0; k < 15; k++)
			if (!c[k])
			{
				fprintf(stdout, "FAIL: a path was never taken (counter %d)\n", k);
				return 1;
			}
		fprintf(stdout, "PASS: model\n");
	}
	return 0;
}
