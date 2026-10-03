// OPTIMIZATION_PLAN 5.13 A1: the PPU runs caique's control pass beside nullAICA and logs every register write;
// one SPU thread runs the full model from the log and checks the PPU's snapshot hash every AICA_BLOCK samples.

// First: types.h defines a RAM_SIZE macro that would break the model's RAM_SIZE member.
#include "plugs/caiqueAICA/aica_model.h"
#include "ps3_aica_spe.h"
#include "ps3_spe.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/spu.h>
#include <ogc/lwp_watchdog.h>
#include "spu/spe_aica.h"

extern "C" const u8 spe_aica_elf[];
extern "C" int get_spe_aica_preset();

typedef char snap_size_check[sizeof(caique::CtlSnap) == AICA_SNAP_BYTES ? 1 : -1];

bool g_aica_spe_on;

static AicaBell s_bell;
static AicaEcho s_echo;
static AicaShared s_shared;
static caique::AicaModel* s_m;   // control pass only
static caique::CtlSnap s_snaps[AICA_SNAPS];
static u32 s_snap_check[AICA_SNAPS];
static u8* s_aram;
static u32 s_head, s_pub, s_tail_seen, s_checks, s_bad_seen;
static sysSpuImage s_img;
static sys_spu_group_t s_group;
static sys_spu_thread_t s_thread;
static bool s_up;

// Window counters (SH4 thread).
static u32 s_writes, s_full_waits, s_ctl_samples;
static u64 s_ctl_ticks, s_win_t0;

// The PPU never runs the render pass; these only satisfy the link (and read aica_ram if it ever does).
uint8_t caique_rd8(uint32_t a) { return s_aram[a ^ 3]; }
uint16_t caique_rd16(uint32_t a) { return (uint16_t)(s_aram[a ^ 3] | (s_aram[(a + 1) ^ 3] << 8)); }
uint16_t caique_dsp_rd16(uint32_t a, uint32_t, uint32_t) { return caique_rd16(a); }
void caique_dsp_wr16(uint32_t, uint32_t, uint32_t, uint16_t) {}

static inline const volatile AicaEcho* echo() { return &s_echo; }

static bool wait_echo_state(u32 want, u64 budget_ticks)
{
	const u64 t0 = gettime();
	while (echo()->state != want)
		if (gettime() - t0 > budget_ticks)
			return false;
	return true;
}

static void stats(u64 now);

static void publish()
{
	__asm__ volatile("lwsync" ::: "memory");
	s_bell.head = s_pub = s_head;
}

// A log that stays full for 100 ms means the SPU is gone or far too slow: the check turns off.
static void push(u8 op, u16 off, u32 val, u32 hash)
{
	if (s_head - s_tail_seen >= AICA_LOG)
	{
		s_full_waits++;
		publish();
		const u64 t0 = gettime();
		while (s_head - (s_tail_seen = echo()->tail) >= AICA_LOG)
		{
			if (gettime() - t0 > ps3_timebase_hz / 10)
			{
				printf("[aica-spe] write log full for 100 ms (spu samples %u, ppu %u); check off\n",
				       echo()->samples, (u32)s_m->samples);
				g_aica_spe_on = false;
				stats(gettime());   // the counts and mismatch reports so far
				return;
			}
		}
	}
	AicaRec* r = &s_shared.log[s_head & (AICA_LOG - 1)];
	r->sample = (u32)s_m->samples;
	r->op = op;
	r->pad = 0;
	r->off = off;
	r->val = val;
	r->hash = hash;
	s_head++;
	if (op == AICA_OP_CHECK || s_head - s_pub >= 1024)
		publish();
}

// The upper halfword of a 32-bit slot holds no register; a byte write keeps the other byte nullAICA had.
void ps3aica_write(u32 off, u32 data, u32 size, u16 old16)
{
	if (off & 2)
		return;
	u32 val;
	if (size == 1)
		val = (off & 1) ? ((old16 & 0x00FF) | ((data & 0xFF) << 8)) : ((old16 & 0xFF00) | (data & 0xFF));
	else
		val = data & 0xFFFF;
	off &= 0x7FFC;
	const u64 t0 = gettime();
	s_m->write(off, val);
	push(AICA_OP_WRITE, (u16)off, val, 0);
	s_writes++;
	s_ctl_ticks += gettime() - t0;
}

