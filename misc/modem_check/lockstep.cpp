// modem_check: plugs/nullExtDev/nullExtDev.cpp in lockstep with Flycast's modem.cpp of df2bdfc9b (ref/, through
// ref_glue.cpp) on one stream of register writes, reads, ticks and network bytes. Any difference fails.
#include "types.h"
#include "ps3/ps3_presets.h"
#include "ps3/ps3_dcnet.h"
#include <deque>
#include <stdarg.h>

extern "C" {
void ref_init();
void ref_reset();
u32 ref_read(u32 addr);
void ref_write(u32 addr, u32 v);
void ref_tick(u32 cycles);
int ref_irq();
u32 ref_irq_calls();
u32 ref_starts();
u32 ref_stops();
void ref_rx_push(u8 b);
int ref_tx_pop();
u32 ref_rx_size();
}

// The PS3 build's entry points.
s32 FASTCALL libExtDevice_Init(ext_device_init_params* p);
void FASTCALL libExtDevice_Reset(bool Manual);
u32 FASTCALL libExtDevice_ReadMem_A0_006(u32 addr, u32 size);
void FASTCALL libExtDevice_WriteMem_A0_006(u32 addr, u32 data, u32 size);
void FASTCALL libExtDevice_Update(u32 cycles);

int g_modem_preset = 1;
extern "C" int get_sh4_clock_preset() { return 200; }

static bool s_irq;
static u32 s_irq_calls;
static void FASTCALL ours_raise(HollyInterruptID id) { s_irq = id == holly_EXP_8BIT; s_irq_calls++; }
static void FASTCALL ours_cancel(HollyInterruptID id) { s_irq = id != holly_EXP_8BIT && s_irq; s_irq_calls++; }

static std::deque<u8> s_rx, s_tx;
static u32 s_starts, s_stops;
void dcnet_start() { s_starts++; s_rx.clear(); }
void dcnet_stop() { s_stops++; }
void dcnet_write(u8 b) { s_tx.push_back(b); }
int dcnet_read()
{
	if (s_rx.empty())
		return -1;
	int b = s_rx.front();
	s_rx.pop_front();
	return b;
}

// The checked file's log lines (-include log.h).
enum { L_RESET, L_DIAL, L_RING, L_HAND, L_CONN, L_WARN, L_INIT, L_N };
static u32 s_log[L_N];
static char s_last_conn[160];
extern "C" int modem_log(const char* fmt, ...)
{
	char b[160];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(b, sizeof(b), fmt, ap);
	va_end(ap);
	if (!strcmp(b, "[modem] reset\n")) s_log[L_RESET]++;
	else if (!strcmp(b, "[modem] dialing\n")) s_log[L_DIAL]++;
	else if (!strcmp(b, "[modem] ringing\n")) s_log[L_RING]++;
	else if (!strcmp(b, "[modem] handshake\n")) s_log[L_HAND]++;
	else if (!strncmp(b, "[modem] connected: ", 19)) { s_log[L_CONN]++; snprintf(s_last_conn, sizeof(s_last_conn), "%s", b); }
	else if (!strncmp(b, "[modem] Modem ", 14)) s_log[L_INIT]++;
	else s_log[L_WARN]++;
	return (int)strlen(b);
}

static u32 s_rng;
static u32 rnd() { s_rng ^= s_rng << 13; s_rng ^= s_rng >> 17; s_rng ^= s_rng << 5; return s_rng; }
static u32 rnd(u32 n) { return rnd() % n; }

static int g_fail;
static u64 g_ops, g_reads, g_tx_bytes, g_rx_bytes;
static const char* g_op = "";
#define CHECK(c, ...) do { if (!(c)) { if (g_fail++ < 10) { printf("FAIL (op %llu, %s): ", (unsigned long long)g_ops, g_op); \
	printf(__VA_ARGS__); printf("\n"); } } } while (0)

enum { TICK = 7168, MS = 200000 };   // the FAST preset's medium update; SH4 cycles per ms at 200 MHz
static u32 reg_addr(u32 reg) { return 0x00600000 + ((reg + 0x100) << 2); }

