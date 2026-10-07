// 5.12 #2 PC check, SPU side: spe_vb_build.h (or SPEVB_SOURCE, a mutated copy) on the shims, over a fake EA space.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "spe_vb.h"

static SpeVbEcho echo __attribute__((aligned(128)));
static uint64_t echo_ea;
static SpeVbSetup setup __attribute__((aligned(128)));
static SpeVbDiag diag __attribute__((aligned(128)));

#ifndef SPEVB_SOURCE
#define SPEVB_SOURCE "ps3/spu/spe_vb_build.h"
#endif
// x86 is little-endian: the high word of a double is the second word of its lane pair.
#define VB_HI 1
#include SPEVB_SOURCE

#define EA0 0x10000000u

uint8_t* g_arena;
uint32_t g_arena_size;
double shim_re_err = 1.0 / 4096;
void (*g_land_hook)(uint64_t ea, uint32_t size);
uint64_t g_seed = 1;
uint64_t g_cmds;

// SPEVB_ABORT=1: abort, so ASAN_OPTIONS=handle_abort=1 prints where.
void shim_die(const char* why, uint32_t a, uint32_t b)
{
	printf("FAIL: shim: %s (%08X %08X)\n", why, a, b);
	fflush(stdout);
	if (getenv("SPEVB_ABORT"))
		abort();
	exit(1);
}

static uint8_t* ea_ptr(uint64_t ea, uint32_t size)
{
	if (ea < EA0 || ea + size > (uint64_t)EA0 + g_arena_size)
		shim_die("DMA outside the EA space", (uint32_t)ea, size);
	return g_arena + (ea - EA0);
}

typedef struct
{
	int put, fence;
	volatile uint8_t* ls;
	uint64_t ea;
	uint32_t size, tag;
} Cmd;
static Cmd q[16];
static int nq;
static uint32_t tag_mask;

static uint32_t rnd(void)
{
	g_seed = g_seed * 6364136223846793005ull + 1442695040888963407ull;
	return (uint32_t)(g_seed >> 33);
}

static void complete(int i)
{
	const Cmd c = q[i];
	memmove(&q[i], &q[i + 1], (size_t)(nq - i - 1) * sizeof(Cmd));
	nq--;
	if (c.put)
	{
		memcpy(ea_ptr(c.ea, c.size), (const void*)c.ls, c.size);
		if (g_land_hook)
			g_land_hook(c.ea, c.size);
	}
	else
		memcpy((void*)c.ls, ea_ptr(c.ea, c.size), c.size);
}

// A fenced command waits for the earlier commands of its tag.
static int eligible(int i)
{
	if (!q[i].fence)
		return 1;
	for (int j = 0; j < i; j++)
		if (q[j].tag == q[i].tag)
			return 0;
	return 1;
}

static void complete_any(void)
{
	int cand[16], n = 0;
	for (int i = 0; i < nq; i++)
		if (eligible(i))
			cand[n++] = i;
	complete(cand[rnd() % n]);
}

void shim_mfc(int put, int fence, volatile void* ls, uint64_t ea, uint32_t size, uint32_t tag)
{
	const uintptr_t l = (uintptr_t)ls;
	if (tag > 31)
		shim_die("tag", tag, 0);
	if (size == 0 || size > 16384)
		shim_die("DMA size", size, (uint32_t)ea);
	if (size < 16)
	{
		if ((size & (size - 1)) || size > 8 || l % size || ea % size || (l & 15) != (ea & 15))
			shim_die("small DMA alignment", size, (uint32_t)ea);
	}
	else if (size % 16 || l % 16 || ea % 16)
		shim_die("DMA alignment", size, (uint32_t)ea);
	ea_ptr(ea, size);
	// The MFC queue holds 16 commands; a full one waits for any of them.
	while (nq == 16)
		complete_any();
	if (!put)
		memset((void*)ls, 0xDB, size);
	q[nq].put = put;
	q[nq].fence = fence;
	q[nq].ls = (volatile uint8_t*)ls;
	q[nq].ea = ea;
	q[nq].size = size;
	q[nq].tag = tag;
	nq++;
	g_cmds++;
}

void shim_tag_mask(uint32_t mask) { tag_mask = mask; }

uint32_t shim_queue_room(void)
{
	if (nq == 16)
		complete_any();
	return 16u - (uint32_t)nq;
}

// Completes every command of the masked tags, in a random order; others may complete too.
void shim_tag_wait(void)
{
	for (;;)
	{
		int any = 0;
		for (int i = 0; i < nq; i++)
			if ((tag_mask >> q[i].tag) & 1)
				any = 1;
		if (!any)
			return;
		int cand[16], n = 0;
		for (int i = 0; i < nq; i++)
			if (eligible(i) && (((tag_mask >> q[i].tag) & 1) || rnd() % 4 == 0))
				cand[n++] = i;
		complete(cand[rnd() % n]);
	}
}

// ---- entry points for the driver -----------------------------------------

void spu_arena(uint32_t size)
{
	g_arena = (uint8_t*)aligned_alloc(128, size);
	g_arena_size = size;
	memset(g_arena, 0, size);
}

uint64_t spu_ea(const void* p) { return EA0 + (uint64_t)((const uint8_t*)p - g_arena); }

void spu_setup(const uint32_t* out_ea, uint32_t runs_ea, uint64_t echo_ea_)
{
	for (int i = 0; i < SPE_CTX; i++)
		setup.out_ea[i] = out_ea[i];
	setup.runs_ea = runs_ea;
	echo_ea = echo_ea_;
}

// One request, then every command still queued completes, as the SPU main's last echo wait does.
int spu_build(const SpeVbBell* req)
{
	build(req);
	shim_tag_mask(0xFFFFFFFFu);
	shim_tag_wait();
	return nq;
}

uint32_t t_fmul(uint32_t a, uint32_t b) { return fmul_rn(a, b); }
void t_mulxy(uint32_t x, uint32_t y, uint32_t w, uint32_t* ox, uint32_t* oy) { mul_xy(x, y, w, ox, oy); }
void t_recip2(uint32_t a, uint32_t b, uint32_t* qa, uint32_t* qb)
{
	const vec_uint4 q = recip2((vec_uint4){ a, a, b, b });
	*qa = spu_extract(q, 0);
	*qb = spu_extract(q, 2);
}
uint32_t t_add(uint32_t a, uint32_t b)
{
	const uint32_t s = spu_extract(add_pos(spu_splats(a), spu_splats(b)), 0);
	return s >= 0x7F800000u ? 0x7F800000u : s;
}
uint32_t t_div(uint32_t a, uint32_t n) { return div_cnt(a, n); }
