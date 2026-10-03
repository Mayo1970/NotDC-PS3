#pragma once
// OPTIMIZATION_PLAN 5.13 A1b: DSP recompiler runtime (tables, compile on demand, check, self-test, ISA probe), for
// spe_aica_model.cpp and the PC test. The includer defines DJ_LS, DJ_CALL (0 = ok), DJ_SYNC, DJ_TICKS, DJ_*REPORT.

#ifndef DJ_CODE_WORDS
#define DJ_CODE_WORDS 6144   // 24 KB (LS is short); a program that does not fit runs on the interpreter
#endif
#define DJ_COLD __attribute__((cold))   // boot self-test and reports: optimized for size
#ifndef DJ_CHECK_EVERY
#define DJ_CHECK_EVERY 7     // CHECK mode: one sample in 7 also runs the interpreter (odd: both MIXS banks)
#endif

static DspJitCtx dj_ctx;
static DspQ dj_coef[128], dj_madrs[64];
static DspQ dj_probe_io[4];
static uint32_t dj_code[DJ_CODE_WORDS] __attribute__((aligned(128)));
static DspJitLayout dj_layout;
static DspJitInfo dj_info;
static caique::AicaModel* dj_m;
static int dj_mode;
static bool dj_dirty = true, dj_ok;
static uint32_t dj_phase_n;
static volatile uint32_t* dj_where;   // the echo's phase word (LS), for a stall dump
static uint32_t dj_compiles, dj_fails, dj_odd, dj_checks, dj_bad, dj_bad_n, dj_ticks;
static AicaDspState dj_s0, dj_si, dj_sj;
static AicaDspBad dj_rep;
static AicaDspIsaBad dj_isa_rep;

static inline int32_t dj_sext(uint32_t v, int bits) { return (int32_t)(v << (32 - bits)) >> (32 - bits); }
static inline void dj_at(uint32_t p)
{
	if (dj_where)
		*dj_where = p;
}

// The interpreter's memory path for TABLE accesses and rings bigger than the LS copy (called from generated code)
extern "C" uint32_t dspjit_rd(uint32_t addr)
{
	const uint16_t rbpl = dj_m->r(0x2804);
	const uint32_t RBP = (rbpl & 0xFFF) * 2048u, RBL = (8192u << ((rbpl >> 13) & 3)) - 1;
	return caique_dsp_rd16(((addr << 1) + RBP) & (ARAM_SIZE - 1), RBP, RBL);
}
extern "C" void dspjit_wr(uint32_t addr, uint32_t v)
{
	const uint16_t rbpl = dj_m->r(0x2804);
	const uint32_t RBP = (rbpl & 0xFFF) * 2048u, RBL = (8192u << ((rbpl >> 13) & 3)) - 1;
	caique_dsp_wr16(((addr << 1) + RBP) & (ARAM_SIZE - 1), RBP, RBL, (uint16_t)v);
}

// After every model write: COEF and MADRS feed the tables, MPRO and RBP/RBL force a recompile.
static void dj_reg_written(uint32_t off)
{
	off &= 0x7FFC;
	if (off >= 0x3000 && off < 0x3200)
		dj_coef[(off - 0x3000) >> 2].w[0] = (uint32_t)((int32_t)(int16_t)dj_m->r(off) >> 3);
	else if (off >= 0x3200 && off < 0x3300)
		dj_madrs[(off - 0x3200) >> 2].w[0] = dj_m->r(off);
	else if ((off >= 0x3400 && off < 0x3C00) || off == 0x2804)
		dj_dirty = true;
}

static void dj_tables_all()
{
	for (uint32_t o = 0x3000; o < 0x3300; o += 4)
		dj_reg_written(o);
	dj_dirty = true;
}

static void dj_compile()
{
	dj_at(3);
	dj_dirty = false;
	const uint16_t rbpl = dj_m->r(0x2804);
	const uint32_t rbl = (8192u << ((rbpl >> 13) & 3)) - 1;
	dj_ok = dspjit_compile(&dj_layout, dj_m->prog, rbl, DSP_RING_BYTES, dj_code, DJ_CODE_WORDS, &dj_info) == 0;
	dj_compiles++;
	if (!dj_ok)
		dj_fails++;
	DJ_SYNC();
	dj_at(0);
}

