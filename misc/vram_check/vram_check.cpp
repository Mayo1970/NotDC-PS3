// Host-only check for bundle 1 (OPTIMIZATION_PLAN section 7): dc/pvr/pvr_if.cpp after its vram_check marker, built
// as A (P43/P55 OFF) and B (ON), against C (the frozen old file, 958964c) and a model of Flycast's LMMODE and FB watch.
#include "types.h"
#include "dc/pvr/pvr_vram.h"
#include <cstdlib>
#include <vector>

#ifndef VRAM_SECTION
#define VRAM_SECTION "/tmp/vram_sec.inc"
#endif

class VArray2
{
public:
	u8* data;
	u32 size;
	void Zero() { memset(data, 0, size); }
	u8& operator[](const u32 i) { return data[i]; }
};

// The PS3 reads guest byte k at host byte k^3 of a native BE word; on this LE host it is byte k, no xor.
template<typename T> static inline T* host_ptr_xor(T* x) { return x; }
enum { holly_YUV_DMA = 7 };

#define VRAM_ALLOC (VRAM_SIZE + 4 * 1024 * 1024)   // slack: the converter does not clip its rows

static u32 conv64(u32 o)   // dc/pvr/pvrLock.cpp vramlock_ConvOffset32toOffset64
{
	o &= VRAM_MASK;
	return ((o & 0x3FFFFC) << 1) | (((o >> 22) & 1) << 2) | (o & 3);
}

#define INSTANCE_STUBS \
	VArray2 vram; \
	u32 SB_LMMODE0, SB_LMMODE1; \
	int g_yuv_conv_fast_preset, g_vram_direct_preset, g_lmmode_preset, g_fb_watch_preset; \
	u32 regs[0x800]; \
	u32 irqs, ta_dma_bytes, ta_sq_blocks; \
	u32 libPvr_ReadReg(u32 addr, u32) { return regs[(addr & 0x1FFF) >> 2]; } \
	void libPvr_WriteReg(u32 addr, u32 data, u32) { regs[(addr & 0x1FFF) >> 2] = data; } \
	void libPvr_TaDMA(u32*, u32 n) { ta_dma_bytes += n * 32; } \
	void libPvr_TaSQ(u32*) { ta_sq_blocks++; } \
	void asic_RaiseInterrupt(int id) { irqs += id == holly_YUV_DMA; } \
	u32 vramlock_ConvOffset32toOffset64(u32 o) { return conv64(o); } \
	u32 pvr_readreg_TA(u32 addr, u32 sz); \
	void WriteMemBlock_nommu_ptr(u32 dst, u32* src, u32 size) \
	{ for (u32 i = 0; i < size; i += 4) memcpy(&vram.data[(dst + i) & VRAM_MASK], &src[i >> 2], 4); }

#define printf(...) ((void)0)
namespace C {
#define yuv_conv_lookup C_yuv_conv_lookup
#define yuv_conv_generation C_yuv_conv_generation
INSTANCE_STUBS
#include "ref_pvr_if.inc"
#undef yuv_conv_lookup
#undef yuv_conv_generation
}
namespace A {
#define yuv_conv_lookup A_yuv_conv_lookup
#define yuv_conv_generation A_yuv_conv_generation
INSTANCE_STUBS
#include VRAM_SECTION
#undef yuv_conv_lookup
#undef yuv_conv_generation
}
namespace B {
#define yuv_conv_lookup B_yuv_conv_lookup
#define yuv_conv_generation B_yuv_conv_generation
INSTANCE_STUBS
#include VRAM_SECTION
#undef yuv_conv_lookup
#undef yuv_conv_generation
}
#undef printf

struct World
{
	const char* name;
	VArray2* vram;
	u32 *lm0, *lm1, *regs, *irqs, *ta_dma, *ta_sq;
	int *p43, *p55, *p7, *p26;
	void (*writereg)(u32, u32, u32);
	u32 (*readreg)(u32, u32);
	void (*ta_write)(u32, u32*, u32);
	void (*ta_sqw)(u32, u32*);
	void (*reset)(bool);
	int (*lookup)(u32, u32*, u32*);
	int (*genf)(u32, u32*);
	void (*w32)(u32, u32);
	void (*w16)(u32, u16);
	u32 *index, *dest, *done, *count, *xc, *yc, *xs, *ys, *gen, *rnext;
	void* regions;
	size_t regions_size;
	// The new file only.
	void (*dma)(u32, u32*, u32);
	void (*watch_set)(u32, u32);
	bool* dirty;
	VramSplit* split;
	void (*fmt)(char*, u32, double, u32);
};

