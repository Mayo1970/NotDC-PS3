// Host-only cross-check for TArender TR4 and plan 5.17: the table store stage and the flat SQ path
// must walk the TA stream exactly as the FIFO splitter does. Not part of the PS3 build.
#include "plugs/drkPvr/ta.h"
#include <vector>
#include <cstdio>
#include <cstdlib>

using namespace TASplitter;

namespace TASplitter { TaListFP* TaCmd = 0; void TA_ListInit() {} void TA_ListCont() {} void TA_SoftReset() {} }
int msgboxf(const char*, unsigned int, ...) { return 0; }
// Bundle 4: ta.h's TA Bad Records and the store's TA Data Gate, both off (the old path).
int g_ta_bad_rec_preset;
u32 g_ta_bad_records;
static int g_ta_data_gate_preset;

static std::vector<u32>* g_irq;
void spg_SchedListEndIrq(u32 list) { if (g_irq) g_irq->push_back(list); }

// The interrupt sequence the generator intends: one per list, in order.
static std::vector<u32> g_expect;

struct TaStoreDecoder;
extern FifoSplitter<TaStoreDecoder, false> TileAccel;

// A list end flushes the raw stream to the SPE (5.12); there is no SPE here.
struct TaContext;
static TaContext* s_ta;
static void spe_flush(TaContext*) {}

#include "misc/tr4_check/extracted.inc"

FifoSplitter<TaStoreDecoder> TaStore;
FifoSplitter<TaStoreDecoder, false> TileAccel;

// ---- stream generator -------------------------------------------------------
static u32 rnd_state = 1;
static u32 rnd(u32 n) { rnd_state = rnd_state * 1103515245 + 12345; return (rnd_state >> 16) % n; }

static std::vector<u32> g_words;   // 8 u32 per record

static void emit(u32 pcw, bool payload_only = false)
{
	(void)payload_only;
	g_words.push_back(pcw);
	for (u32 i = 0; i < 7; i++)
		g_words.push_back(rnd(0xFFFF));
}

// A 64-byte record's second word carries an End_Of_List PCW, so a walker that
// mistakes payload for a header fires an interrupt no one else fires.
static void emit64(u32 pcw)
{
	emit(pcw);
	g_words.push_back((0u << 29) | (4u << 24));  // ParaType 0, ListType 4
	for (u32 i = 0; i < 7; i++)
		g_words.push_back(rnd(0xFFFF));
}

static u32 mk(u32 para, u32 list, u32 objc, u32 eos)
{
	return (para << 29) | (eos << 28) | (list << 24) | (objc & 0xFF);
}

static void gen_stream()
{
	g_words.clear();
	g_expect.clear();
	u32 lists = 1 + rnd(4);
	for (u32 l = 0; l < lists; l++)
	{
		u32 list = rnd(5);
		u32 strips = 1 + rnd(4);
		// Only the first header of a list picks the list. Later headers and the
		// end parameter carry a junk ListType that both walkers must ignore.
		u32 open = 0;
		for (u32 sidx = 0; sidx < strips; sidx++)
		{
			// Volume 1 with Col_Type 1 is an invalid encoding; the splitter
			// indexes its LUTs out of range on it, so no real stream has it.
			u32 objc;
			do { objc = rnd(256); }
			while ((objc & 0x40) && ((objc >> 4) & 3) == 1);
			u32 vtx64, kind = IsModVolList(list) ? 0 : (rnd(6) == 0 ? 1 : 2);
			if (kind == 0)          // modifier volume header: 32B, 64B vertices
			{
				emit(mk(ParamType_Polygon_or_Modifier_Volume, open++ ? rnd(8) : list, objc, 0));
				vtx64 = 1;
			}
			else if (kind == 1)     // sprite header: 32B, 64B vertices
			{
				emit(mk(ParamType_Sprite, open++ ? rnd(8) : list, objc, 0));
				vtx64 = 1;
			}
			else                    // polygon header: size from the shared LUT
			{
				u32 uid = TileAccel.ta_type_lut[objc];
				u32 pdid = (u8)uid;
				vtx64 = (pdid == 5 || pdid == 6 || pdid == 11 ||
				         pdid == 12 || pdid == 13 || pdid == 14) ? 1 : 0;
				if ((uid >> 30) == SZ64)
					emit64(mk(ParamType_Polygon_or_Modifier_Volume, open++ ? rnd(8) : list, objc, 0));
				else
					emit(mk(ParamType_Polygon_or_Modifier_Volume, open++ ? rnd(8) : list, objc, 0));
			}
			u32 n = 1 + rnd(5);
			for (u32 v = 0; v < n; v++)
			{
				u32 pcw = mk(ParamType_Vertex_Parameter, list, objc, v + 1 == n ? 1 : 0);
				if (vtx64) emit64(pcw); else emit(pcw);
			}
			if (rnd(5) == 0)
				emit(mk(ParamType_User_Tile_Clip, list, 0, 0));
		}
		emit(mk(ParamType_End_Of_List, rnd(8), 0, 0));
		g_expect.push_back(list);
		// A vertex with no list open: the splitter has no vertex handler left,
		// so both walkers must skip 32 bytes.
		if (rnd(4) == 0)
			emit(mk(ParamType_Vertex_Parameter, rnd(8), rnd(256), 1));
	}
}