// dsp_step() leaves no even read waiting and at most one read landing at step 1; the code assumes that.
static bool dj_state_ok(const caique::AicaModel* m, uint64_t smp)
{
	return m->mrd_even_masa < 0 && (m->mem_qn == 0 || (m->mem_qn == 1 && (uint64_t)m->mem_q[0].land == smp * 128 + 1));
}

static int dj_run(caique::AicaModel* m, uint64_t smp, uint32_t mdec)
{
	DspJitCtx& c = dj_ctx;
	c.acc.w[0] = (uint32_t)m->ACC;
	c.frc.w[0] = (uint32_t)dj_sext((uint32_t)m->FRC_REG, 13);
	c.y.w[0] = (uint32_t)m->Y_REG;
	c.adrs.w[0] = (uint32_t)dj_sext(m->ADRS_REG, 12);
	c.memval.w[0] = m->memval;
	c.pend_m.w[0] = m->mem_qn ? ~0u : 0;
	c.pend_v.w[0] = m->mem_qn ? m->mem_q[0].val : 0;
	c.nofl0.w[0] = m->nofl_pipe[1] ? ~0u : 0;
	c.nofl1.w[0] = m->nofl_pipe[0] ? ~0u : 0;
	c.mdec.w[0] = mdec;
	c.m4.w[0] = (mdec & 127) * 4;
	c.mixb.w[0] = DJ_LS(&m->MIXS_bank[(smp - 1) & 1][0]);
	dj_at(1);
	const uint32_t t0 = DJ_TICKS();
	const int r = DJ_CALL(DJ_LS(dj_code));
	dj_ticks += t0 - DJ_TICKS();
	dj_at(0);
	m->ACC = (int32_t)c.acc.w[0];
	m->FRC_REG = (int32_t)(c.frc.w[0] & 0x1FFF);
	m->Y_REG = (int32_t)c.y.w[0];
	m->ADRS_REG = c.adrs.w[0] & 0xFFF;
	m->memval = (uint16_t)c.memval.w[0];
	if (dj_info.pend_out)
	{
		m->mem_q[0].land = (int64_t)((smp + 1) * 128 + 1);
		m->mem_q[0].val = (uint16_t)c.pend_out.w[0];
		m->mem_qn = 1;
	}
	else
		m->mem_qn = 0;
	m->nofl_pipe[0] = dj_info.nofl127;
	m->nofl_pipe[1] = dj_info.nofl126;
	m->mrd_even_masa = -1;
	return r;
}

static void dj_save(const caique::AicaModel* m, AicaDspState* s)
{
	memcpy(s->temp, m->TEMP, sizeof(s->temp));
	memcpy(s->mems, m->MEMS, sizeof(s->mems));
	memcpy(s->efreg, m->EFREG, sizeof(s->efreg));
	s->acc = m->ACC;
	s->frc = m->FRC_REG;
	s->y = m->Y_REG;
	s->adrs = m->ADRS_REG;
	s->memval = m->memval;
	s->nofl = m->nofl_pipe[0] | ((uint32_t)m->nofl_pipe[1] << 8);
	s->mem_qn = m->mem_qn;
	s->mrd_even_masa = m->mrd_even_masa;
	const uint64_t land = m->mem_qn ? (uint64_t)m->mem_q[0].land : 0;
	s->land_lo = (uint32_t)land;
	s->land_hi = (uint32_t)(land >> 32);
	s->qval = m->mem_qn ? m->mem_q[0].val : 0;
	s->pad = 0;
}

