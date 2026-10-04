/* caique AICA sample model (caique-rtl 5aa2376 sample-model, MIT, see LICENSE): 64 slots, DSP, mixer.
 * step() = control + render; step_control() alone never reads render state, so the PPU can run it (5.13). */
#pragma once
#include <stdint.h>
#include <string.h>
#include "dsp_asm.h"
#include "dsp_op.h"

#ifdef CAIQUE_RAM_HOOKS
/* Wave RAM outside the model (PS3 aica_ram, SPU cache); a = byte address, 16-bit reads are even and little-endian */
uint8_t caique_rd8(uint32_t a);
uint16_t caique_rd16(uint32_t a);
uint16_t caique_dsp_rd16(uint32_t a, uint32_t rbp, uint32_t rbl);
void caique_dsp_wr16(uint32_t a, uint32_t rbp, uint32_t rbl, uint16_t v);
/* The 128-byte line holding byte a, PS3 order (byte a at [(a & 127) ^ 3]); valid until the next wave RAM access */
const uint8_t *caique_line(uint32_t a);
#define CAIQUE_SWZ 3
#else
#define CAIQUE_SWZ 0
#endif
#ifdef CAIQUE_PROF
/* full-model section marks: 0 start, 1 slots, 2 envelopes, 3 preview + buses, 4 DSP, 5 mix; v = slots rendered */
void caique_prof(int point, uint32_t v);
#define CAIQUE_PROF_AT(p, v) caique_prof(p, v)
#else
#define CAIQUE_PROF_AT(p, v)
#endif
#ifdef CAIQUE_DSP_HOOK
namespace caique { struct AicaModel; }
/* SPU DSP recompiler (5.13 A1b): true when it ran the DSP pass of sample smp, else dsp_step() runs */
bool caique_dsp_hook(caique::AicaModel *m, uint64_t smp, uint32_t mdec);
#define CAIQUE_DSP_ALIGN __attribute__((aligned(16)))   /* the recompiled code's quadword loads */
#else
#define CAIQUE_DSP_ALIGN
#endif