static void compare()
{
	CHECK(ref_irq() == (int)s_irq, "interrupt line: Flycast %d, ours %d", ref_irq(), (int)s_irq);
	CHECK(ref_irq_calls() == s_irq_calls, "interrupt calls: Flycast %u, ours %u", ref_irq_calls(), s_irq_calls);
	CHECK(ref_starts() == s_starts && ref_stops() == s_stops, "network start/stop: Flycast %u/%u, ours %u/%u",
	      ref_starts(), ref_stops(), s_starts, s_stops);
	for (;;)
	{
		const int a = ref_tx_pop();
		const int b = s_tx.empty() ? -1 : s_tx.front();
		if (b >= 0)
			s_tx.pop_front();
		CHECK(a == b, "byte to DCNet: Flycast %d, ours %d", a, b);
		if (a < 0 || b < 0)
			break;
		g_tx_bytes++;
	}
	CHECK(ref_rx_size() == s_rx.size(), "bytes from DCNet not taken: Flycast %u, ours %u", ref_rx_size(), (u32)s_rx.size());
}

static u32 read_reg(u32 reg)
{
	g_ops++;
	g_op = "read";
	const u32 a = ref_read(reg_addr(reg)), b = libExtDevice_ReadMem_A0_006(reg_addr(reg), 1);
	CHECK(a == b, "read reg %02X: Flycast %02X, ours %02X", reg, a, b);
	g_reads++;
	compare();
	return a;
}

static void write_reg(u32 reg, u32 v)
{
	g_ops++;
	g_op = "write";
	ref_write(reg_addr(reg), v);
	libExtDevice_WriteMem_A0_006(reg_addr(reg), v, 1);
	compare();
}

static void ticks(u32 n)
{
	g_op = "tick";
	for (u32 i = 0; i < n; i++)
	{
		g_ops++;
		ref_tick(TICK);
		libExtDevice_Update(TICK);
		compare();
		if (g_fail)
			return;
	}
}

static void wait_ms(u32 ms) { ticks((u32)((u64)ms * MS / TICK) + 1); }

static void push_rx(u32 n)
{
	for (u32 i = 0; i < n; i++)
	{
		const u8 b = (u8)rnd();
		ref_rx_push(b);
		s_rx.push_back(b);
	}
	compare();
}

// A write as a game's driver makes it, or any value.
static u32 value_for(u32 reg)
{
	if (rnd(4) == 0)
		return rnd(256);
	switch (reg)
	{
	case 0x08: return (rnd(2) ? 0x80 : 0) | (rnd(4) ? 1 : 0);                   // ASYN, RTS
	case 0x09: return 0x01 | (rnd(2) ? 0x04 : 0) | (rnd(4) == 0 ? 0x20 : 0);   // DTR, DATA, DTMF
	case 0x12: return rnd(2) ? 0xAA : rnd(256);                                 // V.8 auto
	case 0x15: return rnd(2) ? 0x08 : 0;                                        // AUTO
	case 0x1E: return rnd(256) & 0x24;                                          // TDBIE, RDBIE
	case 0x1F: return rnd(256) & 0x1D;                                          // NSIE NEWS NCIE NEWC
	case 0x1A: return rnd(8) == 0 ? 0x80 : rnd(256) & 0x77;                     // SFRES
	case 0x14: return rnd(3) == 0 ? (rnd(2) ? 0x4F : 0x5F) : (rnd(2) ? 0 : rnd(256));   // module download
	default: return rnd(256);
	}
}

// One DSP RAM byte, as a driver sets an interrupt mask: MEDAL, the address, MEACC with MEMW.
static void dsp_write(u32 addr, u32 v)
{
	write_reg(0x18, v);
	write_reg(0x1C, addr & 0xFF);
	write_reg(0x1D, 0x80 | 0x20 | (addr >> 8));
}

