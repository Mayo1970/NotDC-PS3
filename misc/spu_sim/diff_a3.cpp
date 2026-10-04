// 5.13 A3: base run() with the DSP per sample vs advance() + dsp_sample() + final_mix() in random chunks; DAC per sample,
// control, DSP state and DSP RAM per chunk. Slots read below DSP_LO, the DSP above (as the SPU's LS ring). diff_a3 [episodes] [seed]
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <random>
#include <vector>
#include "dsp_asm.h"

extern "C" {
void* base_create(int); void base_write(void*, uint32_t, uint32_t); uint32_t base_read(void*, uint32_t);
void base_step(void*, int32_t, int32_t, int16_t*); uint32_t base_snap_hash(void*); uint32_t base_dsp_hash(void*);
uint8_t* base_ram(void*); uint32_t base_ram_read32(void*, uint32_t);
void* cur_create(int); void cur_write(void*, uint32_t, uint32_t); uint32_t cur_read(void*, uint32_t);
void cur_advance_dsp(void*, uint32_t, const int32_t*, int16_t*); uint32_t cur_snap_hash(void*); uint32_t cur_dsp_hash(void*);
uint8_t* cur_ram(void*); uint32_t cur_ram_read32(void*, uint32_t); uint64_t cur_quiet(void*);
}

static const uint32_t DSP_LO = 0x1C0000;   // ring and TABLE range: RBP in [DSP_LO, 0x1E0000], 128 KB above it at most
static std::mt19937_64 rng;
static uint32_t rnd(uint32_t n) { return (uint32_t)(rng() % n); }
static bool pct(double p) { return (rng() % 1000000) < (uint64_t)(p * 10000); }
static void *B, *N;
static bool g_slow, g_real;
static uint32_t g_flag;   // a DSP program flag is set with probability 1 / g_flag
static uint64_t g_cmp, g_reads, g_out, g_ramw;

static void w(uint32_t off, uint32_t v) { base_write(B, off, v); cur_write(N, off, v); }
static uint32_t rate5() { if (g_slow && rnd(3)) return 1 + rnd(10); uint32_t r = rnd(10); return r < 3 ? 0 : r < 5 ? 0x1F : r < 6 ? 1 + rnd(4) : rnd(32); }
static uint32_t flv() { uint32_t r = rnd(12); return r < 2 ? 0 : r < 4 ? 0x1FF8 : r < 5 ? 0x1FFF : r < 7 ? 0x1000 : rnd(0x2000); }

static void slot_reg(int ch, uint32_t o) {
    const uint32_t b = 0x80 * ch;
    switch (o) {
    case 0x00: {   /* SA below 0x1A0000: with CA <= 0xFFFF every slot read stays below DSP_LO */
        const uint32_t fmt = rnd(4), sa_hi = rnd(0x1A);
        w(b, sa_hi | fmt << 7 | (rnd(3) ? (g_real ? 1u : 0u) : 1u) << 9 | (rnd(40) ? 0 : 1u) << 10 | (base_read(B, b) & 0x4000));
        break;
    }
    case 0x04: w(b + 4, rnd(0x10000)); break;
    case 0x08: w(b + 8, rnd(4) ? rnd(64) : rnd(0x10000)); break;
    case 0x0C: w(b + 0xC, g_slow ? 8 + rnd(64) : rnd(4) ? 16 + rnd(g_real ? 8000 : 400) : rnd(0x10000)); break;
    case 0x10: w(b + 0x10, rate5() | rate5() << 6 | rate5() << 11); break;
    case 0x14: w(b + 0x14, rate5() | (rnd(4) ? rnd(32) : 0u) << 5 | (rnd(3) ? 15u : rnd(16)) << 10 | (rnd(5) ? 0 : 1u) << 14); break;
    case 0x18: { const uint32_t oct = rnd(3) ? (rnd(5) - 2) & 0xF : rnd(16); w(b + 0x18, oct << 11 | rnd(0x400)); break; }
    case 0x1C: w(b + 0x1C, rnd(3) ? 0 : (uint32_t)rnd(0x10000)); break;
    case 0x20: w(b + 0x20, rnd(4) ? rnd(0x100) : rnd(16)); break;   /* IMXL, ISEL; some slots send nothing */
    case 0x24: w(b + 0x24, rnd(0x1000)); break;
    case 0x28: w(b + 0x28, rnd(0x20) | (rnd(4) ? 0 : 0x20u) | (rnd(30) ? 0 : 0x40u) | rnd(0x100) << 8); break;
    case 0x2C: case 0x30: case 0x34: case 0x38: case 0x3C: w(b + o, flv()); break;
    case 0x40: w(b + 0x40, rate5() | rate5() << 8); break;
    case 0x44: w(b + 0x44, rate5() | rate5() << 8); break;
    }
}
static void key(int ch, bool on) {
    const uint32_t b = 0x80 * ch, r0 = base_read(B, b) & 0x47FF;
    w(b, on ? (r0 | 0x4000) : (r0 & ~0x4000u));
}
static void kyonex() { const int ch = rnd(64); w(0x80 * ch, (base_read(B, 0x80 * ch) & 0x47FF) | 0x8000); }

