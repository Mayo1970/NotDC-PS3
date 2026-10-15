// OPTIMIZATION_PLAN 5.15: the ARM7, nullAICA's registers and the caique control twin on an SPU. Runs the PPU's command
// ring (MediumUpdates, SH4 register accesses) and writes the AICA log that the sound SPU (spe_aica) plays.
#include <spu_intrinsics.h>
#include <spu_mfcio.h>
#include <sys/spu_thread.h>
#include <stddef.h>
#include "aica_model.h"
#include "spe_aica.h"
#include "spe_arm7.h"
static void ac_dsp_read(uint32_t a);
#define AC_DSP_READ(a) ac_dsp_read(a)
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

// Plan 6.6 / P20 G2 (the code is after the log's): the hazard map and counts, and the DSP ring the sound SPU mirrors
static AicaHz hz;
static uint32_t ring_base, ring_len;   // in wave RAM bytes; len 0 = no ring
static void hz_arm_store(uint32_t a);
static void ring_wrote(uint32_t lo, uint32_t hi);

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
// Plan 6.6: a store to a marked page first asks if the sound SPU has yet to play that byte.
static void aram_store(uint32_t h, uint32_t v, uint32_t n)
{
	if (__builtin_expect(hz.map[h >> 11] & (0x80 >> ((h >> 8) & 7)), 0))
		hz_arm_store(n == 1 ? h ^ 3 : h);
	const uint32_t la = h & 0x1FFF80u, i = (la >> 7) & (LINES - 1);
	uint8_t* p = line_tag[i] == (la | 1) ? &line_data[i][h & 127] : &stage[stage_i++ & (STAGE_N - 1)][h & 15];
	if (n == 1)
		*p = (uint8_t)v;
	else
		*(uint32_t*)p = v;
	mfc_putf(p, setup.aram_ea + h, n, TAG_MEM, 0, 0);
	st.puts++;
	if (__builtin_expect(((h - ring_base) & 0x1FFFFFu) < ring_len, 0))
		ring_wrote(h >> 7, h >> 7);
}

// ── ARM7 memory (arm_mem.cpp ReadMemArm / WriteMemArm) ──

static uint32_t s_unal, s_unal_addr[4];   // unaligned word stores and the first 4 addresses (bundle 9, log only)

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
		if (s_unal < 4)
			s_unal_addr[s_unal] = addr;
		s_unal++;
		// ARM7 STR Align (6.8): the ARM7DI and Flycast write the aligned word; without it the store scatters 4 bytes.
		if (!(setup.flags & ARM_SETUP_STR_ALIGN))
		{
			for (uint32_t i = 0; i < 4; i++)
				aram_store(((addr + i) ^ 3) & 0x1FFFFF, v >> (8 * i), 1);
			return;
		}
		addr &= ~3u;
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

// Bundle 9 (log only): the P49 census and the unaligned stores go to the PPU when a count moved, or each 1024
// instructions looked at. All terms only rise, so an equal key means equal counts.
static ArmCensus cen;
static uint32_t cen_key;

static void census_put(void)
{
	const uint32_t key = cpu.p49.n[0] + cpu.p49.n[1] + s_unal + (cpu.p49.scan >> 10);
	if (key == cen_key || !setup.census_ea)
		return;
	cen_key = key;
	wait_tag(TAG_MEM);   // the last put of the block is done: its LS copy can change
	cen.p49 = cpu.p49;
	cen.unal = s_unal;
	memcpy(cen.unal_addr, s_unal_addr, sizeof(cen.unal_addr));
	mfc_putf(&cen, setup.census_ea, sizeof(cen), TAG_MEM, 0, 0);
}

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

static void push_at(uint32_t sample, uint8_t op, uint16_t off, uint32_t val, uint32_t hash)
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
	r->sample = sample;
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

// P20 G2: the wave RAM lines the guest wrote in the ring since the last RING record, and the first write's sample
static uint32_t ring_plo = 1, ring_phi, ring_pt;

static void ring_wrote(uint32_t lo, uint32_t hi)
{
	if (ring_phi < ring_plo)
	{
		ring_plo = lo;
		ring_phi = hi;
		ring_pt = s_now;
		return;
	}
	if (lo < ring_plo)
		ring_plo = lo;
	if (hi > ring_phi)
		ring_phi = hi;
}

