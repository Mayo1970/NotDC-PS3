// OPTIMIZATION_PLAN 5.15: the ARM7, nullAICA's registers and the caique control twin on an SPU. Runs the PPU's command
// ring (MediumUpdates, SH4 register accesses) and writes the AICA log that the sound SPU (spe_aica) plays.
#include <spu_intrinsics.h>
#include <spu_mfcio.h>
#include <sys/spu_thread.h>
#include <stddef.h>
#include "aica_model.h"
#include "spe_aica.h"
#include "spe_arm7.h"
#include "aica_ctl.h"

#define TAG_CMD  1
#define TAG_MEM  2         // ARAM gets, ARAM write puts and log puts: all fenced, so one ordered stream
#define TAG_AECHO 3
#define DEC_TICKS 79800    // 1 ms at the 79.8 MHz timebase: fallback wake
#define CMD_BATCH 64
#define REC_BATCH 64
#define LINES 256          // ARAM cache, 32 KB direct-mapped
#define STAGE_N 64         // put sources for uncached bytes; more than the MFC queue holds, so a reused one is done
#define LOG_WAIT_MAX (79800u * 100)   // 100 ms without sound SPU progress: drop the log

extern char _end[];

static ArmBell bell;
static ArmEcho st;         // counters; copied to the put buffer
static ArmEcho eb;
static ArmSetup setup;
static ArmCmd cmds[CMD_BATCH] __attribute__((aligned(128)));
static uint64_t bell_ea, echo_ea;

static void wait_tag(int tag)
{
	mfc_write_tag_mask(1u << tag);
	mfc_read_tag_status_all();
}

// One atomic line, so the PPU never sees a read reply without its value. The ARAM puts land first: a command
// reported done has all its ARM7 stores in memory.
static void put_echo(void)
{
	wait_tag(TAG_MEM);
	eb = st;
	eb.irq = ac.sh4_irq;
	eb.samples = ac.smp;
	mfc_putlluc(&eb, echo_ea, 0, 0);
	mfc_read_atomic_status();
}

// ── ARAM: PS3 aica_ram holds DC byte a at host byte a ^ 3, so a 32-bit word reads as a big-endian word ──

static uint8_t line_data[LINES][128] __attribute__((aligned(128)));
static uint32_t line_tag[LINES];   // DC line address | 1
static uint8_t stage[STAGE_N][16] __attribute__((aligned(16)));
static uint32_t stage_i;

static void aram_invalidate_all(void) { memset(line_tag, 0, sizeof(line_tag)); }

// lo..hi: DC line numbers (address >> 7) the SH4 wrote
static void aram_invalidate(uint32_t lo, uint32_t hi)
{
	if (hi - lo >= LINES)
	{
		aram_invalidate_all();
		return;
	}
	for (uint32_t l = lo; l <= hi; l++)
		if (line_tag[l & (LINES - 1)] == ((l << 7) | 1))
			line_tag[l & (LINES - 1)] = 0;
}

static inline uint8_t* aram_line(uint32_t a)
{
	const uint32_t la = a & 0x1FFF80u, i = (la >> 7) & (LINES - 1);
	if (__builtin_expect(line_tag[i] != (la | 1), 0))
	{
		mfc_getf(line_data[i], setup.aram_ea + la, 128, TAG_MEM, 0, 0);   // fenced: after this SPU's earlier puts
		wait_tag(TAG_MEM);
		line_tag[i] = la | 1;
		st.misses++;
	}
	return line_data[i];
}

// h: host byte offset, n = 1 or 4 (aligned). Write-through: a cached copy changes too; the bytes go out at once.
static void aram_store(uint32_t h, uint32_t v, uint32_t n)
{
	const uint32_t la = h & 0x1FFF80u, i = (la >> 7) & (LINES - 1);
	uint8_t* p = line_tag[i] == (la | 1) ? &line_data[i][h & 127] : &stage[stage_i++ & (STAGE_N - 1)][h & 15];
	if (n == 1)
		*p = (uint8_t)v;
	else
		*(uint32_t*)p = v;
	mfc_putf(p, setup.aram_ea + h, n, TAG_MEM, 0, 0);
	st.puts++;
}

// ── ARM7 memory (arm_mem.cpp ReadMemArm / WriteMemArm) ──

static inline uint32_t a7_fetch(uint32_t a) { return *(const uint32_t*)(aram_line(a) + (a & 124)); }

