// 5.13 A2 high voices: base run() per sample against advance() in random chunks, same writes/reads between chunks;
// control snapshot and SCIPD/MCIPD per chunk, DAC per sample (no DSP). Usage: diff_adv [episodes] [seed]
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <random>
#include <vector>

extern "C" {
void* base_create(int); void base_write(void*, uint32_t, uint32_t); uint32_t base_read(void*, uint32_t);
void base_step(void*, int32_t, int32_t, int16_t*); void base_stepc(void*); uint32_t base_snap_hash(void*);
uint8_t* base_ram(void*); uint64_t base_samples(void*);
void* cur_create(int); void cur_write(void*, uint32_t, uint32_t); uint32_t cur_read(void*, uint32_t);
void cur_advance(void*, uint32_t, int, const int32_t*, int16_t*); uint32_t cur_snap_hash(void*);
uint8_t* cur_ram(void*); uint64_t cur_samples(void*); uint64_t cur_quiet(void*);
void cur_step(void*, int32_t, int32_t, int16_t*); void cur_stepc(void*); void cur_set_adpcm(void*, int);
}
// The reference's entry points: the base model, or the tree's (env ADPCM)
static void* (*B_create)(int) = base_create;
static void (*B_write)(void*, uint32_t, uint32_t) = base_write;
static uint32_t (*B_read)(void*, uint32_t) = base_read;
static void (*B_step)(void*, int32_t, int32_t, int16_t*) = base_step;
static void (*B_stepc)(void*) = base_stepc;
static uint32_t (*B_snap_hash)(void*) = base_snap_hash;
static uint8_t* (*B_ram)(void*) = base_ram;
#define base_create B_create
#define base_write B_write
#define base_read B_read
#define base_step B_step
#define base_stepc B_stepc
#define base_snap_hash B_snap_hash
#define base_ram B_ram

static std::mt19937_64 rng;
static uint32_t rnd(uint32_t n) { return (uint32_t)(rng() % n); }
static bool pct(double p) { return (rng() % 1000000) < (uint64_t)(p * 10000); }
static void *B, *N;
static bool g_slow, g_real;
static uint64_t g_cmp, g_reads, g_out;

static void w(uint32_t off, uint32_t v) { base_write(B, off, v); cur_write(N, off, v); }
static uint32_t rate5() { if (g_slow && rnd(3)) return 1 + rnd(10); uint32_t r = rnd(10); return r < 3 ? 0 : r < 5 ? 0x1F : r < 6 ? 1 + rnd(4) : rnd(32); }
static uint32_t flv() { uint32_t r = rnd(12); return r < 2 ? 0 : r < 4 ? 0x1FF8 : r < 5 ? 0x1FFF : r < 7 ? 0x1000 : rnd(0x2000); }