static unsigned fl() { return rnd(g_flag) == 0; }
static void dsp_step_write(int s) {   /* one MPRO step, word by word as a sound driver loads it */
    dsp_inst_t i;
    i.TRA = rnd(0x80); i.TWT = fl(); i.TWA = rnd(0x80);
    i.XSEL = rnd(2); i.YSEL = rnd(4); i.IRA = rnd(4) ? 0x20 + rnd(18) : rnd(0x40); i.IWT = fl(); i.IWA = rnd(0x20);
    i.TABLE = rnd(4) == 0 && fl(); i.MWT = fl(); i.MRD = fl(); i.EWT = fl(); i.EWA = rnd(16);
    i.ADRL = fl(); i.FRCL = fl(); i.SHIFT = rnd(4); i.YRL = fl(); i.NEGB = fl(); i.ZERO = fl(); i.BSEL = fl();
    i.NOFL = fl(); i.MASA = rnd(0x40); i.ADREB = fl(); i.NXADR = fl();
    uint16_t wd[4];
    dsp_encode(&i, wd);
    for (int k = 0; k < 4; k++) w(0x3400 + 16 * s + 4 * k, wd[k]);
}
static void dsp_ring() { w(0x2804, (DSP_LO / 2048 + rnd(0x41)) | rnd(4) << 13); }   /* RBP 0x1C0000..0x1E0000, RBL any */
static void dsp_setup() {
    g_flag = 2 << rnd(4);
    for (int s = 0; s < 128; s++)
        if (rnd(g_flag == 2 ? 1 : 3)) dsp_step_write(s);
        else for (int k = 0; k < 4; k++) w(0x3400 + 16 * s + 4 * k, 0);
    for (int s = 0; s < 128; s++) w(0x3000 + 4 * s, rnd(0x10000) & 0xFFF8);
    for (int k = 0; k < 64; k++) w(0x3200 + 4 * k, rnd(0x10000));
    dsp_ring();
    for (int k = 0; k < 16; k++) w(0x2000 + 4 * k, rnd(3) ? 0 : rnd(0x10000));   /* EFSDL, EFPAN */
    w(0x2040, rnd(0x10000));
    w(0x2044, rnd(0x10000));
}
static void dsp_poke() {   /* CPU traffic to the DSP while it runs */
    switch (rnd(8)) {
    case 0: dsp_step_write(rnd(128)); break;
    case 1: w(0x3000 + 4 * rnd(128), rnd(0x10000) & 0xFFF8); break;
    case 2: w(0x3200 + 4 * rnd(64), rnd(0x10000)); break;
    case 3: dsp_ring(); break;
    case 4: w(0x4000 + 4 * rnd(256), rnd(0x10000)); break;   /* TEMP */
    case 5: w(0x4400 + 4 * rnd(64), rnd(0x10000)); break;    /* MEMS */
    case 6: w(0x4500 + 4 * rnd(32), rnd(0x10000)); break;    /* MIXS */
    default: w(0x4580 + 4 * rnd(16), rnd(0x10000)); break;   /* EFREG */
    }
}
static void ram_w32(uint32_t off, uint32_t v) {
    off &= (2u << 20) - 4;
    for (int i = 0; i < 4; i++) base_ram(B)[off + i] = cur_ram(N)[off + i] = (uint8_t)(v >> (8 * i));
    g_ramw++;
}

static void fail(int ep, uint64_t s, const char* what, uint32_t b, uint32_t n) {
    printf("MISMATCH episode %d sample %llu %s base %08X new %08X\n", ep, (unsigned long long)s, what, b, n);
    exit(1);
}

