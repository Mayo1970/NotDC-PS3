// Host-only check for ps3/ps3_jit_link.h: the site words against the emitter and the HW trace, then a cleared cache
// that puts a new block's site where an old one was. Not part of the PS3 build.
#include <cstdint>
#include <cstdio>

typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
typedef int8_t s8; typedef int16_t s16; typedef int32_t s32; typedef int64_t s64;
typedef intptr_t snat;

static u32 g_code[64];
static u32 g_n;
void ppc_emit(u32 insn) { g_code[g_n++] = insn; }
void* emit_GetCCPtr() { return &g_code[g_n]; }

#include "emitter/PPCEmit/ppc_emitter.h"
#include "ps3/ps3_jit_link.h"

static int g_fail, g_checks;

static void check(bool ok, const char* what, u32 pc)
{
	g_checks++;
	if (!ok)
	{
		if (g_fail < 12)
			printf("FAIL %s (pc %08X)\n", what, pc);
		g_fail++;
	}
}

static u64 g_seed = 88172645463325252ull;
static u32 rnd32()
{
	g_seed ^= g_seed << 13; g_seed ^= g_seed >> 7; g_seed ^= g_seed << 17;
	return (u32)(g_seed >> 16);
}

// DoStatic's emission before the header: the emitter's addis + ori.
static void emit_ref(u32 pc)
{
	g_n = 0;
	ppc_addis(ppc_rarg0, 0, pc >> 16);
	ppc_ori(ppc_rarg0, ppc_rarg0, (u16)pc);
}

static void words(u32 pc)
{
	u32 w[2];
	static_site_pc_words(pc, w);
	emit_ref(pc);
	check(w[0] == g_code[0] && w[1] == g_code[1], "words differ from the emitter", pc);
	check(static_site_wants(g_code, pc), "emitter site not wanted", pc);
}

// A site for `want` that a new block's site for `now` overwrote: slot 2 holds the same bl (one stub), so only
// slots 0 and 1 tell them apart.
static void cleared(u32 want, u32 now)
{
	u32 site[2];
	static_site_pc_words(now, site);
	check(static_site_wants(site, want) == (want == now), "cleared site", want);
}

int main()
{
	static const u32 edge[] = { 0x00000000u, 0x0000FFFFu, 0x00010000u, 0x7FFF8000u, 0x7FFFFFFFu, 0x80000000u,
	                            0x80000080u, 0x8C00B800u, 0xA0000018u, 0xAC13DBF8u, 0xFFFF0000u, 0xFFFFFFFFu };
	for (u32 i = 0; i < sizeof(edge) / sizeof(edge[0]); i++)
		words(edge[i]);
	for (u32 i = 0; i < 200000; i++)
		words(rnd32());

	// The boot 3 [link] trace (2026-10-08): slots 0 and 1 of three live sites, as read on the PS3.
	static const u32 hw[][3] = { { 0xA0000018u, 0x3C60A000u, 0x60630018u },
	                             { 0x800000A8u, 0x3C608000u, 0x606300A8u },
	                             { 0x8C00B800u, 0x3C608C00u, 0x6063B800u } };
	for (u32 i = 0; i < 3; i++)
	{
		const u32 site[2] = { hw[i][1], hw[i][2] };
		check(static_site_wants(site, hw[i][0]), "HW site not wanted", hw[i][0]);
		check(!static_site_wants(site, hw[i][0] ^ 0x10u), "HW site wanted for another pc", hw[i][0]);
	}

	// Its dead site: the new block's words at 0x46414 (bl, addis r4, lwz) ask for no pc.
	const u32 dead[3] = { 0x4BFFFE0Du, 0x3C8054D7u, 0x8004BD90u };
	check(!static_site_wants(dead, 0x80000080u), "dead HW site wanted", 0x80000080u);

	for (u32 i = 0; i < 200000; i++)
	{
		const u32 a = rnd32();
		u32 b = rnd32();
		switch (i & 3)
		{
		case 0: b = a; break;
		case 1: b = a ^ (1u << (rnd32() & 31)); break;
		case 2: b = (a & 0xFFFF0000u) | (b & 0xFFFFu); break;
		default: b = (b & 0xFFFF0000u) | (a & 0xFFFFu); break;
		}
		cleared(a, b);
	}

	printf("%s: %d checks, %d failed\n", g_fail ? "FAIL" : "PASS", g_checks, g_fail);
	return g_fail ? 1 : 0;
}