static void dj_load(caique::AicaModel* m, const AicaDspState* s)
{
	memcpy(m->TEMP, s->temp, sizeof(s->temp));
	memcpy(m->MEMS, s->mems, sizeof(s->mems));
	memcpy(m->EFREG, s->efreg, sizeof(s->efreg));
	m->ACC = s->acc;
	m->FRC_REG = s->frc;
	m->Y_REG = s->y;
	m->ADRS_REG = s->adrs;
	m->memval = (uint16_t)s->memval;
	m->nofl_pipe[0] = (uint8_t)s->nofl;
	m->nofl_pipe[1] = (uint8_t)(s->nofl >> 8);
	m->mem_qn = s->mem_qn;
	m->mrd_even_masa = s->mrd_even_masa;
	if (s->mem_qn)
	{
		m->mem_q[0].land = (int64_t)(((uint64_t)s->land_hi << 32) | s->land_lo);
		m->mem_q[0].val = (uint16_t)s->qval;
	}
}

static uint32_t dj_cmp(const AicaDspState* a, const AicaDspState* b)
{
	uint32_t f = 0;
	if (memcmp(a->temp, b->temp, sizeof(a->temp))) f |= AICA_DSPD_TEMP;
	if (memcmp(a->mems, b->mems, sizeof(a->mems))) f |= AICA_DSPD_MEMS;
	if (memcmp(a->efreg, b->efreg, sizeof(a->efreg))) f |= AICA_DSPD_EFREG;
	if (a->acc != b->acc) f |= AICA_DSPD_ACC;
	if (a->frc != b->frc) f |= AICA_DSPD_FRC;
	if (a->y != b->y) f |= AICA_DSPD_Y;
	if (a->adrs != b->adrs) f |= AICA_DSPD_ADRS;
	if (a->memval != b->memval) f |= AICA_DSPD_MEMVAL;
	if (a->nofl != b->nofl || a->mem_qn != b->mem_qn || a->mrd_even_masa != b->mrd_even_masa || a->land_lo != b->land_lo ||
	    a->land_hi != b->land_hi || a->qval != b->qval)
		f |= AICA_DSPD_PIPE;
	return f;
}

DJ_COLD static void dj_report(caique::AicaModel* m, uint64_t smp, uint32_t mdec, uint32_t flags, uint32_t prog_id)
{
	AicaDspBad& r = dj_rep;
	r.sample_lo = (uint32_t)smp;
	r.mdec = mdec;
	r.rbpl = m->r(0x2804);
	r.flags = flags;
	r.prog_id = prog_id;
	r.nw = (uint32_t)dsp_wn;
	r.nr = (uint32_t)dsp_rn;
	r.pad = 0;
	r.start = dj_s0;
	r.interp = dj_si;
	r.jit = dj_sj;
	for (int i = 0; i < 512; i++)
		r.mpro[i] = m->reg[(0x3400 >> 2) + i];
	for (int i = 0; i < 128; i++)
		r.coef[i] = m->reg[(0x3000 >> 2) + i];
	for (int i = 0; i < 64; i++)
		r.madrs[i] = m->reg[(0x3200 >> 2) + i];
	memcpy(r.mixs, m->MIXS_bank[(smp - 1) & 1], sizeof(r.mixs));
	r.exts[0] = m->EXTS[0];
	r.exts[1] = m->EXTS[1];
	r.pad2[0] = r.pad2[1] = 0;
	for (int i = 0; i < 64; i++)
	{
		r.coll_claim[i] = m->coll_claim[i];
		r.coll_word[i] = m->coll_word[i];
	}
	memset(r.wr, 0, sizeof(r.wr));
	memset(r.rd, 0, sizeof(r.rd));
	for (int i = 0; i < dsp_wn; i++)
	{
		r.wr[i].a = dsp_wlog[i].a;
		r.wr[i].v = dsp_wlog[i].v;
		r.wr[i].jv = dsp_rd16_raw(dsp_wlog[i].a, dsp_wlog[i].rbp, dsp_wlog[i].rbl);
	}
	for (int i = 0; i < dsp_rn; i++)
	{
		r.rd[i].a = dsp_rlog[i].a;
		r.rd[i].v = dsp_rlog[i].v;
	}
	DJ_REPORT(&dj_rep, dj_bad_n);
	dj_bad_n++;
}

