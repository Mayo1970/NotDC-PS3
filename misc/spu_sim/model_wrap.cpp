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
#endif
uint64_t F(quiet)(void* h) { return ((M*)h)->quiet; }
void F(stepc)(void* h) { ((M*)h)->step_control(); }
uint8_t* F(ram)(void* h) { return ((M*)h)->ram; }
uint64_t F(samples)(void* h) { return ((M*)h)->samples; }
void F(live)(void* h, uint32_t* l) { ((M*)h)->live_stats(l[0], l[1], l[2], l[3]); }
}