// ---- runners ----------------------------------------------------------------
static void run_split(std::vector<u32>& irq, std::vector<u32>& blocks)
{
	g_irq = &irq;
	TaStore.ListInit();
	u32 w = 0;
	for (u32 b = 0; b < blocks.size(); b++)
	{
		Ta_Dma* p = (Ta_Dma*)&g_words[w * 8];
		Ta_Dma* end = p + blocks[b] - 1;
		do { p = TaStore.TaCmd(p, end); } while (p <= end);
		w += blocks[b];
	}
}

static void run_fsm(std::vector<u32>& irq, std::vector<u32>& blocks)
{
	g_irq = &irq;
	ta_store_reset();
	u32 w = 0;
	for (u32 b = 0; b < blocks.size(); b++)
	{
		ta_store_fsm_run((Ta_Dma*)&g_words[w * 8], blocks[b]);
		w += blocks[b];
	}
}

// do_sqw_nommu's flat path (plan 5.17): one record per flush, the table entry first, the slow step on TAS_SLOW.
static void run_flat(std::vector<u32>& irq, u32 total)
{
	g_irq = &irq;
	ta_store_reset();
	for (u32 r = 0; r < total; r++)
	{
		Ta_Dma* rec = (Ta_Dma*)&g_words[r * 8];
		const u32 pcw = g_words[r * 8];
		const u32 e = ta_fsm_entry(s_fsm_state, pcw);
		s_fsm_state = (u8)((e & TAS_SLOW) ? ta_fsm_step(s_fsm_state, pcw, rec) : e);
	}
}

int main()
{
	TaStore.Init();
	TileAccel.Init();
	ta_fsm_build();

	u32 fails = 0, recs = 0;
	for (u32 seed = 1; seed <= 200000; seed++)
	{
		rnd_state = seed;
		if (getenv("TR4_TRACE")) { printf("seed %u\n", seed); fflush(stdout); }
		gen_stream();
		u32 total = (u32)g_words.size() / 8;
		recs += total;

		// Split the stream into SQ (1 record) and DMA blocks, so both walkers
		// must carry their resume state across calls.
		std::vector<u32> blocks;
		for (u32 w = 0; w < total; )
		{
			u32 n = 1 + rnd(4);
			if (w + n > total) n = total - w;
			blocks.push_back(n);
			w += n;
		}

		std::vector<u32> a, b, c;
		run_split(a, blocks);
		run_fsm(b, blocks);
		run_flat(c, total);

		// Every walker must fire exactly the interrupts the stream asks for. One that loses record
		// alignment reads a payload word, whose PCW is a planted End_Of_List, and fires one too many.
		if (a != g_expect || b != g_expect || c != g_expect)
		{
			if (fails < 3)
				printf("seed %u MISMATCH: split=%u fsm=%u flat=%u expected=%u\n",
				       seed, (u32)a.size(), (u32)b.size(), (u32)c.size(), (u32)g_expect.size());
			fails++;
		}
	}
	printf("%s: %u streams, %u records, %u failures\n",
	       fails ? "FAIL" : "PASS", 200000u, recs, fails);
	return fails != 0;
}
