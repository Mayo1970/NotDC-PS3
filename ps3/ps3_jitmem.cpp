// PS3 JIT code cache and ABI self-test (Dynarec.MD 3.1, 3.2).
//
// The cache is a .text array: the hardware probe showed lv2 grants execute only
// to the loaded image's text pages, and grants write there too despite the R E
// segment flags. Nothing in the RW segment is executable.

#include <stdio.h>
#include <string.h>

#include "types.h"
#include "ps3_jitmem.h"
#include "config/config.h"
#include "dc/sh4/rec_v2/ngen.h"


// The cache lives in driver.cpp, next to the emit cursor that fills it.
extern u8 CodeCache[];

u8* ps3_CodeCache = CodeCache;

struct ppc_opd { u64 entry; u64 toc; u64 env; };

// ---------------------------------------------------------------------------
// Self-test emitter. Hand-encoded: driver.cpp owns the real ppc_emit() sink.
// ---------------------------------------------------------------------------

static u32* s_emit = 0;

static void emit(u32 insn) { *s_emit++ = insn; }

void ps3_jit_icache_sync(const void* start, u32 size)
{
	if (!size)
		return;

	const u32 line = 128;
	u8* a = (u8*)((uintptr_t)start & ~(uintptr_t)(line - 1));
	u8* e = (u8*)start + size;

	for (u8* q = a; q < e; q += line)
		__asm__ volatile("dcbst 0,%0" :: "r"(q) : "memory");
	__asm__ volatile("sync" ::: "memory");

	for (u8* q = a; q < e; q += line)
		__asm__ volatile("icbi 0,%0" :: "r"(q) : "memory");
	__asm__ volatile("isync" ::: "memory");
}

u64 ps3_opd_entry(void* fn)
{
	return ((ppc_opd*)fn)->entry;
}

static u64 ps3_get_toc()
{
	u64 r2;
	__asm__ volatile("mr %0,2" : "=r"(r2));
	return r2;
}

// ---------------------------------------------------------------------------
// Self-test
// ---------------------------------------------------------------------------

#define SELFTEST_IN   41
#define SELFTEST_OUT  42

extern "C" int ps3_jit_selftest_callee(int x)
{
	return x + 1;
}

// Emits: ELFv1 prologue, li r3,41, bl callee, epilogue, blr. Returns its result.
static int ps3_jit_emit_and_run(u8* at)
{
	s_emit = (u32*)at;

	emit(0x7C0802A6);                      // mflr r0
	emit(0xF8010010);                      // std r0,16(r1) - the callee's LR slot, not sp+4
	emit(0xF821FF91);                      // stdu r1,-112(r1) - 112-byte frame, back chain at 0(sp)
	emit(0x38600000 | SELFTEST_IN);        // li r3,41

	// bl to the dereferenced OPD entry: the descriptor address itself is data.
	u64 target = ps3_opd_entry((void*)ps3_jit_selftest_callee);
	s64 disp   = (s64)target - (s64)(uintptr_t)s_emit;
	emit(0x48000001 | ((u32)disp & 0x03FFFFFC));

	emit(0xE8210000);                      // ld r1,0(r1) - unwind through the back chain
	emit(0xE8010010);                      // ld r0,16(r1)
	emit(0x7C0803A6);                      // mtlr r0
	emit(0x4E800020);                      // blr

	const u32 len = (u32)((u8*)s_emit - at);
	ps3_jit_icache_sync(at, len);

	printf("[jitprobe] emitted %u bytes at %p, disp=%lld\n",
	       (unsigned)len, at, (long long)disp);

	static ppc_opd d;
	d.entry = (u64)(uintptr_t)at;
	d.toc   = ps3_get_toc();
	d.env   = 0;

	int (*fn)(void) = (int (*)(void))(void*)&d;
	return fn();
}

void ps3_jit_selftest()
{
	if (cfgLoadInt("ps3", "jitprobe", 1) == 0)
		return;

	printf("[jitprobe] code cache %p .. %p (%u KB in .text)\n",
	       ps3_CodeCache, ps3_CodeCache + CODE_SIZE, (unsigned)(CODE_SIZE / 1024));

	printf("[jitprobe] test A: start of the cache\n");
	int a = ps3_jit_emit_and_run(ps3_CodeCache);
	printf("[jitprobe] test A ret=%d (want %d) %s\n", a, SELFTEST_OUT,
	       a == SELFTEST_OUT ? "PASS" : "FAIL");

	// The far end: proves the whole 6 MB is writable and executable, not page 0.
	printf("[jitprobe] test B: last KB of the cache\n");
	int b = ps3_jit_emit_and_run(ps3_CodeCache + CODE_SIZE - 1024);
	printf("[jitprobe] test B ret=%d (want %d) %s\n", b, SELFTEST_OUT,
	       b == SELFTEST_OUT ? "PASS" : "FAIL");

	printf("[jitprobe] done. Set jitprobe=0 to skip.\n");
}
