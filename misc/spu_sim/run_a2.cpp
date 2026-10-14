// 5.13 A2 high voices / A3: the real spe_aica image (sound mode) in the simulator, fed like ps3_aica_spe.cpp; checks bad=0
// and every 48 kHz frame against the base model, reports SPU cycles. Usage: run_a2 elf [scenario seconds voices seed]
#include "spusim.h"
#include "spe_aica.h"
#include "dsp_asm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <math.h>
#include <random>
#include <vector>

extern "C" {
#define DECL(p) void* p##create(int); void p##write(void*, uint32_t, uint32_t); uint32_t p##read(void*, uint32_t); \
    void p##step(void*, int32_t, int32_t, int16_t*); void p##catch_up(void*, uint32_t); uint32_t p##snap_hash(void*); \
    uint8_t* p##ram(void*); uint64_t p##samples(void*); void p##live(void*, uint32_t*);
DECL(base_) DECL(cur_)
void cur_set_mono(void*, int);
void cur_set_gain(void*, uint32_t);
void cur_set_adpcm(void*, int);
void cur_set_stream(void*, int);
}

// ---- main memory (big-endian data, PS3 layout) ----
enum { EA_BELL = 0x10000000, EA_ECHO = 0x10001000, EA_SHARED = 0x10100000, EA_ARAM = 0x20000000, EA_RING = 0x30000000,
       EA_WORDS = 0x30100000 };
static const uint32_t RING_FRAMES = 8192;
struct Region { uint64_t ea; uint32_t size; uint8_t* p; };
static Region g_reg[6];
static uint8_t *g_bell, *g_echo, *g_shared, *g_aram, *g_ring, *g_words;