void ps3aica_step()
{
	u64 t0 = gettime();
	if (t0 - s_win_t0 >= 5 * ps3_timebase_hz)
	{
		stats(t0);
		t0 = gettime();   // the print is not control-pass cost
	}
	s_m->step_control();
	if (((u32)s_m->samples & (AICA_BLOCK - 1)) == 0)
	{
		const u32 i = s_checks & (AICA_SNAPS - 1);
		s_m->snap(s_snaps[i]);
		s_snap_check[i] = s_checks;
		push(AICA_OP_CHECK, 0, s_checks, caique::AicaModel::snap_hash(s_snaps[i]));
		s_checks++;
	}
	s_ctl_ticks += gettime() - t0;
	s_ctl_samples++;
}

static void print_bad(const AicaBad* b)
{
	const caique::CtlSnap* ss = (const caique::CtlSnap*)b->snap;
	const u32 j = b->check & (AICA_SNAPS - 1);
	printf("[aica-spe] MISMATCH check=%u sample=%u ppu=%08X spu=%08X", b->check, ss->sample, b->ppu_hash, b->spu_hash);
	if (s_snap_check[j] != b->check)
	{
		printf(" (PPU snapshot already reused)\n");
		return;
	}
	const caique::CtlSnap* ps = &s_snaps[j];
	if (ps->sample != ss->sample) printf(" sample %u/%u", ps->sample, ss->sample);
	if (ps->lfsr != ss->lfsr) printf(" lfsr %X/%X", ps->lfsr, ss->lfsr);
	if (ps->mdec != ss->mdec) printf(" mdec %X/%X", ps->mdec, ss->mdec);
	if (ps->eg_cnt != ss->eg_cnt) printf(" eg_cnt %X/%X", ps->eg_cnt, ss->eg_cnt);
	printf("\n");
	u32 shown = 0;
	for (int c = 0; c < 64 && shown < 6; c++)
	{
		const caique::SlotSnap& p = ps->s[c];
		const caique::SlotSnap& s = ss->s[c];
		if (!memcmp(&p, &s, sizeof(p)))
			continue;
		shown++;
		printf("[aica-spe]   ppu/spu s%d CA %X/%X step %X/%X a %X/%X fv %X/%X aeg %u/%u feg %u/%u fl %02X/%02X lfo %X/%X cnt %u/%u\n",
		       c, p.CA, s.CA, p.step, s.step, p.a, s.a, p.fv, s.fv, p.aeg, s.aeg, p.feg, s.feg, p.flags, s.flags,
		       p.lfo, s.lfo, p.lfo_cnt, s.lfo_cnt);
	}
}

// Every ~5 s of wall time, from the sample step.
static void stats(u64 now)
{
	const double w = (double)(now - s_win_t0) / (double)ps3_timebase_hz;
	s_win_t0 = now;
	static u32 l_samples, l_busy, l_misses, l_wakes, l_timeouts, l_checks, l_prof[6];
	const volatile AicaEcho* e = echo();
	u32 prof[6];
	for (int i = 0; i < 6; i++)
		prof[i] = e->prof[i] - l_prof[i];
	const u32 samples = e->samples, busy = e->busy, misses = e->misses, wakes = e->wakes, timeouts = e->timeouts;
	const u32 checks = e->checks, bad = e->bad, bad_n = e->bad_n;
	printf("[aica-spe] %s samples/s ppu=%.0f spu=%.0f lag=%d | checks=%u bad=%u | writes/s=%.0f full=%u"
	       " | ppu ctl %.2f%% %.0f ns/sample | spu busy %.1f%% misses/s=%.0f wakes/s=%.0f timeouts/s=%.0f\n",
	       g_aica_spe_on ? "on" : "OFF", s_ctl_samples / w, (samples - l_samples) / w, (s32)((u32)s_m->samples - samples),
	       checks - l_checks, bad, s_writes / w, s_full_waits,
	       (double)s_ctl_ticks * 100.0 / ((double)ps3_timebase_hz * w),
	       s_ctl_samples ? (double)s_ctl_ticks * 1e9 / (double)ps3_timebase_hz / s_ctl_samples : 0.0,
	       (busy - l_busy) * 100.0 / ((double)ps3_timebase_hz * w), (misses - l_misses) / w,
	       (wakes - l_wakes) / w, (timeouts - l_timeouts) / w);
	const double tw = (double)ps3_timebase_hz * w / 100.0;
	printf("[aica-spe] spu %% of wall: slots %.1f (%.1f rendered/sample) env %.1f preview+bus %.1f dsp %.1f mix %.1f\n",
	       prof[0] / tw, samples != l_samples ? (double)prof[5] / (samples - l_samples) : 0.0, prof[1] / tw,
	       prof[2] / tw, prof[3] / tw, prof[4] / tw);
	for (int i = 0; i < 6; i++)
		l_prof[i] = e->prof[i];
	// Orders the report loads after the bad_n load; the SPU completed each report before it moved bad_n.
	__asm__ volatile("lwsync" ::: "memory");
	for (; s_bad_seen < bad_n && s_bad_seen < AICA_BAD_MAX; s_bad_seen++)
		print_bad(&s_shared.bad[s_bad_seen]);
	l_samples = samples;
	l_busy = busy;
	l_misses = misses;
	l_wakes = wakes;
	l_timeouts = timeouts;
	l_checks = checks;
	s_writes = s_full_waits = s_ctl_samples = 0;
	s_ctl_ticks = 0;
}