static uint32_t a7_rd32(uint32_t addr)
{
	addr &= 0xFFFFFF;
	if (addr >= 0x800000)
		return ac_arm_read(addr, 4);
	const uint32_t w = *(const uint32_t*)(aram_line(addr & 0x1FFFFF) + (addr & 124));
	if (!(addr & 3))
		return w;
	const uint32_t rot = (addr & 3) * 8;   // ARM7DI: the aligned word, rotated
	return (w >> rot) | (w << (32 - rot));
}

static uint32_t a7_rd8(uint32_t addr)
{
	addr &= 0xFFFFFF;
	if (addr >= 0x800000)
		return ac_arm_read(addr, 1);
	const uint32_t a = (addr ^ 3) & 0x1FFFFF;
	return aram_line(a)[a & 127];
}

static void a7_wr32(uint32_t addr, uint32_t v)
{
	addr &= 0xFFFFFF;
	if (addr >= 0x800000)
	{
		ac_arm_write(addr, v, 4);
		return;
	}
	if (addr & 3)
	{
		for (uint32_t i = 0; i < 4; i++)
			aram_store(((addr + i) ^ 3) & 0x1FFFFF, v >> (8 * i), 1);
		return;
	}
	aram_store(addr & 0x1FFFFF, v, 4);
}

static void a7_wr8(uint32_t addr, uint32_t v)
{
	addr &= 0xFFFFFF;
	if (addr >= 0x800000)
	{
		ac_arm_write(addr, v & 0xFF, 1);
		return;
	}
	aram_store((addr ^ 3) & 0x1FFFFF, v, 1);
}

#define A7_MAP_N 8192u
#define A7_E68K_OUT ac.e68k_out
#include "arm7_spu.h"

static A7Cpu cpu;
static uint32_t s_resets;   // ARM7 resets under ARM7 Reset Fix

static void ac_race(void) { a7_race(&cpu); }

// An ARM7 reset (ARMRST release, or the PPU's arm_Reset); ARM7 Reset Fix also clears the FIQ latch.
static void arm_reset_edge(void)
{
	aram_invalidate_all();
	if (setup.flags & ARM_SETUP_RESET_FIX)
	{
		s_resets++;
		ac_reset_latch();
	}
}

// ── The caique control twin and the AICA log (ps3_aica_spe.cpp's PPU side) ──

static uint8_t s_store[sizeof(caique::AicaModel)] __attribute__((aligned(128)));
static caique::AicaModel* const M = (caique::AicaModel*)s_store;
static caique::CtlSnap s_snap __attribute__((aligned(16)));
static uint32_t s_now, s_checks;

uint8_t caique_rd8(uint32_t a) { return aram_line(a & 0x1FFFFF)[(a & 127) ^ 3]; }
uint16_t caique_rd16(uint32_t a)
{
	const uint8_t* l = aram_line(a & 0x1FFFFF);
	const uint32_t o = a & 127;
	return (uint16_t)(l[o ^ 3] | (l[(o + 1) ^ 3] << 8));
}
const uint8_t* caique_line(uint32_t a) { return aram_line(a & 0x1FFFFF); }
uint16_t caique_dsp_rd16(uint32_t a, uint32_t, uint32_t) { return caique_rd16(a); }
void caique_dsp_wr16(uint32_t, uint32_t, uint32_t, uint16_t) {}

static AicaRec recs[REC_BATCH] __attribute__((aligned(128)));
static AicaBell abell;
static AicaEcho aecho;
static uint32_t rec_n, log_head, log_pub, log_tail_seen;
static bool log_dead;   // the sound SPU stopped taking records

static void flush_recs(void)
{
	const uint32_t first = log_head - rec_n;
	for (uint32_t done = 0; done < rec_n;)
	{
		const uint32_t idx = (first + done) & (AICA_LOG - 1);
		uint32_t n = rec_n - done;
		if (n > AICA_LOG - idx)
			n = AICA_LOG - idx;
		mfc_putf(&recs[done], setup.aica_log_ea + idx * sizeof(AicaRec), n * sizeof(AicaRec), TAG_MEM, 0, 0);
		done += n;
	}
	rec_n = 0;
	wait_tag(TAG_MEM);   // recs is refilled at once; also every ARAM put before them is in memory
}

// The new head goes in with a reservation, so the PPU's quit store in the same line is never lost.
static void publish(void)
{
	flush_recs();
	do
	{
		mfc_getllar(&abell, setup.aica_bell_ea, 0, 0);
		mfc_read_atomic_status();
		abell.head = log_head;
		mfc_putllc(&abell, setup.aica_bell_ea, 0, 0);
	} while (mfc_read_atomic_status() & MFC_PUTLLC_STATUS);
	log_pub = log_head;
}

