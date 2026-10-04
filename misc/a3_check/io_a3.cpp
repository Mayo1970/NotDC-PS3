/* OPTIMIZATION_PLAN 5.13 A3 PC test: every caique case on a plain model M (run() with the DSP, drives the case) and on A,
 * which runs as the SPU does: advance() rounds, then dsp_sample() and final_mix() per sample. DAC every sample. */
#include "../cases/aica_io.h"
#include "aica_model.h"
#include <stdlib.h>
#include <vector>

static caique::AicaModel *M, *A;
static FILE *g_a3;
static std::vector<int16_t> g_out;   /* M's DAC pairs since A last caught up */
static uint64_t g_samples, g_out_bad, g_rounds;
static uint32_t g_dsp_checks, g_dsp_bad, g_ctl_bad, g_ram_bad;
static int32_t g_mix[4 * caique::AicaModel::DSP_ROUND] __attribute__((aligned(16)));

static uint32_t dsp_hash(const caique::AicaModel *m) {
    uint32_t x = 2166136261u;
    auto h = [&](uint32_t w) { x = (x ^ w) * 16777619u; };
    for (int i = 0; i < 128; i++) h((uint32_t)m->TEMP[i]);
    for (int i = 0; i < 32; i++) h((uint32_t)m->MEMS[i]);
    for (int i = 0; i < 16; i++) h((uint32_t)m->EFREG[i]);
    for (int b = 0; b < 2; b++)
        for (int i = 0; i < 16; i++) h((uint32_t)m->MIXS_bank[b][i]);
    h((uint32_t)m->ACC); h((uint32_t)m->FRC_REG); h((uint32_t)m->Y_REG); h(m->ADRS_REG); h(m->memval);
    h(m->nofl_pipe[0] | (uint32_t)m->nofl_pipe[1] << 8); h((uint32_t)m->mrd_even_masa); h((uint32_t)m->mem_qn);
    for (int q = 0; q < m->mem_qn; q++) { h((uint32_t)m->mem_q[q].land); h(m->mem_q[q].val); }
    return x;
}

/* A runs to M's sample in rounds; every DAC pair against M's, then the DSP and control state */
static void catch_up() {
    uint32_t j = 0;
    while (A->samples < M->samples) {
        const uint32_t n = (uint32_t)(M->samples - A->samples), k = n < caique::AicaModel::DSP_ROUND ? n : caique::AicaModel::DSP_ROUND;
        A->advance(k, true, g_mix);
        g_rounds++;
        for (uint32_t i = 0; i < k; i++, j++) {
            A->EXTS[0] = M->EXTS[0];
            A->EXTS[1] = M->EXTS[1];
            A->dsp_sample(i);
            A->final_mix(g_mix[4 * i], g_mix[4 * i + 1]);
            if ((A->outL != g_out[2 * j] || A->outR != g_out[2 * j + 1]) && g_out_bad++ < 5)
                fprintf(g_a3, "OUT MISMATCH sample %llu M %d/%d A %d/%d\n", (unsigned long long)A->samples - k + i, g_out[2 * j],
                        g_out[2 * j + 1], A->outL, A->outR);
        }
    }
    g_out.clear();
    g_dsp_checks++;
    if (dsp_hash(M) != dsp_hash(A) && g_dsp_bad++ < 5) fprintf(g_a3, "DSP STATE MISMATCH sample %llu\n", (unsigned long long)M->samples);
    static caique::CtlSnap a, b;
    M->snap(a);
    A->snap(b);
    if (memcmp(&a, &b, sizeof a) && g_ctl_bad++ < 5) fprintf(g_a3, "CTL MISMATCH sample %u\n", a.sample);
}

extern "C" {
const char *io_platform = "model";
static const char *g_outdir;

static uint64_t g_ns; /* model time */
static const uint64_t ACCESS_NS = 2400;
static bool g_rp_have, g_rp_pending, g_rp_done;
static uint32_t g_rp_mdec, g_rp_lfsr, g_rp_K, g_rp_par, g_rp_xm;
static uint32_t lfsr_step(uint32_t l) { return (l >> 1) | ((((l >> 0) ^ (l >> 5)) & 1) << 16); }
static void replay_apply() {
    catch_up();   /* A takes the console's counters at the same sample */
    const uint32_t d = ((g_rp_xm - 1) - M->MDEC_CT) & 0xFFFF;
    uint32_t l = g_rp_lfsr;
    for (uint32_t i = 0; i < 64 * d; i++) l = lfsr_step(l);
    for (caique::AicaModel *m : {M, A}) {
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
        g_out.push_back(M->outL);
        g_out.push_back(M->outR);
        g_samples++;
    }
}
uint32_t io_r(uint32_t off) {
    advance(ACCESS_NS);
    catch_up();
    const uint32_t v = M->read(off), va = A->read(off);   /* a read has side effects (0x2810 clears LP) */
    if (v != va && g_out_bad++ < 5) fprintf(g_a3, "READ MISMATCH %04X M %X A %X\n", off, v, va);
    return v;
}
void io_w(uint32_t off, uint32_t v) {
    advance(ACCESS_NS);
    catch_up();
    M->write(off, v);
    A->write(off, v);
}
void io_wn(int n, const uint32_t *off, const uint32_t *v) { for (int i = 0; i < n; i++) io_w(off[i], v[i]); }
static void check_align(uint32_t off) {
    if (off & 3) { fprintf(stderr, "misaligned 32-bit wave RAM access at %08x (the console takes an address error)\n", off); exit(3); }
}
uint32_t io_ram_r32(uint32_t off) {
    check_align(off);
    advance(ACCESS_NS);
    catch_up();
    A->ram_read32(off);   /* the read latch is DSP state */
    return M->ram_read32(off);
}
void io_ram_w32(uint32_t off, uint32_t v) {
    check_align(off);
    advance(ACCESS_NS);
    catch_up();
    off &= caique::AicaModel::RAM_SIZE - 4;
    for (caique::AicaModel *m : {M, A})
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
    A = new caique::AicaModel();
    { char p[512]; snprintf(p, sizeof p, "%s/a3.txt", g_outdir); g_a3 = fopen(p, "w"); }
    if (const char *rp = getenv("CAIQUE_REPLAY"); rp && *rp) { if (!replay_load(rp)) return 2; g_rp_have = true; }
    if (!getenv("CAIQUE_NO_PREAMBLE")) replay_preamble();
    int rc = test_main();
    catch_up();
    if (memcmp(M->ram, A->ram, caique::AicaModel::RAM_SIZE)) g_ram_bad = 1;
    fprintf(g_a3, "A3 samples %llu out-mismatches %llu | dsp checks %u mismatches %u | ctl mismatches %u | ram %s | rounds %llu\n",
            (unsigned long long)g_samples, (unsigned long long)g_out_bad, g_dsp_checks, g_dsp_bad, g_ctl_bad,
            g_ram_bad ? "DIFFERS" : "same", (unsigned long long)g_rounds);
    fclose(g_a3);
    return rc;
}