namespace caique {

enum EgState { EG_ATTACK = 0, EG_DECAY1 = 1, EG_DECAY2 = 2, EG_RELEASE = 3 };

/* Render-only fields: s0, s1, adpcm_quant, lpf_*, fix_*. Everything else is control state. */
struct Slot {
    uint32_t CA;           /* current sample address (samples from SA) */
    uint32_t step;         /* phase fraction, 14 bits */
    uint32_t update_rate;  /* phase increment per sample, 1/2^14 sample units */
    int32_t s0, s1;        /* samples at CA and CA+1 */
    uint32_t LSA, LEA;
    bool looped;
    bool in_loop;          /* CA has reached LSA since key-on (arms the loop-end check) */
    int32_t adpcm_quant;
    struct {
        uint16_t a;        /* 10-bit attenuation (TL counts 4); steps on to 0x3FF after the 0x3C0 stop (tests/slot_tail) */
        bool off;          /* stopped: monitor reads 0x1FFF (tests/ca_stop); output is not muted, the filter tail goes on */
        EgState state;
    } AEG;
    struct {
        EgState state;
        uint16_t v;        /* 13-bit cutoff envelope; after a step it already holds the next sample's value */
        uint16_t vo;       /* the value this sample's filter used */
        int8_t dir;        /* segment direction, latched when the segment starts (tests/feg_track) */
        bool passed;       /* attack / decay 1 crossed the target: advance on the next clock */
    } FEG;
    int32_t lpf_low, lpf_band; /* filter state, 1/8 sample units; never cleared (tests/filt_reset) */
    bool lpf_fix;              /* the last zero-input step at (fix_v, fix_q) left the state unchanged: skip repeats */
    uint8_t fix_q;
    uint16_t fix_v;
    struct {
        uint32_t counter, start_value;
        uint8_t state, alfo_w, alfo_shft, plfo, plfo_shft; /* alfo_w: 8-bit ALFO waveform value */
    } lfo;
    uint32_t lfo_t;       /* LFO ticks applied (low 32 bits of samples); lfo_sync catches up in closed form */
    uint16_t lfo_r1c;     /* register 0x1C the ticks since the last LFO write ran with */
    bool enabled;         /* sample fetch running; cleared one sample after the 0x3C0 clock, or by a one-shot end */
    uint8_t stop_in;      /* stop armed by aeg_clock, committed after the next sample's output (tests/slot_tail) */
    bool ca_clr;          /* one-shot end: CA reads 0 from the next sample (tests/oneshot) */
    bool keyed_fetch;     /* keyed on by the previous envelope pass: this sample's fetch does not advance */
    bool keyed;           /* key-on sample: no envelope step (an R 63 attack still leaves attack, tests/eg_kprobe) */
    bool keyed_off;       /* key-off sample: a clock takes one more step of the old segment, not from attack (tests/aeg_koff) */
    EgState aeg_prev, feg_prev; /* the states before that key-off */
    int8_t feg_prev_dir;        /* FEG direction before that key-off (tests/feg_koffdir) */
    bool feg_prev_passed;       /* passed flag before that key-off: the clock steps the NEXT segment's way (tests/feg_koffpass) */
    uint8_t aeg_R[4], feg_R[4]; /* eff_rate per EgState, refreshed on writes to 0x10/0x14/0x18/0x40/0x44 */
    uint16_t env_skip;          /* idle slot: a clock with (eg_cnt - 1) & env_skip set moves neither envelope */
};

/* What the render pass does at one CA position of a slot's stream walk */
enum { OP_RESET = 1, OP_DECODE = 2, OP_LAST = 4 };
struct RenderOp { uint16_t ca; uint8_t flags; };

/* Max CA advances in one sample: OCT 7, FNS 0x3FF, full PLFO -> (2298 << 11) + 0x3FFF over 2^14 */
static const int MAX_OPS_PER_SLOT = 288;

/* Control state one slot's render pass reads, taken at the slot's output point */
struct SlotFrame {
    uint32_t lfsr;          /* LFSR after this slot's step */
    uint32_t step;          /* phase fraction after the walk */
    uint32_t ca_old, ca;    /* CA before / after the walk (collision fetch) */
    uint16_t fv, aeg_a;     /* FEG value for the filter, AEG level */
    bool en_fetch, keyed;   /* enabled / keyed_fetch at the walk start */
    bool en_out, aeg_off;   /* enabled after the walk, AEG off */
};

/* Control -> render hand-off of the current sample; the slot ops cover one slot at a time */
struct Frame {
    uint64_t sample;
    uint32_t mdec;          /* MDEC_CT of this sample's DSP pass */
    struct { bool en, keyed; uint32_t ca_old, ca; } pv[7];  /* next sample's fetch of slots 0..6 (coll_preview) */
    uint32_t nops;
    RenderOp ops[MAX_OPS_PER_SLOT];
};

/* Control state for the PPU / SPU compare (5.13 A1); same layout on both, a multiple of 16 bytes */
struct SlotSnap {
    uint32_t CA, step;
    uint16_t a, fv;
    uint8_t aeg, feg, flags, lfo;  /* flags: enabled, off, keyed_fetch, stop_in, in_loop, ca_clr, passed, dir < 0 */
    uint32_t lfo_cnt;
};
struct CtlSnap {
    uint32_t sample, lfsr, mdec, eg_cnt;
    SlotSnap s[64];
};

struct AicaModel {
    static const uint32_t RAM_SIZE = 2u << 20;
    uint8_t *ram;              /* RAM_SIZE bytes; null with CAIQUE_RAM_HOOKS */
    uint16_t reg[0x4600 / 4];  /* register file, one 16-bit value per 32-bit slot, offset >> 2; none is stored above 0x45BC */

    Slot slot[64];

    /* DSP state */
    int32_t TEMP[128] CAIQUE_DSP_ALIGN;  /* 24-bit */
    int32_t MEMS[32];   /* 24-bit */
    int32_t MIXS[16];   /* 20-bit SGC sum; the DSP reads one of two banks on alternate samples (tests/eg_lock mixs) */
    int32_t MIXS_bank[2][16] __attribute__((aligned(16)));   /* the recompiled code and dsp_sample() use quadwords */
    int32_t EXTS[2];    /* 16-bit */
    int32_t EFREG[16];  /* 16-bit */
    uint32_t MDEC_CT;
    int32_t ACC, FRC_REG, Y_REG;
    uint32_t ADRS_REG;
    uint16_t memval;          /* last word read by MRD, as seen by IWT */
    struct { int64_t land; uint16_t val; } mem_q[4]; /* reads in flight: global step at which each lands */
    int mem_qn;
    int mrd_even_masa;        /* even-step MRD waiting for the next (odd) memory slot, -1 = none */
    dsp_op_t mrd_even;
    uint8_t nofl_pipe[2];     /* NOFL of steps s-1, s-2 */

