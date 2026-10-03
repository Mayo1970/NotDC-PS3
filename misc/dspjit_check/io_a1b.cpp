/* OPTIMIZATION_PLAN 5.13 A1b PC test: io_a1.cpp of the A1 harness (caique io_model.cpp + control twin + SPU stand-in),
 * plus the DSP recompiler summary of the stand-in. Plain model M drives the trace, as in caique. */
#include "../cases/aica_io.h"
#include "aica_model.h"
#include <stdlib.h>

static caique::AicaModel *M;
static caique::AicaModel *PP;   // 5.13 A1: control-only twin (the PPU)
extern "C" { void sim_init(); void sim_set(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t); void sim_ram_w32(uint32_t, uint32_t);
             void sim_write(uint32_t, uint32_t, uint32_t); uint32_t sim_check(uint32_t, void*); uint32_t sim_misses();
             void sim_dsp_summary(FILE*); void sim_dsp_replog(FILE*); }
static FILE *g_a1; static uint32_t g_checks, g_bad;
static void a1_check() {
    static caique::CtlSnap ps, ss;
    PP->snap(ps);
    const uint32_t hp = caique::AicaModel::snap_hash(ps), hs = sim_check((uint32_t)PP->samples, &ss);
    g_checks++;
    if (hp == hs) return;
    if (g_bad++ < 5) {
        fprintf(g_a1, "MISMATCH sample %u ppu %08x spu %08x:", ps.sample, hp, hs);
        if (ps.lfsr != ss.lfsr) fprintf(g_a1, " lfsr %x/%x", ps.lfsr, ss.lfsr);
        if (ps.mdec != ss.mdec) fprintf(g_a1, " mdec %x/%x", ps.mdec, ss.mdec);
        for (int c = 0; c < 64; c++) if (memcmp(&ps.s[c], &ss.s[c], sizeof ps.s[c])) fprintf(g_a1, " s%d(CA %x/%x a %x/%x fv %x/%x fl %x/%x)", c, ps.s[c].CA, ss.s[c].CA, ps.s[c].a, ss.s[c].a, ps.s[c].fv, ss.s[c].fv, ps.s[c].flags, ss.s[c].flags);
        fputc('\n', g_a1);
    }
}
/* CAIQUE_TRACE=<file>: every access {u32 sample gap, u8 kind W/R/w/r/O/K, 3 pad, u32 offset, u32 value}, reads with
 * the model's value; CAIQUE_TRACE_OUT=1 adds the DAC output of every step (caique io_model.cpp). */
