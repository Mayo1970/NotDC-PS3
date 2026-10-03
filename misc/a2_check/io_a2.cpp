/* OPTIMIZATION_PLAN 5.13 A2 PC test: every caique case on three plain models. N has no_dsp; R runs the DSP with every
 * EFSDL write forced to 0. Their DAC output must match every sample, their control state every 64. M drives the case. */
#include "../cases/aica_io.h"
#include "aica_model.h"
#include <stdlib.h>
#include <initializer_list>

static caique::AicaModel *M, *R, *N;
static FILE *g_a2;
static uint64_t g_samples, g_out_bad;
static uint32_t g_checks, g_ctl_bad;

static void compare() {
    if (R->outL != N->outL || R->outR != N->outR) {
        if (g_out_bad++ < 5)
            fprintf(g_a2, "OUT MISMATCH sample %llu R %d/%d N %d/%d, wave RAM %s\n", (unsigned long long)N->samples - 1,
                    R->outL, R->outR, N->outL, N->outR,
                    memcmp(R->ram, N->ram, caique::AicaModel::RAM_SIZE) ? "differs (DSP writes)" : "same");
    }
    if ((N->samples & 63) == 0) {
        static caique::CtlSnap a, b;
        R->snap(a);
        N->snap(b);
        g_checks++;
        if (memcmp(&a, &b, sizeof a) && g_ctl_bad++ < 5) fprintf(g_a2, "CTL MISMATCH sample %u\n", a.sample);
    }
}

extern "C" {
const char *io_platform = "model";
static const char *g_outdir;

static uint64_t g_ns; /* model time */
static const uint64_t ACCESS_NS = 2400;
/* CAIQUE_REPLAY=<replay.txt>: at the preamble's sync point the next step takes the console's MDEC_CT, LFSR and eg_K */
static bool g_rp_have, g_rp_pending, g_rp_done;
static uint32_t g_rp_mdec, g_rp_lfsr, g_rp_K, g_rp_par, g_rp_xm;
static uint32_t lfsr_step(uint32_t l) { return (l >> 1) | ((((l >> 0) ^ (l >> 5)) & 1) << 16); }
static void replay_apply() {
    const uint32_t d = ((g_rp_xm - 1) - M->MDEC_CT) & 0xFFFF;
    uint32_t l = g_rp_lfsr;
    for (uint32_t i = 0; i < 64 * d; i++) l = lfsr_step(l);
    for (caique::AicaModel *m : {M, R, N}) {
        m->MDEC_CT = (g_rp_mdec - 1 - d) & 0xFFFF;
        m->lfsr = l;
        m->eg_K = g_rp_K;
        m->eg_par = g_rp_par;
    }
    g_rp_pending = false;
}
static void advance(uint64_t ns) {
    g_ns += ns;
    uint64_t target = (g_ns * 44100) / 1000000000ull;
    while (M->samples < target) {
        if (g_rp_pending) replay_apply();
        M->step();
        R->step();
        N->step();
        g_samples++;
        compare();
    }
}
uint32_t io_r(uint32_t off) { advance(ACCESS_NS); return M->read(off); }
void io_w(uint32_t off, uint32_t v) {
    advance(ACCESS_NS);
    M->write(off, v);
    N->write(off, v);
    const uint32_t o = off & 0x7FFC;
    R->write(off, o >= 0x2000 && o < 0x2040 ? v & ~0x0F00u : v);   /* EFSDL 0: no EFREG in R's mix */
}
void io_wn(int n, const uint32_t *off, const uint32_t *v) { for (int i = 0; i < n; i++) io_w(off[i], v[i]); }
static void check_align(uint32_t off) {
    if (off & 3) { fprintf(stderr, "misaligned 32-bit wave RAM access at %08x (the console takes an address error)\n", off); exit(3); }
}
uint32_t io_ram_r32(uint32_t off) {
    check_align(off);
    advance(ACCESS_NS);
    R->ram_read32(off);   /* the read latch is DSP state: keep R's in step with M's */
    N->ram_read32(off);
    return M->ram_read32(off);
}
void io_ram_w32(uint32_t off, uint32_t v) {
    check_align(off);
    advance(ACCESS_NS);
    off &= caique::AicaModel::RAM_SIZE - 4;
    for (caique::AicaModel *m : {M, R, N})
        for (int i = 0; i < 4; i++) m->ram[off + i] = (uint8_t)(v >> (8 * i));
}
uint64_t io_now_us(void) { advance(200); return g_ns / 1000; }
void io_wait_us(uint32_t us) { advance((uint64_t)us * 1000); }
int io_write_file(const char *name, const void *data, uint32_t bytes) {
    char p[512];
    snprintf(p, sizeof p, "%s/%s", g_outdir, name);
    FILE *f = fopen(p, "wb");
    if (!f) { fprintf(stderr, "cannot open %s\n", p); return -1; }
    fwrite(data, 1, bytes, f);
    fclose(f);
    return 0;
}
void io_print(const char *s) { fputs(s, stdout); }
uint32_t io_sh4_irq(void) { advance(ACCESS_NS); return M->sh4_irq() ? 1 : 0; }
void io_replay_point(uint32_t sync_mdec) {
    if (!g_rp_have || g_rp_done) return;
    g_rp_done = true;
    g_rp_xm = sync_mdec;
    g_rp_pending = true;
}
}
static bool replay_load(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return false; }
    char line[256];
    bool ok = false;
    while (fgets(line, sizeof line, f))
        if (sscanf(line, "mdec %x lfsr %x K %u", &g_rp_mdec, &g_rp_lfsr, &g_rp_K) == 3) {
            ok = true;
            const char *q = strstr(line, " par ");
            g_rp_par = q ? (uint32_t)atoi(q + 5) & 1 : 0;
        }
    fclose(f);
    if (!ok) fprintf(stderr, "%s: no \"mdec X lfsr L K k\" line\n", path);
    return ok;
}

int main(int argc, char **argv) {
    g_outdir = argc > 1 ? argv[1] : ".";
    M = new caique::AicaModel();
    R = new caique::AicaModel();
    N = new caique::AicaModel();
    N->no_dsp = true;
    { char p[512]; snprintf(p, sizeof p, "%s/a2.txt", g_outdir); g_a2 = fopen(p, "w"); }
    if (const char *rp = getenv("CAIQUE_REPLAY"); rp && *rp) { if (!replay_load(rp)) return 2; g_rp_have = true; }
    if (!getenv("CAIQUE_NO_PREAMBLE")) replay_preamble();
    int rc = test_main();
    fprintf(g_a2, "A2 samples %llu out-mismatches %llu | ctl checks %u mismatches %u\n", (unsigned long long)g_samples,
            (unsigned long long)g_out_bad, g_checks, g_ctl_bad);
    fclose(g_a2);
    return rc;
}
