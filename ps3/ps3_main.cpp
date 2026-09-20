// PS3 entry point, UDP log sink and os_* platform callbacks.
#include "types.h"
#include "stdclass.h"
#include "ps3_log.h"
#include "ps3_menu.h"
#include "ps3_presets.h"
#include "config/config.h"
#include "plugins/plugin_manager.h"
#include "dc/sh4/sh4_registers.h"
#include "plugs/drkPvr/drkPvr.h"

#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <ppu-types.h>
#include <sys/process.h>
#include <sys/systime.h>
#include <sys/thread.h>
#include <sys/mutex.h>
#include <sysutil/sysutil.h>
#include <sysmodule/sysmodule.h>
#include <net/net.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ogc/lwp_watchdog.h>

// 1 MB main thread stack: the SH4 interpreter and plugin init nest deep.
SYS_PROCESS_PARAM(1001, 0x100000);

#ifndef PS3_LOG_IP
#define PS3_LOG_IP "192.168.178.99"
#endif
#define PS3_LOG_PORT 18194

// Data root; BIOS lives in data/, nullDC.cfg at the top.
#define PS3_DATA_ROOT "/dev_hdd0/data/NullDC/"

uint64_t ps3_timebase_hz = 79800000u;

static int ps3_log_socket = -1;
static struct sockaddr_in ps3_log_addr;
static sys_mutex_t ps3_log_mutex;
static bool ps3_log_mutex_ok;
static volatile int ps3_exit_requested;
static volatile int ps3_monitor_quit;
static sys_ppu_thread_t ps3_monitor_tid;

// Wii main.cpp owned this; os_GetFile() and nullDC.cpp read it.
char selectedFilePath[512];

extern int EmuMain(int argc, char* argv[]);
extern void ps3_input_init();
extern void ps3_input_term();
extern "C" void wii_audio_init();
extern "C" void wii_audio_term();
extern void ps3_audio_stats();

// No block manager without a JIT. nullDC.cpp calls this with C linkage, which
// clashes with blockmanager.h, so it lives here and not in ps3_nojit.cpp.
extern "C" void bm_Reset() {}

static void ps3_log_send(const char* buf, int len)
{
	if (ps3_log_socket >= 0 && len > 0)
		sendto(ps3_log_socket, buf, len, 0, (struct sockaddr*)&ps3_log_addr, sizeof(ps3_log_addr));
}

extern "C" int ps3_log_vprintf(const char* fmt, va_list ap)
{
	// static, not stack: deep call chains on HW overran the stack in xashPS3.
	// The mutex guards it because the monitor thread logs too.
	static char buf[2048];
	if (ps3_log_mutex_ok)
		sysMutexLock(ps3_log_mutex, 0);
	int len = vsnprintf(buf, sizeof(buf), fmt, ap);
	if (len > (int)sizeof(buf) - 1)
		len = sizeof(buf) - 1;
	ps3_log_send(buf, len);
	if (ps3_log_mutex_ok)
		sysMutexUnlock(ps3_log_mutex);
	return len;
}

extern "C" int ps3_log_printf(const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int len = ps3_log_vprintf(fmt, ap);
	va_end(ap);
	return len;
}

// stdout has no console on PS3, so the linker routes printf/puts/putchar here (--wrap).
extern "C" int __wrap_printf(const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int len = ps3_log_vprintf(fmt, ap);
	va_end(ap);
	return len;
}

extern "C" int __wrap_vprintf(const char* fmt, va_list ap)
{
	return ps3_log_vprintf(fmt, ap);
}

extern "C" int __wrap_puts(const char* s)
{
	return ps3_log_printf("%s\n", s);
}

extern "C" int __wrap_putchar(int c)
{
	char ch = (char)c;
	ps3_log_send(&ch, 1);
	return c;
}

static void ps3_log_init()
{
	sys_mutex_attr_t attr;
	sysMutexAttrInitialize(attr);
	ps3_log_mutex_ok = (sysMutexCreate(&ps3_log_mutex, &attr) == 0);

	ps3_log_socket = socket(AF_INET, SOCK_DGRAM, 0);
	if (ps3_log_socket < 0)
		return;
	memset(&ps3_log_addr, 0, sizeof(ps3_log_addr));
	ps3_log_addr.sin_family = AF_INET;
	ps3_log_addr.sin_port = htons(PS3_LOG_PORT);
	ps3_log_addr.sin_addr.s_addr = inet_addr(PS3_LOG_IP);
}

// The front end polls this so an XMB exit closes the menu as well.
extern "C" int ps3_exit_pending()
{
	return ps3_exit_requested;
}

// XMB exit arrives here; only latch it, the emulator loop acts on it.
static void ps3_sysutil_cb(u64 status, u64 param, void* userdata)
{
	if (status == SYSUTIL_EXIT_GAME)
		ps3_exit_requested = 1;
}

