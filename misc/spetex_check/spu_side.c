// 5.12 #3 PC check, SPU side: spe_tex_hash.h (or SPETEX_SOURCE, a mutated copy) on the shims, over a fake EA space.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "spe_tex.h"

static SpeTexEcho echo __attribute__((aligned(128)));
static SpeTexSetup setup __attribute__((aligned(128)));

#ifndef SPETEX_SOURCE
#define SPETEX_SOURCE "ps3/spu/spe_tex_hash.h"
#endif
#include SPETEX_SOURCE

uint64_t g_seed = 1;
uint64_t g_cmds;
int g_lazy;  // commands complete only when the queue is full or their tag is waited for
void (*g_land_hook)(uint64_t ea, uint32_t size);

void shim_die(const char* why, uint32_t a, uint32_t b)
{
	printf("FAIL: shim: %s (%08X %08X)\n", why, a, b);
	fflush(stdout);
	if (getenv("SPETEX_ABORT"))
		abort();
	exit(1);
}

// ---- EA regions: a get must fall inside one, a put inside a writable one ----

typedef struct { uint64_t ea; uint32_t size; uint8_t* p; int writable; } Region;
static Region regions[8];
static int nregions;

void spu_regions_reset(void) { nregions = 0; }
void spu_region(uint64_t ea, uint32_t size, uint8_t* p, int writable)
{
	regions[nregions].ea = ea;
	regions[nregions].size = size;
	regions[nregions].p = p;
	regions[nregions].writable = writable;
	nregions++;
}

static uint8_t* ea_ptr(uint64_t ea, uint32_t size, int put)
{
	for (int i = 0; i < nregions; i++)
		if (ea >= regions[i].ea && ea + size <= regions[i].ea + regions[i].size)
		{
			if (put && !regions[i].writable)
				shim_die("put into a read-only region", (uint32_t)ea, size);
			return regions[i].p + (ea - regions[i].ea);
		}
	shim_die(put ? "put outside the EA regions" : "get outside the EA regions", (uint32_t)ea, size);
	return 0;
}

// LS: each command must stay inside one of the program's buffers.
static void ls_check(volatile void* ls, uint32_t size)
{
	static const struct { const void* p; uint32_t size; } bufs[] = {
		{ vbuf, sizeof(vbuf) }, { pbuf, sizeof(pbuf) }, { rbuf, sizeof(rbuf) }, { bgline, sizeof(bgline) },
		{ pal, sizeof(pal) }, { (const void*)&echo, sizeof(echo) }, { (const void*)&setup, sizeof(setup) },
	};
	const uintptr_t l = (uintptr_t)ls;
	for (unsigned i = 0; i < sizeof(bufs) / sizeof(bufs[0]); i++)
	{
		const uintptr_t b = (uintptr_t)bufs[i].p;
		if (l >= b && l + size <= b + bufs[i].size)
		{
			// A vbuf command must stay inside its own half.
			if (bufs[i].p == vbuf && (l - b) / sizeof(vbuf[0]) != (l + size - 1 - b) / sizeof(vbuf[0]))
				shim_die("LS command crosses vbuf halves", (uint32_t)(l - b), size);
			if (bufs[i].p == pbuf && (l - b) / sizeof(pbuf[0]) != (l + size - 1 - b) / sizeof(pbuf[0]))
				shim_die("LS command crosses pbuf halves", (uint32_t)(l - b), size);
			return;
		}
	}
	shim_die("LS command outside the program's buffers", (uint32_t)l, size);
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

// Puts read LS when they complete, as the MFC may.
static void complete(int i)
{
	const Cmd c = q[i];
	memmove(&q[i], &q[i + 1], (size_t)(nq - i - 1) * sizeof(Cmd));
	nq--;
	if (c.put)
	{
		memcpy(ea_ptr(c.ea, c.size, 1), (const void*)c.ls, c.size);
		if (g_land_hook)
			g_land_hook(c.ea, c.size);
	}
	else
		memcpy((void*)c.ls, ea_ptr(c.ea, c.size, 0), c.size);
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

// Lazy runs take the newest eligible command, so an old put stays pending as long as the rules allow.
static void complete_any(void)
{
	int cand[16], n = 0;
	for (int i = 0; i < nq; i++)
		if (eligible(i))
			cand[n++] = i;
	complete(cand[g_lazy ? n - 1 : (int)(rnd() % n)]);
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
	ls_check(ls, size);
	ea_ptr(ea, size, put);
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
	// Now and then a command completes at once, before any wait; lazy runs keep each one until it must complete.
	if (!g_lazy && rnd() % 4 == 0)
		complete_any();
}

void shim_tag_mask(uint32_t mask) { tag_mask = mask; }

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
			if (eligible(i) && (((tag_mask >> q[i].tag) & 1) || (!g_lazy && rnd() % 4 == 0)))
				cand[n++] = i;
		if (n == 0)
			shim_die("tag wait with no eligible command", tag_mask, (uint32_t)nq);
		complete(cand[rnd() % n]);
	}
}

// ---- entry points for the driver -----------------------------------------

void spu_setup(uint32_t vram_ea, uint32_t pal_ea, uint32_t res_ea)
{
	setup.vram_ea = vram_ea;
	setup.pal_ea = pal_ea;
	setup.res_ea = res_ea;
}

// One request; then every command still queued completes, as the SPU main's echo wait does.
int spu_run(const SpeTexBell* b)
{
	const SpeTexBell r = *b;
	run(&r);
	const int left = nq;
	shim_tag_mask(0xFFFFFFFFu);
	shim_tag_wait();
	return left;
}

void spu_counts(uint32_t* params, uint32_t* hashed, uint32_t* dups, uint32_t* skips, uint32_t* bytes, uint32_t* wraps)
{
	*params = echo.params;
	*hashed = echo.hashed;
	*dups = echo.dups;
	*skips = echo.skips;
	*bytes = echo.bytes;
	*wraps = echo.wraps;
}

uint32_t t_hash_range(uint32_t addr, uint32_t bytes, uint32_t seed)
{
	const uint32_t h = hash_range(addr, bytes, seed);
	if (nq)
		shim_die("hash_range left commands queued", (uint32_t)nq, 0);
	return h;
}