#define WORLD(NS, ...) { #NS, &NS::vram, &NS::SB_LMMODE0, &NS::SB_LMMODE1, NS::regs, &NS::irqs, &NS::ta_dma_bytes, \
	&NS::ta_sq_blocks, &NS::g_yuv_conv_fast_preset, &NS::g_vram_direct_preset, &NS::g_lmmode_preset, \
	&NS::g_fb_watch_preset, NS::pvr_writereg_TA, NS::pvr_readreg_TA, NS::TAWrite, NS::TAWriteSQ, NS::pvr_Reset, \
	NS::NS##_yuv_conv_lookup, NS::NS##_yuv_conv_generation, NS::pvr_write_area1_32, NS::pvr_write_area1_16, \
	&NS::YUV_index, &NS::YUV_dest, &NS::YUV_doneblocks, &NS::YUV_blockcount, &NS::YUV_x_curr, &NS::YUV_y_curr, \
	&NS::YUV_x_size, &NS::YUV_y_size, &NS::YUV_generation, &NS::YUV_region_next, NS::YUV_regions, \
	sizeof(NS::YUV_regions), __VA_ARGS__ }

static World Wc = WORLD(C, nullptr, nullptr, nullptr, nullptr, nullptr);
static World Wa = WORLD(A, A::pvr_vram_dma, A::fb_watch_set, &A::g_fb_dirty, &A::g_vram_split, A::vram_split_format);
static World Wb = WORLD(B, B::pvr_vram_dma, B::fb_watch_set, &B::g_fb_dirty, &B::g_vram_split, B::vram_split_format);
static World* const W3[3] = { &Wc, &Wa, &Wb };

static u32 s_seed = 0x2545F491u;
static u32 rnd()
{
	s_seed ^= s_seed << 13;
	s_seed ^= s_seed >> 17;
	s_seed ^= s_seed << 5;
	return s_seed;
}

static int s_fails;
static const char* s_where = "";
#define CHECK(cond, ...) do { if (!(cond)) { if (s_fails++ < 12) { fprintf(stderr, "FAIL %s: ", s_where); \
	fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } } while (0)

static void reset_world(World& w, bool manual)
{
	if (!manual)
	{
		memset(w.regs, 0, 0x800 * 4);
		*w.irqs = *w.ta_dma = *w.ta_sq = 0;
	}
	w.reset(manual);
	if (w.split)
		memset(w.split, 0, sizeof(*w.split));
}

// ---- YUV converter: A (P43 OFF) and B (P43 ON, flipped live) against C ----

static void yuv_state_check(int op)
{
	for (int i = 1; i < 3; i++)
	{
		const World& w = *W3[i];
		CHECK(*w.index == *Wc.index && *w.dest == *Wc.dest && *w.done == *Wc.done && *w.count == *Wc.count,
		      "op %d %s index %u/%u dest %X/%X done %u/%u count %u/%u", op, w.name, *w.index, *Wc.index, *w.dest,
		      *Wc.dest, *w.done, *Wc.done, *w.count, *Wc.count);
		CHECK(*w.xc == *Wc.xc && *w.yc == *Wc.yc && *w.xs == *Wc.xs && *w.ys == *Wc.ys,
		      "op %d %s pos %u,%u/%u,%u size %ux%u/%ux%u", op, w.name, *w.xc, *w.yc, *Wc.xc, *Wc.yc, *w.xs, *w.ys,
		      *Wc.xs, *Wc.ys);
		CHECK(*w.irqs == *Wc.irqs && *w.gen == *Wc.gen && *w.rnext == *Wc.rnext,
		      "op %d %s irqs %u/%u gen %u/%u rnext %u/%u", op, w.name, *w.irqs, *Wc.irqs, *w.gen, *Wc.gen, *w.rnext,
		      *Wc.rnext);
		CHECK(!memcmp(w.regions, Wc.regions, Wc.regions_size), "op %d %s regions differ", op, w.name);
		CHECK(w.readreg(0x5F8150, 4) == Wc.readreg(0x5F8150, 4), "op %d %s TA_YUV_TEX_CNT", op, w.name);
		CHECK(*w.ta_dma == *Wc.ta_dma && *w.ta_sq == *Wc.ta_sq, "op %d %s TA poly counts", op, w.name);
	}
}