static FILE *g_trace;
static bool g_trace_out;
static void trace(char kind, uint32_t off, uint32_t val) {
    if (!g_trace) return;
    uint32_t rec[4] = {(uint32_t)M->samples, (uint32_t)(uint8_t)kind, off, val};
    fwrite(rec, 4, 4, g_trace);
}
extern "C" {
const char *io_platform = "model";
static const char *g_outdir;

static uint64_t g_ns; /* model time */
static const uint64_t ACCESS_NS = 2400;
/* CAIQUE_REPLAY=<replay.txt>: at the preamble's sync point (model sample X_m) the next step takes the console's
 * MDEC_CT, LFSR and eg_K (as cycle-model/io_cycle.cpp). Not traced. */
static bool g_rp_have, g_rp_pending, g_rp_done;
static uint32_t g_rp_mdec, g_rp_lfsr, g_rp_K, g_rp_par, g_rp_xm;
static uint32_t lfsr_step(uint32_t l) { return (l >> 1) | ((((l >> 0) ^ (l >> 5)) & 1) << 16); }
static void replay_apply() {
    const uint32_t d = ((g_rp_xm - 1) - M->MDEC_CT) & 0xFFFF;
    uint32_t l = g_rp_lfsr;
    for (uint32_t i = 0; i < 64 * d; i++) l = lfsr_step(l);
    M->MDEC_CT = (g_rp_mdec - 1 - d) & 0xFFFF;
    M->lfsr = l;
    M->eg_K = g_rp_K;
    M->eg_par = g_rp_par;
    PP->MDEC_CT = M->MDEC_CT; PP->lfsr = l; PP->eg_K = g_rp_K; PP->eg_par = g_rp_par;
    sim_set((uint32_t)M->samples, M->MDEC_CT, l, g_rp_K, g_rp_par);
    g_rp_pending = false;
}
static void advance(uint64_t ns) {
    g_ns += ns;
    uint64_t target = (g_ns * 44100) / 1000000000ull;
    while (M->samples < target) {
        if (g_rp_pending) replay_apply();
        M->step();
        PP->step_control();
        if ((PP->samples & 63) == 0) a1_check();
        if (g_trace && g_trace_out) {
            uint32_t rec[4] = {(uint32_t)(M->samples - 1), (uint32_t)'O', (uint32_t)(uint16_t)M->outL | ((uint32_t)(uint16_t)M->outR << 16), 0};
            fwrite(rec, 4, 4, g_trace);
        }
    }
}
uint32_t io_r(uint32_t off) { advance(ACCESS_NS); uint32_t v = M->read(off); trace('R', off, v); return v; }
void io_w(uint32_t off, uint32_t v) { advance(ACCESS_NS); trace('W', off, v); M->write(off, v); PP->write(off, v); sim_write((uint32_t)PP->samples, off, v & 0xFFFF); }
void io_wn(int n, const uint32_t *off, const uint32_t *v) { for (int i = 0; i < n; i++) io_w(off[i], v[i]); }   /* no sub-sample timing here */
static void check_align(uint32_t off) {
    if (off & 3) { fprintf(stderr, "misaligned 32-bit wave RAM access at %08x (the console takes an address error)\n", off); exit(3); }
}
uint32_t io_ram_r32(uint32_t off) {
    check_align(off);
    advance(ACCESS_NS);
    uint32_t v = M->ram_read32(off);
    trace('r', off, v);
    return v;
}
void io_ram_w32(uint32_t off, uint32_t v) {
    check_align(off);
    advance(ACCESS_NS);
    off &= caique::AicaModel::RAM_SIZE - 4;
    trace('w', off, v);
    for (int i = 0; i < 4; i++) M->ram[off + i] = (uint8_t)(v >> (8 * i));
    sim_ram_w32(off, v);
}
uint64_t io_now_us(void) { advance(200); return g_ns / 1000; } /* a timer read costs time too */
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
    if (!g_rp_have || g_rp_done) return;   /* the preamble's point only */
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
            g_rp_par = q ? (uint32_t)atoi(q + 5) & 1 : 0;   /* the envelope clock's MDEC_CT parity (older files: 0) */
        }
    fclose(f);
    if (!ok) fprintf(stderr, "%s: no \"mdec X lfsr L K k\" line\n", path);
    return ok;
}

int main(int argc, char **argv) {
    g_outdir = argc > 1 ? argv[1] : ".";
    M = new caique::AicaModel();
    PP = new caique::AicaModel();
    sim_init();
    { char p[512]; snprintf(p, sizeof p, "%s/a1.txt", g_outdir); g_a1 = fopen(p, "w"); }
    { char p[512]; snprintf(p, sizeof p, "%s/a1b_mismatch.txt", g_outdir); sim_dsp_replog(fopen(p, "w")); }
    if (const char *t = getenv("CAIQUE_TRACE")) {
        g_trace = fopen(t, "wb");
        if (!g_trace) { fprintf(stderr, "cannot open %s\n", t); return 2; }
        g_trace_out = getenv("CAIQUE_TRACE_OUT") != nullptr;
        trace('K', 0, M->eg_K);
    }
    if (const char *rp = getenv("CAIQUE_REPLAY"); rp && *rp) { if (!replay_load(rp)) return 2; g_rp_have = true; }
    if (!getenv("CAIQUE_NO_PREAMBLE")) replay_preamble();
    int rc = test_main();
    if (g_trace) fclose(g_trace);
    a1_check();
    fprintf(g_a1, "A1 checks %u mismatches %u aram misses %u%c", g_checks, g_bad, sim_misses(), '\n');
    sim_dsp_summary(g_a1);
    fclose(g_a1);
    delete M;
    return rc;
}