// DSP RAM: the address, MEDAL/MEDAM for a write, then MEACC with or without MEMW. Not 0xFFF: Flycast reads past it.
static void dsp_access()
{
	u32 addr = rnd(0xFFF);
	if (rnd(2))
	{
		write_reg(0x18, rnd(256));
		if (rnd(2))
			write_reg(0x19, rnd(256));
	}
	write_reg(0x1C, addr & 0xFF);
	write_reg(0x1D, 0x80 | (rnd(2) ? 0x20 : 0) | (rnd(4) == 0 ? 0x10 : 0) | (addr >> 8));
	read_reg(0x18);
	read_reg(0x19);
}

// Every DSP RAM byte, two at a time through MEDAL and MEDAM.
static void dsp_dump()
{
	for (u32 a = 0; a < 0xFFF; a += 2)
	{
		write_reg(0x1C, a & 0xFF);
		write_reg(0x1D, 0x80 | (a >> 8));
		read_reg(0x18);
		read_reg(0x19);
	}
}

static void read_all()
{
	for (u32 r = 0; r <= 0x20; r++)
		read_reg(r);
	// The ID area and an address past the registers.
	g_op = "read id";
	const u32 id = rnd(0x100);
	CHECK(ref_read(0x00600000 + (id << 2)) == libExtDevice_ReadMem_A0_006(0x00600000 + (id << 2), 1), "ID read %02X", id);
	CHECK(ref_read(reg_addr(0x21 + rnd(0xDE))) == 0, "past the registers");
}

// One call: reset, self test, set-up, dial, ring, answer, data; noise anywhere.
static void episode()
{
	write_reg(0x20, 0);
	write_reg(0x20, rnd(4) ? 1 : 1 + rnd(255));
	wait_ms(rnd(4) ? 120 + rnd(100) : rnd(150));
	read_all();
	// A driver ends each module download it starts; no reset ends one.
	if (rnd(3) == 0)
	{
		write_reg(0x14, rnd(2) ? 0x4F : 0x5F);
		for (u32 n = rnd(40); n; n--)
			write_reg(0x10, rnd(256));
		read_reg(0x0D);
		write_reg(0x14, 0);
		read_reg(0x16);
		read_reg(0x17);
		read_reg(0x1B);
	}
	else
		write_reg(0x14, 0);
	// The NEWS interrupt masks of the status registers (regs_int_mask_addr).
	static const u16 k_masks[] = { 0x247, 0x246, 0x245, 0x244, 0x243, 0x242, 0x241, 0x089, 0x38a, 0x370, 0x371, 0x27d, 0x27c };
	for (u32 n = rnd(8); n; n--)
		dsp_write(k_masks[rnd(sizeof(k_masks) / sizeof(k_masks[0]))], rnd(3) ? 0xFF : rnd(256));
	for (u32 n = rnd(12); n; n--)
	{
		static const u8 k_cfg[] = { 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x11, 0x12, 0x13, 0x15, 0x1A, 0x1E, 0x1F };
		const u32 reg = k_cfg[rnd(sizeof(k_cfg))];
		write_reg(reg, value_for(reg));
		if (rnd(3) == 0)
			dsp_access();
	}
	write_reg(0x1F, 0x01 | (rnd(2) ? 0x04 : 0) | (rnd(2) ? 0x10 : 0));   // NEWC, NCIE, NSIE
	if (rnd(8) == 0)
	{
		ticks(rnd(40));
		return;
	}
	// Dial: TBUFFER bytes with gaps under a second. Some gaps end next to 900 ms after the 100 ms callback: there
	// Flycast's jitter term in the 1 s test decides (100 ms is 2791 ticks, 900 ms past it 25111).
	for (u32 n = 1 + rnd(8); n; n--)
	{
		write_reg(0x10, '0' + rnd(10));
		if (rnd(3) == 0)
			ticks(2791 + 25108 + rnd(7));
		else
			wait_ms(20 + rnd(300));
		if (rnd(4) == 0)
			read_reg(0x0B);
	}
	wait_ms(900 + rnd(400));
	read_reg(0x0F);
	if (rnd(6) == 0)
		write_reg(0x15, 0x08);
	write_reg(0x09, 0x01 | (rnd(5) ? 0x04 : 0));
	if (rnd(5) == 0)
		write_reg(0x08, 0x81);
	wait_ms(rnd(4) ? 520 + rnd(400) : rnd(520));
	read_all();
	// Data both ways.
	write_reg(0x08, (rnd(3) ? 0x81 : 0x01));
	write_reg(0x1E, rnd(256) & 0x24);
	for (u32 n = 50 + rnd(400); n; n--)
	{
		switch (rnd(10))
		{
		case 0: case 1: case 2:
			write_reg(0x10, rnd(256));
			break;
		case 3:
			push_rx(1 + rnd(20));
			break;
		case 4: case 5:
			if (read_reg(0x1E) & 0x01)
			{
				read_reg(0x00);
				g_rx_bytes++;
			}
			break;
		case 6:
			ticks(1 + rnd(12));
			break;
		case 7:
			read_reg(rnd(0x21));
			break;
		case 8:
			if (rnd(30) == 0)
				dsp_access();
			else if (rnd(60) == 0)
			{
				write_reg(0x1A, 0x80);
				write_reg(0x1F, 0x01);
			}
			else if (rnd(80) == 0)
				write_reg(0x20, 0);
			else
				write_reg(rnd(0x20), rnd(256));
			break;
		default:
			ticks(1);
			break;
		}
		if (g_fail)
			return;
	}
	dsp_dump();
	if (rnd(10) == 0)
	{
		g_op = "reset";
		ref_reset();
		libExtDevice_Reset(false);
		compare();
	}
}

