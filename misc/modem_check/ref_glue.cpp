// modem_check: Flycast's sh4_sched (core/hw/sh4/sh4_sched.cpp) for the reference modem's one callback, ticked
// where the PS3 runs MediumUpdate; its interrupt line and its net::modbba queues. Built with fake_ref/ and ref/.
#include "types.h"
#include "modem.h"
#include "hw/holly/holly_intc.h"
#include "hw/sh4/sh4_sched.h"
#include "network/netservice.h"
#include <algorithm>
#include <deque>

// ---- sh4_sched.cpp: register, request, handle_cb and tick, for one entry ----
static sh4_sched_callback* s_cb;
static int s_tag;
static void* s_arg;
static int s_start = -1, s_end = -1;
static u64 s_now;

static u32 sched_now() { return (u32)s_now; }

int sh4_sched_register(int tag, sh4_sched_callback* ssc, void* arg)
{
	s_cb = ssc;
	s_tag = tag;
	s_arg = arg;
	s_start = s_end = -1;
	return 0;
}

void sh4_sched_unregister(int id)
{
	s_cb = nullptr;
	s_end = -1;
}

u64 sh4_sched_now64() { return s_now; }

// Flycast's int sums wrap past 2^31 (21 s): done here in u32, which gives the same bits without the UB.
void sh4_sched_request(int id, int cycles)
{
	s_start = (int)sched_now();
	if (cycles == -1)
		s_end = -1;
	else
	{
		s_end = (int)((u32)s_start + (u32)cycles);
		if (s_end == -1)
			s_end++;
	}
}

static void handle_cb()
{
	int remain = (int)((u32)s_end - (u32)s_start);
	int elapsd = (int)(sched_now() - (u32)s_start);
	s_start = (int)sched_now();
	int jitter = elapsd - remain;
	s_end = -1;
	int re_sch = s_cb(s_tag, remain, jitter, s_arg);
	if (re_sch > 0)
		sh4_sched_request(0, std::max(0, re_sch - jitter));
}

void sh4_sched_serialize(Serializer& ser, int id) {}
void sh4_sched_deserialize(Deserializer& deser, int id) {}

// ---- the interrupt line and the network ----
static bool s_irq;
static u32 s_irq_calls;
void asic_RaiseInterrupt(HollyInterruptID inter) { s_irq = inter == holly_EXP_8BIT; s_irq_calls++; }
void asic_CancelInterrupt(HollyInterruptID inter) { s_irq = !(inter == holly_EXP_8BIT) && s_irq; s_irq_calls++; }

static std::deque<u8> s_rx, s_tx;
static u32 s_starts, s_stops;
namespace net { namespace modbba {
// Flycast's DCNetThread::run() starts with toModem.clear().
bool start() { s_starts++; s_rx.clear(); return true; }
void stop() { s_stops++; }
void writeModem(u8 b) { s_tx.push_back(b); }
int readModem()
{
	if (s_rx.empty())
		return -1;
	int b = s_rx.front();
	s_rx.pop_front();
	return b;
}
int modemAvailable() { return (int)s_rx.size(); }
} }

extern "C" {
void ref_init() { ModemInit(); }
void ref_reset() { ModemReset(); }
u32 ref_read(u32 addr) { return ModemReadMem_A0_006(addr, 1); }
void ref_write(u32 addr, u32 v) { ModemWriteMem_A0_006(addr, v, 1); }
// Flycast's sh4_sched_tick: a callback due in this window runs once.
void ref_tick(u32 cycles)
{
	s_now += cycles;
	const u32 fztime = sched_now() - cycles;
	if (s_cb && s_end != -1)
	{
		const int remaining = (int)((u32)s_end - fztime);
		if (remaining >= 0 && remaining <= (int)cycles)
			handle_cb();
	}
}
int ref_irq() { return s_irq; }
u32 ref_irq_calls() { return s_irq_calls; }
u32 ref_starts() { return s_starts; }
u32 ref_stops() { return s_stops; }
void ref_rx_push(u8 b) { s_rx.push_back(b); }
int ref_tx_pop()
{
	if (s_tx.empty())
		return -1;
	int b = s_tx.front();
	s_tx.pop_front();
	return b;
}
u32 ref_rx_size() { return (u32)s_rx.size(); }
}