// A waiting RING record goes first: the sound SPU must reload those lines before it plays on.
static void push(uint8_t op, uint16_t off, uint32_t val, uint32_t hash)
{
	if (__builtin_expect(ring_phi >= ring_plo, 0))
	{
		const uint32_t v = ring_plo | ring_phi << 16;
		ring_plo = 1;
		ring_phi = 0;
		hz.ring_recs++;
		push_at(ring_pt, AICA_OP_RING, 0, v, 0);
	}
	push_at(s_now, op, off, val, hash);
}

static void catch_up(void) { M->advance(s_now - (uint32_t)M->samples, false, 0); }

// ── Plan 6.6 SPE AICA Sync, P20 G2-G4: the exact test, the sync with the sound SPU, DSP register reads ──

#define HZ_WAIT_MAX (79800u * 50)   // 50 ms without an answer; the PPU gives this SPU 100 ms
static uint32_t hz_live;                  // ARM_CFG_*, from the PPU
static uint32_t hz_synced_at = ~0u;       // the sample the sound SPU last caught up to
static uint32_t hz_blk = ~0u, hz_tests, hz_syncs, hz_seq, hz_rd_seq, s_in_arm;
static bool hz_dead, hz_dirty;            // the sound SPU did not answer: sync is off; counts wait for a put

static void aecho_get(void)
{
	mfc_get(&aecho, setup.aica_echo_ea, sizeof(aecho), TAG_AECHO, 0, 0);
	wait_tag(TAG_AECHO);
}

// The map: the slots' play points while "SPE AICA Sync" is on, and the DSP ring. lag = samples the sound SPU is behind.
static void hz_map(int32_t lag)
{
	wait_tag(TAG_MEM);   // the last put of the block is done: its LS copy can change
	catch_up();
	memset(hz.map, 0, sizeof(hz.map));
	if (!hz_dead)
	{
		if (hz_live)
			M->hz_mark(hz.map, (uint32_t)(lag < 0 ? 0 : lag) + AICA_HZ_MARGIN, AICA_HZ_FWD);
		for (uint32_t p = ring_base >> 8, n = ring_len >> 8; n; n--, p++)
			hz.map[(p & 8191) >> 3] |= (uint8_t)(0x80 >> (p & 7));
	}
	hz.map_sample = s_now;
	mfc_putf(&hz, setup.hz_ea, sizeof(hz), TAG_MEM, 0, 0);
	hz_dirty = false;
}

static void hz_remap(void)
{
	aecho_get();
	hz_map((int32_t)(s_now - aecho.samples));
}

// At each CHECK: a new map while a row that uses it is on (the PPU counts on its date), else the counts if they moved
static void hz_tick(void)
{
	if (ring_len)   // the sound SPU wrote its ring back by now: cached ring lines are old
		for (uint32_t i = 0; i < LINES; i++)
			if (line_tag[i] && (((line_tag[i] & ~1u) - ring_base) & 0x1FFFFFu) < ring_len)
				line_tag[i] = 0;
	if (hz_live || (setup.flags & (ARM_SETUP_RING | ARM_SETUP_RING64)))
		hz_remap();
	else if (hz_dirty)
	{
		wait_tag(TAG_MEM);
		mfc_putf(&hz.map_sample, setup.hz_ea + AICA_HZ_MAP, sizeof(hz) - AICA_HZ_MAP, TAG_MEM, 0, 0);
		hz_dirty = false;
	}
}

// The ring the sound SPU mirrors, after a write to RBP/RBL or to the DSP program
static void ring_update(void)
{
	uint32_t base, bytes;
	const bool on = M->ring_range(base, bytes) && (setup.flags & (bytes <= 65536 ? ARM_SETUP_RING : ARM_SETUP_RING64));
	const uint32_t len = on ? bytes : 0;
	if (len == ring_len && (!len || base == ring_base))
		return;
	ring_base = base;
	ring_len = len;
	hz_remap();
}

// The SH4 wrote lines lo..hi: the part in the ring goes to the sound SPU (all of it when the ring wraps wave RAM)
static void ring_sh4(uint32_t lo, uint32_t hi)
{
	const uint32_t rb = ring_base >> 7, re = rb + (ring_len >> 7) - 1;
	if (re < (2u << 20) / 128)
	{
		if (lo < rb)
			lo = rb;
		if (hi > re)
			hi = re;
		if (lo > hi)
			return;
	}
	ring_wrote(lo, hi);
}

