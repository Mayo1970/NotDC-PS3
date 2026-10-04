// OPTIMIZATION_PLAN 5.13 A1/A1b/A2: the PPU runs caique's control pass and logs every register write; one SPU thread
// runs the full model from the log and checks the PPU's snapshot every AICA_BLOCK samples. A2: the SPU makes the sound.

// First: types.h defines a RAM_SIZE macro that would break the model's RAM_SIZE member.
#include "plugs/caiqueAICA/aica_model.h"
#include "ps3_aica_spe.h"
#include "ps3_spe.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/spu.h>
#include <ogc/lwp_watchdog.h>
#include "spu/spe_aica.h"
#include "spu/spe_dsp_jit.h"

extern "C" const u8 spe_aica_elf[];
extern "C" int get_spe_aica_preset();
extern "C" int get_spe_aica_snd_preset();
extern "C" int get_spe_dsp_jit_preset();
// ps3_audio.cpp
void ps3_audio_ring_info(u32* ring_ea, u32* frames, u32* wr_ea, u32* rd_ea, u32* wr_now);
u64 ps3_audio_spu_wait(u32 end);

typedef char snap_size_check[sizeof(caique::CtlSnap) == AICA_SNAP_BYTES ? 1 : -1];

bool g_aica_spe_on, g_aica_spe_snd;

static AicaBell s_bell;
static AicaEcho s_echo;
static AicaShared s_shared;
static caique::AicaModel* s_m;   // control pass only
static caique::CtlSnap s_snaps[AICA_SNAPS];
static u32 s_snap_check[AICA_SNAPS];
static u8* s_aram;
static u32 s_head, s_pub, s_tail_seen, s_checks, s_bad_seen, s_dbad_seen;
static bool s_dsp_test_shown;
static sysSpuImage s_img;
static sys_spu_group_t s_group;
static sys_spu_thread_t s_thread;
static bool s_up;

// Window counters (SH4 thread).
static u32 s_writes, s_full_waits, s_ctl_samples, s_reads;
static u64 s_ctl_ticks, s_win_t0, s_spu_wait_ticks, s_ring_wait_ticks;
static u32 s_now;   // samples the guest has run; the twin catches up to it at a write, a read or a CHECK
static u32 s_live[4], s_live_n;   // live_stats sums over the window (live, en, aeg, feg)

// 5.13 A2 sound mode
static bool s_snd_req;   // asked for at boot (the [aica-spe] sound line)
static u32 s_w0;         // ring_wr when the SPU started
static u32 s_cdda_rec[AICA_CDDA_SLOTS], s_cdda_n;   // per slot: 1 + the record that last used it

// The PPU never runs the render pass; these only satisfy the link (and read aica_ram if it ever does).
uint8_t caique_rd8(uint32_t a) { return s_aram[a ^ 3]; }
uint16_t caique_rd16(uint32_t a) { return (uint16_t)(s_aram[a ^ 3] | (s_aram[(a + 1) ^ 3] << 8)); }
const uint8_t* caique_line(uint32_t a) { return s_aram + (a & ~127u); }
uint16_t caique_dsp_rd16(uint32_t a, uint32_t, uint32_t) { return caique_rd16(a); }
void caique_dsp_wr16(uint32_t, uint32_t, uint32_t, uint16_t) {}

static inline const volatile AicaEcho* echo() { return &s_echo; }

// 48 kHz frames ps3_audio.cpp's resampler makes from n samples: ceil(n * 48000 / 44100)
static inline u32 frames_for(u32 n) { return (u32)(((u64)n * 160 + 146) / 147); }

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

// Reads the SPU's own echo from LS: when it is stuck the copy in main memory is stale.
static void dump_ls_echo()
{
	AicaEcho e;
	u64* w = (u64*)&e;
	s32 r = 0;
	for (u32 i = 0; i < sizeof(e) / 8 && r == 0; i++)
		r = sysSpuThreadReadLocalStorage(s_thread, echo()->ls_addr + i * 8, &w[i], 8);
	s32 exit_st = 0;
	const s32 rx = sysSpuThreadGetExitStatus(s_thread, &exit_st);
	printf("[aica-spe] SPU state: ls_read=0x%08X exit=0x%08X/%d | LS echo: tail=%u samples=%u phase=%u dsp checks=%u bad=%u\n",
	       (u32)r, (u32)rx, exit_st, e.tail, e.samples, e.phase, e.dsp_checks, e.dsp_bad);
}