int main(int argc, char** argv)
{
	s_rng = argc > 2 ? (u32)atoi(argv[2]) : 0x2545F491;
	if (!s_rng)
		s_rng = 1;
	const int episodes = argc > 1 ? atoi(argv[1]) : 300;
	ext_device_init_params p;
	p.RaiseInterrupt = ours_raise;
	p.CancelInterrupt = ours_cancel;
	p.SB_ISTEXT = 0;

	// Row OFF: the old stub, no device at 0x600000.
	g_modem_preset = 0;
	libExtDevice_Init(&p);
	libExtDevice_WriteMem_A0_006(reg_addr(0x20), 0, 1);
	for (u32 r = 0; r <= 0x20; r++)
	{
		libExtDevice_WriteMem_A0_006(reg_addr(r), r == 0x20 ? 1 : 0xFF, 1);
		libExtDevice_Update(TICK * 100);
	}
	for (u32 a = 0; a < 0x800; a += 4)
		CHECK(libExtDevice_ReadMem_A0_006(0x00600000 + a, 1) == 0, "row OFF: %06X reads a value", 0x600000 + a);
	CHECK(s_irq_calls == 0 && s_starts == 0 && s_stops == 0 && s_log[L_INIT] == 1, "row OFF: the stub calls something");

	g_modem_preset = 1;
	libExtDevice_Init(&p);
	ref_init();
	for (int e = 0; e < episodes && !g_fail; e++)
		episode();

	printf("episodes %d, ops %llu, reads %llu; calls %u, rings %u, answers %u, connected %u (last: %.*s), bytes to DCNet "
	       "%llu, from DCNet %llu, warnings %u\n", episodes, (unsigned long long)g_ops, (unsigned long long)g_reads,
	       s_log[L_DIAL], s_log[L_RING], s_log[L_HAND], s_log[L_CONN], (int)strcspn(s_last_conn, "\n"), s_last_conn,
	       (unsigned long long)g_tx_bytes, (unsigned long long)g_rx_bytes, s_log[L_WARN]);
	// A run that never connects or moves data tests too little.
	CHECK(s_log[L_CONN] >= (u32)episodes / 4 && g_tx_bytes > (u64)episodes * 5 && g_rx_bytes > (u64)episodes * 2 &&
	      s_log[L_RESET] > 0,
	      "the run reached too little: %u connected, %llu and %llu bytes", s_log[L_CONN], (unsigned long long)g_tx_bytes,
	      (unsigned long long)g_rx_bytes);
	if (g_fail)
	{
		printf("FAIL: %d check(s)\n", g_fail);
		return 1;
	}
	printf("PASS: nullExtDev.cpp = Flycast df2bdfc9b modem.cpp on every read, interrupt and network byte\n");
	return 0;
}