// Publishes, then waits for the sound SPU's answer to request `want` (a SYNC, or a DSPRD). No answer: sync goes off.
static bool snd_answer(uint32_t want, bool rd)
{
	publish();
	const uint32_t t0 = spu_read_decrementer();
	for (;;)
	{
		aecho_get();
		if (rd ? (aecho.dsp_rd >> 16) == (want & 0xFFFF) : aecho.sync == want)
			break;
		if (t0 - spu_read_decrementer() > HZ_WAIT_MAX)
		{
			hz.timeouts++;
			hz_dead = true;
			hz_map(0);
			return false;
		}
	}
	hz.wait_ticks += t0 - spu_read_decrementer();
	log_tail_seen = aecho.tail;
	return true;
}

// 0 = no hazard; 1 = a slot has yet to play byte a on the sound SPU; 2 = a is in the mirrored ring
static uint32_t hz_test(uint32_t a, uint32_t src)
{
	hz.tests[src]++;
	hz_dirty = true;
	if (((a - ring_base) & 0x1FFFFFu) < ring_len)
	{
		hz.ring_hits++;
		return 2;
	}
	if (!hz_live)
		return 0;
	aecho_get();
	const int32_t lag = (int32_t)(s_now - aecho.samples);
	if (lag <= 0)
		return 0;
	catch_up();
	const int32_t d = M->hz_hit(a, (uint32_t)lag + AICA_HZ_MARGIN);
	if (d < 0)
		return 0;
	hz.hazards[src]++;
	hz.near[d < 16 ? 0 : d < 64 ? 1 : d < 256 ? 2 : 3]++;
	return 1;
}

// A write to byte a of a marked page at this sample (src: 0 SH4, 1 G2 DMA, 2 ARM7). Returns ARM_HZ_*.
// The exact test, then one sync per 64-sample block at most; ARM_CFG_BLOCK and ARM_CFG_ALL sync with no test.
static uint32_t hz_write(uint32_t a, uint32_t src)
{
	if (hz_dead || log_dead)
		return ARM_HZ_DEAD;
	if (s_now == hz_synced_at)
		return ARM_HZ_SYNCED;
	if ((s_now >> 6) != hz_blk)
	{
		hz_blk = s_now >> 6;
		hz_tests = hz_syncs = 0;
	}
	if (hz_live & (ARM_CFG_BLOCK | ARM_CFG_ALL))   // no test: the hit syncs; BLOCK keeps one sync per block
	{
		hz_dirty = true;
		if (hz_syncs && !(hz_live & ARM_CFG_ALL))
		{
			hz.miss++;
			return ARM_HZ_MISS;
		}
	}
	else
	{
		if (hz_tests >= AICA_HZ_TESTS)
		{
			hz.miss++;
			hz_dirty = true;
			return ARM_HZ_MISS;
		}
		hz_tests++;
		if (!hz_test(a, src))
			return ARM_HZ_NONE;
		if (hz_syncs)   // one sync per block: this hazard stays
		{
			hz.miss++;
			return ARM_HZ_MISS;
		}
	}
	hz_syncs = 1;
	push(AICA_OP_SYNC, 0, ++hz_seq, 0);
	if (log_dead || !snd_answer(hz_seq, false))
		return ARM_HZ_DEAD;
	hz.syncs++;
	hz_synced_at = s_now;
	hz_map(0);
	return ARM_HZ_SYNCED;
}

// Bundle 21, SPE AICA Defer: the SH4's store of this SYNC reaches wave RAM only now, from the PPU; nothing runs before it.
// No time limit: a paused SH4 thread writes it when it goes on (a hold over 50 ms is only counted).
static void hz_hold(void)
{
	st.held++;
	hz.holds++;
	hz_dirty = true;
	put_echo();
	spu_write_decrementer(0x7FFFFFFFu);   // the PPU's bell store is the wake, so the hold time needs no reload
	const uint32_t t0 = spu_read_decrementer();
	for (;;)
	{
		mfc_getllar(&bell, bell_ea, 0, 0);
		mfc_read_atomic_status();
		if ((int32_t)(bell.applied - st.held) >= 0 || bell.quit)
			break;
		spu_write_event_ack(spu_read_event_status());
	}
	const uint32_t dt = t0 - spu_read_decrementer();
	hz.hold_ticks += dt;
	if (dt > HZ_WAIT_MAX)
		hz.hold_timeouts++;
	spu_write_decrementer(DEC_TICKS);
}

static void hz_arm_store(uint32_t a)
{
	hz.arm_hits++;
	hz_dirty = true;
	hz_write(a, 2);
}