// Runs the interpreter (ring writes undone after), then the recompiled code from the same start, and compares.
// The recompiled result stays. The aram cache restarts empty for both runs when the program uses it.
static void dj_check(caique::AicaModel* m, uint64_t smp, uint32_t mdec, uint32_t self_id)
{
	dj_save(m, &dj_s0);
	if (dj_info.has_slow)
		aram_invalidate();
	dj_at(2);
	dsp_wn = dsp_rn = 0;
	dsp_log_on = 1;
	m->dsp_step(smp, mdec);
	dsp_log_on = 0;
	dj_save(m, &dj_si);
	for (int i = dsp_wn - 1; i >= 0; i--)
		dsp_wr16_raw(dsp_wlog[i].a, dsp_wlog[i].rbp, dsp_wlog[i].rbl, dsp_wlog[i].old);
	dj_load(m, &dj_s0);
	if (dj_info.has_slow)
		aram_invalidate();
	const int fault = dj_run(m, smp, mdec);
	dj_save(m, &dj_sj);
	uint32_t f = dj_cmp(&dj_si, &dj_sj) | (fault ? AICA_DSPD_FAULT : 0);
	// The last interpreter write to each LS-ring word must be what the recompiled code left there
	for (int i = dsp_wn - 1; i >= 0; i--)
	{
		const DspRingLog* w = &dsp_wlog[i];
		if (dsp_ring_off(w->a, w->rbp, w->rbl) < 0)
			continue;
		bool later = false;
		for (int j = i + 1; j < dsp_wn && !later; j++)
			later = dsp_wlog[j].a == w->a;
		if (!later && dsp_rd16_raw(w->a, w->rbp, w->rbl) != w->v)
			f |= AICA_DSPD_RING;
	}
	dj_checks++;
	if (f)
	{
		dj_bad++;
		if (dj_bad_n < AICA_DSP_BAD_MAX)
			dj_report(m, smp, mdec, f | (self_id ? AICA_DSPD_SELF : 0), self_id ? self_id - 1 : 0);
	}
}

bool caique_dsp_hook(caique::AicaModel* m, uint64_t smp, uint32_t mdec)
{
	if (!dj_mode)
		return false;
	if (dj_dirty)
		dj_compile();
	if (!dj_ok)
		return false;
	if (!dj_state_ok(m, smp))
	{
		dj_odd++;
		return false;
	}
	if (dj_mode == 2 && ++dj_phase_n >= DJ_CHECK_EVERY)
	{
		dj_phase_n = 0;
		dj_check(m, smp, mdec, 0);
		return true;
	}
	dj_run(m, smp, mdec);
	return true;
}

static void dj_init(caique::AicaModel* m, int mode, uint32_t rd_fn, uint32_t wr_fn, volatile uint32_t* where)
{
	dj_m = m;
	dj_mode = mode;
	dj_where = where;
	DspJitLayout& L = dj_layout;
	L.ctx = DJ_LS(&dj_ctx);
	L.temp = DJ_LS(&m->TEMP[0]);
	L.mems = DJ_LS(&m->MEMS[0]);
	L.efreg = DJ_LS(&m->EFREG[0]);
	L.exts = DJ_LS(&m->EXTS[0]);
	L.coll_claim = DJ_LS(&m->coll_claim[0]);
	L.coll_word = DJ_LS(&m->coll_word[0]);
	L.ring = DJ_LS(&dsp_ring[0]);
	L.coef = DJ_LS(&dj_coef[0]);
	L.madrs = DJ_LS(&dj_madrs[0]);
	L.rd_fn = rd_fn;
	L.wr_fn = wr_fn;
	if ((L.ctx | L.temp | DJ_LS(&m->MIXS_bank[0][0]) | L.ring | L.coef | L.madrs) & 15)
		dj_mode = 0;   // the generated code needs these 16-aligned
	dj_tables_all();
}

// --- Boot self-test: random programs and states, the interpreter against the recompiled code ---