// Sound mode: waits until the SPU has done `upto` records; false when it made no progress for 100 ms.
static bool wait_tail(u32 upto)
{
	u32 t = echo()->tail;
	if ((s32)(t - upto) >= 0)
		return true;
	publish();
	const u64 t0 = gettime();
	u64 moved = t0;
	for (;;)
	{
		usleep(20);
		const u32 n = echo()->tail;
		const u64 now = gettime();
		if ((s32)(n - upto) >= 0)
			break;
		if (n != t)
		{
			t = n;
			moved = now;
		}
		else if (now - moved > ps3_timebase_hz / 10)
		{
			s_spu_wait_ticks += now - t0;
			return false;
		}
	}
	s_spu_wait_ticks += gettime() - t0;
	return true;
}

// The SPU is gone: nullAICA makes the sound again (its channels missed the time in between).
static void snd_fail(const char* why)
{
	printf("[aica-spe] %s; SPU sound off, nullAICA back\n", why);
	dump_ls_echo();
	stats(gettime());
	g_aica_spe_snd = false;
	ps3aica_term();
}

// A full log: the check stops at once, so the game never waits for it; the sound waits for the SPU.
static void push(u8 op, u16 off, u32 val, u32 hash)
{
	if (s_head - s_tail_seen >= AICA_LOG && s_head - (s_tail_seen = echo()->tail) >= AICA_LOG)
	{
		s_full_waits++;
		if (!g_aica_spe_snd)
		{
			publish();
			printf("[aica-spe] write log full (spu samples %u, ppu %u); check off\n", echo()->samples, (u32)s_m->samples);
			dump_ls_echo();
			g_aica_spe_on = false;
			stats(gettime());   // the counts and mismatch reports so far
			return;
		}
		if (!wait_tail(s_head - AICA_LOG + 1))
		{
			snd_fail("write log full and no SPU progress for 100 ms");
			return;
		}
		s_tail_seen = echo()->tail;
	}
	AicaRec* r = &s_shared.log[s_head & (AICA_LOG - 1)];
	r->sample = s_now;
	r->op = op;
	r->pad = 0;
	r->off = off;
	r->val = val;
	r->hash = hash;
	s_head++;
	if (op == AICA_OP_CHECK || s_head - s_pub >= 1024)
		publish();
}

// Batched control pass: the samples since the last write or CHECK, slot by slot (5.13 A2 high voices).
static void catch_up()
{
	const u32 n = s_now - (u32)s_m->samples;
	s_m->advance(n, false, 0);
	s_ctl_samples += n;
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
	catch_up();
	s_m->write(off, val);
	push(AICA_OP_WRITE, (u16)off, val, 0);
	s_writes++;
	s_ctl_ticks += gettime() - t0;
}

// Per sample only a count; every AICA_BLOCK samples the twin catches up and logs a CHECK.
void ps3aica_step()
{
	if ((++s_now & (AICA_BLOCK - 1)) != 0)
		return;
	u64 t0 = gettime();
	if (t0 - s_win_t0 >= 5 * ps3_timebase_hz)
	{
		stats(t0);
		t0 = gettime();   // the print is not control-pass cost
	}
	catch_up();
	const u32 i = s_checks & (AICA_SNAPS - 1);
	s_m->snap(s_snaps[i]);
	s_snap_check[i] = s_checks;
	push(AICA_OP_CHECK, 0, s_checks, caique::AicaModel::snap_hash(s_snaps[i]));
	s_checks++;
	s_ctl_ticks += gettime() - t0;
	// Sound mode pacing: the frames of every sample run so far must fit the ring (the SPU drops what does not).
	if (g_aica_spe_snd)
		s_ring_wait_ticks += ps3_audio_spu_wait(s_w0 + frames_for(s_now) - echo()->snd_drops);
	if ((s_checks & 15) == 0)   // stats only, outside the timed part
	{
		u32 l[4];
		s_m->live_stats(l[0], l[1], l[2], l[3]);
		for (int k = 0; k < 4; k++)
			s_live[k] += l[k];
		s_live_n++;
	}
}