    int16_t outL, outR;       /* DAC output of the last sample */
    uint64_t samples;         /* samples run */
    uint32_t lfsr;            /* noise: 17-bit LFSR x^17 + x^12 + 1, one step per slot processed */
    /* Envelope clock ticks on even MDEC_CT, eg_cnt = eg_K - MDEC_CT/2 (mod 2^14); eg_K is a per-boot constant
     * (6491 on the tests/eg_lock boot, changes at a reset). See tests/eg_lock, feg_track, aeg_dl0. */
    uint32_t eg_cnt;
    uint32_t eg_K;
    struct { uint16_t scieb, scipd, mcieb, mcipd; uint8_t cnt[3], pre[3]; } irq;
    uint32_t tim_phase;
    uint32_t eg_par;           /* MDEC_CT parity the envelope clock ticks on (a 0x2804 bit 15 write may flip it, TODO 7.1) */
    /* KYONEX only raises this; the next envelope pass reads every KYONB, events land one sample later (tests/kon_defer) */
    bool kyonex_pending;
    /* Slot K's fetch owns DSP step 2K - 14's wave RAM slot (tests/dsp_coll): an MWT there drops, an MRD gets
     * coll_word. Slots 0..6 fall in the next sample's sweep. 16-aligned: dsp_sample() copies them as quadwords. */
    bool coll_claim[64] __attribute__((aligned(16)));
    uint16_t coll_word[64] __attribute__((aligned(16)));
    bool mrd_even_coll;       /* the waiting even-step MRD was in a channel's slot */
    uint16_t mrd_even_word;
    uint32_t mpro_nz;         /* non-zero MPRO words: 0 runs the DSP's all-NOP fast path */
    dsp_op_t prog[128];       /* MPRO decoded on write */
    uint32_t stops_pending;   /* slots with stop_in set */
    bool keys_live;           /* some slot has keyed / keyed_off set */
    uint64_t quiet;           /* slots with no control work until a register write or a key event */
    uint64_t fetching;        /* control-only CA loop: every slot with enabled / ca_clr / keyed_fetch (and some idle ones) */
    uint64_t rsilent;         /* render: the slot's last visit output 0 with its filter at rest */
    uint8_t isel_cnt[16];     /* slots per ISEL bus: every slot writes its bus each sample (tests/mixs_write) */
    bool no_dsp;              /* 5.13 A2: no DSP pass, EFREG mix or collision fetch; init() clears it, reset() keeps it */

    Frame fr;                 /* control -> render hand-off of the current sample */
    static const uint32_t ADV_MAX = 64;   /* advance() works in rounds of this many samples */
    uint32_t adv_l0[ADV_MAX]; /* LFSR at the start of each sample of the round, made on first use */
    bool adv_l0_ok;
    /* 5.13 A3: a render round with the DSP leaves, per sample, the SGC bus sums and the claims dsp_step reads (slots
     * 7..63 from that sample's fetch, 0..6 from the next sample's, as run()'s preview) */
    static const uint32_t DSP_ROUND = 32;
    int32_t adv_row[DSP_ROUND][4] __attribute__((aligned(16)));   /* one slot's slot_fast rows: l, r, V16, CA */
    int32_t adv_bus[DSP_ROUND][16] __attribute__((aligned(16)));
    bool adv_cc[DSP_ROUND][64] __attribute__((aligned(16)));
    uint16_t adv_cw[DSP_ROUND][64] __attribute__((aligned(16)));
    int32_t adv_busw[16] __attribute__((aligned(16)));   /* -1 for each bus some slot points at (isel_cnt) */
    uint64_t coll_need;       /* slots whose claim the program reads: even steps with MWT or MRD */
    uint64_t adv_s0;          /* the round's first sample and its MDEC_CT */
    uint32_t adv_mdec0;