static void vram_cmp(const World& w, const u8* want, const char* what)
{
	if (!memcmp(w.vram->data, want, VRAM_ALLOC))
		return;
	u32 i = 0;
	while (w.vram->data[i] == want[i])
		i++;
	CHECK(false, "%s vram differs from %s at %06X: %02X want %02X", w.name, what, i, w.vram->data[i], want[i]);
}

static u32 s_yuv_ops, s_inplace, s_copied;

static void yuv_case(int ops)
{
	for (World* w : W3)
		reset_world(*w, false);
	*Wb.p43 = rnd() & 1;
	std::vector<u32> buf(64 * 8);
	for (int op = 0; op < ops; op++)
	{
		const u32 r = rnd() % 100;
		if (r < 8)
		{
			const u32 base = (rnd() % VRAM_SIZE) & ((rnd() & 3) ? ~7u : ~1u);
			for (World* w : W3)
				w->writereg(0x5F8148, base, 4);
		}
		else if (r < 16)
		{
			const u32 bx = (rnd() & 3) ? rnd() % 8 : rnd() % 64;
			const u32 by = (rnd() & 3) ? rnd() % 8 : rnd() % 64;
			const u32 ctrl = bx | (by << 8) | ((u32)(rnd() % 10 == 0) << 16) | ((u32)(rnd() % 7 == 0) << 24);
			for (World* w : W3)
				w->writereg(0x5F814C, ctrl, 4);
		}
		else if (r < 19)
		{
			for (World* w : W3)
				w->writereg(0x5F8014, 1, 4);
		}
		else if (r < 21)
		{
			for (World* w : W3)
				reset_world(*w, true);
		}
		else if (r < 55)
		{
			for (int i = 0; i < 8; i++)
				buf[i] = rnd();
			const u32 addr = 0x10800000 | (rnd() & 0x7FFFE0);
			for (World* w : W3)
				w->ta_sqw(addr, buf.data());
		}
		else if (r < 92)
		{
			const u32 k = rnd() % 5;
			const u32 n = k == 0 ? 12 * (1 + rnd() % 5) : k == 1 ? 16 * (1 + rnd() % 3) : 1 + rnd() % 40;
			for (u32 i = 0; i < n * 8; i++)
				buf[i] = rnd();
			for (World* w : W3)
				w->ta_write(0x10800000, buf.data(), n);
		}
		else if (r < 95)
		{
			const u32 n = 1 + rnd() % 8;
			const bool dma = rnd() & 1;
			for (World* w : W3)
				dma ? w->ta_write(0x10000000, buf.data(), n) : w->ta_sqw(0x10000000, buf.data());
		}
		else
			*Wb.p43 ^= 1;
		yuv_state_check(op);
		s_yuv_ops++;
	}
	vram_cmp(Wa, Wc.vram->data, "C");
	vram_cmp(Wb, Wc.vram->data, "C");
	for (int i = 0; i < 64; i++)
	{
		const u32 a = rnd() % VRAM_SIZE;
		u32 w0 = 0, h0 = 0, w1 = 0, h1 = 0, g0 = 0, g1 = 0;
		const int l0 = Wc.lookup(a, &w0, &h0), e0 = Wc.genf(a, &g0);
		for (int j = 1; j < 3; j++)
		{
			const int l1 = W3[j]->lookup(a, &w1, &h1), e1 = W3[j]->genf(a, &g1);
			CHECK(l0 == l1 && w0 == w1 && h0 == h1 && e0 == e1 && g0 == g1, "%s lookup at %06X", W3[j]->name, a);
		}
	}
	s_inplace += Wb.split->yuv_inplace;
	s_copied += Wb.split->yuv_copied;
	CHECK(Wa.split->yuv_inplace == 0 && Wa.split->yuv_copied == 0, "A (P43 OFF) counted words-path blocks");
}

// ---- VRAM writes: A (P55 OFF), B (P55 ON, flipped live) and C (P7 OFF cases) against the model ----

