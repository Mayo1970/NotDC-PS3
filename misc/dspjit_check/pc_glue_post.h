#pragma once
// OPTIMIZATION_PLAN 5.13 A1b PC test: LS layout of the simulator and the mismatch printers.
// Field phases differ from the SPU build on purpose (MEMS +4, EFREG +8, EXTS +12, coll_claim +3, coll_word +6).

static uint32_t g_stack[0xFF00 / 4];

static uint32_t hc_rd(uint32_t a, uint32_t) { return dspjit_rd(a); }
static uint32_t hc_wr(uint32_t a, uint32_t v)
{
	dspjit_wr(a, v);
	return 0;
}

// Maps the model and the recompiler state into the simulator, then dj_init()
static void pc_setup(caique::AicaModel* m, int mode)
{
	spusim_init(&g_sim);
	pc_map(&dj_ctx, sizeof(dj_ctx), 0x01000, 4);
	pc_map(dj_coef, sizeof(dj_coef), 0x01400, 4);
	pc_map(dj_madrs, sizeof(dj_madrs), 0x01C00, 4);
	pc_map(dj_probe_io, sizeof(dj_probe_io), 0x02000, 4);
	pc_map(m->TEMP, sizeof(m->TEMP), 0x02100, 4);
	pc_map(m->MEMS, sizeof(m->MEMS), 0x02304, 4);
	pc_map(m->EFREG, sizeof(m->EFREG), 0x02388, 4);
	pc_map(m->EXTS, sizeof(m->EXTS), 0x023CC, 4);
	pc_map(m->MIXS_bank, sizeof(m->MIXS_bank), 0x02400, 4);
	pc_map(m->coll_claim, sizeof(m->coll_claim), 0x02483, 1);
	pc_map(m->coll_word, sizeof(m->coll_word), 0x024C6, 2);
	pc_map(dsp_ring, sizeof(dsp_ring), 0x10000, 1);
	pc_map(dj_code, sizeof(dj_code), 0x20400, 4);   // after the ring's spare quadword
	pc_map(g_stack, sizeof(g_stack), 0x30000, 4);
	spusim_hostcall(&g_sim, PC_HELPER_RD, hc_rd);
	spusim_hostcall(&g_sim, PC_HELPER_WR, hc_wr);
	dj_init(m, mode, PC_HELPER_RD, PC_HELPER_WR, nullptr);
}

static void pc_diff_arr(FILE* f, const char* name, const int32_t* s, const int32_t* i, const int32_t* j, int n)
{
	int shown = 0;
	for (int k = 0; k < n && shown < 6; k++)
		if (i[k] != j[k])
		{
			fprintf(f, "  %s[%d] start %X interp %X jit %X\n", name, k, (uint32_t)s[k], (uint32_t)i[k], (uint32_t)j[k]);
			shown++;
		}
}

static void pc_report(const AicaDspBad* r, uint32_t n)
{
	FILE* f = g_replog ? g_replog : stderr;
	g_reports++;
	fprintf(f, "DSP MISMATCH #%u sample %u mdec %04X rbpl %04X flags %X prog %u nw %u nr %u\n", n, r->sample_lo, r->mdec,
	        r->rbpl, r->flags, r->prog_id, r->nw, r->nr);
	const AicaDspState *s = &r->start, *i = &r->interp, *j = &r->jit;
	pc_diff_arr(f, "TEMP", s->temp, i->temp, j->temp, 128);
	pc_diff_arr(f, "MEMS", s->mems, i->mems, j->mems, 32);
	pc_diff_arr(f, "EFREG", s->efreg, i->efreg, j->efreg, 16);
	fprintf(f, "  ACC %X/%X/%X FRC %X/%X/%X Y %X/%X/%X ADRS %X/%X/%X memval %X/%X/%X nofl %X/%X/%X qn %d/%d/%d qval %X/%X/%X\n",
	        s->acc, i->acc, j->acc, s->frc, i->frc, j->frc, s->y, i->y, j->y, s->adrs, i->adrs, j->adrs, s->memval,
	        i->memval, j->memval, s->nofl, i->nofl, j->nofl, s->mem_qn, i->mem_qn, j->mem_qn, s->qval, i->qval, j->qval);
	for (uint32_t k = 0; k < r->nw; k++)
		if (r->wr[k].v != r->wr[k].jv)
			fprintf(f, "  ring wr #%u a %06X interp %04X jit %04X\n", k, r->wr[k].a, r->wr[k].v, r->wr[k].jv);
	for (int st = 0; st < 128; st++)
	{
		const uint16_t* w = &r->mpro[st * 4];
		if (w[0] | w[1] | w[2] | w[3])
		{
			dsp_inst_t d;
			dsp_decode(w, &d);
			fprintf(f, "  s%-3d TRA %02X TWT %u TWA %02X XSEL %u YSEL %u IRA %02X IWT %u IWA %02X TABLE %u MWT %u MRD %u EWT %u EWA %X"
			        " ADRL %u FRCL %u SHIFT %u YRL %u NEGB %u ZERO %u BSEL %u NOFL %u MASA %02X ADREB %u NXADR %u\n",
			        st, d.TRA, d.TWT, d.TWA, d.XSEL, d.YSEL, d.IRA, d.IWT, d.IWA, d.TABLE, d.MWT, d.MRD, d.EWT, d.EWA, d.ADRL,
			        d.FRCL, d.SHIFT, d.YRL, d.NEGB, d.ZERO, d.BSEL, d.NOFL, d.MASA, d.ADREB, d.NXADR);
		}
	}
}

static void pc_isa_report(const AicaDspIsaBad* r)
{
	fprintf(stderr, "ISA MISMATCH %s imm %d a %08X %08X %08X %08X b %08X %08X %08X %08X c %08X %08X %08X %08X\n"
	        "  exp %08X %08X %08X %08X got %08X %08X %08X %08X\n",
	        dspjit_form_names[r->form], r->imm, r->in[0], r->in[1], r->in[2], r->in[3], r->in[4], r->in[5], r->in[6],
	        r->in[7], r->in[8], r->in[9], r->in[10], r->in[11], r->exp[0], r->exp[1], r->exp[2], r->exp[3], r->got[0],
	        r->got[1], r->got[2], r->got[3]);
}
