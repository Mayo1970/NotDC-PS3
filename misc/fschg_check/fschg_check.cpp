// Plan 5.37 PC check (WSL): decoder.cpp's fschg handler (run.sh cuts it out) on a mock of the decoder's block and
// state. Levels 0 and 1 must emit what they did; level 2 only the xor, with no block end. Not in the build.
#include <cstdint>
#include <cstdio>
#include <vector>

typedef uint32_t u32;

enum shilop { shop_xor, shop_sync_fpscr };
enum Sh4RegType { reg_none, reg_fpscr };
enum BlockEndType { BET_StaticJump, BET_Other };
// shil.h's flag value; the handler names it.
#define SYNC_FPSCR_SZ_ONLY 1

struct shil_param
{
	int kind;   // 0 null, 1 register, 2 immediate
	u32 v;
	shil_param() : kind(0), v(0) {}
	shil_param(Sh4RegType r) : kind(1), v(r) {}
};
static shil_param mk_imm(u32 i)
{
	shil_param p;
	p.kind = 2;
	p.v = i;
	return p;
}

struct Op { shilop op; shil_param rd, rs1, rs2; u32 flags; };
static std::vector<Op> g_ops;
static struct
{
	void Emit(shilop op, shil_param rd = shil_param(), shil_param rs1 = shil_param(), shil_param rs2 = shil_param(),
	          u32 flags = 0)
	{
		g_ops.push_back({ op, rd, rs1, rs2, flags });
	}
} block;
static struct { struct { bool FSZ64, is_delayslot; u32 rpc; } cpu; } state;

static int g_ends, g_level;
static u32 g_end_dst;
static BlockEndType g_end_type;
static bool g_end_delay;
static void dec_End(u32 dst, BlockEndType t, bool delay)
{
	g_ends++;
	g_end_dst = dst;
	g_end_type = t;
	g_end_delay = delay;
}
extern "C" int get_jit_fschg_preset() { return g_level; }

#define sh4dec(str) static void dec_##str(u32 op)
#include "fschg.inc"

static bool is_param(const shil_param& p, int kind, u32 v) { return p.kind == kind && (kind == 0 || p.v == v); }

int main()
{
	int fails = 0, checks = 0;
	for (g_level = 0; g_level <= 3; g_level++)
		for (int slot = 0; slot < 2; slot++)
			for (int sz = 0; sz < 2; sz++)
			{
				g_ops.clear();
				g_ends = 0;
				state.cpu.FSZ64 = sz != 0;
				state.cpu.is_delayslot = slot != 0;
				state.cpu.rpc = 0x8C0873C8;
				dec_i1111_0011_1111_1101(0xF3FD);

				// Every level: fpscr ^= SZ first, and the decoder's own SZ flips.
				bool ok = !g_ops.empty() && g_ops[0].op == shop_xor && is_param(g_ops[0].rd, 1, reg_fpscr) &&
				          is_param(g_ops[0].rs1, 1, reg_fpscr) && is_param(g_ops[0].rs2, 2, 1u << 20) &&
				          state.cpu.FSZ64 == (sz == 0);
				if (g_level >= 2)
					ok = ok && g_ops.size() == 1 && g_ends == 0;
				else
				{
					// The SZ-only sync, then the block ends at the next op unless a delay slot holds the fschg.
					ok = ok && g_ops.size() == 2 && g_ops[1].op == shop_sync_fpscr && is_param(g_ops[1].rd, 0, 0) &&
					     is_param(g_ops[1].rs1, 0, 0) && is_param(g_ops[1].rs2, 0, 0) &&
					     g_ops[1].flags == SYNC_FPSCR_SZ_ONLY && g_ends == (slot ? 0 : 1);
					if (!slot)
						ok = ok && g_end_dst == 0x8C0873CA && g_end_type == BET_StaticJump && !g_end_delay;
				}
				checks++;
				if (!ok)
				{
					printf("FAIL level %d slot %d SZ %d: %zu ops, %d block ends\n", g_level, slot, sz, g_ops.size(), g_ends);
					fails++;
				}
			}
	printf("%s: %d cases, %d failures\n", fails ? "FAIL" : "PASS", checks, fails);
	return fails != 0;
}
