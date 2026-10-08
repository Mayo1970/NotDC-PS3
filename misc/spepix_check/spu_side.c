// 5.12 #4/#5 PC check, SPU side: spe_pix_job.h (or SPEPIX_SOURCE, a mutated copy) on the shims, over a fake EA space.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "spe_pix.h"

void shim_die(const char* why, uint32_t a, uint32_t b);

static SpePixEcho echo __attribute__((aligned(128)));
static SpePixSetup setup __attribute__((aligned(128)));

#ifndef SPEPIX_SOURCE
#define SPEPIX_SOURCE "ps3/spu/spe_pix_job.h"
#endif
#include SPEPIX_SOURCE

uint64_t g_seed = 1;
uint64_t g_cmds, g_elems;
int g_lazy;  // commands complete only when the queue is full or their tag is waited for

void shim_die(const char* why, uint32_t a, uint32_t b)
{
	printf("FAIL: shim: %s (%08X %08X)\n", why, a, b);
	fflush(stdout);
	if (getenv("SPEPIX_ABORT"))
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

// LS: each transfer must stay inside one of the program's DMA buffers (one half of a double buffer).
static void ls_check(volatile void* ls, uint32_t size)
{
	static const struct { const void* p; uint32_t size; } bufs[] = {
		{ inb[0], sizeof(inb[0]) }, { inb[1], sizeof(inb[1]) }, { outb[0], sizeof(outb[0]) }, { outb[1], sizeof(outb[1]) },
		{ rawb, sizeof(rawb) }, { fbin[0], sizeof(fbin[0]) }, { fbin[1], sizeof(fbin[1]) },
		{ fbout[0], sizeof(fbout[0]) }, { fbout[1], sizeof(fbout[1]) },
		{ (const void*)&echo, sizeof(echo) }, { (const void*)&setup, sizeof(setup) },
	};
	const uintptr_t l = (uintptr_t)ls;
	for (unsigned i = 0; i < sizeof(bufs) / sizeof(bufs[0]); i++)
	{
		const uintptr_t b = (uintptr_t)bufs[i].p;
		if (l >= b && l + size <= b + bufs[i].size)
			return;
	}
	shim_die("LS transfer outside the program's buffers", (uint32_t)l, size);
}

// One transfer's size and alignment rules (a single command or one list element).
static void dma_rules(uintptr_t l, uint64_t ea, uint32_t size)
{
	if (size == 0 || size > 16384)
		shim_die("DMA size", size, (uint32_t)ea);
	if (size < 16)
	{
		if ((size & (size - 1)) || size > 8 || l % size || ea % size || (l & 15) != (ea & 15))
			shim_die("small DMA alignment", size, (uint32_t)ea);
	}
	else if (size % 16 || l % 16 || ea % 16)
		shim_die("DMA alignment", size, (uint32_t)ea);
}

typedef struct
{
	int put, fence, list;
	volatile uint8_t* ls;
	uint64_t ea;
	uint32_t size, tag;
	const volatile uint32_t* le;  // list words, read when the command completes
} Cmd;
static Cmd q[16];
static int nq;
static uint32_t tag_mask;

static uint32_t rnd(void)
{
	g_seed = g_seed * 6364136223846793005ull + 1442695040888963407ull;
	return (uint32_t)(g_seed >> 33);
}

static void xfer(int put, volatile uint8_t* ls, uint64_t ea, uint32_t size)
{
	if (put)
		memcpy(ea_ptr(ea, size, 1), (const void*)ls, size);
	else
		memcpy((void*)ls, ea_ptr(ea, size, 0), size);
}

// Puts read LS when they complete, as the MFC may; a list's elements are read then too.
static void complete(int i)
{
	const Cmd c = q[i];
	memmove(&q[i], &q[i + 1], (size_t)(nq - i - 1) * sizeof(Cmd));
	nq--;
	if (!c.list)
	{
		xfer(c.put, c.ls, c.ea, c.size);
		return;
	}
	volatile uint8_t* l = c.ls;
	for (uint32_t k = 0; k < c.size / 8; k++)
	{
		const uint32_t hi = c.le[2 * k], size = hi & 0x7FFF;
		const uint64_t ea = (c.ea & 0xFFFFFFFF00000000ull) | c.le[2 * k + 1];
		if (hi >> 15)
			shim_die("list element notify/reserved bits", k, hi);
		dma_rules((uintptr_t)l, ea, size);
		ls_check(l, size);
		xfer(c.put, l, ea, size);
		l += size;
		g_elems++;
	}
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

static void enqueue(Cmd c)
{
	if (c.tag > 31)
		shim_die("tag", c.tag, 0);
	// The MFC queue holds 16 commands; a full one waits for any of them.
	while (nq == 16)
		complete_any();
	q[nq++] = c;
	g_cmds++;
	// Now and then a command completes at once; lazy runs keep each one until it must complete.
	if (!g_lazy && rnd() % 4 == 0)
		complete_any();
}

void shim_mfc(int put, int fence, volatile void* ls, uint64_t ea, uint32_t size, uint32_t tag)
{
	dma_rules((uintptr_t)ls, ea, size);
	ls_check(ls, size);
	ea_ptr(ea, size, put);
	if (!put)
		memset((void*)ls, 0xDB, size);
	Cmd c = { put, fence, 0, (volatile uint8_t*)ls, ea, size, tag, 0 };
	enqueue(c);
}

void shim_mfcl(int put, volatile void* ls, uint64_t ea, volatile void* list, uint32_t lsize, uint32_t tag)
{
	if (lsize == 0 || lsize % 8 || lsize > 16384 || ((uintptr_t)list & 7))
		shim_die("list size or alignment", lsize, (uint32_t)(uintptr_t)list);
	// The list must sit in one of the four list arrays.
	const void* const arrays[4] = { lin[0], lin[1], lout[0], lout[1] };
	int inside = 0;
	for (int a = 0; a < 4; a++)
		if ((uintptr_t)list >= (uintptr_t)arrays[a] && (uintptr_t)list + lsize <= (uintptr_t)arrays[a] + sizeof(lin[0]))
			inside = 1;
	if (!inside)
		shim_die("list outside the list arrays", lsize, 0);
	// A get's whole destination is junk until its tag wait.
	const volatile uint32_t* const le = (const volatile uint32_t*)list;
	if (!put)
	{
		uint32_t total = 0;
		for (uint32_t k = 0; k < lsize / 8; k++)
			total += le[2 * k] & 0x7FFF;
		ls_check(ls, total);
		memset((void*)ls, 0xDB, total);
	}
	Cmd c = { put, 0, 1, (volatile uint8_t*)ls, ea, lsize, tag, le };
	enqueue(c);
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

void spu_setup(uint32_t vram_ea, uint32_t pal_ea)
{
	setup.vram_ea = vram_ea;
	setup.pal_ea = pal_ea;
}

// One job; nothing may be left queued after it (the job's own waits cover its DMA).
uint32_t spu_tex(const SpePixTex* b)
{
	const SpePixTex j = *b;
	const uint32_t res = tex_job(&j);
	if (nq)
		shim_die("texture job left commands queued", (uint32_t)nq, 0);
	return res;
}

void spu_fb(const SpePixFb* b)
{
	const SpePixFb j = *b;
	fb_job(&j);
	if (nq)
		shim_die("framebuffer job left commands queued", (uint32_t)nq, 0);
}