int main(int argc, char** argv) {
    const int episodes = argc > 1 ? atoi(argv[1]) : 200;
    const uint64_t seed0 = argc > 2 ? strtoull(argv[2], 0, 0) : 1;
    uint64_t total = 0;
    double quiet_sum = 0, quiet_n = 0;
    for (int ep = 0; ep < episodes; ep++) {
        rng.seed(seed0 * 1000003u + ep);
        B = base_create(0);
        N = cur_create(0);
        {
            uint8_t *rb = base_ram(B), *rn = cur_ram(N);
            for (uint32_t i = 0; i < (2u << 20); i += 8) { const uint64_t v = rng(); memcpy(rb + i, &v, 8); memcpy(rn + i, &v, 8); }
        }
        g_slow = rnd(3) == 0;
        g_real = rnd(2) == 0;
        const int nact = 1 + rnd(rnd(2) ? 64 : 16);
        int act[64];
        for (int i = 0; i < 64; i++) act[i] = i;
        for (int i = 0; i < 64; i++) { const int j = rnd(64); const int t = act[i]; act[i] = act[j]; act[j] = t; }
        for (int i = 0; i < nact; i++)
            for (uint32_t o = 0; o < 0x48; o += 4) slot_reg(act[i], o);
        dsp_setup();
        for (int i = 0; i < nact; i++)
            if (rnd(4)) key(act[i], true);
        kyonex();
        const double p_key = rnd(2) ? 1.0 : 8.0, p_reg = rnd(2) ? 0.5 : 4.0, p_dsp = rnd(2) ? 0.5 : 5.0;
        const uint64_t len = 10000 + rnd(30000);
        std::vector<int32_t> exts;
        std::vector<int16_t> ob, on;
        for (uint64_t s = 0; s < len;) {
            if (pct(p_key * 4)) {
                const int n = 1 + rnd(4);
                for (int i = 0; i < n; i++) key(act[rnd(nact)], rnd(2));
                if (rnd(2)) kyonex();
            }
            if (pct(p_key * 2)) kyonex();
            for (int k = rnd(3); k > 0; k--)
                if (pct(p_reg * 10)) slot_reg(act[rnd(nact)], 4 * rnd(18));
            for (int k = rnd(3); k > 0; k--)
                if (pct(p_dsp * 10)) dsp_poke();
            if (pct(20)) {
                const uint32_t ch = act[rnd(nact)], sel = rnd(2) << 14;
                w(0x280C, ch << 8 | sel);
                const uint32_t off = rnd(2) ? 0x2810 : 0x2814;
                const uint32_t rb = base_read(B, off), rn = cur_read(N, off);
                g_reads++;
                if (rb != rn) fail(ep, s, "read", rb, rn);
            }
            if (pct(5)) {   /* DSP registers the CPU reads back: TEMP, MEMS, MIXS, EFREG */
                static const uint32_t base_off[4] = {0x4000, 0x4400, 0x4500, 0x4580}, span[4] = {256, 64, 32, 16};
                const int k = rnd(4);
                const uint32_t off = base_off[k] + 4 * rnd(span[k]);
                const uint32_t rb = base_read(B, off), rn = cur_read(N, off);
                if (rb != rn) fail(ep, s, "dsp read", rb, rn);
            }
            if (pct(10)) {   /* wave RAM: the ring, a table, sample data; a read moves the DSP's read latch */
                const uint32_t lo = rnd(2) ? DSP_LO : 0, span = rnd(2) ? 0x40000 : 0x1C0000;
                const uint32_t off = (lo + rnd(lo ? 0x40000 : span)) & ~3u;
                if (rnd(3)) ram_w32(off, (uint32_t)rng());
                else {
                    const uint32_t rb = base_ram_read32(B, off), rn = cur_ram_read32(N, off);
                    if (rb != rn) fail(ep, s, "ram read", rb, rn);
                }
            }
            if (pct(2)) w(0x2800, rnd(0x10000));
            if (pct(2)) w(0x2040 + 4 * rnd(2), rnd(0x10000));
            if (pct(2)) w(0x2000 + 4 * rnd(16), rnd(0x10000));
            if (pct(3)) w(0x2890 + 4 * rnd(3), rnd(0x800));
            if (pct(5)) w(rnd(2) ? 0x28A4 : 0x28BC, rnd(0x800));
            for (uint32_t off : {0x28A0u, 0x28B8u}) {
                const uint32_t rb = base_read(B, off), rn = cur_read(N, off);
                if (rb != rn) fail(ep, s, off == 0x28A0 ? "scipd" : "mcipd", rb, rn);
            }
            const uint32_t n = rnd(4) ? 1 + rnd(20) : 1 + rnd(150);
            exts.resize(2 * n);
            ob.resize(2 * n);
            on.resize(2 * n);
            for (uint32_t i = 0; i < 2 * n; i++) exts[i] = rnd(3) ? 0 : (int32_t)rnd(65536) - 32768;
            for (uint32_t i = 0; i < n; i++) base_step(B, exts[2 * i], exts[2 * i + 1], &ob[2 * i]);
            cur_advance_dsp(N, n, exts.data(), on.data());
            for (uint32_t i = 0; i < n; i++, g_out++)
                if (ob[2 * i] != on[2 * i] || ob[2 * i + 1] != on[2 * i + 1])
                    fail(ep, s + i, "out", (uint16_t)ob[2 * i] | (uint32_t)(uint16_t)ob[2 * i + 1] << 16,
                         (uint16_t)on[2 * i] | (uint32_t)(uint16_t)on[2 * i + 1] << 16);
            s += n;
            g_cmp++;
            uint32_t hb = base_snap_hash(B), hn = cur_snap_hash(N);
            if (hb != hn) fail(ep, s, "snap", hb, hn);
            hb = base_dsp_hash(B);
            hn = cur_dsp_hash(N);
            if (hb != hn) fail(ep, s, "dsp state", hb, hn);
            if (memcmp(base_ram(B) + DSP_LO, cur_ram(N) + DSP_LO, (2u << 20) - DSP_LO))
                fail(ep, s, "dsp ram", 0, 0);
            quiet_sum += __builtin_popcountll(cur_quiet(N));
            quiet_n++;
        }
        total += len;
    }
    printf("OK: %d episodes, %llu samples, %llu chunk compares, %llu DAC samples, %llu reads, %llu RAM writes; avg quiet %.2f\n",
           episodes, (unsigned long long)total, (unsigned long long)g_cmp, (unsigned long long)g_out,
           (unsigned long long)g_reads, (unsigned long long)g_ramw, quiet_sum / quiet_n);
    return 0;
}
