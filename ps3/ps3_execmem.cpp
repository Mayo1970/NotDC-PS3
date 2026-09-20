// Executable-memory probe for the PS3 dynarec question (Dynarec.MD 3.1).
// Each test writes "li r3,42; blr" into a buffer, syncs the caches, and calls it.
//
// A failing test takes the whole process down (GameOS gives no recoverable DSI),
// so progress is journalled to PROBE_LOG before every attempt. Relaunch the app
// to resume at the next test; the log from earlier runs is replayed to UDP first.

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include <ppu-types.h>
#include <sys/memory.h>
#include <lv2/memory.h>

#include "ps3_execmem.h"
#include "config/config.h"

#define PROBE_LOG   "/dev_hdd0/data/NullDC/execprobe.log"
#define PROBE_MAGIC 42
#define PROBE_SIZE  (1024 * 1024)

// ELFv1: a function pointer points at a descriptor, never at code.
struct ppc_opd { u64 entry; u64 toc; u64 env; };

static u8 s_bss_buf[4096] __attribute__((aligned(128)));

// Inside the ELF's executable segment. Writing to it is part of the test.
static u8 s_text_buf[4096] __attribute__((aligned(128), section(".text"), used));

static u64 ps3_get_toc()
{
	u64 r2;
	__asm__ volatile("mr %0,2" : "=r"(r2));
	return r2;
}

static void ps3_icache_sync(void* p, u32 len)
{
	const u32 line = 128;   // Cell PPE cache line, not the Wii's 32
	u8* a = (u8*)((uintptr_t)p & ~(uintptr_t)(line - 1));
	u8* e = (u8*)p + len;

	for (u8* q = a; q < e; q += line)
		__asm__ volatile("dcbst 0,%0" :: "r"(q) : "memory");
	__asm__ volatile("sync" ::: "memory");

	for (u8* q = a; q < e; q += line)
		__asm__ volatile("icbi 0,%0" :: "r"(q) : "memory");
	__asm__ volatile("isync" ::: "memory");
}

static int ps3_try_exec(void* buf)
{
	u32* c = (u32*)buf;
	c[0] = 0x38600000 | PROBE_MAGIC;   // li r3,42
	c[1] = 0x4E800020;                 // blr
	ps3_icache_sync(buf, 8);

	static ppc_opd d;
	d.entry = (u64)(uintptr_t)buf;
	d.toc   = ps3_get_toc();
	d.env   = 0;

	int (*fn)(void) = (int (*)(void))(void*)&d;
	return fn();
}

static void probe_append(const char* fmt, ...)
{
	char line[192];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);

	printf("[execprobe] %s", line);

	FILE* f = fopen(PROBE_LOG, "a");
	if (!f)
		return;
	fputs(line, f);
	fclose(f);   // the next test may kill us, so land it on disk now
}

static int probe_next_index()
{
	FILE* f = fopen(PROBE_LOG, "r");
	if (!f)
		return 0;

	char line[192];
	int  next = 0;
	printf("[execprobe] --- log from earlier runs ---\n");
	while (fgets(line, sizeof(line), f))
	{
		printf("[execprobe] %s", line);
		int idx;
		if (sscanf(line, "a %d", &idx) == 1 && idx + 1 > next)
			next = idx + 1;
	}
	printf("[execprobe] --- end of earlier log ---\n");
	fclose(f);
	return next;
}

#define PROBE_COUNT 5

static const char* probe_name(int i)
{
	switch (i)
	{
	case 0: return ".bss static array (the Wii way)";
	case 1: return "malloc";
	case 2: return "sysMemoryAllocate PAGE_SIZE_1M";
	case 3: return "sysMMapper allocate + search_and_map";
	case 4: return "array in .text (linker section)";
	}
	return "?";
}

static void probe_run_one(int i)
{
	switch (i)
	{
	case 0:
		probe_append("r %d ret=%d\n", i, ps3_try_exec(s_bss_buf));
		break;

	case 1:
	{
		void* p = malloc(4096);
		if (!p)
		{
			probe_append("r %d SKIP malloc failed\n", i);
			break;
		}
		probe_append("r %d ret=%d ptr=%p\n", i, ps3_try_exec(p), p);
		free(p);
		break;
	}

	case 2:
	{
		sys_mem_addr_t addr = 0;
		s32 rc = sysMemoryAllocate(PROBE_SIZE, SYS_MEMORY_PAGE_SIZE_1M, &addr);
		if (rc != 0)
		{
			probe_append("r %d SKIP sysMemoryAllocate rc=0x%08X\n", i, (unsigned)rc);
			break;
		}
		probe_append("r %d ret=%d addr=0x%08X\n", i,
		             ps3_try_exec((void*)(uintptr_t)addr), (unsigned)addr);
		sysMemoryFree(addr);
		break;
	}

	case 3:
	{
		sys_mem_id_t   mem  = 0;
		sys_mem_addr_t resv = 0, mapped = 0;

		s32 rc = sysMMapperAllocateMemory(PROBE_SIZE, SYS_MEMORY_PAGE_SIZE_1M, &mem);
		if (rc != 0)
		{
			probe_append("r %d SKIP AllocateMemory rc=0x%08X\n", i, (unsigned)rc);
			break;
		}
		rc = sysMMapperAllocateAddress(PROBE_SIZE, SYS_MEMORY_PAGE_SIZE_1M, PROBE_SIZE, &resv);
		if (rc != 0)
		{
			probe_append("r %d SKIP AllocateAddress rc=0x%08X\n", i, (unsigned)rc);
			sysMMapperFreeMemory(mem);
			break;
		}
		rc = sysMMapperSearchAndMap(resv, mem, SYS_MEMORY_PROT_READ_WRITE, &mapped);
		if (rc != 0)
		{
			probe_append("r %d SKIP SearchAndMap rc=0x%08X\n", i, (unsigned)rc);
			sysMMapperFreeAddress(resv);
			sysMMapperFreeMemory(mem);
			break;
		}
		probe_append("r %d ret=%d mapped=0x%08X resv=0x%08X\n", i,
		             ps3_try_exec((void*)(uintptr_t)mapped),
		             (unsigned)mapped, (unsigned)resv);

		sys_mem_id_t out = 0;
		sysMMapperUnmapMemory(mapped, &out);
		sysMMapperFreeAddress(resv);
		sysMMapperFreeMemory(mem);
		break;
	}

	case 4:
		probe_append("r %d ret=%d ptr=%p\n", i, ps3_try_exec(s_text_buf), s_text_buf);
		break;
	}
}

void ps3_execmem_probe()
{
	const int mode = cfgLoadInt("ps3", "execprobe", 0);
	if (mode == 0)
		return;

	if (mode == 2)
	{
		remove(PROBE_LOG);
		printf("[execprobe] log reset (execprobe=2)\n");
	}

	printf("[execprobe] expecting %d from every test that survives\n", PROBE_MAGIC);

	int i = probe_next_index();
	if (i >= PROBE_COUNT)
	{
		printf("[execprobe] all %d tests already done. Set execprobe=0, or =2 to redo.\n",
		       PROBE_COUNT);
		return;
	}

	for (; i < PROBE_COUNT; i++)
	{
		probe_append("a %d %s\n", i, probe_name(i));
		probe_run_one(i);
	}

	printf("[execprobe] all %d tests done. Set execprobe=0 to stop probing.\n", PROBE_COUNT);
}