// Sound mode: the channel monitor (0x2810 or 0x2814) from the control twin at this sample.
u16 ps3aica_read_mon(u32 reg, bool keep_lp)
{
	const u64 t0 = gettime();
	catch_up();
	caique::Slot& c = s_m->slot[(s_m->r(0x280C) >> 8) & 0x3F];
	const bool lp = c.looped;
	const u16 v = (u16)s_m->read(reg);
	if (keep_lp)
		c.looped = lp;
	s_reads++;
	s_ctl_ticks += gettime() - t0;
	return v;
}

// Sound mode: a CD-DA sector goes to a free slot; the SPU copies it to LS when it reaches the record.
void ps3aica_cdda(const s16* sector, bool silent)
{
	u32 val = AICA_CDDA_SILENT;
	if (!silent)
	{
		const u32 k = s_cdda_n & (AICA_CDDA_SLOTS - 1);
		if (!wait_tail(s_cdda_rec[k]))
		{
			snd_fail("CD-DA slot still in use after 100 ms without SPU progress");
			return;
		}
		memcpy(s_shared.cdda[k].s, sector, sizeof(s_shared.cdda[k].s));
		s_cdda_rec[k] = s_head + 1;
		s_cdda_n++;
		val = k;
	}
	push(AICA_OP_CDDA, 0, val, 0);
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

static void print_dsp_arr(const char* name, const s32* s, const s32* i, const s32* j, int n)
{
	int shown = 0;
	for (int k = 0; k < n && shown < 4; k++)
		if (i[k] != j[k])
		{
			printf("[aica-spe]   %s[%d] start/interp/jit %X/%X/%X\n", name, k, (u32)s[k], (u32)i[k], (u32)j[k]);
			shown++;
		}
}

// A DSP sample where the recompiled code and the interpreter differ, then the whole report as hex for a PC replay
static void print_dsp_bad(u32 n, const AicaDspBad* b)
{
	printf("[aica-spe] DSP MISMATCH #%u%s sample=%u mdec=%04X rbpl=%04X parts=%03X prog=%u ring wr=%u rd=%u\n", n,
	       (b->flags & AICA_DSPD_SELF) ? " (self-test)" : "", b->sample_lo, b->mdec, b->rbpl, b->flags & 0xFFF, b->prog_id,
	       b->nw, b->nr);
	const AicaDspState *s = &b->start, *i = &b->interp, *j = &b->jit;
	print_dsp_arr("TEMP", s->temp, i->temp, j->temp, 128);
	print_dsp_arr("MEMS", s->mems, i->mems, j->mems, 32);
	print_dsp_arr("EFREG", s->efreg, i->efreg, j->efreg, 16);
	printf("[aica-spe]   start/interp/jit ACC %X/%X/%X FRC %X/%X/%X Y %X/%X/%X ADRS %X/%X/%X memval %X/%X/%X nofl %X/%X/%X qn %d/%d/%d\n",
	       s->acc, i->acc, j->acc, s->frc, i->frc, j->frc, s->y, i->y, j->y, s->adrs, i->adrs, j->adrs, s->memval, i->memval,
	       j->memval, s->nofl, i->nofl, j->nofl, s->mem_qn, i->mem_qn, j->mem_qn);
	u32 shown = 0;
	for (u32 k = 0; k < b->nw && k < AICA_DSP_LOG && shown < 4; k++)
		if (b->wr[k].v != b->wr[k].jv)
		{
			printf("[aica-spe]   ring write #%u a=%06X interp %04X jit %04X\n", k, b->wr[k].a, b->wr[k].v, b->wr[k].jv);
			shown++;
		}
	const u32* w = (const u32*)b;
	const u32 nw = sizeof(*b) / 4;
	for (u32 o = 0; o < nw; o += 16)
	{
		printf("[aica-spe] dspbad%u %04X:", n, o * 4);
		for (u32 k = o; k < o + 16 && k < nw; k++)
			printf(" %08X", w[k]);
		printf("\n");
	}
}

static void print_dsp_test(const volatile AicaEcho* e)
{
	const u32 df = e->dsp_flags;
	printf("[aica-spe] dsp jit self-test %s: %u samples of random programs | SPU LS used %u of 256 KB\n",
	       (df & (AICA_DSPF_ISA | AICA_DSPF_FAILED)) ? "FAIL, recompiler off" : "PASS", df >> 16, e->ls_end >> 10);
	if (df & AICA_DSPF_ISA)
	{
		const AicaDspIsaBad* b = &s_shared.isabad;
		printf("[aica-spe] DSP ISA PROBE FAIL %s imm=%d a=%08X %08X %08X %08X b=%08X %08X %08X %08X c=%08X %08X %08X %08X"
		       " exp=%08X %08X %08X %08X got=%08X %08X %08X %08X\n",
		       b->form < DJF_COUNT ? dspjit_form_names[b->form] : "?", b->imm, b->in[0], b->in[1], b->in[2], b->in[3], b->in[4],
		       b->in[5], b->in[6], b->in[7], b->in[8], b->in[9], b->in[10], b->in[11], b->exp[0], b->exp[1], b->exp[2],
		       b->exp[3], b->got[0], b->got[1], b->got[2], b->got[3]);
	}
}

// Every ~5 s of wall time, from the sample step.
static void stats(u64 now)
{
	const double w = (double)(now - s_win_t0) / (double)ps3_timebase_hz;
	s_win_t0 = now;
	static u32 l_samples, l_busy, l_misses, l_wakes, l_timeouts, l_checks, l_prof[6], l_dsp_ticks, l_dsp_checks;
	const volatile AicaEcho* e = echo();
	u32 prof[6];
	for (int i = 0; i < 6; i++)
		prof[i] = e->prof[i] - l_prof[i];
	const u32 samples = e->samples, busy = e->busy, misses = e->misses, wakes = e->wakes, timeouts = e->timeouts;
	const u32 checks = e->checks, bad = e->bad, bad_n = e->bad_n;
	const double ln = s_live_n ? (double)s_live_n : 1.0;
	printf("[aica-spe] %s samples/s ppu=%.0f spu=%.0f lag=%d | checks=%u bad=%u | writes/s=%.0f full=%u"
	       " | ppu ctl %.2f%% %.0f ns/sample live %.1f (en %.1f aeg %.1f feg %.1f)"
	       " | spu busy %.1f%% misses/s=%.0f wakes/s=%.0f timeouts/s=%.0f\n",
	       g_aica_spe_on ? "on" : "OFF", s_ctl_samples / w, (samples - l_samples) / w, (s32)(s_now - samples),
	       checks - l_checks, bad, s_writes / w, s_full_waits,
	       (double)s_ctl_ticks * 100.0 / ((double)ps3_timebase_hz * w),
	       s_ctl_samples ? (double)s_ctl_ticks * 1e9 / (double)ps3_timebase_hz / s_ctl_samples : 0.0,
	       s_live[0] / ln, s_live[1] / ln, s_live[2] / ln, s_live[3] / ln,
	       (busy - l_busy) * 100.0 / ((double)ps3_timebase_hz * w), (misses - l_misses) / w,
	       (wakes - l_wakes) / w, (timeouts - l_timeouts) / w);
	const double tw = (double)ps3_timebase_hz * w / 100.0;
	printf("[aica-spe] spu %% of wall: slots %.1f (%.1f rendered/sample) env %.1f preview+bus %.1f dsp %.1f mix %.1f\n",
	       prof[0] / tw, samples != l_samples ? (double)prof[5] / (samples - l_samples) : 0.0, prof[1] / tw,
	       prof[2] / tw, prof[3] / tw, prof[4] / tw);
	for (int i = 0; i < 6; i++)
		l_prof[i] = e->prof[i];
	static const char* const k_mode[4] = {"OFF", "ON", "CHECK", "?"};
	const u32 dsp_ticks = e->dsp_ticks, dsp_checks = e->dsp_checks, dsp_bad_n = e->dsp_bad_n;
	if ((e->dsp_flags & AICA_DSPF_TESTED) && !s_dsp_test_shown)
	{
		s_dsp_test_shown = true;
		print_dsp_test(e);
	}
	if (s_snd_req)   // A2 runs without the DSP
	{
		static u32 l_frames, l_cdda;
		const u32 frames = e->snd_frames, cdda = e->cdda;
		printf("[aica-spe] sound %s: frames/s=%.0f drops=%u cdda/s=%.1f reads/s=%.0f"
		       " | ppu wait %% of wall: ring %.1f spu %.1f\n",
		       g_aica_spe_snd ? "SPU" : "nullAICA", (frames - l_frames) / w, e->snd_drops, (cdda - l_cdda) / w,
		       s_reads / w, s_ring_wait_ticks / tw, s_spu_wait_ticks / tw);
		l_frames = frames;
		l_cdda = cdda;
	}
	else
		printf("[aica-spe] dsp jit %s: compiles=%u fails=%u odd=%u code=%u words live=%u/128 slow=%u | checks=%u bad=%u"
		       " | jit %.2f%% of wall\n",
		       k_mode[e->dsp_flags & AICA_DSPF_MODE], e->dsp_compiles, e->dsp_fails, e->dsp_odd, e->dsp_words,
		       e->dsp_live, e->dsp_slow, dsp_checks - l_dsp_checks, e->dsp_bad, (dsp_ticks - l_dsp_ticks) / tw);
	l_dsp_ticks = dsp_ticks;
	l_dsp_checks = dsp_checks;
	// Orders the report loads after the bad_n load; the SPU completed each report before it moved bad_n.
	__asm__ volatile("lwsync" ::: "memory");
	for (; s_bad_seen < bad_n && s_bad_seen < AICA_BAD_MAX; s_bad_seen++)
		print_bad(&s_shared.bad[s_bad_seen]);
	for (; s_dbad_seen < dsp_bad_n && s_dbad_seen < AICA_DSP_BAD_MAX; s_dbad_seen++)
		print_dsp_bad(s_dbad_seen, &s_shared.dbad[s_dbad_seen]);
	l_samples = samples;
	l_busy = busy;
	l_misses = misses;
	l_wakes = wakes;
	l_timeouts = timeouts;
	l_checks = checks;
	s_writes = s_full_waits = s_ctl_samples = s_reads = 0;
	s_ctl_ticks = s_spu_wait_ticks = s_ring_wait_ticks = 0;
	memset(s_live, 0, sizeof(s_live));
	s_live_n = 0;
}

bool ps3aica_init(u8* aica_ram)
{
	const bool snd = get_spe_aica_snd_preset() != 0;
	if ((!get_spe_aica_preset() && !snd) || s_up)
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
	s_head = s_pub = s_tail_seen = s_checks = s_bad_seen = s_dbad_seen = s_now = 0;
	memset(s_live, 0, sizeof(s_live));
	s_live_n = 0;
	s_dsp_test_shown = false;
	s_win_t0 = gettime();
	memset(&s_echo, 0, sizeof(s_echo));
	memset(s_snap_check, 0xFF, sizeof(s_snap_check));
	memset(&s_shared.setup, 0, sizeof(s_shared.setup));
	memset(s_shared.dbad, 0, sizeof(s_shared.dbad));
	memset(&s_shared.isabad, 0, sizeof(s_shared.isabad));
	s_shared.setup.aram_ea = (u32)(uintptr_t)aica_ram;
	s_shared.setup.dsp_mode = snd ? 0 : (u32)get_spe_dsp_jit_preset() & AICA_DSPF_MODE;
	s_snd_req = snd;
	memset(s_cdda_rec, 0, sizeof(s_cdda_rec));
	s_cdda_n = 0;
	if (snd)
	{
		AicaSetup& su = s_shared.setup;
		ps3_audio_ring_info(&su.ring_ea, &su.ring_frames, &su.wr_ea, &su.rd_ea, &s_w0);
		su.w0 = s_w0;
		su.snd = 1;
	}

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
	g_aica_spe_snd = snd;
	static const char* const k_mode[4] = {"OFF (interpreter)", "ON", "CHECK", "?"};
	if (snd)
		printf("[aica-spe] SPU thread up: caique control on the PPU, full model on the SPU, check every %u samples;"
		       " SOUND from the SPU (no DSP), nullAICA mixer off, ring_wr %u\n", AICA_BLOCK, s_w0);
	else
		printf("[aica-spe] SPU thread up: caique control on the PPU, full model on the SPU, check every %u samples;"
		       " dsp jit %s\n", AICA_BLOCK, k_mode[s_shared.setup.dsp_mode & AICA_DSPF_MODE]);
	return true;
}

void ps3aica_term()
{
	if (!s_up)
		return;
	g_aica_spe_on = false;
	g_aica_spe_snd = false;
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