static uint32_t dj_rng = 0x9E3779B9u;
static uint32_t dj_rand()
{
	dj_rng ^= dj_rng << 13;
	dj_rng ^= dj_rng >> 17;
	dj_rng ^= dj_rng << 5;
	return dj_rng;
}
static unsigned dj_flag() { return (dj_rand() & 3) == 0; }

static void dj_w(uint32_t off, uint32_t v)
{
	dj_m->write(off, v);
	dj_reg_written(off);
}

DJ_COLD static void dj_rand_program()
{
	for (int s = 0; s < 128; s++)
	{
		dsp_inst_t i;
		uint16_t w[4];
		i.TRA = dj_rand() & 0x7F; i.TWT = dj_flag(); i.TWA = dj_rand() & 0x7F;
		i.XSEL = dj_rand() & 1; i.YSEL = dj_rand() & 3; i.IRA = dj_rand() & 0x3F; i.IWT = dj_flag(); i.IWA = dj_rand() & 0x1F;
		i.TABLE = dj_flag(); i.MWT = dj_flag(); i.MRD = dj_flag(); i.EWT = dj_flag(); i.EWA = dj_rand() & 0xF;
		i.ADRL = dj_flag(); i.FRCL = dj_flag(); i.SHIFT = dj_rand() & 3; i.YRL = dj_flag(); i.NEGB = dj_flag();
		i.ZERO = dj_flag(); i.BSEL = dj_flag();
		i.NOFL = dj_flag(); i.MASA = dj_rand() & 0x3F; i.ADREB = dj_flag(); i.NXADR = dj_flag();
		dsp_encode(&i, w);
		for (int k = 0; k < 4; k++)
			dj_w(0x3400 + 16 * s + 4 * k, w[k]);
	}
	for (int s = 0; s < 128; s++)
		dj_w(0x3000 + 4 * s, dj_rand() & 0xFFF8);
	for (int k = 0; k < 64; k++)
		dj_w(0x3200 + 4 * k, dj_rand() & 0xFFFF);
	dj_w(0x2804, dj_rand() & 0xFFFF);
}

// Inputs that change every sample: the SGC buses and the slot collisions
DJ_COLD static void dj_rand_inputs(caique::AicaModel* m)
{
	for (int b = 0; b < 2; b++)
		for (int i = 0; i < 16; i++)
			m->MIXS_bank[b][i] = dj_sext(dj_rand(), 20);
	for (int k = 0; k < 64; k++)
	{
		m->coll_claim[k] = (dj_rand() & 3) == 0;
		m->coll_word[k] = (uint16_t)dj_rand();
	}
}

// Only states the model can hold: TEMP/MEMS/Y 24-bit, ACC 26-bit, FRC 13, ADRS 12, EXTS 16 (no << 8 overflow)
DJ_COLD static void dj_rand_state(caique::AicaModel* m, uint64_t smp)
{
	for (int i = 0; i < 128; i++)
		m->TEMP[i] = dj_sext(dj_rand(), 24);
	for (int i = 0; i < 32; i++)
		m->MEMS[i] = dj_sext(dj_rand(), 24);
	for (int i = 0; i < 16; i++)
		m->EFREG[i] = (int16_t)dj_rand();
	m->EXTS[0] = (int16_t)dj_rand();
	m->EXTS[1] = (int16_t)dj_rand();
	m->ACC = dj_sext(dj_rand(), 26);
	m->FRC_REG = (int32_t)(dj_rand() & 0x1FFF);
	m->Y_REG = dj_sext(dj_rand(), 24);
	m->ADRS_REG = dj_rand() & 0xFFF;
	m->memval = (uint16_t)dj_rand();
	m->nofl_pipe[0] = dj_rand() & 1;
	m->nofl_pipe[1] = dj_rand() & 1;
	m->mrd_even_masa = -1;
	m->mem_qn = dj_rand() & 1;
	m->mem_q[0].land = (int64_t)(smp * 128 + 1);
	m->mem_q[0].val = (uint16_t)dj_rand();
	for (uint32_t i = 0; i < DSP_RING_BYTES; i++)
		dsp_ring[i] = (uint8_t)dj_rand();
	dj_rand_inputs(m);
}