// The log is full: wait for the sound SPU; after 100 ms without progress the log is dropped (no sound, the game runs).
static void log_room(void)
{
	publish();
	uint32_t t0 = spu_read_decrementer(), last = log_tail_seen;
	for (;;)
	{
		mfc_get(&aecho, setup.aica_echo_ea, sizeof(aecho), TAG_AECHO, 0, 0);
		wait_tag(TAG_AECHO);
		log_tail_seen = aecho.tail;
		if (log_head - log_tail_seen < AICA_LOG)
			return;
		st.log_waits++;
		if (log_tail_seen != last)
		{
			last = log_tail_seen;
			t0 = spu_read_decrementer();
		}
		else if (t0 - spu_read_decrementer() > LOG_WAIT_MAX)
		{
			log_dead = true;
			return;
		}
	}
}

static void push(uint8_t op, uint16_t off, uint32_t val, uint32_t hash)
{
	if (log_dead)
	{
		st.log_drops++;
		return;
	}
	if (log_head - log_tail_seen >= AICA_LOG)
	{
		log_room();
		if (log_dead)
		{
			st.log_drops++;
			return;
		}
	}
	AicaRec* r = &recs[rec_n++];
	r->sample = s_now;
	r->op = op;
	r->pad = 0;
	r->off = off;
	r->val = val;
	r->hash = hash;
	log_head++;
	st.recs++;
	if (op == AICA_OP_CHECK || log_head - log_pub >= 1024)
		publish();
	else if (rec_n == REC_BATCH)
		flush_recs();
}

static void catch_up(void) { M->advance(s_now - (uint32_t)M->samples, false, 0); }

// The upper halfword of a 32-bit slot holds no register; a byte write keeps the other byte nullAICA had.
static void ac_twin_write(uint32_t off, uint32_t data, uint32_t size, uint32_t old16)
{
	if (off & 2)
		return;
	uint32_t val;
	if (size == 1)
		val = (off & 1) ? ((old16 & 0x00FF) | ((data & 0xFF) << 8)) : ((old16 & 0xFF00) | (data & 0xFF));
	else
		val = data & 0xFFFF;
	off &= 0x7FFC;
	catch_up();
	M->write(off, val);
	push(AICA_OP_WRITE, (uint16_t)off, val, 0);
}

static uint32_t ac_twin_mon(uint32_t reg, int keep_lp)
{
	catch_up();
	caique::Slot& c = M->slot[(M->r(0x280C) >> 8) & 0x3F];
	const bool lp = c.looped;
	const uint16_t v = (uint16_t)M->read(reg);
	if (keep_lp)
		c.looped = lp;
	st.mon_reads++;
	return v;
}

static void ac_twin_step(void)
{
	if ((++s_now & (AICA_BLOCK - 1)) != 0)
		return;
	catch_up();
	M->snap(s_snap);
	push(AICA_OP_CHECK, 0, s_checks, caique::AicaModel::snap_hash(s_snap));
	s_checks++;
	st.checks++;
}

// CD-DA: the PPU read the sector at this sample and queued its slot before the MediumUpdate.
static uint32_t cdda_fifo[16], cdda_w, cdda_r;

static void ac_cdda_refill(void)
{
	uint32_t v = AICA_CDDA_SILENT;
	if (cdda_r != cdda_w)
		v = cdda_fifo[cdda_r++ & 15];
	else
		st.cdda_under++;
	push(AICA_OP_CDDA, 0, v, 0);
}

// ── Commands ──

static uint32_t s_batch_acc, s_batch_calls;

// arm_aica_run: the ARM7 slice (Arm7Batch, arm7_speed), then one AICA step per sample.
static void run_mu(const ArmCmd* c)
{
	const uint32_t stage_n = ARM_MU_STAGE(c->flags);
	uint32_t batch = ARM_MU_BATCH(c->flags), ticks;
	if (batch <= 1)
	{
		ticks = (c->b / 20) >> stage_n;
		s_batch_acc = 0;
		s_batch_calls = 0;
	}
	else
	{
		if (batch > 16)
			batch = 16;
		s_batch_acc += c->b;
		if (++s_batch_calls < batch)
			goto deferred;
		ticks = (s_batch_acc / 20) >> stage_n;
		s_batch_acc = 0;
		s_batch_calls = 0;
	}
	if (cpu.enabled)
	{
		st.runs++;
		st.ticks += ticks;
	}
	cpu.idle_pref = (uint8_t)ARM_MU_SAFE(c->flags);
	a7_run(&cpu, ticks);
deferred:
	for (uint32_t n = c->a; n; n--)
		ac_timestep();
}