// P20 G4: nullAICA's register copy gets the DSP's value from the sound SPU before the read returns it
static void ac_dsp_read(uint32_t a)
{
	hz.dsp_rd[s_in_arm]++;
	hz_dirty = true;
	if (!(setup.flags & ARM_SETUP_DSPRD) || hz_dead || log_dead)
		return;
	const uint32_t off = a & 0x7FFC;
	push(AICA_OP_DSPRD, (uint16_t)off, ++hz_rd_seq, 0);
	if (!log_dead && snd_answer(hz_rd_seq, true))
		ac_wr16(off + 2, aecho.dsp_rd & 0xFFFF);
}

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
	if ((off == 0x2804 || (off >= 0x3400 && off < 0x3C00)) && (setup.flags & (ARM_SETUP_RING | ARM_SETUP_RING64)))
		ring_update();
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
	hz_tick();
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

static uint32_t s_batch_acc, s_batch_calls, s_batch_smp;
static int32_t s_owed;   // "ARM7 Interleave" SAMPLE: ticks not run yet; below 0 = the last run's overshoot

// "ARM7 Rate" (plan 6.3): rate ticks per sample, or SH4 cycles / 20 as before; then the arm7_speed stage.
static inline uint32_t mu_ticks(uint32_t rate, uint32_t cycles, uint32_t samples, uint32_t stage_n)
{
	return (rate ? rate * samples : cycles / 20) >> stage_n;
}

// arm_aica_run: the ARM7 slice (Arm7Batch, arm7_speed), then one AICA step per sample.
// "ARM7 Interleave" SAMPLE (plan 6.2, as Flycast): a run before each sample's step, Arm7Batch not used.
static void run_mu(const ArmCmd* c)
{
	const uint32_t stage_n = ARM_MU_STAGE(c->flags), rate = ARM_MU_RATE(c->c);
	uint32_t batch = ARM_MU_BATCH(c->flags), ticks;
	if (ARM_MU_SAMPLE(c->c))
	{
		s_batch_acc = s_batch_smp = s_batch_calls = 0;
		s_owed += (int32_t)mu_ticks(rate, c->b, c->a, stage_n);
		for (uint32_t n = c->a; n; n--)
		{
			const int32_t share = s_owed / (int32_t)n;   // what is owed, over the samples left in this call
			if (!cpu.enabled)
				s_owed = 0;
			else if (share > 0)
			{
				st.runs++;
				st.ticks += (uint32_t)share;
				cpu.idle_pref = (uint8_t)ARM_MU_SAFE(c->flags);
				s_in_arm = 1;
				s_owed -= a7_run(&cpu, (uint32_t)share);
				s_in_arm = 0;
			}
			ac_timestep();
		}
		return;
	}
	s_owed = 0;
	if (batch <= 1)
	{
		ticks = mu_ticks(rate, c->b, c->a, stage_n);
		s_batch_acc = s_batch_smp = 0;
		s_batch_calls = 0;
	}
	else
	{
		if (batch > 16)
			batch = 16;
		s_batch_acc += c->b;
		s_batch_smp += c->a;
		if (++s_batch_calls < batch)
			goto deferred;
		ticks = mu_ticks(rate, s_batch_acc, s_batch_smp, stage_n);
		s_batch_acc = s_batch_smp = 0;
		s_batch_calls = 0;
	}
	if (cpu.enabled)
	{
		st.runs++;
		st.ticks += ticks;
	}
	cpu.idle_pref = (uint8_t)ARM_MU_SAFE(c->flags);
	s_in_arm = 1;
	a7_run(&cpu, ticks);
	s_in_arm = 0;
deferred:
	for (uint32_t n = c->a; n; n--)
		ac_timestep();
}

static void run_cmd(const ArmCmd* c)
{
	if (c->dhi >= c->dlo)
	{
		aram_invalidate(c->dlo, c->dhi);
		if (ring_len)
			ring_sh4(c->dlo, c->dhi);
	}
	switch (c->op)
	{
	case ARM_CMD_SYNC:
		st.hz_res = hz_write(c->b & 0x1FFFFFu, c->c ? 1 : 0);
		if (c->flags & ARM_SYNC_HOLD)
			hz_hold();
		break;
	case ARM_CMD_CFG:
		hz_live = c->b & (ARM_CFG_SYNC | ARM_CFG_BLOCK | ARM_CFG_ALL);
		hz_remap();
		break;
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
		census_put();
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
	ac.lfore_keep = (setup.flags & ARM_SETUP_LFORE) != 0;
	a7_init(&cpu, (setup.flags & ARM_SETUP_RESET_FIX) != 0);
	M->init();
	M->stream_lsa = (setup.flags & ARM_SETUP_STREAM) != 0;
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
