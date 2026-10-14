// One caique model build behind a C API; compiled once per model with -DPFX=<prefix>_ (and -Dcaique=<ns> for the base).
#include "aica_model.h"
#define CAT2(a, b) a##b
#define CAT(a, b) CAT2(a, b)
#define F(n) CAT(PFX, n)
typedef caique::AicaModel M;
extern "C" {
void* F(create)(int no_dsp) { M* m = new M(); m->no_dsp = no_dsp != 0; return m; }
void F(write)(void* h, uint32_t off, uint32_t v) { ((M*)h)->write(off, v); }
uint32_t F(read)(void* h, uint32_t off) { return ((M*)h)->read(off); }
void F(step)(void* h, int32_t e0, int32_t e1, int16_t* out)
{
    M* m = (M*)h;
    m->EXTS[0] = e0;
    m->EXTS[1] = e1;
    m->step();
    out[0] = m->outL;
    out[1] = m->outR;
}
// The PPU's catch-up: the control pass up to sample `to`
void F(catch_up)(void* h, uint32_t to)
{
    M* m = (M*)h;
#ifdef HAVE_ADVANCE
    const uint32_t n = to - (uint32_t)m->samples;
    if (n) m->advance(n, false, 0);
#else
    while ((uint32_t)m->samples != to) m->step_control();
#endif
}
// snap_hash of the big-endian CtlSnap image, as the PPU and the SPU compute it
uint32_t F(snap_hash)(void* h)
{
    static caique::CtlSnap s;
    ((M*)h)->snap(s);
    uint32_t w[sizeof(s) / 4], n = 0;
    w[n++] = s.sample; w[n++] = s.lfsr; w[n++] = s.mdec; w[n++] = s.eg_cnt;
    for (int c = 0; c < 64; c++) {
        const caique::SlotSnap& p = s.s[c];
        w[n++] = p.CA;
        w[n++] = p.step;
        w[n++] = (uint32_t)p.a << 16 | p.fv;
        w[n++] = (uint32_t)p.aeg << 24 | (uint32_t)p.feg << 16 | (uint32_t)p.flags << 8 | p.lfo;
        w[n++] = p.lfo_cnt;
    }
    uint32_t x = 2166136261u;
    for (uint32_t i = 0; i < n; i++) x = (x ^ w[i]) * 16777619u;
    return x;
}
#ifdef HAVE_ADVANCE
// n samples slot by slot; render: out gets n L/R pairs through final_mix with the EXTS pairs in exts
void F(advance)(void* h, uint32_t n, int render, const int32_t* exts, int16_t* out)
{
    M* m = (M*)h;
    static int32_t mix[4 * 256] __attribute__((aligned(16)));
    while (n) {
        const uint32_t k = n < 256 ? n : 256;
        m->advance(k, render != 0, render ? mix : 0);
        if (render)
            for (uint32_t i = 0; i < k; i++) {
                m->EXTS[0] = exts[2 * i];
                m->EXTS[1] = exts[2 * i + 1];
                m->final_mix(mix[4 * i], mix[4 * i + 1]);
                out[2 * i] = m->outL;
                out[2 * i + 1] = m->outR;
            }
        exts += 2 * k;
        out += 2 * k;
        n -= k;
    }
}
// 5.13 A3: n samples with the DSP as the SPU runs them: rounds of DSP_ROUND, then EXTS, dsp_sample, final_mix
void F(advance_dsp)(void* h, uint32_t n, const int32_t* exts, int16_t* out)
{
    M* m = (M*)h;
    static int32_t mix[4 * M::DSP_ROUND] __attribute__((aligned(16)));
    while (n) {
        const uint32_t k = n < M::DSP_ROUND ? n : M::DSP_ROUND;
        m->advance(k, true, mix);
        for (uint32_t i = 0; i < k; i++) {
            m->EXTS[0] = exts[2 * i];
            m->EXTS[1] = exts[2 * i + 1];
            m->dsp_sample(i);
            m->final_mix(mix[4 * i], mix[4 * i + 1]);
            out[2 * i] = m->outL;
            out[2 * i + 1] = m->outR;
        }
        exts += 2 * k;
        out += 2 * k;
        n -= k;
    }
}
#endif
// DSP state run() leaves: TEMP, MEMS, EFREG, both MIXS banks, the registers and the read pipeline
uint32_t F(dsp_hash)(void* h)
{
    const M* m = (const M*)h;
    uint32_t x = 2166136261u;
    auto mixw = [&](uint32_t w) { x = (x ^ w) * 16777619u; };
    for (int i = 0; i < 128; i++) mixw((uint32_t)m->TEMP[i]);
    for (int i = 0; i < 32; i++) mixw((uint32_t)m->MEMS[i]);
    for (int i = 0; i < 16; i++) mixw((uint32_t)m->EFREG[i]);
    for (int b = 0; b < 2; b++)
        for (int i = 0; i < 16; i++) mixw((uint32_t)m->MIXS_bank[b][i]);
    mixw((uint32_t)m->ACC); mixw((uint32_t)m->FRC_REG); mixw((uint32_t)m->Y_REG); mixw(m->ADRS_REG); mixw(m->memval);
    mixw(m->nofl_pipe[0] | (uint32_t)m->nofl_pipe[1] << 8);
    mixw((uint32_t)m->mrd_even_masa);
    mixw((uint32_t)m->mem_qn);
    for (int q = 0; q < m->mem_qn; q++) { mixw((uint32_t)m->mem_q[q].land); mixw((uint32_t)(m->mem_q[q].land >> 32)); mixw(m->mem_q[q].val); }
    return x;
}
uint32_t F(ram_read32)(void* h, uint32_t off) { return ((M*)h)->ram_read32(off); }
uint64_t F(quiet)(void* h) { return ((M*)h)->quiet; }
void F(stepc)(void* h) { ((M*)h)->step_control(); }
uint8_t* F(ram)(void* h) { return ((M*)h)->ram; }
uint64_t F(samples)(void* h) { return ((M*)h)->samples; }
void F(live)(void* h, uint32_t* l) { ((M*)h)->live_stats(l[0], l[1], l[2], l[3]); }
#ifdef HAVE_ADVANCE
void F(set_mono)(void* h, int on) { ((M*)h)->mono_half = on != 0; }   // P31 Mono Mix Fix
void F(set_gain)(void* h, uint32_t g) { ((M*)h)->out_gain = g; }       // plan 6.6 SPE AICA Gain, in 1/256
void F(set_adpcm)(void* h, int on) { ((M*)h)->adpcm_restore = on != 0; }   // plan 6.9 B ADPCM Loop RESTORE
void F(set_stream)(void* h, int on) { ((M*)h)->stream_lsa = on != 0; }     // plan 6.9 B ADPCM Stream FIXED
#endif
}