struct Model
{
	std::vector<u8> v;
	bool dirty;
	u32 lo, len, lm0, lm1;
	int p7, p26;
	u32 sq64, sq32, dma64, dma32;
};
static Model M;

static u32 m_lm(u32 address) { return (address & 0x02000000) ? M.lm1 : M.lm0; }

static void m_watch(u32 a)
{
	if ((a & VRAM_MASK) - M.lo < M.len)
		M.dirty = true;
}

static void m_store32(u32 off, u32 w)
{
	m_watch(off & ~3u);
	memcpy(&M.v[conv64(off)], &w, 4);
}

static void m_sq(u32 address, const u32* d)
{
	const u32 off = address & VRAM_MASK;
	if (M.p7 && m_lm(address) == 0)
	{
		memcpy(&M.v[off], d, 32);
		M.sq64++;
		return;
	}
	for (u32 i = 0; i < 8; i++)
		m_store32(off + 4 * i, d[i]);
	M.sq32++;
}

static void m_dma(u32 dst, const u32* d, u32 bytes)
{
	if (m_lm(dst) == 0)
	{
		for (u32 i = 0; i < bytes; i += 4)
			memcpy(&M.v[((dst & 0xFFFFFF) + i) & VRAM_MASK], &d[i >> 2], 4);
		M.dma64 += bytes;
		return;
	}
	for (u32 i = 0; i < bytes; i += 4)
		m_store32((dst & 0xFFFFFF) + i, d[i >> 2]);
	M.dma32 += bytes;
}

static void m_watch_set(u32 start, u32 size)
{
	M.lo = start & VRAM_MASK;
	M.len = M.p26 ? size : 0;
}

static u32 pick_lm()
{
	static const u32 k[6] = { 0, 1, 0, 1, 2, 3 };
	return k[rnd() % 6];
}

// An offset near a watch edge, or anywhere.
static u32 near_watch(u32 lo, u32 len, u32 span)
{
	switch (rnd() % 4)
	{
	case 0: return lo - span / 2 + rnd() % span;
	case 1: return lo + len - span / 2 + rnd() % span;
	default: return rnd();
	}
}

static u32 s_vram_ops, s_dirty_hits;