static uint8_t* ea_fn(void*, uint64_t ea, uint32_t size)
{
    for (auto& r : g_reg)
        if (r.p && ea >= r.ea && ea + size <= r.ea + r.size) return r.p + (ea - r.ea);
    return 0;
}
static void put32(uint8_t* p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static uint32_t get32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
#define ECHO(f) get32(g_echo + offsetof(AicaEcho, f))

// ---- SPU float rounding (truncation) for the reference resampler ----
static float f_trunc(double d)
{
    if (d == 0.0 || fabs(d) < 1.1754943508222875e-38) return 0.0f;
    float f = (float)d;
    if (fabs((double)f) > fabs(d)) { uint32_t w; memcpy(&w, &f, 4); w--; memcpy(&f, &w, 4); }
    return f;
}
struct RefSnd { uint32_t phase = 0; int32_t pl = 0, pr = 0; std::vector<float> out; };
static void ref_sample(RefSnd& s, int32_t l, int32_t r)
{
    const float scale = 1.0f / (32768.0f * 48000);
    uint32_t ph = s.phase;
    while (ph < 48000)
    {
        s.out.push_back(f_trunc((double)f_trunc((double)(s.pl * (int32_t)(48000 - ph) + l * (int32_t)ph)) * scale));
        s.out.push_back(f_trunc((double)f_trunc((double)(s.pr * (int32_t)(48000 - ph) + r * (int32_t)ph)) * scale));
        ph += 44100;
    }
    s.phase = ph - 48000;
    s.pl = l;
    s.pr = r;
}

// ---- scenario: a voice-heavy load like the ChuChu Rocket stage ----
static std::mt19937 g_rng;
static uint32_t rnd(uint32_t n) { return (uint32_t)(g_rng() % n); }
struct Ev { uint32_t off, val; };
static void* g_base;   // reference: full model; run() with the DSP in the dsp scenarios (A3)
// The reference is the base model, or the working tree's (plain build, run()) for what the base lacks (bundle 5)
static bool g_ref_cur;
static void base_write_(uint32_t o, uint32_t v) { g_ref_cur ? cur_write(g_base, o, v) : base_write(g_base, o, v); }
static uint32_t base_read_(uint32_t o) { return g_ref_cur ? cur_read(g_base, o) : base_read(g_base, o); }
static uint8_t* base_ram_() { return g_ref_cur ? cur_ram(g_base) : base_ram(g_base); }
static void* g_cur;    // the PPU's control twin (current tree)
static std::vector<uint8_t> g_dc;   // DC-order wave RAM, mirrored into both models and the PS3-layout aica_ram

static uint32_t g_head, g_checks, g_cdda_n;
static uint32_t g_now;   // samples run (the PPU's s_now)

// Bundle 5 (plan section 7). ring*: P20 G2/G3 on; the guest writes the DSP ring behind a SYNC and reads DSP registers
// (G4); wave RAM must match a model whose ring is plain RAM. mono: P31 (env MONO=0: the old sum).
static bool g_rscn, g_mono;   // a ring* scenario; the mono scenario
static uint32_t g_gain;       // env GAIN: SPE AICA Gain in 1/256 from the start; env GAIN2: the level from half the run on
static uint32_t g_adpcm;      // env ADPCM: ADPCM Loop (1 = RESTORE, plan 6.9 B) from the start; env ADPCM2: from half the run on
static uint32_t g_prog_at = ~0u;   // the sample a new DSP program comes at
static uint32_t g_setup_flags, g_rbp = 0x1E0000, g_ring_bytes = 65536;
static uint32_t g_ring_plo = 1, g_ring_phi, g_ring_pt;
static uint32_t g_sync_seq, g_rd_seq, g_guest_wr, g_rd_ok, g_rd_bad, g_ring_bad, g_ram_bad;
static void pump();

static void push_at(uint32_t sample, uint8_t op, uint16_t off, uint32_t val, uint32_t hash)
{
    uint8_t* r = g_shared + offsetof(AicaShared, log) + (g_head & (AICA_LOG - 1)) * sizeof(AicaRec);
    put32(r, sample);
    r[4] = op;
    r[5] = 0;
    r[6] = (uint8_t)(off >> 8);
    r[7] = (uint8_t)off;
    put32(r + 8, val);
    put32(r + 12, hash);
    g_head++;
}

// As ps3_aica_spe.cpp: a waiting RING record goes first
static void push(uint8_t op, uint16_t off, uint32_t val, uint32_t hash)
{
    if (g_ring_phi >= g_ring_plo)
    {
        const uint32_t v = g_ring_plo | g_ring_phi << 16;
        g_ring_plo = 1;
        g_ring_phi = 0;
        push_at(g_ring_pt, AICA_OP_RING, 0, v, 0);
    }
    push_at(g_now, op, off, val, hash);
}

static void wr(uint32_t off, uint32_t val)
{
    cur_catch_up(g_cur, g_now);
    cur_write(g_cur, off, val);
    base_write_(off, val);
    push(AICA_OP_WRITE, (uint16_t)off, val, 0);
}

struct Voice { bool on; uint32_t fmt; };
static Voice g_v[64];
static int g_nv;
static const char* g_scn = "mix";
static bool g_dsp;   // dsp / dsprand: the SPU runs the DSP (A3); env DSPJIT = its mode, default 1 (recompiler)

static uint32_t pick_fmt()
{
    if (!strcmp(g_scn, "adpcm")) return 2;
    if (!strcmp(g_scn, "stream")) return 3;   // ADPCM streams: loop ends that are no multiple of 4 (env STREAM)
    if (!strcmp(g_scn, "pcm16")) return 0;
    if (!strcmp(g_scn, "pcm8")) return 1;
    const uint32_t r = rnd(10);
    return r < 6 ? 2 : r < 9 ? 0 : 1;
}

static void program_voice(int ch)
{
    const uint32_t b = 0x80 * ch, fmt = pick_fmt();
    g_v[ch].fmt = fmt;
    // dsp scenarios: 0x1E0000 up is the DSP's (ring, then a table), no slot reads it
    // ring scenarios: nor the first 128 KB, where a ring or a table past the wave RAM end lands
    const uint32_t sa = fmt == 0 ? (g_rscn ? 0x020000 + rnd(0x6F000) * 2 : 0x000000 + rnd(0x7F000) * 2)
                      : fmt == 1 ? 0x180000 + rnd(g_dsp ? 0x50000 : 0x70000) : 0x100000 + rnd(0x70000);
    const uint32_t len = 2000 + rnd(12000), lsa = rnd(500);
    wr(b + 0x00, (sa >> 16) | fmt << 7 | 1u << 9);   // LPCTL loop
    wr(b + 0x04, sa & 0xFFFF);
    wr(b + 0x08, lsa);
    wr(b + 0x0C, lsa + len);
    wr(b + 0x10, 31 | (8 + rnd(10)) << 6 | rnd(6) << 11);   // AR fast, D1R, D2R slow
    wr(b + 0x14, (10 + rnd(10)) | (4 + rnd(16)) << 5 | 15 << 10);   // RR, DL, KRS off
    const uint32_t oct = rnd(4) == 0 ? 0xE : rnd(2) ? 0xF : 0;
    wr(b + 0x18, oct << 11 | rnd(0x400));
    wr(b + 0x1C, rnd(5) == 0 ? (0x0200 | rnd(8) << 10 | 2u << 5) : 0);   // some vibrato
    wr(b + 0x20, rnd(16) | rnd(16) << 4);
    wr(b + 0x24, (0xD + rnd(3)) << 8 | rnd(32));   // DISDL, DIPAN
    wr(b + 0x28, rnd(64) << 8 | rnd(32) | (rnd(8) == 0 ? 0x20 : 0));   // TL, Q, mostly filter on
    for (uint32_t o = 0x2C; o <= 0x3C; o += 4) wr(b + o, 0x400 + rnd(0x1C00));
    wr(b + 0x40, rnd(20) << 8 | rnd(20));
    wr(b + 0x44, rnd(20) << 8 | rnd(20));
}

static void key(int ch, bool on)
{
    const uint32_t b = 0x80 * ch;
    const uint32_t r0 = base_read_(b) & 0x47FF;
    wr(b, on ? (r0 | 0x4000) : (r0 & ~0x4000u));
    g_v[ch].on = on;
}
static void kyonex() { wr(0, (base_read_(0) & 0x47FF) | 0x8000); }

// A3: a program with half its steps busy: bus and EXTS inputs, ring reads and writes (some on even steps, so playing
// slots collide), TABLE reads above the ring, EFREG outputs. No TABLE write and no 64K-word ring: the SPU keeps neither.
static void program_dsp()
{
    uint32_t rbl = !strcmp(g_scn, "dsprand") ? rnd(3) : 2, base = 0x1E0000;
    if (g_rscn)   // ring: 32K words. ring64: 64K words (the line cache). ringwrap: past the wave RAM end. ringrand: any
    {
        rbl = !strcmp(g_scn, "ring64") ? 3 : !strcmp(g_scn, "ringrand") ? rnd(4) : 2;
        if (!strcmp(g_scn, "ringwrap")) base = 0x1F8000;
        else if (!strcmp(g_scn, "ringrand") && rbl < 2) base += 0x4000 * rnd(4);
    }
    g_rbp = base;
    g_ring_bytes = 16384u << rbl;
    const bool table_wr = g_rscn && strcmp(g_scn, "ring") != 0;   // TABLE writes need P20 G3
    if (getenv("VERBOSE")) fprintf(stderr, "sample %u: DSP program on ring %06X + %u bytes\n", g_now, base, g_ring_bytes);
    wr(0x2804, (base / 2048) | rbl << 13);
    for (int s = 0; s < 128; s++)
    {
        dsp_inst_t i = {};
        if (rnd(2))
        {
            i.TRA = rnd(0x80); i.TWT = rnd(3) == 0; i.TWA = rnd(0x80);
            i.XSEL = rnd(2); i.YSEL = rnd(4); i.IRA = rnd(3) ? 0x20 + rnd(16) : rnd(4) ? rnd(0x20) : 0x30 + rnd(2);
            i.IWT = rnd(6) == 0; i.IWA = rnd(0x20);
            i.MRD = rnd(3) == 0; i.MWT = !i.MRD && rnd(3) == 0; i.TABLE = (i.MRD || (table_wr && i.MWT)) && rnd(6) == 0;
            i.EWT = rnd(4) == 0; i.EWA = rnd(16);
            i.ADRL = rnd(8) == 0; i.FRCL = rnd(6) == 0; i.SHIFT = rnd(4); i.YRL = rnd(6) == 0;
            i.NEGB = rnd(4) == 0; i.ZERO = rnd(4) == 0; i.BSEL = rnd(3) == 0;
            i.NOFL = rnd(6) == 0; i.MASA = rnd(0x40); i.ADREB = rnd(4) == 0; i.NXADR = rnd(4) == 0;
        }
        uint16_t w[4];
        dsp_encode(&i, w);
        for (int k = 0; k < 4; k++) wr(0x3400 + 16 * s + 4 * k, w[k]);
    }
    for (int s = 0; s < 128; s++) wr(0x3000 + 4 * s, rnd(0x10000) & 0xFFF8);
    for (int k = 0; k < 64; k++) wr(0x3200 + 4 * k, rnd(0x10000));
    for (int k = 0; k < 16; k++) wr(0x2000 + 4 * k, rnd(2) ? (8 + rnd(8)) << 8 | rnd(32) : 0);   // EFSDL, EFPAN
}

// ---- bundle 5: what the guest does to the DSP ring and to the DSP registers ----
static void fail_at(const char* what, uint32_t a, uint32_t b, uint32_t c)
{
    fprintf(stderr, "%s at sample %u: %08X %08X %08X\n", what, g_now, a, b, c);
}

// Plan 6.6: the SPU plays up to this sample, writes its ring changes back and drops its cache; then the guest writes
static void sync()
{
    push(AICA_OP_SYNC, 0, ++g_sync_seq, 0);
    pump();
    if (ECHO(sync) != g_sync_seq)
    {
        fail_at("SYNC not answered", ECHO(sync), g_sync_seq, 0);
        exit(1);
    }
}

// A word, a short run, a "clear" of whole lines or a halfword inside the ring, behind a SYNC; then its RING record
static void guest_ring_write()
{
    const uint32_t kind = rnd(4), n = kind == 0 ? 4 : kind == 1 ? 4 * (1 + rnd(16)) : kind == 2 ? 128 * (1 + rnd(16)) : 2;
    const uint32_t off = rnd(g_ring_bytes - n + 1) & ~(n >= 4 ? 3u : 1u);
    sync();
    uint8_t* ref = base_ram_();
    for (uint32_t k = 0; k < n; k++)
    {
        const uint32_t a = (g_rbp + off + k) & 0x1FFFFF;
        const uint8_t v = kind == 2 ? 0 : (uint8_t)rnd(256);
        g_dc[a] = ref[a] = g_aram[a ^ 3] = v;
        const uint32_t l = a >> 7;   // ps3aica_ring_wrote
        if (g_ring_phi < g_ring_plo) { g_ring_plo = g_ring_phi = l; g_ring_pt = g_now; }
        else { if (l < g_ring_plo) g_ring_plo = l; if (l > g_ring_phi) g_ring_phi = l; }
    }
    if (rnd(4) == 0)   // a wider range, as an SH4 dirty range can be: the SPU must take the ring's part only
    {
        const uint32_t d = rnd(64);
        g_ring_plo = g_ring_plo > d ? g_ring_plo - d : 0;
        g_ring_phi = g_ring_phi + d < 16384 ? g_ring_phi + d : 16383;
    }
    g_guest_wr++;
}

// P20 G4: TEMP, MEMS, MIXS or EFREG at this sample, against the reference's read()
static void dsp_read_check()
{
    const uint32_t r = rnd(4);
    const uint32_t off = r == 0 ? 0x4000 + 4 * rnd(256) : r == 1 ? 0x4400 + 4 * rnd(64) : r == 2 ? 0x4500 + 4 * rnd(32) : 0x4580 + 4 * rnd(16);
    push(AICA_OP_DSPRD, (uint16_t)off, ++g_rd_seq, 0);
    pump();
    const uint32_t got = ECHO(dsp_rd), want = base_read_(off) & 0xFFFF;
    if ((got >> 16) == (g_rd_seq & 0xFFFF) && (got & 0xFFFF) == want)
        g_rd_ok++;
    else if (g_rd_bad++ < 4)
        fail_at("DSP register read", off, got, want);
}

// After a CHECK: the ring in wave RAM (every time) and all of wave RAM (now and then) against the reference's
static void ram_compare(bool all)
{
    const uint8_t* ref = base_ram_();
    if (g_setup_flags & (g_ring_bytes <= 65536 ? AICA_SETUP_RING : AICA_SETUP_RING64))
        for (uint32_t o = 0; o < g_ring_bytes; o++)
        {
            const uint32_t a = (g_rbp + o) & 0x1FFFFF;
            if (g_aram[a ^ 3] != ref[a] && g_ring_bad++ < 4)
                fail_at("ring byte in wave RAM", a, g_aram[a ^ 3], ref[a]);
        }
    if (all)
        for (uint32_t a = 0; a < (2u << 20); a++)
            if (g_aram[a ^ 3] != ref[a] && g_ram_bad++ < 4)
                fail_at("wave RAM byte", a, g_aram[a ^ 3], ref[a]);
}

// Writes for the sample about to run: register traffic like a game's sound driver
static void scenario_tick(uint32_t t, uint32_t total)
{
    const bool pause = !strcmp(g_scn, "pause") && t >= total / 2;
    if (t == 0)
    {
        if (g_ref_cur || getenv("MVOL"))   // the older scenarios leave MVOL at 0: their frames are silent
            wr(0x2800, 0x000F | (g_mono ? 0x8000 : 0));
        if (g_dsp) program_dsp();
        for (int ch = 0; ch < g_nv; ch++) { program_voice(ch); key(ch, true); }
        kyonex();
        return;
    }
    if (pause)
    {
        if (t == total / 2)   // fast release: the slots stop soon and leave their filter tails
        {
            for (int ch = 0; ch < g_nv; ch++)
            {
                wr(0x80 * ch + 0x14, (base_read_(0x80 * ch + 0x14) & ~0x1Fu) | 31);
                key(ch, false);
            }
            kyonex();
        }
        return;
    }
    if (rnd(10000) < 1100)   // ~4900 writes/s: pitch, volume, pan, filter
    {
        const int ch = (int)rnd((uint32_t)g_nv);
        const uint32_t b = 0x80 * ch;
        switch (rnd(4))
        {
        case 0: wr(b + 0x18, (base_read_(b + 0x18) & 0x7800) | rnd(0x400)); break;
        case 1: wr(b + 0x28, rnd(64) << 8 | (base_read_(b + 0x28) & 0xFF)); break;
        case 2: wr(b + 0x24, (base_read_(b + 0x24) & 0x0F00) | rnd(32)); break;
        default: wr(b + 0x30 + 4 * rnd(3), 0x400 + rnd(0x1C00)); break;
        }
    }
    if (g_dsp && rnd(10000) < 20)   // ~880 COEF writes/s
        wr(0x3000 + 4 * rnd(128), rnd(0x10000) & 0xFFF8);
    if (rnd(10000) < 8)   // ~35 note changes/s
    {
        const int ch = (int)rnd((uint32_t)g_nv);
        if (g_v[ch].on) key(ch, false);
        else { program_voice(ch); key(ch, true); }
        kyonex();
    }
    if (!g_rscn)
        return;
    if (rnd(10000) < 40)   // ~176 guest writes/s into the ring
        guest_ring_write();
    if (rnd(10000) < 30)   // ~132 DSP register reads/s
        dsp_read_check();
    if (rnd(100000) < 60)   // ~26/s: a MADRS tap moves
        wr(0x3200 + 4 * rnd(64), rnd(0x10000));
    if (rnd(1000000) < 60)   // ~2.6/s: the program goes (the ring is nobody's), then a new one, maybe on another ring
    {
        for (int s = 0; s < 128; s++)
            for (int k = 0; k < 4; k++) wr(0x3400 + 16 * s + 4 * k, 0);
        g_prog_at = t + 1 + rnd(200);
    }
    if (t == g_prog_at)
        program_dsp();
}

// ---- the SPU, and the audio feeder that checks its frames ----
static SpuSim* g_sim;
static RefSnd g_ref;
static uint64_t g_busy, g_busy_late;
static uint32_t g_total, g_rd, g_frames_ok, g_frames_bad, g_frames_nz, g_first_bad = ~0u;

// Publishes the log, lets the SPU work through it, then compares and consumes every frame it wrote
static void pump()
{
    put32(g_bell, g_head);
    spusim_llr_lost(g_sim);
    const uint64_t c0 = g_sim->cycle;
    const int rc = spusim_run(g_sim, 400000000);
    if (rc != SPU_RUN_BLOCKED) { fprintf(stderr, "SPU stopped at sample %u: rc %d: %s\n", g_now, rc, g_sim->msg); exit(1); }
    g_busy += g_sim->cycle - c0;
    if (g_now > g_total * 7 / 10) g_busy_late += g_sim->cycle - c0;
    if (ECHO(tail) != g_head) { fprintf(stderr, "SPU tail %u != head %u\n", ECHO(tail), g_head); exit(1); }
    const uint32_t w = get32(g_words + 0);
    for (; g_rd != w; g_rd++)
    {
        const uint8_t* f = g_ring + (g_rd & (RING_FRAMES - 1)) * 8;
        for (int k = 0; k < 2; k++)
        {
            const uint32_t bits = get32(f + 4 * k);
            float got;
            memcpy(&got, &bits, 4);
            const size_t idx = (size_t)g_rd * 2 + k;
            if (bits & 0x7FFFFFFF) g_frames_nz++;
            if (idx < g_ref.out.size() && memcmp(&got, &g_ref.out[idx], 4) == 0) g_frames_ok++;
            else { if (g_first_bad == ~0u) g_first_bad = g_rd; g_frames_bad++; }
        }
    }
    put32(g_words + 128, g_rd);
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "usage: run_a2 elf [mix|adpcm|stream|pcm16|pcm8|pause|dsp|dsprand|mono|ring|ringrand|ring64|ringwrap]"
                        " [seconds] [voices] [seed]\n");
        return 2;
    }
    g_scn = argc > 2 ? argv[2] : "mix";
    const double secs = argc > 3 ? atof(argv[3]) : 2.0;
    g_nv = argc > 4 ? atoi(argv[4]) : 28;
    g_rng.seed(argc > 5 ? (uint32_t)atoi(argv[5]) : 1);
    g_rscn = !strncmp(g_scn, "ring", 4);
    g_mono = !strcmp(g_scn, "mono");
    g_dsp = !strncmp(g_scn, "dsp", 3) || g_rscn;
    const bool mono_fix = g_mono && (!getenv("MONO") || atoi(getenv("MONO")) != 0);
    if (g_rscn)   // P20 G2 alone in "ring"; G3 too where the program writes tables or the ring has 64K words
        g_setup_flags = AICA_SETUP_RING | (strcmp(g_scn, "ring") ? AICA_SETUP_RING64 : 0);
    if (getenv("ROWS"))   // ROWS=0: the rows off in a ring scenario, for the SPU cost alone (the run then fails its checks)
        g_setup_flags = (uint32_t)atoi(getenv("ROWS"));
    if (mono_fix)
        g_setup_flags |= AICA_SETUP_MONO;
    if (getenv("STREAM") && atoi(getenv("STREAM")))   // plan 6.9 B ADPCM Stream FIXED: the SPU, the reference, the twin
        g_setup_flags |= AICA_SETUP_STREAM;
    g_gain = getenv("GAIN") ? (uint32_t)atoi(getenv("GAIN")) : 0;
    const uint32_t gain2 = getenv("GAIN2") ? (uint32_t)atoi(getenv("GAIN2")) : g_gain;
    g_adpcm = getenv("ADPCM") ? (uint32_t)atoi(getenv("ADPCM")) : 0;
    const uint32_t adpcm2 = getenv("ADPCM2") ? (uint32_t)atoi(getenv("ADPCM2")) : g_adpcm;
    g_ref_cur = g_rscn || g_mono || g_gain || getenv("ADPCM") || getenv("ADPCM2") || getenv("STREAM");
    const uint32_t dspjit = getenv("DSPJIT") ? (uint32_t)atoi(getenv("DSPJIT")) : 1;
    const bool snd = !getenv("SND") || atoi(getenv("SND")) != 0;   // SND=0: the A1 check alone, no frames
    const uint32_t total = g_total = (uint32_t)(secs * 44100) & ~63u;

    g_bell = (uint8_t*)calloc(1, 128);
    g_echo = (uint8_t*)calloc(1, sizeof(AicaEcho));
    g_shared = (uint8_t*)calloc(1, sizeof(AicaShared));
    g_aram = (uint8_t*)calloc(1, 2u << 20);
    g_ring = (uint8_t*)calloc(1, RING_FRAMES * 8);
    g_words = (uint8_t*)calloc(1, 256);
    g_reg[0] = {EA_BELL, 128, g_bell};
    g_reg[1] = {EA_ECHO, (uint32_t)sizeof(AicaEcho), g_echo};
    g_reg[2] = {EA_SHARED, (uint32_t)sizeof(AicaShared), g_shared};
    g_reg[3] = {EA_ARAM, 2u << 20, g_aram};
    g_reg[4] = {EA_RING, RING_FRAMES * 8, g_ring};
    g_reg[5] = {EA_WORDS, 256, g_words};

    g_base = g_ref_cur ? cur_create(g_dsp ? 0 : 1) : base_create(g_dsp ? 0 : 1);
    if (g_mono)
        cur_set_mono(g_base, mono_fix);
    if (g_gain)
    {
        cur_set_gain(g_base, g_gain);
        put32(g_bell + offsetof(AicaBell, gain), g_gain);
    }
    if (g_adpcm)
    {
        cur_set_adpcm(g_base, 1);
        put32(g_bell + offsetof(AicaBell, adpcm_loop), 1);
    }
    g_cur = cur_create(0);
    if (g_setup_flags & AICA_SETUP_STREAM)
    {
        cur_set_stream(g_base, 1);
        if (!getenv("TWIN_OLD")) cur_set_stream(g_cur, 1);   // TWIN_OLD=1: the twin keeps the old rule, the checks must fail
    }
    // wave RAM: smooth PCM16, PCM8, random ADPCM nibbles
    g_dc.resize(2u << 20);
    for (uint32_t a = 0; a < 0x100000; a += 2)
    {
        const int16_t v = (int16_t)(12000 * sin(a * 0.0021) + 6000 * sin(a * 0.017) + (int)rnd(2000) - 1000);
        g_dc[a] = (uint8_t)v;
        g_dc[a + 1] = (uint8_t)(v >> 8);
    }
    for (uint32_t a = 0x100000; a < 0x180000; a++) g_dc[a] = (uint8_t)rnd(256);
    for (uint32_t a = 0x180000; a < 0x200000; a++) g_dc[a] = (uint8_t)(int8_t)(90 * sin(a * 0.013) + (int)rnd(20) - 10);
    if (g_rscn)   // the ring holds data from the start: the SPU must load it
        for (uint32_t a = 0x1E0000; a < 0x200000; a++) g_dc[a] = (uint8_t)(a * 0x9E3779B1u >> 24);
    else if (g_dsp)   // the ring starts at 0 like the SPU's LS copy; a table above it for TABLE reads
        for (uint32_t a = 0x1E0000; a < 0x200000; a++) g_dc[a] = a < 0x1F0000 ? 0 : (uint8_t)(a * 0x9E3779B1u >> 24);
    memcpy(base_ram_(), g_dc.data(), g_dc.size());
    for (uint32_t a = 0; a < g_dc.size(); a++) g_aram[a ^ 3] = g_dc[a];

    uint8_t* su = g_shared + offsetof(AicaShared, setup);
    put32(su + offsetof(AicaSetup, aram_ea), EA_ARAM);
    put32(su + offsetof(AicaSetup, dsp_mode), g_dsp || !snd ? dspjit : 0);
    put32(su + offsetof(AicaSetup, snd_dsp), g_dsp ? 1 : 0);
    put32(su + offsetof(AicaSetup, snd), snd ? 1 : 0);
    put32(su + offsetof(AicaSetup, ring_ea), EA_RING);
    put32(su + offsetof(AicaSetup, ring_frames), RING_FRAMES);
    put32(su + offsetof(AicaSetup, wr_ea), EA_WORDS);
    put32(su + offsetof(AicaSetup, rd_ea), EA_WORDS + 128);
    put32(su + offsetof(AicaSetup, w0), 0);
    put32(su + offsetof(AicaSetup, flags), g_setup_flags);

    SpuSim* sim = g_sim = (SpuSim*)calloc(1, sizeof(SpuSim));
    spusim_init(sim, ea_fn, 0);
    const uint32_t entry = spusim_load_elf(sim, argv[1]);
    if (!entry) { fprintf(stderr, "cannot load %s\n", argv[1]); return 2; }
    sim->pc = entry;
    sim->r[3].w[1] = EA_BELL;
    sim->r[4].w[1] = EA_ECHO;
    sim->r[5].w[1] = EA_SHARED;
    sim->prof = (uint64_t*)calloc(0x10000, sizeof(uint64_t));
    sim->brmiss = (uint32_t*)calloc(0x10000, sizeof(uint32_t));
    int rc = spusim_run(sim, 4000000000ull);   // the DSP recompiler's boot self-test runs first
    if (rc != SPU_RUN_BLOCKED || ECHO(state) != AICA_STATE_RUNNING)
    {
        fprintf(stderr, "SPU boot: rc %d state %u: %s\n", rc, ECHO(state), sim->msg);
        return 1;
    }
    for (uint32_t a = 0; a < g_dc.size(); a++)   // P20 G3 starts after the self-test: its random programs wrote nothing
        if (g_aram[a ^ 3] != g_dc[a]) { fprintf(stderr, "wave RAM changed during the SPU boot at %06X\n", a); return 1; }
    memset(sim->prof, 0, 0x10000 * sizeof(uint64_t));
    memset(sim->brmiss, 0, 0x10000 * sizeof(uint32_t));
    const uint64_t c_boot = sim->cycle;
    uint32_t late_n = 0;

    std::vector<int16_t> cdda(AICA_CDDA_WORDS);
    uint32_t cdda_i = AICA_CDDA_WORDS;
    bool cdda_on = false;
    double live_sum = 0;
    uint32_t live_n = 0;
    for (g_now = 0; g_now < total;)
    {
        scenario_tick(g_now, total);
        if (g_now % 588 == 0)   // a CD-DA sector, as AICA_Sample sends it: data half the time
        {
            cdda_on = rnd(2) == 0;
            uint32_t val = AICA_CDDA_SILENT;
            if (cdda_on)
            {
                const uint32_t k = g_cdda_n++ & (AICA_CDDA_SLOTS - 1);
                uint8_t* d = g_shared + offsetof(AicaShared, cdda) + k * sizeof(AicaCddaSlot);
                for (uint32_t i = 0; i < AICA_CDDA_WORDS; i++)
                {
                    cdda[i] = (int16_t)(8000 * sin((g_now + i / 2) * 0.05 + (i & 1)));
                    d[2 * i] = (uint8_t)(cdda[i] >> 8);
                    d[2 * i + 1] = (uint8_t)cdda[i];
                }
                val = k;
            }
            cdda_i = cdda_on ? 0 : AICA_CDDA_WORDS;
            push(AICA_OP_CDDA, 0, val, 0);
        }
        int32_t e0 = 0, e1 = 0;
        if (cdda_i < AICA_CDDA_WORDS) { e0 = cdda[cdda_i]; e1 = cdda[cdda_i + 1]; cdda_i += 2; }
        int16_t o[2];
        if (g_ref_cur) cur_step(g_base, e0, e1, o);
        else base_step(g_base, e0, e1, o);
        ref_sample(g_ref, o[0], o[1]);
        g_now++;
        if (g_now % AICA_BLOCK)
            continue;
        cur_catch_up(g_cur, g_now);
        push(AICA_OP_CHECK, 0, g_checks++, cur_snap_hash(g_cur));
        if ((g_checks & 15) == 0)
        {
            uint32_t l[4];
            cur_live(g_cur, l);
            live_sum += l[0];
            live_n++;
        }
        pump();   // publish and let the SPU work through the block
        if (gain2 != g_gain && g_now == ((total / 2) & ~63u))   // the row is live: the SPU reads the bell at its next wake
        {
            cur_set_gain(g_base, gain2);
            put32(g_bell + offsetof(AicaBell, gain), gain2);
            put32(g_bell + offsetof(AicaBell, peak_seq), 1);
        }
        if (adpcm2 != g_adpcm && g_now == ((total / 2) & ~63u))   // live too
        {
            cur_set_adpcm(g_base, adpcm2 != 0);
            put32(g_bell + offsetof(AicaBell, adpcm_loop), adpcm2 != 0);
        }
        if (g_now > total * 7 / 10) late_n += AICA_BLOCK;
        if (g_rscn)
            ram_compare((g_checks & 15) == 0 || g_now == total);
        // the sim's clock runs on as wall time would: one block of real time between bells
        const uint64_t wall = sim->cycle - c_boot, want = (uint64_t)g_now * 3200000000ull / 44100;
        if (want > wall) sim->cycle = c_boot + want;
    }
    const uint64_t busy = g_busy, busy_late = g_busy_late;
    const uint32_t checks = ECHO(checks), bad = ECHO(bad), samples = ECHO(samples), drops = ECHO(snd_drops);
    uint32_t prof[6];
    for (int i = 0; i < 6; i++) prof[i] = get32(g_echo + offsetof(AicaEcho, prof) + 4 * i);
    const double rendered = samples ? (double)prof[5] / samples : 0;
    printf("scenario %s voices %d seed: samples %u (spu %u) | checks %u bad %u | frames ok %u bad %u (first at frame %d) drops %u\n",
           g_scn, g_nv, g_now, samples, checks, bad, g_frames_ok, g_frames_bad, g_first_bad == ~0u ? -1 : (int)g_first_bad, drops);
    printf("SPU: %.0f cycles/sample = %.1f%% of real time | live %.1f, rendered %.1f per sample -> %.0f cycles per rendered slot\n",
           (double)busy / g_now, busy * 100.0 / ((double)g_now * 3200000000.0 / 44100), live_n ? live_sum / live_n : 0, rendered,
           rendered > 0 ? (double)prof[0] * 40 / ((double)prof[5]) : 0.0);
    printf("SPU %% of busy (decrementer sections): slots %.1f env %.1f preview+bus %.1f dsp %.1f mix %.1f | ipc %.2f dual %.1f%% br_miss %llu dma_wait %.1f%%\n",
           prof[0] * 40 * 100.0 / busy, prof[1] * 40 * 100.0 / busy, prof[2] * 40 * 100.0 / busy, prof[3] * 40 * 100.0 / busy,
           prof[4] * 40 * 100.0 / busy, (double)sim->insns / (double)busy, sim->dual * 100.0 / (double)sim->insns,
           (unsigned long long)sim->br_miss, sim->dma_wait * 100.0 / busy);
    if (g_dsp)
        printf("DSP %s: self-test %s (%u samples), compiles %u fails %u odd %u | program %u words, %u live steps, %u slow | checks %u bad %u | dsp %.1f%% of real time\n",
               (ECHO(dsp_flags) & 3) == 2 ? "CHECK" : (ECHO(dsp_flags) & 3) == 1 ? "recompiler" : "interpreter",
               (ECHO(dsp_flags) & 0x600) ? "FAIL" : (ECHO(dsp_flags) & 0x100) ? "pass" : "not run", ECHO(dsp_flags) >> 16,
               ECHO(dsp_compiles), ECHO(dsp_fails), ECHO(dsp_odd), ECHO(dsp_words), ECHO(dsp_live), ECHO(dsp_slow),
               ECHO(dsp_checks), ECHO(dsp_bad), prof[3] * 40 * 100.0 / ((double)g_now * 3200000000.0 / 44100));
    printf("bundle 5: flags %u | frames not silent %u | guest ring writes %u syncs %u | DSP reads ok %u bad %u | ring bytes"
           " bad %u, wave RAM bytes bad %u | SPU ring lines in %u out %u, cache lines out %u | clip %u/%u | gain %u -> %u"
           " of 256, peak %u | adpcm loop %u -> %u\n",
           g_setup_flags, g_frames_nz, g_guest_wr, ECHO(sync), g_rd_ok, g_rd_bad, g_ring_bad, g_ram_bad,
           ECHO(ring_in), ECHO(ring_out), ECHO(line_out),
           get32(g_echo + offsetof(AicaEcho, clip)), get32(g_echo + offsetof(AicaEcho, clip) + 4), g_gain, gain2, ECHO(peak),
           g_adpcm, adpcm2);
    if (late_n) printf("SPU after 70%% of the run: %.0f cycles/sample = %.1f%% of real time\n", (double)busy_late / late_n,
                       busy_late * 100.0 / ((double)late_n * 3200000000.0 / 44100));
    printf("SPU LS: image end %05X, lowest stack store %05X: %u bytes of stack used, %u spare\n", sim->stack_floor,
           sim->stack_low, 0x40000 - sim->stack_low, sim->stack_low - sim->stack_floor);
    spusim_print_profile(sim, busy, 25);
    if (getenv("HOT")) spusim_print_hot(sim, getenv("HOT"), getenv("HOTN") ? atoi(getenv("HOTN")) : 60);
    return bad || g_frames_bad || (g_dsp && ECHO(dsp_bad)) || g_rd_bad || g_ring_bad || g_ram_bad ? 1 : 0;
}