bool ps3aica_init(u8* aica_ram)
{
	if (!get_spe_aica_preset() || s_up)
		return s_up;
	s_aram = aica_ram;
	ps3spe_sys_init();
	if (!s_m)
		s_m = new caique::AicaModel();
	s_m->reset();

	s32 r = sysSpuImageImport(&s_img, spe_aica_elf, SPU_IMAGE_PROTECT);
	if (r != 0)
	{
		printf("[aica-spe] sysSpuImageImport failed: 0x%08X\n", (u32)r);
		return false;
	}
	sysSpuThreadGroupAttribute gattr;
	sysSpuThreadGroupAttributeInitialize(gattr);
	sysSpuThreadGroupAttributeName(gattr, "nulldc_aica");
	r = sysSpuThreadGroupCreate(&s_group, 1, 100, &gattr);
	if (r != 0)
	{
		printf("[aica-spe] sysSpuThreadGroupCreate failed: 0x%08X\n", (u32)r);
		sysSpuImageClose(&s_img);
		return false;
	}

	s_bell.head = 0;
	s_bell.quit = 0;
	s_head = s_pub = s_tail_seen = s_checks = s_bad_seen = 0;
	s_win_t0 = gettime();
	memset(&s_echo, 0, sizeof(s_echo));
	memset(s_snap_check, 0xFF, sizeof(s_snap_check));
	memset(&s_shared.setup, 0, sizeof(s_shared.setup));
	s_shared.setup.aram_ea = (u32)(uintptr_t)aica_ram;

	sysSpuThreadAttribute tattr;
	sysSpuThreadAttributeInitialize(tattr);
	sysSpuThreadAttributeName(tattr, "spe_aica");
	sysSpuThreadArgument arg;
	sysSpuThreadArgumentInitialize(arg);
	arg.arg0 = (u64)(uintptr_t)&s_bell;
	arg.arg1 = (u64)(uintptr_t)&s_echo;
	arg.arg2 = (u64)(uintptr_t)&s_shared;
	r = sysSpuThreadInitialize(&s_thread, s_group, 0, &s_img, &tattr, &arg);
	if (r == 0)
		r = sysSpuThreadGroupStart(s_group);
	if (r != 0)
	{
		printf("[aica-spe] SPU thread init/start failed: 0x%08X\n", (u32)r);
		sysSpuThreadGroupDestroy(s_group);
		sysSpuImageClose(&s_img);
		return false;
	}
	s_up = true;
	if (!wait_echo_state(AICA_STATE_RUNNING, ps3_timebase_hz / 10))
	{
		printf("[aica-spe] SPU thread did not report in 100 ms; check off\n");
		ps3aica_term();
		return false;
	}
	g_aica_spe_on = true;
	printf("[aica-spe] SPU thread up: caique control on the PPU, full model on the SPU, check every %u samples\n",
	       AICA_BLOCK);
	return true;
}

void ps3aica_term()
{
	if (!s_up)
		return;
	g_aica_spe_on = false;
	__asm__ volatile("lwsync" ::: "memory");
	s_bell.quit = 1;
	if (!wait_echo_state(AICA_STATE_EXITED, ps3_timebase_hz / 10))
	{
		printf("[aica-spe] SPU thread did not exit in 100 ms; terminating the group\n");
		sysSpuThreadGroupTerminate(s_group, 0);
	}
	u32 cause, status;
	sysSpuThreadGroupJoin(s_group, &cause, &status);
	sysSpuThreadGroupDestroy(s_group);
	sysSpuImageClose(&s_img);
	s_up = false;
	printf("[aica-spe] SPU thread down\n");
}