static void vram_case(int ops)
{
	for (World* w : W3)
		reset_world(*w, false);
	M.v.assign(VRAM_ALLOC, 0);
	M.dirty = false;
	M.sq64 = M.sq32 = M.dma64 = M.dma32 = 0;
	M.p7 = rnd() & 1;
	M.p26 = rnd() & 1;
	M.lm0 = pick_lm();
	M.lm1 = pick_lm();
	const bool use_c = !M.p7;
	*Wa.p55 = 0;
	*Wb.p55 = rnd() & 1;
	u32 start = rnd(), size = (rnd() & 1) ? rnd() % 0x100000 : rnd() % 64;
	for (World* w : W3)
	{
		*w->lm0 = M.lm0;
		*w->lm1 = M.lm1;
		*w->p7 = M.p7;
		*w->p26 = M.p26;
		if (w->watch_set)
			w->watch_set(start, size);
	}
	m_watch_set(start, size);
	std::vector<u32> buf(4096 * 8);
	for (int op = 0; op < ops; op++)
	{
		const u32 r = rnd() % 100;
		if (r < 30)
		{
			for (int i = 0; i < 8; i++)
				buf[i] = rnd();
			const u32 off = near_watch(M.lo, size, 256) & 0xFFFFE0;
			const u32 addr = ((rnd() & 1) ? 0x11000000 : 0x13000000) | off;
			for (World* w : W3)
				if (w != &Wc || use_c)
					w->ta_sqw(addr, buf.data());
			m_sq(addr, buf.data());
		}
		else if (r < 55)
		{
			if (use_c)
				continue;
			u32 off;
			switch (rnd() % 7)
			{
			case 0: off = 0x400000 - 32 * (rnd() % 64); break;
			case 1: off = 0x800000 - 32 * (rnd() % 64); break;
			case 2: off = 0xC00000 - 4 * (rnd() % 64); break;
			case 3: off = rnd() & ~3u; break;
			case 4: off = rnd(); break;
			case 5: off = near_watch(M.lo, size, 4096) + ((rnd() & 1) ? 0x800000 : 0); break;
			default: off = rnd() & ~31u; break;
			}
			const u32 dst = ((rnd() & 1) ? 0x11000000 : 0x13000000) | (off & 0xFFFFFF);
			const u32 bytes = 32 * (1 + rnd() % ((rnd() & 7) ? 64 : 4096));
			for (u32 i = 0; i < bytes / 4; i++)
				buf[i] = rnd();
			Wa.dma(dst, buf.data(), bytes);
			Wb.dma(dst, buf.data(), bytes);
			m_dma(dst, buf.data(), bytes);
		}
		else if (r < 75)
		{
			const u32 addr = 0xA5000000 | (near_watch(M.lo, size, 16) & 0xFFFFFF);
			const u32 val = rnd();
			const bool half = rnd() & 1;
			for (World* w : W3)
				half ? w->w16(addr, (u16)val) : w->w32(addr & ~3u, val);
			if (half)
			{
				m_watch(addr & ~1u);
				const u16 h = (u16)val;
				memcpy(&M.v[conv64(addr)], &h, 2);
			}
			else
				m_store32(addr & ~3u, val);
		}
		else if (r < 80)
		{
			M.lm0 = pick_lm();
			M.lm1 = pick_lm();
			for (World* w : W3)
			{
				*w->lm0 = M.lm0;
				*w->lm1 = M.lm1;
			}
		}
		else if (r < 83)
		{
			if (use_c)
				continue;
			M.p7 ^= 1;
			*Wa.p7 = *Wb.p7 = M.p7;
		}
		else if (r < 86)
		{
			M.p26 ^= 1;
			*Wa.p26 = *Wb.p26 = M.p26;
		}
		else if (r < 90)
		{
			start = (rnd() & 1) ? rnd() : M.lo + rnd() % 64;
			size = (rnd() & 1) ? rnd() % 0x100000 : rnd() % 64;
			Wa.watch_set(start, size);
			Wb.watch_set(start, size);
			m_watch_set(start, size);
		}
		else if (r < 93)
			*Wa.dirty = *Wb.dirty = M.dirty = false;
		else
			*Wb.p55 ^= 1;
		CHECK(*Wa.dirty == M.dirty && *Wb.dirty == M.dirty, "vram op %d dirty A %d B %d model %d (lo %06X len %X)",
		      op, *Wa.dirty, *Wb.dirty, M.dirty, M.lo, M.len);
		for (int i = 1; i < 3; i++)
		{
			const VramSplit& s = *W3[i]->split;
			CHECK(s.sq_v64 == M.sq64 && s.sq_v32 == M.sq32 && s.dma_v64 == M.dma64 && s.dma_v32 == M.dma32,
			      "vram op %d %s counts sq %u/%u %u/%u dma %u/%u %u/%u", op, W3[i]->name, s.sq_v64, M.sq64, s.sq_v32,
			      M.sq32, s.dma_v64, M.dma64, s.dma_v32, M.dma32);
		}
		s_dirty_hits += M.dirty;
		s_vram_ops++;
	}
	vram_cmp(Wa, M.v.data(), "the model");
	vram_cmp(Wb, M.v.data(), "the model");
	if (use_c)
		vram_cmp(Wc, M.v.data(), "the model");
	char line[400];
	Wb.fmt(line, sizeof(line), 1.0, 100000);
	CHECK(strstr(line, "| p43=") && strstr(line, "v32=") && Wb.split->sq_v32 == 0, "probe line: %s", line);
}

int main(int argc, char** argv)
{
	const int cases = argc > 1 ? atoi(argv[1]) : 200;
	for (World* w : W3)
	{
		w->vram->data = (u8*)calloc(VRAM_ALLOC, 1);
		w->vram->size = VRAM_ALLOC;
	}
	s_where = "yuv";
	for (int c = 0; c < cases && s_fails == 0; c++)
		yuv_case(50 + rnd() % 400);
	s_where = "vram";
	for (int c = 0; c < cases && s_fails == 0; c++)
		vram_case(50 + rnd() % 300);
	if (s_fails)
	{
		printf("vram_check FAIL (%d)\n", s_fails);
		return 1;
	}
	printf("vram_check PASS: %d+%d cases, %u yuv ops (B words path: %u blocks in place, %u copied), %u vram ops "
	       "(%u with the dirty mark set)\n", cases, cases, s_yuv_ops, s_inplace, s_copied, s_vram_ops, s_dirty_hits);
	return 0;
}