// Returns the number of differing samples; *ran = samples checked. Leaves the model reset.
DJ_COLD static uint32_t dj_selftest(uint32_t programs, uint32_t samples, uint32_t* ran)
{
	caique::AicaModel* m = dj_m;
	const uint32_t bad0 = dj_bad;
	uint64_t smp = 1000;
	uint32_t mdec = dj_rand() & 0xFFFF;
	*ran = 0;
	dj_at(4);
	for (uint32_t p = 0; p < programs; p++)
	{
		dj_rand_program();
		dj_rand_state(m, smp);
		dj_compile();
		if (!dj_ok)
			continue;   // too big for the buffer: the interpreter would run it
		for (uint32_t k = 0; k < samples; k++)
		{
			dj_at(4);
			dj_check(m, smp, mdec, p + 1);
			(*ran)++;
			smp++;
			mdec = (mdec - 1) & 0xFFFF;
			dj_rand_inputs(m);
		}
	}
	m->reset();
	dj_tables_all();
	memset(dsp_ring, 0, sizeof(dsp_ring));
	aram_invalidate();
	dj_at(0);
	return dj_bad - bad0;
}

// --- ISA probe: each emitted form on random operands against this C model of the SPU ISA ---

static uint8_t dj_qb(const DspQ* q, int i) { return (uint8_t)(q->w[i >> 2] >> (24 - 8 * (i & 3))); }
static void dj_set_qb(DspQ* q, int i, uint8_t v)
{
	const int sh = 24 - 8 * (i & 3);
	q->w[i >> 2] = (q->w[i >> 2] & ~(0xFFu << sh)) | ((uint32_t)v << sh);
}
static void dj_mask(DspQ* o, uint32_t t, int n)
{
	for (int i = 0; i < 16; i++)
		dj_set_qb(o, i, (uint8_t)(0x10 + i));
	for (int i = 0; i < n; i++)
		dj_set_qb(o, (int)t + i, (uint8_t)(4 - n + i));
}