    AicaModel();
    ~AicaModel();
    void init();              /* the constructor's work, for storage that no constructor ran on (SPU) */
    void reset();
    void write(uint32_t off, uint32_t val);
    uint32_t read(uint32_t off);
    bool sh4_irq() const { return (irq.mcieb & irq.mcipd) != 0; }   /* the AICA's SH4 interrupt line (tests/timer_irq L) */
    uint32_t ram_read32(uint32_t off); /* CPU read of wave RAM (updates the memory read latch) */
    void step() { run(true); }
    void step_control() { run(false); }   /* control state only; outL/outR, MIXS and the DSP do not move */
    void snap(CtlSnap &o);   /* catches every LFO up first */
    static uint32_t snap_hash(const CtlSnap &o);
    /* Slots the control pass visits: live = not quiet; en = fetch or a key/stop flag; aeg / feg = the rest, AEG moving
     * or only the FEG moving (PPU stats) */
    void live_stats(uint32_t &live, uint32_t &en, uint32_t &aeg, uint32_t &feg) const;
    void dsp_step(uint64_t smp, uint32_t mdec);   /* the DSP interpreter; public for the SPU recompiler's check */
    /* 5.13 A2 high voices: n samples slot by slot, as n step() / step_control() calls; render leaves l, r, 0, 0 per sample
     * in mix (16-aligned), MIXS does not move. With the DSP (A3): n <= DSP_ROUND, then dsp_sample(i) for each i in order. */
    void advance(uint32_t n, bool render, int32_t *mix);
    void dsp_sample(uint32_t i);   /* 5.13 A3: sample i of that round: MIXS_bank, the claims, the DSP; EXTS set first */
    void final_mix(int32_t l, int32_t r);   /* run()'s output stage after the slots: EXTS, EFREG, MONO, MVOL, DAC18B */

    uint16_t r(uint32_t off) const { return reg[(off & 0x7FFF) >> 2]; }
    uint16_t chr(int ch, uint32_t o) const { return reg[(0x80 * ch + o) >> 2]; }

  private:
    void run(bool render);
    void slot_regwrite(int ch, uint32_t o);
    void key_on(int ch);
    void key_off(int ch);
    void set_aeg_state(int ch, EgState s);
    void update_pitch(int ch);
    void update_rates(int ch);
    void update_lfo(int ch, bool from_write);
    static uint32_t eff_rate(uint32_t rate, uint16_t r14, uint16_t r18); /* from the KRS (r14) and OCT/FNS (r18) given */
    void stream_step(int ch, Frame *F, uint32_t smp);
    void decode_sample(int ch, bool last, uint32_t CA);
    void aeg_clock(int ch);
    void feg_clock(int ch);
    int32_t lpf_step(int ch, uint32_t v, int32_t x8); /* cutoff v; input and output in 1/8 sample units */
    bool slot_fetch(int ch, bool en, bool keyed, uint32_t ca_old, uint32_t ca, uint16_t &word) const;
    void coll_preview(Frame &F, uint32_t mask = 0x7F);   /* mask: the slots of 0..6 to preview */
    void lfo_calc(int ch);
    void lfo_sync(int ch, uint32_t t);
    inline void lfo_tick(int ch, uint32_t t);
    bool aeg_frozen(int ch) const;
    bool feg_frozen(int ch) const;
    bool slot_quiet(int ch, bool render) const;
    uint16_t env_skip_mask(int ch) const;
    void slot_control(int ch, SlotFrame &sf);
    void slot_render(int ch, const SlotFrame &sf, uint32_t smp, int32_t &l, int32_t &r, int32_t &d);
    struct AdvCtx { uint32_t t0, mdec0, n; bool kx; uint32_t l00; };
    void advance_round(uint32_t n, bool render, int32_t *mix);
    void slot_run(int ch, const AdvCtx &x, bool render, int32_t *mix, uint32_t &rendered);
    void slot_sample(int ch, uint32_t i, const AdvCtx &x, bool render, int32_t *mix, uint32_t &rendered);
    template <bool R> uint32_t slot_fast(int ch, uint32_t i, const AdvCtx &x, int32_t *mix, uint32_t &rendered);
    uint32_t slot_fast_dsp(int ch, uint32_t i, const AdvCtx &x, int32_t *mix, uint32_t &rendered);
    uint32_t slot_ctl_fast(int ch, uint32_t i, const AdvCtx &x, uint32_t &rendered);
    uint32_t lfsr_at(const AdvCtx &x, uint32_t i);
    uint32_t sa_addr(int ch) const;
    int fmt(int ch) const;
#ifdef CAIQUE_RAM_HOOKS
    uint8_t rd8(uint32_t a) const { return caique_rd8(a); }
    uint16_t rd16(uint32_t a) const { return caique_rd16(a); }
#else
    uint8_t rd8(uint32_t a) const { return ram[a]; }
    uint16_t rd16(uint32_t a) const { return (uint16_t)(ram[a] | (ram[a + 1] << 8)); }
#endif
};

} // namespace caique