// Separate thread so exit and heartbeat work even if the guest never reaches a vblank.
static void ps3_monitor_thread(void* arg)
{
	u32 ticks = 0;
	u32 last_vbl = 0;
	u32 stop_ticks = 0;

	while (!ps3_monitor_quit)
	{
		sysUtilCheckCallback();

		if (ps3_exit_requested)
		{
			if (stop_ticks == 0)
			{
				// sh4_cpu is still all-zero while the front end runs.
				printf("[ps3] XMB exit requested, stopping SH4\n");
				if (sh4_cpu.Stop)
					sh4_cpu.Stop();
			}
			// A stuck core (die() loops forever) never returns; an unanswered exit crashes the console.
			else if (stop_ticks >= 30)
			{
				printf("[ps3] SH4 did not stop in 3 s, forcing process exit\n");
				sysProcessExit(0);
			}
			stop_ticks++;
		}

		// Unlocked reads of emulator state: diagnostic only, a torn value is harmless.
		if (++ticks % 10 == 0)
		{
			u32 vbl = spg_VblankCountTotal;
			printf("[hb] t=%us pc=%08X vbl=%u (+%u)\n", ticks / 10, (u32)next_pc, vbl, vbl - last_vbl);
			last_vbl = vbl;
			ps3_audio_stats();
		}

		usleep(100 * 1000);
	}
	sysThreadExit(0);
}

// Largest single malloc that succeeds; the real heap ceiling differs per firmware.
static size_t ps3_largest_block()
{
	size_t lo = 0, hi = 256u * 1024u * 1024u, best = 0;
	for (int i = 0; i < 28 && lo < hi; i++)
	{
		size_t mid = lo + (hi - lo) / 2;
		void* p = malloc(mid);
		if (p)
		{
			free(p);
			best = mid;
			lo = mid + 1;
		}
		else
			hi = mid;
	}
	return best;
}

int os_GetFile(char* szFileName, char* szParse, u32 flags)
{
	size_t len = strlen(selectedFilePath);
	if (len == 0)
		return false;
	// .elf files boot through elf_loader, never through the disc plugin.
	if (len > 4 && strcasecmp(selectedFilePath + len - 4, ".elf") == 0)
		return false;
	strcpy(szFileName, selectedFilePath);
	return true;
}

double os_GetSeconds()
{
	// Delta from the first call: the absolute timebase loses precision as a double.
	static u64 t0 = 0;
	u64 now = gettime();
	if (t0 == 0)
		t0 = now;
	return (double)(now - t0) / (double)ps3_timebase_hz;
}

int os_msgbox(const wchar* text, unsigned int type)
{
	printf("OS_MSGBOX: %s\n", text);
	return 0;
}

int main(int argc, char* argv[])
{
	sysUtilRegisterCallback(SYSUTIL_EVENT_SLOT0, ps3_sysutil_cb, NULL);

	// The net module must be loaded before netInitialize() (ioQuake3-PS3, xashPS3).
	sysModuleLoad(SYSMODULE_NET);
	int net_ret = netInitialize();
	ps3_log_init();

	ps3_timebase_hz = sysGetTimebaseFrequency();

	printf("[ps3] nullDC PS3 " __DATE__ " " __TIME__ "\n");
	printf("[ps3] netInitialize=%d timebase=%llu Hz sizeof(void*)=%u\n",
	       net_ret, (unsigned long long)ps3_timebase_hz, (unsigned)sizeof(void*));
	printf("[ps3] largest free block at boot: %u KB\n", (unsigned)(ps3_largest_block() / 1024));

	mkdir("/dev_hdd0/data", 0777);
	mkdir(PS3_DATA_ROOT, 0777);
	mkdir(PS3_DATA_ROOT "data", 0777);
	SetApplicationPath(PS3_DATA_ROOT);

	s32 trc = sysThreadCreate(&ps3_monitor_tid, ps3_monitor_thread, NULL, 1500, 0x10000,
	                          THREAD_JOINABLE, (char*)"nulldc_monitor");
	if (trc != 0)
		printf("[ps3] monitor thread create failed: 0x%08X\n", (unsigned)trc);

	ps3_input_init();
	wii_audio_init();

	// The front end needs the settings before EmuMain opens the cfg itself.
	int rv = 0;
	if (!cfgOpen())
		printf("[ps3] cfgOpen failed, using built-in defaults\n");
	ps3_presets_load();

	if (!ps3_menu_run())
	{
		printf("[ps3] front end quit, exiting to XMB\n");
		goto shutdown;
	}

	{
		char* av[] = { (char*)"nulldc", NULL };
		rv = EmuMain(1, av);
	}
	printf("[ps3] EmuMain returned %d, exiting to XMB\n", rv);

shutdown:

	// Plugin term may not run on XMB exit; wii_audio_term() is safe to call twice.
	wii_audio_term();
	ps3_input_term();

	if (trc == 0)
	{
		u64 thread_rv;
		ps3_monitor_quit = 1;
		sysThreadJoin(ps3_monitor_tid, &thread_rv);
	}

	if (ps3_log_socket >= 0)
		close(ps3_log_socket);
	// No netDeinitialize(): it froze shutdown on HW in xashPS3.
	sysUtilUnregisterCallback(SYSUTIL_EVENT_SLOT0);
	return 0;
}
