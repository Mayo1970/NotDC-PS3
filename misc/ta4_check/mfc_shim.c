// Bundle 4 PC check: the MFC model of misc/spevb_check/spu_side.c, shared by the two SPU decoder builds. Size and
// alignment rules, a 16-entry queue, random completion order; a put reads LS when it completes.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EA0 0x10000000u

uint8_t* g_arena;
uint32_t g_arena_size;
double shim_re_err = 1.0 / 4096;
uint64_t g_seed = 1;

void shim_die(const char* why, uint32_t a, uint32_t b)
{
	printf("FAIL: shim: %s (%08X %08X)\n", why, a, b);
	fflush(stdout);
	if (getenv("TA4_ABORT"))
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
		memcpy(ea_ptr(c.ea, c.size), (const void*)c.ls, c.size);
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

int shim_queued(void) { return nq; }

void spu_arena(uint32_t size)
{
	g_arena = (uint8_t*)aligned_alloc(128, size);
	g_arena_size = size;
	memset(g_arena, 0, size);
}

uint32_t spu_ea(const void* p) { return EA0 + (uint32_t)((const uint8_t*)p - g_arena); }