static void slot_reg(int ch, uint32_t o) {
    const uint32_t b = 0x80 * ch;
    switch (o) {
    case 0x00: {
        const uint32_t fmt = rnd(4), sa_hi = rnd(0x20);
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
    case 0x20: w(b + 0x20, rnd(0x100)); break;
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

static void fail(int ep, uint64_t s, const char* what, uint32_t b, uint32_t n) {
    printf("MISMATCH episode %d sample %llu %s base %08X new %08X\n", ep, (unsigned long long)s, what, b, n);
    exit(1);
}

int main(int argc, char** argv) {
    const int episodes = argc > 1 ? atoi(argv[1]) : 200;
    const uint64_t seed0 = argc > 2 ? strtoull(argv[2], 0, 0) : 1;
    // Env ADPCM (plan 6.9 B): the reference is the tree's own run(); 1 = ADPCM Loop RESTORE, 2 = the row changes live
    const int adpcm = getenv("ADPCM") ? atoi(getenv("ADPCM")) : 0;
    if (adpcm) {
        B_create = cur_create; B_write = cur_write; B_read = cur_read; B_step = cur_step;
        B_stepc = cur_stepc; B_snap_hash = cur_snap_hash; B_ram = cur_ram;
    }
    uint64_t total = 0;
    double quiet_sum = 0, quiet_n = 0;
    for (int ep = 0; ep < episodes; ep++) {
        rng.seed(seed0 * 1000003u + ep);
        const bool render = ep & 1;
        B = base_create(render);
        N = cur_create(render);
        if (adpcm) { cur_set_adpcm(B, adpcm == 1); cur_set_adpcm(N, adpcm == 1); }
        if (render) {
            uint8_t *rb = base_ram(B), *rn = cur_ram(N);
            for (uint32_t i = 0; i < (2u << 20); i += 8) { const uint64_t v = rng(); memcpy(rb + i, &v, 8); memcpy(rn + i, &v, 8); }
        }
        g_slow = rnd(3) == 0;
        g_real = rnd(2) == 0;   // looping voices with long loops, like a game
        const int nact = 1 + rnd(rnd(2) ? 64 : 16);
        int act[64];
        for (int i = 0; i < 64; i++) act[i] = i;
        for (int i = 0; i < 64; i++) { const int j = rnd(64); const int t = act[i]; act[i] = act[j]; act[j] = t; }
        for (int i = 0; i < nact; i++)
            for (uint32_t o = 0; o < 0x48; o += 4) slot_reg(act[i], o);
        for (int i = 0; i < nact; i++)
            if (rnd(4)) key(act[i], true);
        kyonex();
        const double p_key = rnd(2) ? 1.0 : 8.0, p_reg = rnd(2) ? 0.5 : 4.0;   // percent per chunk event
        const uint64_t len = 20000 + rnd(60000);
        std::vector<int32_t> exts;
        std::vector<int16_t> ob, on;
        for (uint64_t s = 0; s < len;) {
            // events between chunks, as the PPU applies writes and reads between catch-ups
            if (adpcm == 2 && pct(3)) { const int v = (int)rnd(2); cur_set_adpcm(B, v); cur_set_adpcm(N, v); }
            if (pct(p_key * 4)) {
                const int n = 1 + rnd(4);
                for (int i = 0; i < n; i++) key(act[rnd(nact)], rnd(2));
                if (rnd(2)) kyonex();
            }
            if (pct(p_key * 2)) kyonex();
            for (int k = rnd(3); k > 0; k--)
                if (pct(p_reg * 10)) slot_reg(act[rnd(nact)], 4 * rnd(18));
            if (pct(20)) {
                const uint32_t ch = act[rnd(nact)], sel = rnd(2) << 14;
                w(0x280C, ch << 8 | sel);
                const uint32_t off = rnd(2) ? 0x2810 : 0x2814;
                const uint32_t rb = base_read(B, off), rn = cur_read(N, off);
                g_reads++;
                if (rb != rn) fail(ep, s, "read", rb, rn);
            }
            if (pct(2)) w(0x2800, rnd(0x10000));
            if (pct(2)) w(0x2040 + 4 * rnd(2), rnd(0x10000));
            if (pct(3)) w(0x2890 + 4 * rnd(3), rnd(0x800));   /* timer count and prescaler */
            if (pct(5)) w(rnd(2) ? 0x28A4 : 0x28BC, rnd(0x800));   /* clear pending bits */
            for (uint32_t off : {0x28A0u, 0x28B8u}) {   /* SCIPD, MCIPD: the timers run outside the snapshot */
                const uint32_t rb = base_read(B, off), rn = cur_read(N, off);
                if (rb != rn) fail(ep, s, off == 0x28A0 ? "scipd" : "mcipd", rb, rn);
            }
            const uint32_t n = rnd(4) ? 1 + rnd(20) : 1 + rnd(150);
            if (render) {
                exts.resize(2 * n);
                ob.resize(2 * n);
                on.resize(2 * n);
                for (uint32_t i = 0; i < 2 * n; i++) exts[i] = rnd(3) ? 0 : (int32_t)rnd(65536) - 32768;
                for (uint32_t i = 0; i < n; i++) base_step(B, exts[2 * i], exts[2 * i + 1], &ob[2 * i]);
                cur_advance(N, n, 1, exts.data(), on.data());
                for (uint32_t i = 0; i < n; i++, g_out++)
                    if (ob[2 * i] != on[2 * i] || ob[2 * i + 1] != on[2 * i + 1])
                        fail(ep, s + i, "out", (uint16_t)ob[2 * i] | (uint32_t)(uint16_t)ob[2 * i + 1] << 16,
                             (uint16_t)on[2 * i] | (uint32_t)(uint16_t)on[2 * i + 1] << 16);
            } else {
                for (uint32_t i = 0; i < n; i++) base_stepc(B);
                cur_advance(N, n, 0, 0, 0);
            }
            s += n;
            const uint32_t hb = base_snap_hash(B), hn = cur_snap_hash(N);
            g_cmp++;
            if (hb != hn) fail(ep, s, "snap", hb, hn);
            quiet_sum += __builtin_popcountll(cur_quiet(N));
            quiet_n++;
        }
        total += len;
    }
    printf("OK: %d episodes, %llu samples, %llu snapshot compares, %llu DAC samples, %llu reads; avg quiet slots %.2f\n",
           episodes, (unsigned long long)total, (unsigned long long)g_cmp, (unsigned long long)g_out,
           (unsigned long long)g_reads, quiet_sum / quiet_n);
    return 0;
}