DJ_COLD static void dj_isa_ref(int form, const DspQ* a, const DspQ* b, const DspQ* c, int imm, DspQ* o)
{
	for (int i = 0; i < 4; i++)
	{
		const uint32_t x = a->w[i], y = b->w[i];
		const uint32_t nl = y & 63, nr = (0u - y) & 63, il = (uint32_t)imm & 63;
		uint32_t r = 0;
		switch (form)
		{
		case DJF_A: r = x + y; break;
		case DJF_SF: r = y - x; break;
		case DJF_AND: r = x & y; break;
		case DJF_OR: r = x | y; break;
		case DJF_XOR: r = x ^ y; break;
		case DJF_ANDC: r = x & ~y; break;
		case DJF_EQV: r = ~(x ^ y); break;
		case DJF_CEQ: r = x == y ? ~0u : 0; break;
		case DJF_CGT: r = (int32_t)x > (int32_t)y ? ~0u : 0; break;
		case DJF_CLGT: r = x > y ? ~0u : 0; break;
		case DJF_SHL: r = nl < 32 ? x << nl : 0; break;
		case DJF_ROTM: r = nr < 32 ? x >> nr : 0; break;
		case DJF_ROTMA: r = (uint32_t)((int32_t)x >> (nr < 32 ? nr : 31)); break;
		case DJF_MPY: r = (uint32_t)((int32_t)(int16_t)x * (int32_t)(int16_t)y); break;
		case DJF_CLZ: r = x ? (uint32_t)__builtin_clz(x) : 32; break;
		case DJF_XSHW: r = (uint32_t)(int32_t)(int16_t)x; break;
		case DJF_SELB: r = (c->w[i] & y) | (~c->w[i] & x); break;
		case DJF_SHLI: r = il < 32 ? x << il : 0; break;
		case DJF_ROTMI: r = il < 32 ? x >> il : 0; break;
		case DJF_ROTMAI: r = (uint32_t)((int32_t)x >> (il < 32 ? il : 31)); break;
		case DJF_AI: r = x + (uint32_t)imm; break;
		case DJF_SFI: r = (uint32_t)imm - x; break;
		case DJF_ANDI: r = x & (uint32_t)imm; break;
		case DJF_ORI: r = x | (uint32_t)imm; break;
		case DJF_CGTI: r = (int32_t)x > imm ? ~0u : 0; break;
		case DJF_IL: r = (uint32_t)imm; break;
		case DJF_ILHU: r = (uint32_t)imm << 16; break;
		case DJF_IOHL: r = x | (uint32_t)imm; break;
		case DJF_ILA: r = (uint32_t)imm; break;
		default: break;
		}
		o->w[i] = r;
	}
	switch (form)
	{
	case DJF_ROTQBY:
	case DJF_ROTQBYI:
	{
		const uint32_t n = form == DJF_ROTQBY ? b->w[0] & 15 : (uint32_t)imm & 15;
		for (int i = 0; i < 16; i++)
			dj_set_qb(o, i, dj_qb(a, (int)((i + n) & 15)));
		break;
	}
	case DJF_CWX: dj_mask(o, (a->w[0] + b->w[0]) & 0xC, 4); break;
	case DJF_CHX: dj_mask(o, (a->w[0] + b->w[0]) & 0xE, 2); break;
	case DJF_CWD: dj_mask(o, (a->w[0] + (uint32_t)imm) & 0xC, 4); break;
	case DJF_SHUFB:
		for (int i = 0; i < 16; i++)
		{
			const uint8_t k = dj_qb(c, i);
			uint8_t v;
			if ((k & 0xC0) == 0x80)
				v = 0;
			else if ((k & 0xE0) == 0xC0)
				v = 0xFF;
			else if ((k & 0xE0) == 0xE0)
				v = 0x80;
			else
				v = (k & 0x10) ? dj_qb(b, k & 15) : dj_qb(a, k & 15);
			dj_set_qb(o, i, v);
		}
		break;
	default: break;
	}
}

// 0 = every form matched; else the failing form + 1 (details in dj_isa_rep)
DJ_COLD static int dj_isa_probe(uint32_t vectors)
{
	dj_at(5);
	for (int f = 0; f < DJF_COUNT; f++)
		for (uint32_t k = 0; k < vectors; k++)
		{
			DspQ* io = dj_probe_io;
			for (int q = 0; q < 3; q++)
				for (int i = 0; i < 4; i++)
					io[q].w[i] = dj_rand();
			if (k & 1)   // small and negative shift counts
				for (int i = 0; i < 4; i++)
					io[1].w[i] = (dj_rand() & 0x7F) - 64;
			const int lo = dspjit_form_imm[f][0], hi = dspjit_form_imm[f][1];
			const int imm = lo + (int)(dj_rand() % (uint32_t)(hi - lo + 1));
			io[3].w[0] = io[3].w[1] = io[3].w[2] = io[3].w[3] = 0x5A5A5A5Au;
			if (dspjit_probe(dj_code, DJ_CODE_WORDS, f, DJ_LS(&io[0]), DJ_LS(&io[3]), imm) < 0)
				return f + 1;
			DJ_SYNC();
			const int fault = DJ_CALL(DJ_LS(dj_code));
			DspQ exp;
			dj_isa_ref(f, &io[0], &io[1], &io[2], imm, &exp);
			if (fault || memcmp(&exp, &io[3], sizeof(exp)))
			{
				dj_isa_rep.form = (uint32_t)f;
				dj_isa_rep.imm = imm;
				dj_isa_rep.pad[0] = dj_isa_rep.pad[1] = 0;
				memcpy(dj_isa_rep.in, io, 48);
				memcpy(dj_isa_rep.exp, exp.w, 16);
				memcpy(dj_isa_rep.got, io[3].w, 16);
				DJ_ISA_REPORT(&dj_isa_rep);
				dj_dirty = true;
				dj_at(0);
				return f + 1;
			}
		}
	dj_dirty = true;
	dj_at(0);
	return 0;
}