static void run_cmd(const ArmCmd* c)
{
	if (c->dhi >= c->dlo)
		aram_invalidate(c->dlo, c->dhi);
	switch (c->op)
	{
	case ARM_CMD_MU:
		run_mu(c);
		break;
	case ARM_CMD_WR:
		if ((c->a & 0x7FFF) == 0x2C00)   // ARMRST: bit 0 holds the ARM7 in reset
		{
			const int en = !(c->b & 1);
			if (en && !cpu.enabled)
				arm_reset_edge();
			a7_set_enabled(&cpu, en);
		}
		ac_write(c->a, c->b, c->c);
		break;
	case ARM_CMD_RD:
		st.rd_val = ac_read(c->a, c->c);
		st.rd_seq = c->b;
		break;
	case ARM_CMD_CDDA:
		cdda_fifo[cdda_w++ & 15] = c->b;
		break;
	case ARM_CMD_RESET:
		a7_reset(&cpu);
		arm_reset_edge();
		break;
	case ARM_CMD_FLUSH:
		a7_flush(&cpu);
		break;
	}
}

static uint32_t run_cmds(uint32_t tail, uint32_t head)
{
	while (tail != head)
	{
		const uint32_t idx = tail & (ARM_CMDS - 1);
		uint32_t n = head - tail;
		if (n > CMD_BATCH)
			n = CMD_BATCH;
		if (n > ARM_CMDS - idx)
			n = ARM_CMDS - idx;
		mfc_get(cmds, setup.ring_ea + idx * sizeof(ArmCmd), n * sizeof(ArmCmd), TAG_CMD, 0, 0);
		wait_tag(TAG_CMD);
		for (uint32_t i = 0; i < n; i++)
			run_cmd(&cmds[i]);
		tail += n;
		st.tail = tail;
		st.cmds += n;
		st.fiqs = cpu.fiqs;
		st.decodes = cpu.decodes;
		st.evicts = cpu.evicts;
		st.races = cpu.races;
		st.raises = ac.raises;
		st.resets = s_resets;
		st.stale = ac.stale;
		st.ca_reads = ac.ca_reads;
		st.ca_clamped = ac.ca_clamped;
		put_echo();
	}
	return tail;
}

int main(uint64_t arg_bell, uint64_t arg_echo, uint64_t arg_setup)
{
	bell_ea = arg_bell;
	echo_ea = arg_echo;
	mfc_get(&setup, arg_setup, sizeof(setup), TAG_CMD, 0, 0);
	wait_tag(TAG_CMD);
	ac_init();
	ac.race_fix = (setup.flags & ARM_SETUP_RACE_FIX) != 0;
	ac.ca_clamp = (setup.flags & ARM_SETUP_CA_CLAMP) != 0;
	a7_init(&cpu, (setup.flags & ARM_SETUP_RESET_FIX) != 0);
	M->init();
	M->reset();
	aram_invalidate_all();
	st.state = ARM_SPE_STATE_RUNNING;
	st.ls_end = (uint32_t)_end;
	put_echo();

	spu_write_event_mask(MFC_LLR_LOST_EVENT | MFC_DECREMENTER_EVENT);
	spu_write_event_ack(MFC_LLR_LOST_EVENT | MFC_DECREMENTER_EVENT);
	spu_write_decrementer(DEC_TICKS);
	uint32_t tail = 0;
	for (;;)
	{
		// The re-check after each getllar closes the race with a store that lands before the reservation.
		mfc_getllar(&bell, bell_ea, 0, 0);
		mfc_read_atomic_status();
		if (bell.quit)
			break;
		const uint32_t head = bell.head;
		if (head != tail)
		{
			const uint32_t t0 = spu_read_decrementer();
			tail = run_cmds(tail, head);
			st.busy += t0 - spu_read_decrementer();
			continue;
		}
		const uint32_t ev = spu_read_event_status();
		spu_write_event_ack(ev);
		if (ev & MFC_LLR_LOST_EVENT)
			st.wakes++;
		if (ev & MFC_DECREMENTER_EVENT)
		{
			st.timeouts++;
			spu_write_decrementer(DEC_TICKS);
		}
	}

	if (!log_dead)
		publish();
	st.state = ARM_SPE_STATE_EXITED;
	put_echo();
	spu_thread_exit(0);
	return 0;
}
