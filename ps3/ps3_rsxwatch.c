// RSX stall log, see ps3_rsxwatch.h. Built with -mminimal-toc (Makefile): its texts take one TOC entry, not one each.
#include <stdio.h>
#include <string.h>
#include "ps3_timebase.h"
#include "ps3_rsxwatch.h"

// ps3_rsxd.cpp: the counts of the frame in progress, cleared at its begin.
void ps3rsxd_frame_counts(u32* draws, u32* verts, u32* binds, u32* allocs);

#define RW_FRAMES 16
#define RW_WORDS  8

typedef struct { u32 flip, put, vbl, verts, draws, binds, allocs, ns_w, ns_h; } RwFrame;

static struct
{
	RwFrame ring[RW_FRAMES];
	RwFrame next;
	u32 head;
	u64 skip_tb;   // the first skipped frame of this streak, 0 = none
	u32 skips, dumps;
} s_rw;

void ps3_rsxwatch_frame(u32 ns_w, u32 ns_h)
{
	RwFrame* f = &s_rw.next;
	ps3rsxd_frame_counts(&f->draws, &f->verts, &f->binds, &f->allocs);
	f->ns_w = ns_w;
	f->ns_h = ns_h;
}

static double rw_seconds(u64 since)
{
	return (double)(gettime() - since) / (double)ps3_timebase_hz;
}

void ps3_rsxwatch_flip(u32 flip)
{
	RwFrame* f = &s_rw.ring[s_rw.head++ % RW_FRAMES];
	*f = s_rw.next;
	f->flip = flip;
	f->put = gcmGetControlRegister()->put;
	f->vbl = (u32)gcmGetVBlankCount();
	memset(&s_rw.next, 0, sizeof(s_rw.next));
	if (s_rw.dumps)
		printf("[rsxw] flips back after %.1f s, %u frames skipped\n", rw_seconds(s_rw.skip_tb), (unsigned)s_rw.skips);
	s_rw.skip_tb = 0;
	s_rw.skips = s_rw.dumps = 0;
}

// The RSX's queue: put is what the PPU sent, get what the RSX has read. The words at get show the command it is on.
static void rw_dump(gcmContextData* ctx, u32 queued, u32 done, double dt)
{
	gcmControlRegister volatile* c = gcmGetControlRegister();
	const u32 put = c->put, get = c->get, ref = c->ref;
	const u32* b = ctx->begin;
	const u32* e = ctx->end;
	printf("[rsxw] no flip for %.1f s: get=%08X put=%08X ref=%08X | flips queued=%u done=%u status=%u | vblank %u"
	       " | skips %u | cb %p..%p at %p\n", dt, (unsigned)get, (unsigned)put, (unsigned)ref, (unsigned)queued,
	       (unsigned)done, (unsigned)gcmGetFlipStatus(), (unsigned)gcmGetVBlankCount(), (unsigned)s_rw.skips,
	       (const void*)b, (const void*)e, (const void*)ctx->current);
	void* a = NULL;
	if (gcmIoOffsetToAddress(get & ~3u, &a) == 0 && a)
	{
		const u32* w = (const u32*)a;
		const u32* from = w;
		const u32* to = w + RW_WORDS;
		// Inside the command buffer the words before get are read too; elsewhere only from get on.
		if (w >= b && w < e)
		{
			from = w - RW_WORDS < b ? b : w - RW_WORDS;
			to = to > e ? e : to;
		}
		char line[2 * RW_WORDS * 11 + 1];
		int n = 0;
		for (const u32* p = from; p < to; p++)
			n += snprintf(line + n, sizeof(line) - n, p == w ? " [%08X]" : " %08X", (unsigned)*p);
		printf("[rsxw] words at %p (get at %d):%s\n", (const void*)from, (int)(w - from), line);
	}
	else
		printf("[rsxw] get %08X is no IO address\n", (unsigned)get);
	// The last frames, the oldest first.
	const u32 have = s_rw.head < RW_FRAMES ? s_rw.head : RW_FRAMES;
	for (u32 i = s_rw.head - have; i != s_rw.head; i++)
	{
		const RwFrame* f = &s_rw.ring[i % RW_FRAMES];
		printf("[rsxw] flip %u vblank %u put=%08X: %u verts, %u draws, %u binds, %u new tex, scene %ux%u\n",
		       (unsigned)f->flip, (unsigned)f->vbl, (unsigned)f->put, (unsigned)f->verts, (unsigned)f->draws,
		       (unsigned)f->binds, (unsigned)f->allocs, (unsigned)f->ns_w, (unsigned)f->ns_h);
	}
}

void ps3_rsxwatch_skip(gcmContextData* ctx, u32 queued, u32 done)
{
	if (!s_rw.skip_tb)
		s_rw.skip_tb = gettime();
	s_rw.skips++;
	const double dt = rw_seconds(s_rw.skip_tb);
	// The second dump tells whether get moved in between.
	if ((s_rw.dumps == 0 && dt >= 1.0) || (s_rw.dumps == 1 && dt >= 5.0))
	{
		s_rw.dumps++;
		rw_dump(ctx, queued, done, dt);
	}
}
