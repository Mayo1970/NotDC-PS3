/* caique AICA sample model (caique-rtl 5aa2376, MIT, see LICENSE), split into control and render passes.
 * Test names (tests/...) and NOTES refer to the caique-rtl repo. */
#include "aica_model.h"
#include "dsp_asm.h"
#include "dsp_float.h"
#include <stdlib.h>
#ifdef __SPU__
#include <spu_intrinsics.h>
#endif

namespace caique {

static inline int32_t sext(int32_t v, int bits) { return (int32_t)((uint32_t)v << (32 - bits)) >> (32 - bits); }
static inline int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
#ifdef __SPU__
/* The SPU multiplies 16 x 16 bits; GCC sees through the int16_t casts below and emits the 3-multiply 32-bit sequence */
static inline int32_t mul16(int32_t a, int32_t b) { return si_to_int(si_mpy(si_from_int(a), si_from_int(b))); }
static inline int32_t mul32x16(int32_t a, int32_t b) {   /* b in 0..0xFFFF: (a_hi * b) << 16 + a_lo * b */
    const qword x = si_from_int(a), y = si_from_int(b);
    return si_to_int(si_a(si_mpyh(x, y), si_mpyu(x, y)));
}
#else
static inline int32_t mul16(int32_t a, int32_t b) { return (int32_t)(int16_t)a * (int32_t)(int16_t)b; }   /* both fit */
static inline int32_t mul32x16(int32_t a, int32_t b) { return a * b; }   /* b in 0..0xFFFF */
#endif

/* Tables (minicast sgc_if.cpp) */
static const int32_t adpcm_qs[8] = {0x0e6, 0x0e6, 0x0e6, 0x0e6, 0x133, 0x199, 0x200, 0x266};

/* Slot stop at 0x3C0, one sample after its clock, never muted (tests/slot_tail). Macros only so the caique
 * controls (work/verify/s5/model_fixes.md) can rebuild the refuted variants with -D. */
#ifndef CAIQUE_STOP_A
#define CAIQUE_STOP_A 0x3C0
#endif
#ifndef CAIQUE_MUTE
#define CAIQUE_MUTE 0
#endif
#ifndef CAIQUE_STOP_LAG
#define CAIQUE_STOP_LAG 1
#endif

/* Register masks (measured: tests/probe) */
static uint16_t chan_mask(uint32_t o) {
    switch (o) {
    case 0x00: return 0x47FF; /* KYONEX (bit 15) is an action, bits 13:11 not stored */
    case 0x04: case 0x08: case 0x0C: return 0xFFFF;
    case 0x10: return 0xFFDF;
    case 0x14: return 0x7FFF;
    case 0x18: case 0x1C: return 0xFFFF;
    case 0x20: return 0x00FF;
    case 0x24: case 0x28: return 0xFFFF;
    case 0x2C: case 0x30: case 0x34: case 0x38: case 0x3C: return 0x1FFF;
    case 0x40: case 0x44: return 0x1F1F;
    default: return 0;
    }
}

const uint32_t AicaModel::ADV_MAX;
const uint32_t AicaModel::LSA_KEYON;

AicaModel::AicaModel() { init(); }
AicaModel::~AicaModel() { free(ram); }

void AicaModel::init() {
#ifdef CAIQUE_RAM_HOOKS
    ram = 0;
#else
    ram = (uint8_t *)calloc(RAM_SIZE, 1);
#endif
    no_dsp = false;
    mono_half = false;
    out_gain = 0;
    adpcm_restore = false;
    stream_lsa = false;
    reset();
}

void AicaModel::reset() {
    memset(reg, 0, sizeof reg);
    memset(slot, 0, sizeof slot);
    memset(TEMP, 0, sizeof TEMP);
    memset(MEMS, 0, sizeof MEMS);
    memset(MIXS, 0, sizeof MIXS);
    memset(MIXS_bank, 0, sizeof MIXS_bank);
    memset(EXTS, 0, sizeof EXTS);
    memset(EFREG, 0, sizeof EFREG);
    MDEC_CT = 1;
    ACC = FRC_REG = Y_REG = 0;
    ADRS_REG = 0;
    memval = 0;
    mem_qn = 0;
    mrd_even_masa = -1;
    mrd_even_coll = false;
    mrd_even_word = 0;
    memset(coll_claim, 0, sizeof coll_claim);
    memset(coll_word, 0, sizeof coll_word);
    nofl_pipe[0] = nofl_pipe[1] = 0;
    outL = outR = 0;
    samples = 0;
    eg_cnt = 0;
    eg_K = 6491;
    eg_par = 0;
    memset(&irq, 0, sizeof irq);
    tim_phase = 0;
    kyonex_pending = false;
    lfsr = 1;
    mpro_nz = 0;
    memset(prog, 0, sizeof prog);   /* the decode of an all-zero MPRO */
    coll_need = 0;
    memset(adv_row, 0, sizeof adv_row);   /* the A3 rows start zero; each user leaves them so */
    memset(adv_bus, 0, sizeof adv_bus);
    memset(adv_cc, 0, sizeof adv_cc);
    memset(adv_cw, 0, sizeof adv_cw);
    memset(adv_busw, 0, sizeof adv_busw);
    adv_s0 = 0;
    adv_mdec0 = 0;
    clip[0] = clip[1] = 0;
    peak = 0;
    stops_pending = 0;
    keys_live = false;
    quiet = rsilent = fetching = 0;
    memset(isel_cnt, 0, sizeof isel_cnt);
    isel_cnt[0] = 64;
    memset(&fr, 0, sizeof fr);
    for (int ch = 0; ch < 64; ch++) {
        for (uint32_t o = 0; o < 0x48; o += 4) slot_regwrite(ch, o);
        slot[ch].enabled = false;
        slot[ch].AEG.state = EG_RELEASE;
        slot[ch].AEG.a = 0x3FF;
        slot[ch].AEG.off = true;
        lsa_state[ch] = LSA_KEYON;
    }
}

/* SGC (port of minicast ChannelEx) */
int AicaModel::fmt(int ch) const {
    uint16_t r0 = chr(ch, 0x00);
    return ((r0 >> 10) & 1) ? 4 : (r0 >> 7) & 3; /* SSCTL -> noise */
}
uint32_t AicaModel::sa_addr(int ch) const {
    uint16_t r0 = chr(ch, 0x00);
    uint32_t a = ((uint32_t)(r0 & 0x7F) << 16) | chr(ch, 0x04);
    if (((r0 >> 7) & 3) == 0) a &= ~1u;
    return a;
}

void AicaModel::set_aeg_state(int ch, EgState s) {
    slot[ch].AEG.state = s; /* KYONB is never cleared by the hardware (tests/sgc_keys; minicast cleared it) */
}

/* The ADPCM reset of a key-on (quant 127, s0 0) is render state: the next sample's OP_RESET applies it */
void AicaModel::key_on(int ch) {
    Slot &c = slot[ch];
    if (c.AEG.state != EG_RELEASE) return;
    c.enabled = true;
    fetching |= 1ull << ch;
    c.env_skip = 0;
    set_aeg_state(ch, EG_ATTACK);
    c.AEG.a = 0x280; /* key-on level: -60 dB (tests/sgc_aeg) */
    c.AEG.off = false;
    if (c.stop_in) { c.stop_in = 0; stops_pending--; }
    c.ca_clr = false;
    /* R 63: level 0 from the key-on sample; attack ends on the first clock at or after it (tests/sgc_krs, eg_kprobe) */
    if (c.aeg_R[EG_ATTACK] >= 63) c.AEG.a = 0;
    c.FEG.state = EG_ATTACK;
    c.FEG.v = chr(ch, 0x2C) & 0x1FFF; /* FLV0 (tests/feg_probe) */
    c.FEG.dir = c.FEG.v >= (chr(ch, 0x30) & 0x1FFF) ? -1 : 1;
    c.FEG.passed = false;
    c.CA = 0;
    c.step = 0;
    c.looped = false;
    c.in_loop = false;
}

void AicaModel::key_off(int ch) {
    slot[ch].env_skip = 0;
    slot[ch].aeg_prev = slot[ch].AEG.state;
    slot[ch].feg_prev = slot[ch].FEG.state;
    slot[ch].feg_prev_dir = slot[ch].FEG.dir; /* a key-off clock steps the FEG the old way (tests/feg_koffdir) */
    slot[ch].feg_prev_passed = slot[ch].FEG.passed; /* ... or the next segment's way if the old one had passed (feg_koffpass) */
    if (slot[ch].AEG.state != EG_RELEASE) set_aeg_state(ch, EG_RELEASE);
    slot[ch].FEG.state = EG_RELEASE;
    slot[ch].FEG.dir = slot[ch].FEG.v >= (chr(ch, 0x3C) & 0x1FFF) ? -1 : 1;
    slot[ch].FEG.passed = false;
}

/* R = min(63, 2*rate + s), s = k < 0 ? 0 : 2*min(k,15) + FNS[9], k = KRS + OCT; KRS 15: s = 0; rate 0 = none.
 * All 320 KRS x OCT x FNS combos (tests/sgc_krs); registers are live, no latch (tests/eg_latch). */
uint32_t AicaModel::eff_rate(uint32_t re, uint16_t r14, uint16_t r18) {
    if (re == 0) return 0;
    uint32_t KRS = (r14 >> 10) & 0xF, FNS = r18 & 0x3FF, OCT = (r18 >> 11) & 0xF;
    int32_t s = 0;
    if (KRS != 0xF) {
        int32_t k = (int32_t)KRS + ((OCT & 8) ? (int32_t)OCT - 16 : (int32_t)OCT);
        s = k < 0 ? 0 : 2 * (k > 15 ? 15 : k) + (int32_t)((FNS >> 9) & 1);
    }
    int32_t R = (int32_t)re * 2 + s;
    return (uint32_t)(R > 63 ? 63 : R);
}

void AicaModel::update_pitch(int ch) {
    uint32_t OCT = (chr(ch, 0x18) >> 11) & 0xF, FNS = chr(ch, 0x18) & 0x3FF;
    /* inc = (1024 + FNS) << (OCT + 4), OCT signed, 14 fraction bits (tests/sgc_pitch; minicast's 10 lose precision) */
    uint32_t rate = 1024 | FNS;
    int oct = (OCT & 8) ? (int)OCT - 16 : (int)OCT;
    int sh = oct + 4;
    slot[ch].update_rate = sh >= 0 ? rate << sh : rate >> -sh;
}

/* The registers are live (tests/eg_latch): every write to one of them refreshes all eight rates */
void AicaModel::update_rates(int ch) {
    Slot &c = slot[ch];
    const uint16_t r10 = chr(ch, 0x10), r14 = chr(ch, 0x14), r18 = chr(ch, 0x18), r40 = chr(ch, 0x40), r44 = chr(ch, 0x44);
    c.aeg_R[EG_ATTACK] = (uint8_t)eff_rate(r10 & 0x1F, r14, r18);
    c.aeg_R[EG_DECAY1] = (uint8_t)eff_rate((r10 >> 6) & 0x1F, r14, r18);
    c.aeg_R[EG_DECAY2] = (uint8_t)eff_rate((r10 >> 11) & 0x1F, r14, r18);
    c.aeg_R[EG_RELEASE] = (uint8_t)eff_rate(r14 & 0x1F, r14, r18);
    c.feg_R[EG_ATTACK] = (uint8_t)eff_rate((r40 >> 8) & 0x1F, r14, r18);
    c.feg_R[EG_DECAY1] = (uint8_t)eff_rate(r40 & 0x1F, r14, r18);
    c.feg_R[EG_DECAY2] = (uint8_t)eff_rate((r44 >> 8) & 0x1F, r14, r18);
    c.feg_R[EG_RELEASE] = (uint8_t)eff_rate(r44 & 0x1F, r14, r18);
}

/* LFSR jumps as GF(2) matrices, row b = the input bits whose parity is output bit b. Constant tables (caique's
 * LfsrJump(2) / LfsrJump(-67)), so the SPU needs no static constructors. */
static const uint32_t lfsr_alfo_rows[17] = {0x00004, 0x00008, 0x00010, 0x00020, 0x00040, 0x00080, 0x00100, 0x00200,
    0x00400, 0x00800, 0x01000, 0x02000, 0x04000, 0x08000, 0x10000, 0x00021, 0x00042};
static const uint32_t lfsr_plfo_rows[17] = {0x086BB, 0x10D76, 0x01ACD, 0x0359A, 0x06B34, 0x0D668, 0x1ACD0, 0x15981,
    0x0B323, 0x16646, 0x0CCAD, 0x1995A, 0x13295, 0x0650B, 0x0CA16, 0x1942C, 0x12879};
static inline uint32_t lfsr_jump(const uint32_t *row, uint32_t l) {
    uint32_t o = 0;
    for (int b = 0; b < 17; b++) o |= (uint32_t)__builtin_parity(l & row[b]) << b;
    return o;
}
/* n LFSR steps, 12 at a time: each new bit is b[i] ^ b[i+5] of the old state while i + 5 <= 16 */
static inline uint32_t lfsr_adv(uint32_t l, uint32_t n) {
    for (; n >= 12; n -= 12) l = (l >> 12) | (((l ^ (l >> 5)) & 0xFFF) << 5);
    if (n) l = (l >> n) | (((l ^ (l >> 5)) & ((1u << n) - 1)) << (17 - n));
    return l;
}
/* Noise LFOs (tests/sgc_lfo, lfo_noise): ALFO = LFSR byte 2 steps after the slot's step, PLFO = byte 67 steps
 * before its stage-A point, XOR 0x80 (offset binary; ALFO uses it unsigned) */
static inline uint32_t lfsr_alfo_j(uint32_t l) { return lfsr_jump(lfsr_alfo_rows, l); }
static inline uint32_t lfsr_plfo_j(uint32_t l) { return lfsr_jump(lfsr_plfo_rows, l); }
void AicaModel::lfo_calc(int ch) {
    Slot &c = slot[ch];
    uint16_t r1c = chr(ch, 0x1C);
    uint32_t st = c.lfo.state, rv = 0;
    switch ((r1c >> 3) & 3) { /* ALFOWS (noise is taken per sample in slot_render) */
    case 0: rv = st; break;
    case 1: rv = st & 0x80 ? 255 : 0; break;
    case 2: rv = ((st & 0x7f) ^ (st & 0x80 ? 0x7F : 0)) << 1; break;
    case 3: rv = 0; break;
    }
    c.lfo.alfo_w = (uint8_t)rv;
    /* PLFO is signed 8-bit (tests/sgc_lfo): saw = (int8)state, square = +127 / -128; noise is taken in stream_step */
    int32_t pw = 0;
    switch ((r1c >> 8) & 3) { /* PLFOWS */
    case 0: pw = (int8_t)st; break;
    case 1: pw = st & 0x80 ? -128 : 127; break;
    /* triangle (lf_4 / lf_5): 0 up by 2 to 126, 126 again at 64, down to -128, -128 again at 192, up; bit 0 unused */
    case 2: pw = st < 64 ? 2 * (int32_t)st : st < 192 ? 254 - 2 * (int32_t)st : 2 * (int32_t)st - 512; break;
    case 3: pw = 0; break;
    }
    c.lfo.plfo = (uint8_t)(int8_t)pw;
}

/* t - lfo_t per-sample LFO ticks (tests/sgc_lfo hw9: no reload on a write, clamp to a shorter period, LFORE holds
 * state 0). Ticks run one by one until the counter lies in [1, period], the rest in closed form. */
void AicaModel::lfo_sync(int ch, uint32_t t) {
    Slot &c = slot[ch];
    uint32_t k = t - c.lfo_t;
    if (!k) return;
    c.lfo_t = t;
    const bool lfore = (c.lfo_r1c >> 15) & 1;
    const uint32_t sv = c.lfo.start_value;
    const uint8_t st0 = c.lfo.state;
    while (k && (c.lfo.counter == 0 || c.lfo.counter > sv)) {
        if (c.lfo.counter > sv) c.lfo.counter = sv;
        if (--c.lfo.counter == 0) { c.lfo.counter = sv; if (!lfore) c.lfo.state++; }
        if (lfore) c.lfo.state = 0;
        k--;
    }
    if (k) {
        uint32_t wraps = 0;
        if (k < c.lfo.counter) c.lfo.counter -= k;
        else {
            const uint32_t rest = k - c.lfo.counter;
            wraps = 1 + rest / sv;
            c.lfo.counter = sv - rest % sv;
        }
        c.lfo.state = lfore ? 0 : (uint8_t)(c.lfo.state + wraps);
    }
    if (c.lfo.state != st0) lfo_calc(ch);
}

/* lfo_sync(ch, t) for the common case of slot_fast(): one tick due, counter in 1..period */
inline void AicaModel::lfo_tick(int ch, uint32_t t) {
    Slot &c = slot[ch];
    const uint32_t lc = c.lfo.counter, sv = c.lfo.start_value;
    if (__builtin_expect((t - c.lfo_t == 1) & (lc - 1 < sv), 1)) {
        c.lfo_t = t;
        const uint8_t st0 = c.lfo.state;
        uint8_t st = st0;
        if (lc > 1) c.lfo.counter = lc - 1;
        else { c.lfo.counter = sv; st++; }
        if ((c.lfo_r1c >> 15) & 1) st = 0;   /* LFORE */
        if (st != st0) { c.lfo.state = st; lfo_calc(ch); }
    } else
        lfo_sync(ch, t);
}

void AicaModel::update_lfo(int ch, bool from_write) {
    Slot &c = slot[ch];
    if (from_write) lfo_sync(ch, (uint32_t)samples);   /* the ticks so far ran with the old period and LFORE */
    uint16_t r1c = chr(ch, 0x1C);
    c.lfo_r1c = r1c;
    int N = (r1c >> 10) & 0x1F, S = N >> 2, M = (~N) & 3, G = 128 >> S, L = (G - 1) << 2, O = L + G * (M + 1);
    c.lfo.start_value = O;   /* a write does not reload the counter (tests/sgc_lfo hw9) */
    if (!from_write) c.lfo.counter = O;
    c.lfo.plfo_shft = 8 - ((r1c >> 5) & 7);
    c.lfo.alfo_shft = 8 - (r1c & 7);
    if (from_write && (r1c & 0x8000)) c.lfo.state = 0; /* LFORE */
    lfo_calc(ch);
}

void AicaModel::slot_regwrite(int ch, uint32_t o) {
    Slot &c = slot[ch];
    switch (o) {
    case 0x00:
        if (reg[(0x80 * ch) >> 2] & 0x8000) { /* KYONEX */
            reg[(0x80 * ch) >> 2] &= 0x7FFF;
            /* KYONB is read at the next boundary, events land one sample later with no envelope step (tests/eg_lock keys) */
            kyonex_pending = true;
        }
        break;
    case 0x08: case 0x0C:
        c.LSA = chr(ch, 0x08);
        c.LEA = chr(ch, 0x0C);
        break;
    case 0x18: update_pitch(ch); update_rates(ch); break;
    case 0x10: case 0x14: case 0x40: case 0x44: update_rates(ch); break;
    case 0x1C: update_lfo(ch, true); break;
    default: break; /* other FEG / filter registers: minicast has no FEG */
    }
}

/* sample is a nibble. Branch-free for the SPU; quant stays in 127..24576, so its product fits a 16-bit multiply. */
static inline __attribute__((always_inline)) int32_t decode_adpcm(uint32_t sample, int32_t prev, int32_t &quant) {
    const int32_t q = quant, neg = -(int32_t)((sample >> 3) & 1);
    const uint32_t data = sample & 7;
    /* Yamaha delta, each term shifted separately (tests/sgc_formats: nibble 6 at step 127 gives 205, not 206) */
    int32_t rv = (q >> 3) + ((q >> 2) & -(int32_t)(data & 1)) + ((q >> 1) & -(int32_t)((data >> 1) & 1)) + (q & -(int32_t)(data >> 2));
    rv = rv < 0x7FFF ? rv : 0x7FFF;
    rv = prev + ((rv ^ neg) - neg);
    const int32_t nq = mul16(q, adpcm_qs[data]) >> 8;
    quant = nq < 127 ? 127 : nq > 24576 ? 24576 : nq;
    return rv < -32768 ? -32768 : rv > 32767 ? 32767 : rv;
}

void AicaModel::decode_sample(int ch, bool last, uint32_t CA) {
    Slot &c = slot[ch];
    int f = fmt(ch);
    if (!last && (f < 2 || f == 4)) return;
    uint32_t sa = sa_addr(ch);
    int32_t s0 = 0, s1 = 0;
    switch (f) {
    case 4: /* noise: taken from the global LFSR in slot_render */
        s0 = s1 = 0;
        break;
    case 0:
        s0 = (int16_t)rd16((sa + 2 * CA) & (RAM_SIZE - 2));
        s1 = (int16_t)rd16((sa + 2 * CA + 2) & (RAM_SIZE - 2));
        break;
    case 1:
        s0 = (int8_t)rd8((sa + CA) & (RAM_SIZE - 1)) << 8;
        s1 = (int8_t)rd8((sa + CA + 1) & (RAM_SIZE - 1)) << 8;
        break;
    case 2: case 3: {
        uint8_t ad1 = rd8((sa + (CA >> 1)) & (RAM_SIZE - 1));
        uint8_t ad2 = rd8((sa + ((CA + 1) >> 1)) & (RAM_SIZE - 1));
        uint8_t sf = (CA & 1) * 4;
        ad1 = (ad1 >> sf) & 0xF;
        ad2 = (ad2 >> (4 - sf)) & 0xF;
        int32_t q = c.adpcm_quant;
        s0 = decode_adpcm(ad1, c.s0, q);
        c.adpcm_quant = q;
        s1 = last ? decode_adpcm(ad2, s0, q) : 0;
        break;
    }
    }
    c.s0 = s0;
    c.s1 = s1;
}

/* The ADPCM decoder before an op's decode: a key-on clears it, the first pass over LSA keeps a copy, and a loop lap
 * loads that copy (adpcm_restore) or clears the decoder again (caique's cases all loop from LSA 0: the same there) */
void AicaModel::adpcm_op(int ch, uint32_t flags) {
    Slot &c = slot[ch];
    if (flags & OP_LSA) lsa_state[ch] = (uint32_t)c.adpcm_quant << 16 | ((uint32_t)c.s0 & 0xFFFF);
    if (flags & OP_RESET) lsa_state[ch] = LSA_KEYON;
    if (flags & (OP_RESET | OP_LOOP)) {
        const uint32_t v = (flags & OP_LOOP) && adpcm_restore ? lsa_state[ch] : LSA_KEYON;
        c.adpcm_quant = (int32_t)(v >> 16);
        c.s0 = (int16_t)v;
    }
}

/* Whether slot ch fetches this sample, and the word it leaves on the bus: the one holding CA + 1 (tests/dsp_coll).
 * PCM fetches every playing sample; ADPCM only on key-on or a word change, none at OCT 3..7; noise never. */
bool AicaModel::slot_fetch(int ch, bool en, bool keyed, uint32_t ca_old, uint32_t ca, uint16_t &word) const {
    const int f = fmt(ch);
    if (!en || f == 4) return false;
    const uint32_t sa = sa_addr(ch);
    uint32_t b;
    if (f == 0) b = sa + 2 * (ca + 1);
    else if (f == 1) b = sa + ca + 1;
    else {
        const uint32_t o4 = (chr(ch, 0x18) >> 11) & 15;
        if (o4 >= 3 && o4 <= 7) return false;
        auto wd = [&](uint32_t n) { return (sa + (n >> 1)) >> 1; };
        if (!keyed && wd(ca) == wd(ca_old) && wd(ca + 1) == wd(ca)) return false;
        b = sa + ((ca + 1) >> 1);
    }
    b &= (RAM_SIZE - 1) & ~1u;
    word = rd16(b);
    return true;
}

/* Slots 0..6 own DSP steps 114..126, inside the next sample's sweep: predict that fetch with this sample's
 * registers (a write between the two samples is missed, the one known difference to rtl/v1) */
void AicaModel::coll_preview(Frame &F, uint32_t mask) {
    for (int k = 0; k < 7; k++) {
        if (!((mask >> k) & 1)) continue;
        const bool en = slot[k].enabled, keyed = slot[k].keyed_fetch;
        F.pv[k].en = en;
        F.pv[k].keyed = keyed;
        F.pv[k].ca_old = slot[k].CA;
        F.pv[k].ca = keyed ? 0 : slot[k].CA;
        if (!en || keyed) continue;
        const Slot save = slot[k];
        const uint32_t lf = lfsr;
        lfsr = lfsr_adv(lf, k);   /* slots 0..k-1 step the LFSR before slot k does */
        stream_step(k, 0, (uint32_t)samples);
        F.pv[k].ca = slot[k].CA;
        slot[k] = save;
        lfsr = lf;
    }
}

/* CA walk of one sample. Decodes go to F->ops for the render pass (F null: no log). PCM and noise only
 * decode at the final position; ADPCM decodes at every one. */
void AicaModel::stream_step(int ch, Frame *F, uint32_t smp) {
    Slot &c = slot[ch];
    int f = fmt(ch);
    bool LPCTL = (chr(ch, 0x00) >> 9) & 1, LPSLNK = (chr(ch, 0x14) >> 14) & 1;
    /* PLFO scales the mantissa before the octave shift (tests/sgc_lfo, sgc_lfo2):
     * m' = m + floor(m * w' / 1024), w' = (w & ~1) >> (7 - PLFOS), inc = m' << (OCT + 4) */
    uint32_t r1c = chr(ch, 0x1C), plfos = (r1c >> 5) & 7, inc = c.update_rate;
    if (plfos) {
        if (((r1c >> 8) & 3) != 3) lfo_sync(ch, smp);   /* the LFO before sample smp's tick */
        int32_t w = ((r1c >> 8) & 3) == 3 ? (int32_t)(int8_t)((lfsr_plfo_j(lfsr) & 0xFF) ^ 0x80) : (int32_t)(int8_t)c.lfo.plfo;
        int32_t mod = (w & ~1) >> (7 - plfos);
        uint32_t OCT = (chr(ch, 0x18) >> 11) & 0xF, FNS = chr(ch, 0x18) & 0x3FF;
        int oct = (OCT & 8) ? (int)OCT - 16 : (int)OCT, sh = oct + 4;
        int32_t m0 = 1024 + (int32_t)FNS, m = m0 + ((m0 * mod) >> 10);
        inc = (uint32_t)(sh >= 0 ? m << sh : m >> -sh);
    }
    c.step += inc;
    uint32_t ip = c.step >> 14;
    c.step &= 0x3FFF;
    while (ip > 0) {
        ip--;
        uint32_t CA = (c.CA + 1) & 0xFFFF;
        uint32_t ca_t = CA;
        uint8_t op = OP_DECODE | (ip == 0 ? OP_LAST : 0);
        if (f == 3) ca_t &= ~3u;
        if (LPSLNK && c.AEG.state == EG_ATTACK && CA >= c.LSA) { set_aeg_state(ch, EG_DECAY1); c.env_skip = 0; }
        /* Loop end, armed once CA reached LSA (tests/sgc_loop): CA >= LEA -> CA -= LEA - LSA (16-bit).
         * LSA == LEA plays through; LEA < LSA jumps forward and triggers at LEA after the 0xFFFF wrap. */
        bool armed = c.in_loop;
        if (CA >= c.LSA) c.in_loop = true;
        if (!armed && CA == c.LSA && (f == 2 || f == 3)) op |= OP_LSA;
        if (ca_t >= c.LEA && armed) {
            c.looped = true;
            const uint32_t lea = f == 3 && stream_lsa ? (c.LEA + 3) & ~3u : c.LEA;   /* a stream's lap: see stream_lsa */
            CA = (uint32_t)((int32_t)CA - ((int32_t)lea - (int32_t)c.LSA)) & 0xFFFF;
            if (LPCTL) {
                if (f == 2) op |= OP_LOOP;
            } else {
                /* one-shot end (tests/oneshot): only the fetch stops, CA reads LEA now and 0 from the next sample;
                 * the envelope runs on (a key-on is ignored until a key-off) */
                c.CA = (c.CA + 1) & 0xFFFF;   /* the stepped CA, not wrapped */
                c.enabled = false;
                c.ca_clr = true;
                c.env_skip = 0;
                break;
            }
        }
        c.CA = CA;
        if (F && ((op & OP_LAST) || f == 2 || f == 3)) {
            F->ops[F->nops].ca = (uint16_t)CA;
            F->ops[F->nops].flags = op;
            F->nops++;
        }
    }
}

/* OPN envelope clocked every 2 samples (even MDEC_CT; tests/sgc_aeg, eg_lock). R < 48: every 2^(11 - R/4) clocks,
 * row R&3; R >= 48: every clock, rows 4.. (R 60-63 all +8). Rows 5, 9, 13 differ from the YM2612 (tests/eg_lock). */
static const uint8_t eg_inc[17][8] = {
    {0, 1, 0, 1, 0, 1, 0, 1}, {0, 1, 0, 1, 1, 1, 0, 1}, {0, 1, 1, 1, 0, 1, 1, 1}, {0, 1, 1, 1, 1, 1, 1, 1},
    {1, 1, 1, 1, 1, 1, 1, 1}, {1, 2, 1, 1, 1, 2, 1, 1}, {1, 2, 1, 2, 1, 2, 1, 2}, {1, 2, 2, 2, 1, 2, 2, 2},
    {2, 2, 2, 2, 2, 2, 2, 2}, {2, 4, 2, 2, 2, 4, 2, 2}, {2, 4, 2, 4, 2, 4, 2, 4}, {2, 4, 4, 4, 2, 4, 4, 4},
    {4, 4, 4, 4, 4, 4, 4, 4}, {4, 8, 4, 4, 4, 8, 4, 4}, {4, 8, 4, 8, 4, 8, 4, 8}, {4, 8, 8, 8, 4, 8, 8, 8},
    {8, 8, 8, 8, 8, 8, 8, 8}};
/* slow_off: R < 48 rows see the clock counter one step behind R >= 48 rows; AEG and FEG share it (tests/feg_track) */
static inline uint32_t eg_increment(uint32_t R, uint32_t cnt, uint32_t slow_off = 0) {
    if (R == 0) return 0;
    if (R < 48) {
        cnt += slow_off;
        uint32_t shift = 11 - (R >> 2);
        if (cnt & ((1u << shift) - 1)) return 0;
        return eg_inc[R & 3][(cnt >> shift) & 7];
    }
    return eg_inc[R >= 60 ? 16 : 4 + (R - 48)][cnt & 7];
}

/* Smallest non-zero step of rate R (rows of eg_inc) */
static inline uint32_t eg_min_inc(uint32_t R) { return R < 52 ? 1 : R < 56 ? 2 : R < 60 ? 4 : 8; }
static const uint8_t feg_tgt_reg[4] = {0x30, 0x34, 0x38, 0x3C};   /* FLV1..FLV4: the target of each FEG state */

/* No AEG clock can move the slot (no key event): off at 0x3FF, or a rate-0 segment the decay 1 -> 2 check keeps */
bool AicaModel::aeg_frozen(int ch) const {
    const Slot &c = slot[ch];
    if (c.AEG.off && c.AEG.a >= 0x3FF) return true;
    if (c.aeg_R[c.AEG.state]) return false;
    return !(c.AEG.state == EG_DECAY1 && (c.AEG.a >> 5) == ((chr(ch, 0x14) >> 5) & 0x1F));
}

/* No FEG clock can move the slot: never keyed, rate 0, at a bound, or decay 2 / release held within one smallest
 * step of the target (feg_clock's hold rule) */
bool AicaModel::feg_frozen(int ch) const {
    const Slot &c = slot[ch];
    if (c.FEG.passed) return false;
    if (c.FEG.dir == 0) return true;
    const uint32_t R = c.feg_R[c.FEG.state], v = c.FEG.v;
    if (R == 0 || v == (c.FEG.dir > 0 ? 0x1FFFu : 0u)) return true;
    if (c.FEG.state < EG_DECAY2) return false;
    const uint32_t t = chr(ch, feg_tgt_reg[c.FEG.state]) & 0x1FFF, mi = eg_min_inc(R);
    return c.FEG.dir > 0 ? (v < t && v + mi >= t) : (t > 0 && v >= t && v < t + mi);
}

/* Fetch stopped and both envelopes frozen: until a write or a key event every clock is a no-op.
 * The full model also needs vo latched and no collision claim, as nothing of the slot runs while skipped. */
bool AicaModel::slot_quiet(int ch, bool render) const {
    const Slot &c = slot[ch];
    if (c.enabled || c.keyed_fetch || c.ca_clr || c.keyed || c.keyed_off || c.stop_in) return false;
    if (render && (c.FEG.vo != c.FEG.v || coll_claim[ch])) return false;   /* a skipped slot latches nothing */
    return aeg_frozen(ch) && feg_frozen(ch);
}

/* Clocks a slow rate cannot step on: (eg_cnt - 1) & mask set makes eg_increment 0. R >= 44 can step on any clock */
static inline uint32_t eg_skip_mask(uint32_t R) { return R < 44 ? (1u << (11 - (R >> 2))) - 1 : 0; }

/* No key or stop flag: a clock with (eg_cnt - 1) & mask set moves neither envelope (0 = every clock). Writes, key
 * events, a one-shot end and an LPSLNK attack end (the only fetch effect) clear the mask. */
uint16_t AicaModel::env_skip_mask(int ch) const {
    const Slot &c = slot[ch];
    if (c.keyed_fetch || c.ca_clr || c.keyed || c.keyed_off || c.stop_in) return 0;
    const bool af = aeg_frozen(ch), ff = feg_frozen(ch);
    if (af && ff) return c.enabled ? 0x3FFF : 0;   /* idle: not yet quiet (render conditions), check again next clock */
    uint32_t m = 0x3FFF;
    if (!af) {
        if (c.AEG.state == EG_DECAY1 && (c.AEG.a >> 5) == ((chr(ch, 0x14) >> 5) & 0x1F)) return 0;   /* D1 -> D2 on any clock */
        m = eg_skip_mask(c.aeg_R[c.AEG.state]);
    }
    if (!ff) {
        if (c.FEG.passed) return 0;   /* the next clock changes the state */
        const uint32_t f = eg_skip_mask(c.feg_R[c.FEG.state]);
        if (f < m) m = f;
    }
    return (uint16_t)m;
}

void AicaModel::live_stats(uint32_t &live, uint32_t &en, uint32_t &aeg, uint32_t &feg) const {
    live = en = aeg = feg = 0;
    for (uint64_t m = ~quiet; m; m &= m - 1) {
        const int ch = __builtin_ctzll(m);
        const Slot &c = slot[ch];
        live++;
        if (c.enabled || c.keyed_fetch || c.ca_clr || c.keyed || c.keyed_off || c.stop_in) en++;
        else if (!aeg_frozen(ch)) aeg++;
        else if (!feg_frozen(ch)) feg++;
    }
}

/* The slot stop: fetch stops, monitor reads 0x1FFF, CA reads 0 (tests/slot_tail, ca_stop). No second stage at 0x400. */
static inline void slot_stop(Slot &c) { c.enabled = false; c.AEG.off = true; c.CA = 0; }

void AicaModel::aeg_clock(int ch) {
    Slot &c = slot[ch];
    if (c.AEG.off && c.AEG.a >= 0x3FF) return;   /* the value keeps stepping to 0x3FF after the stop (tests/slot_tail tail_b) */
    uint16_t r14 = chr(ch, 0x14);   /* live: a rewrite acts on its effect sample's clock (tests/eg_latch) */
    if (c.keyed) {
        /* no step on a key-on sample, but an R 63 attack (a = 0) leaves attack on this clock (tests/eg_kprobe p5-p7) */
        if (c.AEG.state == EG_ATTACK && c.AEG.a == 0 && !((r14 >> 14) & 1)) set_aeg_state(ch, EG_DECAY1);
        return;
    }
    /* key-off on a clock: from attack no step at all; from decay 1/2 one more step of that segment (tests/aeg_koff) */
    if (c.keyed_off && c.aeg_prev == EG_ATTACK) return;
    /* decay 1 -> 2: the decay 1 step first, then a >> 5 == DL switches, on every decay 1 clock (tests/aeg_dl0) */
    bool was_d1 = c.AEG.state == EG_DECAY1;
    EgState rs = c.keyed_off ? c.aeg_prev : c.AEG.state;   /* key-off sample: the previous segment's rate */
    uint32_t inc = eg_increment(c.aeg_R[rs], eg_cnt, (uint32_t)-1);
    if (!inc) {
        if (was_d1 && (c.AEG.a >> 5) == ((r14 >> 5) & 0x1F)) set_aeg_state(ch, EG_DECAY2);
        return;
    }
    int32_t a = c.AEG.a;
    if (c.AEG.state == EG_ATTACK) {
        a += ((~a) * (int32_t)inc) >> 4;
        if (a <= 0) {
            a = 0;
            if (!((r14 >> 14) & 1)) set_aeg_state(ch, EG_DECAY1);
        }
    } else {
        a += inc;
        /* At 0x3C0 arm the stop for the next sample (this one still outputs its fetch); the value saturates at
         * 0x3FF and the state is kept (tests/slot_tail, ca_stop, sgc_keys K4) */
        if (a >= CAIQUE_STOP_A && !c.AEG.off && !c.stop_in) {
            if (CAIQUE_STOP_LAG) { c.stop_in = CAIQUE_STOP_LAG; stops_pending++; }
            else slot_stop(c);
        }
        if (a > 0x3FF) a = 0x3FF;
    }
    c.AEG.a = (uint16_t)a;
    /* EQUAL, not >=: an LPSLNK decay 1 entered above DL << 5 keeps D1R (tests/sgc_loop lo_2); also on the stop clock (ca_stop) */
    if (was_d1 && (a >> 5) == ((r14 >> 5) & 0x1F)) set_aeg_state(ch, EG_DECAY2);
}

/* FEG: FLV0 -> FLV1..3, key-off -> FLV4, AEG clock and tables. C = (v >= target) sets the direction; attack and
 * decay 1 step until C flips, decay 2 and release hold short of it (tests/feg_probe, feg_track, feg_krs). */
void AicaModel::feg_clock(int ch) {
    Slot &c = slot[ch];
    /* keeps stepping after the fetch stop and after AEG off (tests/slot_tail tail_c) */
    if (c.keyed) return;
    /* Pure no-op cases, skipped: never keyed (dir 0), or a release that no step size can move (held, or at a bound) */
    if (!c.keyed_off) {
        if (c.FEG.dir == 0 && !c.FEG.passed) return;
        if (c.FEG.state == EG_RELEASE) {
            const uint32_t v = c.FEG.v, target = chr(ch, 0x3C) & 0x1FFF;
            if (c.FEG.dir > 0 ? (v == 0x1FFF || v + 1 == target) : (v == 0 || v == target)) return;
        }
    }
    if (c.FEG.passed && c.FEG.state < EG_DECAY2) {
        c.FEG.state = (EgState)(c.FEG.state + 1);
        c.FEG.dir = c.FEG.v >= (chr(ch, feg_tgt_reg[c.FEG.state]) & 0x1FFF) ? -1 : 1;
        c.FEG.passed = false;
    }
    /* key-off sample: the old segment's rate and direction, or the next segment's when the old one had passed
     * (tests/feg_koffdir, feg_koffpass) */
    EgState seg = c.keyed_off ? c.feg_prev : c.FEG.state;
    int32_t dir = c.keyed_off ? c.feg_prev_dir : c.FEG.dir;
    if (c.keyed_off && c.feg_prev_passed && seg < EG_DECAY2) {
        seg = (EgState)(seg + 1);
        dir = c.FEG.v >= (chr(ch, feg_tgt_reg[seg]) & 0x1FFF) ? -1 : 1;
    }
    int32_t target = chr(ch, feg_tgt_reg[c.FEG.state]) & 0x1FFF;   /* the target and the hold rule are the release's */
    uint32_t inc = eg_increment(c.feg_R[seg], eg_cnt, (uint32_t)-1);   /* live rates (tests/eg_latch feg_rate) */
    if (!inc || c.FEG.passed) return;
    bool C = c.FEG.v >= target;
    int32_t nv = (int32_t)c.FEG.v + dir * (int32_t)inc;
    nv = nv < 0 ? 0 : nv > 0x1FFF ? 0x1FFF : nv;
    if (c.FEG.state >= EG_DECAY2) {
        if ((nv >= target) == C) c.FEG.v = (uint16_t)nv;   /* hold short of the target */
    } else {
        c.FEG.v = (uint16_t)nv;
        if ((nv >= target) != C) c.FEG.passed = true;
    }
}

/* SVF, two integrators in 1/8 units; damping rounded UP to 1/4 before the cutoff multiply, band rounds down, low
 * up (gives the small limit cycles). FLV bit 0 unused, 0x1FFE+ is unity. Verified: filt_cyc/id2/imp/edges. */
static const uint8_t lpf_q128[32] = {192, 176, 160, 144, 128, 120, 112, 104, 96, 88, 80, 72, 64, 60, 56, 52,
                                     48, 44, 40, 36, 32, 30, 28, 26, 24, 22, 20, 18, 16, 15, 14, 13};
static inline int64_t ceil_shr(int64_t v, int sh) { return -((-v) >> sh); }

/* lpf_step in 32 bits (the SPU has no 64-bit multiply); exact when |L|, |B| < 2^28 and |x8| < 2^20, else false.
 * Products are split at 2^9 (sh >= 9): floor(k*h / 2^sh) = floor(floor(k*h / 512) / 2^(sh-9)), all terms under 2^29. */
static inline void lpf_ksh(uint32_t v, int32_t &k, int &sh) {   /* cutoff multiplier and shift (less 9) of FEG value v */
    const int32_t m = -(int32_t)(v >= 0x1FFE);
    k = (512 & m) | ((256 + (int32_t)((v >> 1) & 0xFF)) & ~m);
    sh = 15 - (int)(v >> 9);
}
static inline bool lpf32k(int32_t &L, int32_t &B, int32_t k, int sh, int32_t qd, int32_t x8) {
    if (__builtin_expect((((uint32_t)L + (1u << 28)) | ((uint32_t)B + (1u << 28))) >= (1u << 29), 0)) return false;
    const int32_t d1 = mul32x16(B >> 8, qd) + ((mul16(qd, B & 0xFF) + 255) >> 8);   /* ceil(qd * B / 256) */
    int32_t high = x8 - L - 2 * d1;
    high = high < -8388608 ? -8388608 : high > 8388607 ? 8388607 : high;
    const int32_t band = B + ((mul16(k, high >> 9) + (mul16(k, high & 511) >> 9)) >> sh);
    const int32_t nb = -band;   /* ceil(k * band / 2^sh) = -floor(k * -band / 2^sh) */
    L = L - ((mul32x16(nb >> 9, k) + (mul16(k, nb & 511) >> 9)) >> sh);
    B = band;
    return true;
}
static inline bool lpf32(int32_t &L, int32_t &B, uint32_t v, uint32_t q, int32_t x8) {
    int32_t k;
    int sh;
    lpf_ksh(v, k, sh);
    return lpf32k(L, B, k, sh, lpf_q128[q], x8);
}

__attribute__((noinline)) static void lpf64(int32_t &L, int32_t &B, uint32_t v, uint32_t q, int32_t x8) {
    int64_t k = v >= 0x1FFE ? 512 : 256 + ((v >> 1) & 0xFF);
    int sh = 24 - (int)(v >> 9);
    int64_t damping = 2 * ceil_shr((int64_t)lpf_q128[q] * B, 8);
    /* the high-pass difference saturates to signed 24 bits BEFORE the cutoff multiply (tests/filt_overflow) */
    int64_t high = (int64_t)x8 - L - damping;
    high = high < -8388608 ? -8388608 : high > 8388607 ? 8388607 : high;
    int64_t band = B + ((k * high) >> sh);
    int64_t low = L + ceil_shr(k * band, sh);
    B = (int32_t)band;
    L = (int32_t)low;
}

int32_t AicaModel::lpf_step(int ch, uint32_t v, int32_t x8) {
    Slot &c = slot[ch];
    const uint8_t q = chr(ch, 0x28) & 0x1F;
    if (x8 == 0 && c.lpf_fix && c.fix_v == v && c.fix_q == q) return (int32_t)-(int64_t)c.lpf_low;
    int32_t L = c.lpf_low, B = c.lpf_band;
    if (!lpf32(L, B, v, q, x8)) lpf64(L, B, v, q, x8);
    c.lpf_fix = x8 == 0 && B == c.lpf_band && L == c.lpf_low;
    c.fix_v = (uint16_t)v;
    c.fix_q = q;
    c.lpf_band = B;
    c.lpf_low = L;
    return (int32_t)(0u - (uint32_t)L);
}

/* V = floor(sample * (127 - a[5:0]) / 2^(7 + (a >> 6))), a in 0.09375 dB (tests/sgc_level). AEG + ALFO sum unmeasured.
 * 32-bit: |sample| < 2^21, so the product stays under 2^28 (the SPU has no 64-bit multiply). */
static inline int32_t att_apply(int32_t sample, uint32_t a) {
    if (a >= 0x3FF) a = 0x3FF;
    int32_t M = 127 - (int32_t)(a & 63), k = (int32_t)(a >> 6);
    return mul32x16(sample, M) >> (7 + k);
}
/* IMXL (DISDL assumed the same): n = 15 - L, x 2^-(n>>1) x (n odd ? 3/4 : 1), one floor; L 0 is off */
static inline int32_t send_level(int32_t v16, uint32_t L) {   /* every input is under 2^27: 32 bits suffice */
    if (L == 0) return 0;
    uint32_t n = 15 - L;
    int32_t x = v16 * ((n & 1) ? 3 : 4);
    return x >> (2 + (n >> 1));
}

/* Control half of a slot's output point: the LFSR step and the values render needs (the LFO ticks lazily) */
void AicaModel::slot_control(int ch, SlotFrame &sf) {
    Slot &c = slot[ch];
    c.FEG.vo = c.FEG.v;
    /* one global 17-bit LFSR, s[n] = s[n-12] ^ s[n-17], one step per slot (tests/sgc_formats); seed unknown (1) */
    uint32_t nb = ((lfsr >> 0) ^ (lfsr >> 5)) & 1;
    lfsr = (lfsr >> 1) | (nb << 16);
    sf.lfsr = lfsr;
    sf.step = c.step;
    sf.en_out = c.enabled;
    sf.fv = c.FEG.v;
    sf.aeg_a = c.AEG.a;
    sf.aeg_off = c.AEG.off;
}

void AicaModel::slot_render(int ch, const SlotFrame &sf, uint32_t smp, int32_t &l, int32_t &r, int32_t &d) {
    Slot &c = slot[ch];
    /* a stopped slot feeds zero but its filter runs on; a noise slot outputs the LFSR byte even unkeyed (fm_1) */
    const bool noise = (chr(ch, 0x00) >> 10) & 1;
    if (noise) c.s0 = c.s1 = (int32_t)(int8_t)(sf.lfsr & 0xFF) << 8;
    /* interpolation uses the top 6 phase bits, keeps 4 fraction bits (tests/sgc_pitch) */
    int32_t frac6 = (int32_t)(sf.step >> 8) & 63;
    int32_t s16 = (sf.en_out || noise) ? c.s0 * 16 + (((c.s1 - c.s0) * frac6) >> 2) : 0;
    uint16_t r24 = chr(ch, 0x24), r28 = chr(ch, 0x28), r20 = chr(ch, 0x20);
    uint32_t TL = (r28 >> 8) & 0xFF;
    /* filter input is floor(s16 / 2) in 1/8 units (tests/filt_id, filt_frac) */
    if (!((r28 >> 5) & 1)) s16 = clampi(lpf_step(ch, sf.fv, s16 >> 1) * 2, -524288, 524287);
    int32_t V16;
    if ((r28 >> 6) & 1) V16 = s16; /* VOFF: fractional signal through, never muted (tests/slot_tail tail_a) */
    else if (CAIQUE_MUTE == 1 ? sf.aeg_off : CAIQUE_MUTE == 2 ? !sf.en_out : false) V16 = 0;   /* controls only */
    else {
        /* "off" is not muted either: a = 0x3FF keeps applying to the filter tail (tests/slot_tail tail_b).
         * ALFO attenuation = (w & 0xFE) >> (7 - ALFOS); noise = the LFSR byte at this slot (tests/sgc_lfo) */
        uint32_t r1c = chr(ch, 0x1C), ws = (r1c >> 3) & 3, alfos = r1c & 7;
        if (alfos && ws != 3) lfo_sync(ch, smp);   /* the waveform before this sample's tick */
        uint32_t w = ws == 3 ? (lfsr_alfo_j(sf.lfsr) & 0xFF) : c.lfo.alfo_w;
        uint32_t alfo = alfos ? ((w & 0xFE) >> (7 - alfos)) : 0;
        V16 = (att_apply(s16, TL * 4 + sf.aeg_a + alfo) >> 4) * 16;
    }
    int32_t V = V16 >> 4;
    d = send_level(V16, (r20 >> 4) & 0xF);
    /* direct out (not observable digitally): DISDL level, DIPAN 3 dB per step on one side (minicast), 0xF = off */
    int32_t dir = send_level(V, (r24 >> 8) & 0xF);
    uint32_t pan = r24 & 0x1F;
    int32_t side = (pan & 0xF) == 0xF ? 0 : send_level(dir, 15 - (pan & 0xF));
    if (pan & 0x10) { l = dir; r = side; }
    else { l = side; r = dir; }
}

/* floor(X * Y / 4096) in 32 bits for |X| < 2^23, |Y| < 2^13: X = xh * 4096 + xl, xl unsigned (no SPU 64-bit multiply) */
static inline int32_t mul_shr12(int32_t X, int32_t Y) { return (X >> 12) * Y + (((X & 0xFFF) * Y) >> 12); }

/* DSP (measured semantics: tests/dsp_basic; see NOTES "DSP") */
void AicaModel::dsp_step(uint64_t smp, uint32_t mdec) {
    /* All-NOP program, no read in flight: every step only sets ACC from TEMP[MDEC_CT] and FRC_REG; nothing is stored */
    if (mpro_nz == 0 && mem_qn == 0) {
        const int32_t t = TEMP[mdec & 0x7F];
        ACC = sext(mul_shr12(t, sext(FRC_REG, 13)) + t, 26);
        nofl_pipe[0] = nofl_pipe[1] = 0;
        mrd_even_masa = -1;
        return;
    }
    uint16_t rbpl = r(0x2804);
    uint32_t RBP = (rbpl & 0xFFF) * 2048u;
    uint32_t RBL = (8192u << ((rbpl >> 13) & 3)) - 1;
    for (int s = 0; s < 128; s++) {
        uint64_t gstep = smp * 128 + s;
        const dsp_op_t &in = prog[s];

        for (int q = 0; q < mem_qn;) { /* reads landing at this step */
            if ((uint64_t)mem_q[q].land == gstep) {
                memval = mem_q[q].val;
                mem_q[q] = mem_q[--mem_qn];
            } else {
                q++;
            }
        }

        int32_t INPUTS;
        if (in.IRA <= 0x1F) INPUTS = MEMS[in.IRA];
        else if (in.IRA <= 0x2F) INPUTS = MIXS_bank[(smp - 1) & 1][in.IRA - 0x20] << 4;   /* the bank of the previous sample */
        else if (in.IRA <= 0x31) INPUTS = EXTS[in.IRA - 0x30] << 8;
        else INPUTS = 0;
        INPUTS = sext(INPUTS, 24);

        if (in.IWT) MEMS[in.IWA] = nofl_pipe[1] ? sext((int32_t)memval << 8, 24) : dsp_unpack(memval);

        int32_t SHIFTED;
        switch (in.SHIFT) {
        case 0: SHIFTED = clampi(ACC, -0x800000, 0x7FFFFF); break;
        case 1: SHIFTED = clampi(ACC * 2, -0x800000, 0x7FFFFF); break;
        case 2: SHIFTED = sext(ACC * 2, 24); break;
        default: SHIFTED = sext(ACC, 24); break;
        }

        int32_t tempr = TEMP[(in.TRA + mdec) & 0x7F];
        int32_t B = in.ZERO ? 0 : in.BSEL ? ACC : tempr;
        if (in.NEGB) B = -B;
        int32_t X = in.XSEL ? INPUTS : tempr;
        int32_t Y;
        switch (in.YSEL) {
        case 0: Y = sext(FRC_REG, 13); break;
        case 1: Y = sext(r(0x3000 + 4 * s) >> 3, 13); break;
        case 2: Y = sext(Y_REG >> 11, 13); break;
        default: Y = (Y_REG >> 4) & 0xFFF; break;
        }
        if (in.YRL) Y_REG = INPUTS;
        ACC = sext(mul_shr12(X, Y) + B, 26);

        if (in.TWT) TEMP[(in.TWA + mdec) & 0x7F] = SHIFTED;
        if (in.FRCL) FRC_REG = in.SHIFT == 3 ? (SHIFTED & 0xFFF) : ((SHIFTED >> 11) & 0x1FFF);

        /* MWT writes at its own step; odd MRD lands 2 steps later, even MRD takes the next odd slot unless it reads (dsp_mem*).
         * A playing channel owns its even step's slot: MWT drops, MRD gets coll_word (tests/dsp_coll). */
        const int ck = ((s + 14) >> 1) & 63;
        const bool coll = !(s & 1) && coll_claim[ck];
        const dsp_op_t *rq = 0;
        bool rq_coll = false;
        if (in.MRD && !in.MWT && (s & 1)) rq = &in;
        else if (!in.MRD && (s & 1) && mrd_even_masa >= 0) { rq = &mrd_even; rq_coll = mrd_even_coll; }
        if (s & 1) mrd_even_masa = -1;
        if (in.MRD && !in.MWT && !(s & 1)) {
            mrd_even = in; mrd_even_masa = in.MASA; mrd_even_coll = coll; mrd_even_word = coll_word[ck];
        }
        for (int pass = 0; pass < 2; pass++) {
            const dsp_op_t *m = pass == 0 ? (in.MWT && !coll ? &in : 0) : rq;
            if (!m) continue;
            if (pass == 1 && rq_coll) {
                if (mem_qn < 4) { mem_q[mem_qn].land = (int64_t)(gstep + 2); mem_q[mem_qn].val = mrd_even_word; mem_qn++; }
                continue;
            }
            uint32_t ADDR = r(0x3200 + 4 * m->MASA);
            if (m->ADREB) ADDR += (uint32_t)sext((int32_t)ADRS_REG, 12);
            if (m->NXADR) ADDR++;
            if (!m->TABLE) ADDR = (ADDR + mdec) & RBL;
            else ADDR &= 0xFFFF;
            uint32_t ba = ((ADDR << 1) + RBP) & (RAM_SIZE - 1);
            if (pass == 0) {
                uint16_t v = in.NOFL ? (uint16_t)(SHIFTED >> 8) : dsp_pack(SHIFTED);
#ifdef CAIQUE_RAM_HOOKS
                caique_dsp_wr16(ba, RBP, RBL, v);
#else
                ram[ba] = (uint8_t)v;
                ram[ba + 1] = (uint8_t)(v >> 8);
#endif
            } else if (mem_qn < 4) {
                mem_q[mem_qn].land = (int64_t)(gstep + 2);
#ifdef CAIQUE_RAM_HOOKS
                mem_q[mem_qn].val = caique_dsp_rd16(ba, RBP, RBL);
#else
                mem_q[mem_qn].val = (uint16_t)(ram[ba] | (ram[ba + 1] << 8));
#endif
                mem_qn++;
            }
        }
        if (in.ADRL) ADRS_REG = in.SHIFT == 3 ? ((SHIFTED >> 12) & 0xFFF) : ((uint32_t)(INPUTS >> 16) & 0xFFF);
        if (in.EWT) EFREG[in.EWA] = (int16_t)(SHIFTED >> 8);

        nofl_pipe[1] = nofl_pipe[0];
        nofl_pipe[0] = (uint8_t)in.NOFL;
    }
}

/* DAC-side mixing (EFSDL/EFPAN, MVOL, MONO, DAC18B) is not observable digitally: send-level law, unverified */
static void volpan(int32_t value, uint32_t vol, uint32_t pan, int32_t &outl, int32_t &outr) {
    int32_t temp = send_level(value, vol);
    int32_t Sc = (pan & 0xF) == 0xF ? 0 : send_level(temp, 15 - (pan & 0xF));
    if (pan & 0x10) { outl += temp; outr += Sc; }
    else { outl += Sc; outr += temp; }
}

/* Slot k of sample n fetches and outputs with the envelope from its frame of n-1, then computes the envelope for
 * n+1; so a register write between two steps is a write at the hardware's sample boundary (NOTES "Session 7"). */
void AicaModel::run(bool render) {
    Frame &F = fr;
    F.sample = samples;
    F.mdec = MDEC_CT;
    int32_t mixl = 0, mixr = 0;
    uint32_t rendered = 0;
    if (render) { CAIQUE_PROF_AT(0, 0); memset(MIXS, 0, sizeof MIXS); }
    {   /* the one-sample interval edge and the timers (cycle-model: at ph 24 of the sample; here at its start) */
        uint16_t set = 0x400;
        for (int t = 0; t < 3; t++)
            if (((MDEC_CT - tim_phase) & ((1u << irq.pre[t]) - 1)) == 0 && ++irq.cnt[t] == 0) set |= (uint16_t)(0x40 << t);
        irq.scipd |= set;
        irq.mcipd |= set;
    }
    /* (1) the CA walk and the output point of sample n, in slot order. A slot's render pass reads only its own
     * state and its SlotFrame, so it runs right after its control pass. */
    const uint32_t l0 = lfsr;   /* slot ch sees the LFSR stepped ch times */
    if (!render) {   /* control only: no vo latch (read by nothing), the LFSR jumps at the end */
        for (uint64_t m = fetching; m; m &= m - 1) {
            const int ch = __builtin_ctzll(m);
            Slot &c = slot[ch];
            if (!(c.ca_clr | c.enabled | c.keyed_fetch)) { fetching &= ~(1ull << ch); continue; }   /* each flag needs a key_on, which sets the bit */
            if (c.ca_clr) { c.ca_clr = false; c.CA = 0; }
            if (c.enabled && !c.keyed_fetch) {
                if ((chr(ch, 0x1C) & 0x3E0) > 0x300) lfsr = lfsr_adv(l0, ch);   /* read only by a noise PLFO (PLFOWS 3, PLFOS > 0) */
                stream_step(ch, 0, (uint32_t)F.sample);
            }
            c.keyed_fetch = false;
        }
        lfsr = lfsr_adv(l0, 64);
    }
    for (uint64_t m = render ? ~(quiet & rsilent) : 0; m; m &= m - 1) {
        const int ch = __builtin_ctzll(m);
        Slot &c = slot[ch];
        SlotFrame sf;
        lfsr = lfsr_adv(l0, ch);
        if (c.ca_clr) { c.ca_clr = false; c.CA = 0; }   /* the sample after a one-shot end */
        const bool en = c.enabled, keyed = c.keyed_fetch;
        sf.en_fetch = en;
        sf.keyed = keyed;
        sf.ca_old = c.CA;
        F.nops = 0;
        /* the key-on sample fetches CA 0 without advancing (tests/sgc_pitch, sgc_loop, dsp_coll) */
        if (en && !keyed) stream_step(ch, render ? &F : 0, (uint32_t)F.sample);
        else if (keyed && render) {
            F.ops[0].ca = 0;
            F.ops[0].flags = OP_RESET | (en ? OP_DECODE | OP_LAST : 0);
            F.nops = 1;
        }
        c.keyed_fetch = false;
        sf.ca = c.CA;
        slot_control(ch, sf);
        if (!render) continue;
        for (uint32_t i = 0; i < F.nops; i++) {
            const RenderOp &o = F.ops[i];
            if (o.flags & (OP_RESET | OP_LSA | OP_LOOP)) adpcm_op(ch, o.flags);
            if (o.flags & OP_DECODE) decode_sample(ch, (o.flags & OP_LAST) != 0, o.ca);
        }
        if (ch >= 7 && !no_dsp) coll_claim[ch] = slot_fetch(ch, sf.en_fetch, sf.keyed, sf.ca_old, sf.ca, coll_word[ch]);
        /* silent: zero input into LPOFF or a filter resting at 0 gives V16 = 0, so every output of the slot is 0 */
        const uint16_t r28 = chr(ch, 0x28);
        if (!sf.en_out && !((chr(ch, 0x00) >> 10) & 1) &&
            (((r28 >> 5) & 1) || (c.lpf_fix && c.lpf_low == 0 && c.fix_v == sf.fv && c.fix_q == (r28 & 0x1F)))) {
            rsilent |= 1ull << ch;
            continue;
        }
        rsilent &= ~(1ull << ch);
        rendered++;
        int32_t l, rr, d;
        slot_render(ch, sf, (uint32_t)F.sample, l, rr, d);
        mixl += l;
        mixr += rr;
        MIXS[chr(ch, 0x20) & 0xF] += d;   /* IMXL a pure gain (tests/mixs_write); DSP INPUTS = MIXS << 4 */
    }
    if (render) { lfsr = lfsr_adv(l0, 64); CAIQUE_PROF_AT(1, rendered); }
    /* (2) a stop armed by the previous envelope pass takes effect after this sample's output (tests/slot_tail) */
    for (int ch = 0; stops_pending && ch < 64; ch++) {
        if (slot[ch].stop_in && --slot[ch].stop_in == 0) { slot_stop(slot[ch]); stops_pending--; }
    }
    /* (3) key events and the envelope for sample n+1, which takes no envelope step on a key event (tests/eg_lock keys).
     * Clock and counter are sample n+1's: MDEC_CT(n) - 2 even (tests/eg_lock, feg_track, aeg_dl0). */
    bool kx = kyonex_pending;
    kyonex_pending = false;
    uint32_t md = (MDEC_CT - 2) & 0xFFFF;
    bool clk = (md & 1) == eg_par;
    eg_cnt = (eg_K - (md >> 1)) & 0x3FFF;
    const bool clear = keys_live;   /* keyed / keyed_off last only one sample */
    keys_live = false;
    /* only non-quiet slots on a clock, all if keys move; with neither, no flag is set and the pass is a no-op */
    const uint64_t visit = (kx || clear) ? ~0ull : clk ? ~quiet : 0;
    for (uint64_t m = visit; m; m &= m - 1) {
        const int ch = __builtin_ctzll(m);
        if (clear) slot[ch].keyed = slot[ch].keyed_off = false;
        if (kx) {
            bool on = (chr(ch, 0x00) & 0x4000) != 0;
            if (on && slot[ch].AEG.state == EG_RELEASE) { key_on(ch); slot[ch].keyed = true; slot[ch].keyed_fetch = true; keys_live = true; }
            if (!on && slot[ch].AEG.state != EG_RELEASE) { key_off(ch); slot[ch].keyed_off = true; keys_live = true; }
        }
        if (clk && ((eg_cnt - 1) & slot[ch].env_skip) == 0) {
            if (!(slot[ch].AEG.off && slot[ch].AEG.a >= 0x3FF)) aeg_clock(ch);   /* aeg_clock's own first test */
            if (slot[ch].FEG.dir || slot[ch].FEG.passed || slot[ch].keyed_off) feg_clock(ch);   /* else a no-op */
            if (slot_quiet(ch, render)) quiet |= 1ull << ch;
            else slot[ch].env_skip = env_skip_mask(ch);
        }
        if (slot[ch].keyed || slot[ch].keyed_off) quiet &= ~(1ull << ch);
    }
    MDEC_CT = (MDEC_CT - 1) & 0xFFFF;   /* free-running 16-bit sample counter (DSP ring, envelope clock) */
    samples++;
    if (!render) return;
    CAIQUE_PROF_AT(2, 0);
    if (!no_dsp) coll_preview(F);   /* F.pv only feeds the collision words below */
    /* a bus no slot points at keeps its value; SGC of sample n fills bank n&1, the DSP of n+1 reads it (tests/eg_lock mixs) */
    for (int i = 0; i < 16; i++) {
        if (isel_cnt[i]) MIXS_bank[F.sample & 1][i] = sext(MIXS[i], 20); /* the 20-bit sum wraps, no saturation (tests/sgc_mix) */
        MIXS[i] = MIXS_bank[F.sample & 1][i];
    }
    for (int i = 0; i < 2; i++) {
        uint16_t v = r(0x2040 + 4 * i);
        volpan(EXTS[i], (v >> 8) & 0xF, v & 0x1F, mixl, mixr);
    }
    for (int k = 0; k < 7 && !no_dsp; k++)
        coll_claim[k] = slot_fetch(k, F.pv[k].en, F.pv[k].keyed, F.pv[k].ca_old, F.pv[k].ca, coll_word[k]);
    CAIQUE_PROF_AT(3, 0);
    if (!no_dsp) {
#ifdef CAIQUE_DSP_HOOK
        if (!caique_dsp_hook(this, F.sample, F.mdec))
#endif
            dsp_step(F.sample, F.mdec);
    }
    CAIQUE_PROF_AT(4, 0);
    for (int i = 0; i < 16 && !no_dsp; i++) {
        uint16_t v = r(0x2000 + 4 * i);
        volpan(EFREG[i], (v >> 8) & 0xF, v & 0x1F, mixl, mixr);
    }
    uint16_t c0 = r(0x2800);
    if (c0 & 0x8000) { mixl += mixr; if (mono_half) mixl >>= 1; mixr = mixl; }
    mixl = send_level(mixl, c0 & 0xF);
    mixr = send_level(mixr, c0 & 0xF);
    if (c0 & 0x100) { mixl >>= 2; mixr >>= 2; }
    if (out_gain) { mixl = (mixl * (int32_t)out_gain) >> 8; mixr = (mixr * (int32_t)out_gain) >> 8; }
    outL = (int16_t)clampi(mixl, -32768, 32767);
    outR = (int16_t)clampi(mixr, -32768, 32767);
    CAIQUE_PROF_AT(5, 0);
}

/* 5.13 A2 high voices: a slot's sample needs only its own state plus per-sample globals (LFSR, envelope clock, KYONEX)
 * and run()'s cross-slot parts are sums, counts or no-ops, so advance() runs each slot through a whole round. */
#if CAIQUE_MUTE != 0 || CAIQUE_STOP_LAG != 1
#error "advance() assumes the measured stop rule (CAIQUE_MUTE 0, CAIQUE_STOP_LAG 1)"
#endif

static inline int ctz64(uint64_t m) {   /* clz-based: the SPU has no ctz and __builtin_ctzll is a library call */
    const uint32_t lo = (uint32_t)m, hi = (uint32_t)(m >> 32);
    return lo ? 31 - __builtin_clz(lo & (0u - lo)) : 63 - __builtin_clz(hi & (0u - hi));
}

/* One sample of LFSR steps (64) as a GF(2) map, a nibble at a time: t[k][v] = the image of v << 4k. Built at compile
 * time, so the SPU needs no constructor. */
struct Lf64Tab { uint32_t t[5][16]; };
static constexpr Lf64Tab lf64_make() {
    Lf64Tab r{};
    for (int k = 0; k < 5; k++)
        for (uint32_t v = 0; v < 16; v++) {
            uint32_t l = (v << (4 * k)) & 0x1FFFF;
            for (int s = 0; s < 64; s++) l = (l >> 1) | ((((l >> 0) ^ (l >> 5)) & 1) << 16);
            r.t[k][v] = l;
        }
    return r;
}
static constexpr Lf64Tab LF64 = lf64_make();
static inline uint32_t lfsr_adv64(uint32_t l) {
    return LF64.t[0][l & 15] ^ LF64.t[1][(l >> 4) & 15] ^ LF64.t[2][(l >> 8) & 15] ^ LF64.t[3][(l >> 12) & 15] ^
           LF64.t[4][(l >> 16) & 1];
}

uint32_t AicaModel::lfsr_at(const AdvCtx &x, uint32_t i) {
    if (!adv_l0_ok) {
        uint32_t l = x.l00;
        for (uint32_t k = 0; k < x.n; k++) { adv_l0[k] = l; l = lfsr_adv64(l); }
        adv_l0_ok = true;
    }
    return adv_l0[i];
}

void AicaModel::final_mix(int32_t mixl, int32_t mixr) {
    for (int i = 0; i < 2; i++) {   /* level 0 adds 0 to both sides: skipped */
        uint16_t v = r(0x2040 + 4 * i);
        if (v & 0xF00) volpan(EXTS[i], (v >> 8) & 0xF, v & 0x1F, mixl, mixr);
    }
    for (int i = 0; i < 16 && !no_dsp; i++) {
        uint16_t v = r(0x2000 + 4 * i);
        if (v & 0xF00) volpan(EFREG[i], (v >> 8) & 0xF, v & 0x1F, mixl, mixr);
    }
    uint16_t c0 = r(0x2800);
    if (c0 & 0x8000) { mixl += mixr; if (mono_half) mixl >>= 1; mixr = mixl; }
    mixl = send_level(mixl, c0 & 0xF);
    mixr = send_level(mixr, c0 & 0xF);
    if (c0 & 0x100) { mixl >>= 2; mixr >>= 2; }
    const uint32_t al = (uint32_t)(mixl < 0 ? -mixl : mixl), ar = (uint32_t)(mixr < 0 ? -mixr : mixr);
    if (al > peak) peak = al;
    if (ar > peak) peak = ar;
    if (out_gain) { mixl = (mixl * (int32_t)out_gain) >> 8; mixr = (mixr * (int32_t)out_gain) >> 8; }
    clip[0] += (uint32_t)(mixl + 32768) > 65535u;
    clip[1] += (uint32_t)(mixr + 32768) > 65535u;
    outL = (int16_t)clampi(mixl, -32768, 32767);
    outR = (int16_t)clampi(mixr, -32768, 32767);
}

/* Plan 6.6: the sample positions slot ch plays from `back` samples before CA to `fwd` after it, up to 3 spans */
int AicaModel::hz_spans(int ch, uint32_t back, uint32_t fwd, HzSpan *s) const {
    const Slot &c = slot[ch];
    if (!c.enabled || fmt(ch) == 4) return 0;
    uint32_t rate = (c.update_rate >> 4) + 1;   /* CA steps per sample in 1/1024, rounded up */
    if ((chr(ch, 0x1C) >> 5) & 7) rate += (rate >> 3) + 1;   /* PLFO: 1/8 more at most */
    auto steps = [rate](uint32_t n) {
        const uint32_t v = (((n > 4095 ? 4095 : n) * rate) >> 10) + 2;
        return v > 0x10000 ? 0x10000u : v;
    };
    /* an ADPCM stream tests the loop end on CA & ~3 (stream_step): up to 3 positions past LEA */
    const uint32_t s3 = fmt(ch) == 3 ? 4 : 0, db = steps(back) + s3, df = steps(fwd) + s3;
    const uint32_t P = c.CA, LSA = c.LSA, LEA = s3 ? (c.LEA + 3) & ~3u : c.LEA;
    const bool lp = (chr(ch, 0x00) >> 9) & 1;   /* LPCTL */
    uint32_t lo = P > db ? P - db : 0, hi = P + df;
    int n = 1;
    if (c.LEA <= LSA || P > LEA) {   /* no plain 0..LEA walk (tests/sgc_loop) */
        if (lp) { lo = 0; hi = 0xFFFF; }   /* the loop end can move CA at every step: any position */
        else {   /* CA can wrap at 0xFFFF, either way */
            if (hi > 0xFFFF) { s[n].lo = 0; s[n].hi = hi - 0x10000; s[n].d = (int32_t)P - (int32_t)hi; hi = 0xFFFF; n++; }
            if (P < db) { s[n].lo = 0x10000 - (db - P); s[n].hi = 0xFFFF; s[n].d = (int32_t)P + 1; n++; }
        }
    } else if (lp) {
        const uint32_t len = LEA - LSA;
        bool all = false;
        if (hi > LEA) {   /* past the loop end: the lap goes on from LSA */
            const uint32_t over = hi - LEA;
            hi = LEA;
            if (over >= len) all = true;
            else { s[n].lo = LSA; s[n].hi = LSA + over; s[n].d = -(int32_t)(LEA - P + over); n++; }
        }
        if (c.in_loop && P >= LSA && P - LSA < db) {   /* before the loop start: the end of the lap before */
            const uint32_t rem = db - (P - LSA);
            if (rem >= len) all = true;
            else { s[n].lo = LEA - rem; s[n].hi = LEA; s[n].d = (int32_t)(P - LSA); n++; }
        }
        if (all) { if (lo > LSA) lo = LSA; hi = LEA; n = 1; }
    } else if (hi > LEA) hi = LEA;   /* one-shot: the fetch stops at LEA */
    s[0].lo = lo;
    s[0].hi = hi;
    s[0].d = (int32_t)P - (int32_t)hi;
    return n;
}

/* The bytes (not masked) that hold positions lo..hi + 1 of a slot at sa: PCM16, PCM8, ADPCM nibbles */
static inline void hz_bytes(int f, uint32_t sa, uint32_t lo, uint32_t hi, uint32_t &b0, uint32_t &b1) {
    if (f == 0) { b0 = sa + 2 * lo; b1 = sa + 2 * hi + 3; }
    else if (f == 1) { b0 = sa + lo; b1 = sa + hi + 1; }
    else { b0 = sa + (lo >> 1); b1 = sa + ((hi + 1) >> 1); }
}

void AicaModel::hz_mark(uint8_t *map, uint32_t back, uint32_t fwd) const {
    for (int ch = 0; ch < 64; ch++) {
        HzSpan s[3];
        const int n = hz_spans(ch, back, fwd, s);
        const int f = fmt(ch);
        const uint32_t sa = sa_addr(ch);
        for (int k = 0; k < n; k++) {
            uint32_t b0, b1;
            hz_bytes(f, sa, s[k].lo, s[k].hi, b0, b1);
            uint32_t p = b0 >> 8, cnt = (b1 >> 8) - p + 1;
            if (cnt > 8192) cnt = 8192;
            for (; cnt; cnt--, p++) map[(p & 8191) >> 3] |= (uint8_t)(0x80 >> (p & 7));
        }
    }
}

int32_t AicaModel::hz_hit(uint32_t a, uint32_t back) const {
    int32_t best = -1;
    for (int ch = 0; ch < 64; ch++) {
        HzSpan s[3];
        const int n = hz_spans(ch, back, 0, s);
        const int f = fmt(ch);
        const uint32_t sa = sa_addr(ch);
        for (int k = 0; k < n; k++) {
            uint32_t b0, b1;
            hz_bytes(f, sa, s[k].lo, s[k].hi, b0, b1);
            const uint32_t o = (a - b0) & (RAM_SIZE - 1);
            if (o > b1 - b0) continue;
            const uint32_t x = s[k].lo + (f == 0 ? o >> 1 : f == 1 ? o : o << 1);
            int32_t d = s[k].d + (int32_t)(s[k].hi - x);
            if (d < 0) d = 0;
            if (best < 0 || d < best) best = d;
        }
    }
    return best;
}

bool AicaModel::ring_range(uint32_t &base, uint32_t &bytes) const {
    const uint16_t rbpl = r(0x2804);
    base = ((rbpl & 0xFFF) * 2048u) & (RAM_SIZE - 1);
    bytes = 16384u << ((rbpl >> 13) & 3);
    return mpro_nz != 0;
}

void AicaModel::advance(uint32_t n, bool render, int32_t *mix) {
    const uint32_t cap = render && !no_dsp ? DSP_ROUND : ADV_MAX;   /* A3: the rows hold one round */
    while (n) {
        const uint32_t k = n < cap ? n : cap;
        advance_round(k, render, mix);
        if (render) mix += 4 * k;
        n -= k;
    }
}

void AicaModel::advance_round(uint32_t n, bool render, int32_t *mix) {
    AdvCtx x;
    x.t0 = (uint32_t)samples;
    x.mdec0 = MDEC_CT;
    x.n = n;
    x.kx = kyonex_pending;   /* writes come between calls, so KYONEX can only act on the first sample */
    x.l00 = lfsr;
    kyonex_pending = false;
    adv_l0_ok = false;
    uint32_t rendered = 0;
    if (render) { CAIQUE_PROF_AT(0, 0); memset(mix, 0, n * 4 * sizeof(int32_t)); }
    const bool dsp = render && !no_dsp;
    if (dsp) {   /* 5.13 A3: for dsp_sample(), which reads the rows and clears them */
        adv_s0 = samples;
        adv_mdec0 = MDEC_CT;
        for (int b = 0; b < 16; b++) adv_busw[b] = isel_cnt[b] ? -1 : 0;
    }
    {   /* run()'s timers in closed form: timer t counts on samples i == (MDEC_CT - tim_phase) mod 2^pre; the bit on a wrap */
        uint16_t set = 0x400;
        for (int t = 0; t < 3; t++) {
            const uint32_t d0 = (x.mdec0 - tim_phase) & ((1u << irq.pre[t]) - 1);
            if (d0 < n) {
                const uint32_t ticks = 1 + ((n - 1 - d0) >> irq.pre[t]);
                if (irq.cnt[t] + ticks >= 256) set |= (uint16_t)(0x40 << t);
                irq.cnt[t] = (uint8_t)(irq.cnt[t] + ticks);
            }
        }
        irq.scipd |= set;
        irq.mcipd |= set;
    }
    /* a slot outside this set has no flag and nothing to fetch, render or clock until a write or a key event */
    const uint64_t todo = x.kx ? ~0ull : render ? ~(quiet & rsilent) : (fetching | ~quiet);
    bool keys = false;
    for (uint64_t m = todo; m; m &= m - 1) {
        const int ch = ctz64(m);
        slot_run(ch, x, render, mix, rendered);
        keys |= slot[ch].keyed || slot[ch].keyed_off;
    }
    keys_live = keys;
    const uint32_t md = (x.mdec0 - (n - 1) - 2) & 0xFFFF;   /* the last sample's envelope counter */
    eg_cnt = (eg_K - (md >> 1)) & 0x3FFF;
    MDEC_CT = (x.mdec0 - n) & 0xFFFF;
    samples += n;
    uint32_t l = x.l00;
    for (uint32_t i = 0; i < n; i++) l = lfsr_adv64(l);
    lfsr = l;
    if (render) CAIQUE_PROF_AT(1, rendered);
    /* slots 0..6 claim DSP steps 114..126 of the last sample with their next fetch: run()'s preview, at the same point */
    const uint32_t pm = dsp ? (uint32_t)coll_need & 0x7F : 0;
    if (pm) {
        coll_preview(fr, pm);
        for (int k = 0; k < 7; k++) {
            uint16_t w;
            if (((pm >> k) & 1) && slot_fetch(k, fr.pv[k].en, fr.pv[k].keyed, fr.pv[k].ca_old, fr.pv[k].ca, w)) {
                adv_cc[n - 1][k] = true;
                adv_cw[n - 1][k] = w;
            }
        }
    }
}

void AicaModel::slot_run(int ch, const AdvCtx &x, bool render, int32_t *mix, uint32_t &rendered) {
    Slot &c = slot[ch];
    const uint64_t bit = 1ull << ch;
    const uint16_t r0 = chr(ch, 0x00), r1c = chr(ch, 0x1C);
    /* the LFSR users go the generic way: noise, noise PLFO (PLFOWS 3, PLFOS > 0), noise ALFO (ALFOWS 3, ALFOS > 0) */
    const bool lfsr_use = ((r0 >> 10) & 1) || (r1c & 0x3E0) > 0x300 || (render && ((r1c >> 3) & 3) == 3 && (r1c & 7));
    for (uint32_t i = 0; i < x.n;) {
        const bool kx0 = x.kx && i == 0;
        if (!kx0 && (render ? (quiet & rsilent & bit) != 0 : (quiet & bit) != 0)) break;   /* nothing moves until a write */
        if (!kx0 && !lfsr_use && !(c.keyed_fetch | c.ca_clr | c.keyed | c.keyed_off) && !c.stop_in)
#ifdef CAIQUE_ADV_RENDER_ONLY   /* SPU: advance() always renders, the control twin runs on the PPU (saves LS) */
            i = no_dsp ? slot_fast<true>(ch, i, x, mix, rendered) : slot_fast_dsp(ch, i, x, mix, rendered);
#else
            i = !render ? slot_ctl_fast(ch, i, x, rendered)
                : no_dsp ? slot_fast<true>(ch, i, x, mix, rendered) : slot_fast_dsp(ch, i, x, mix, rendered);
#endif
        else
            slot_sample(ch, i++, x, render, mix, rendered);
    }
    if (!render && !(c.ca_clr | c.enabled | c.keyed_fetch)) fetching &= ~bit;
}

/* One sample of one slot in run()'s order: walk + output, a pending stop, key events and the envelope */
void AicaModel::slot_sample(int ch, uint32_t i, const AdvCtx &x, bool render, int32_t *mix, uint32_t &rendered) {
    Slot &c = slot[ch];
    const uint64_t bit = 1ull << ch;
    const uint32_t t = x.t0 + i, mdec = (x.mdec0 - i) & 0xFFFF;
    const bool kx = x.kx && i == 0;
    if (render ? !(quiet & rsilent & bit) : (c.ca_clr | c.enabled | c.keyed_fetch)) {
        const uint16_t r1c = chr(ch, 0x1C);
        if (((chr(ch, 0x00) >> 10) & 1) || (r1c & 0x3E0) > 0x300 || ((r1c >> 3) & 3) == 3)
            lfsr = lfsr_adv(lfsr_at(x, i), ch);   /* only noise reads it; slot_control steps it once more */
        if (c.ca_clr) { c.ca_clr = false; c.CA = 0; }
        const bool en = c.enabled, keyed = c.keyed_fetch;
        if (!render) {
            if (en && !keyed) stream_step(ch, 0, t);
            c.keyed_fetch = false;
        } else {
            Frame &F = fr;
            SlotFrame sf;
            sf.en_fetch = en;
            sf.keyed = keyed;
            sf.ca_old = c.CA;
            F.nops = 0;
            if (en && !keyed) stream_step(ch, &F, t);
            else if (keyed) {
                F.ops[0].ca = 0;
                F.ops[0].flags = OP_RESET | (en ? OP_DECODE | OP_LAST : 0);
                F.nops = 1;
            }
            c.keyed_fetch = false;
            sf.ca = c.CA;
            slot_control(ch, sf);
            for (uint32_t k = 0; k < F.nops; k++) {
                const RenderOp &o = F.ops[k];
                if (o.flags & (OP_RESET | OP_LSA | OP_LOOP)) adpcm_op(ch, o.flags);
                if (o.flags & OP_DECODE) decode_sample(ch, (o.flags & OP_LAST) != 0, o.ca);
            }
            if (!no_dsp) {   /* 5.13 A3: run()'s fetch claim; for slots 0..6 it is the previous sample's */
                bool cl = false;
                uint16_t w;
                if ((coll_need >> ch) & 1) {
                    cl = slot_fetch(ch, sf.en_fetch, sf.keyed, sf.ca_old, sf.ca, w);
                    if (cl && (ch >= 7 || i > 0)) { adv_cc[i - (ch < 7)][ch] = true; adv_cw[i - (ch < 7)][ch] = w; }
                }
                coll_claim[ch] = cl;   /* slot_quiet() reads it, as in run() */
            }
            const uint16_t r28 = chr(ch, 0x28);
            if (!sf.en_out && !((chr(ch, 0x00) >> 10) & 1) &&
                (((r28 >> 5) & 1) || (c.lpf_fix && c.lpf_low == 0 && c.fix_v == sf.fv && c.fix_q == (r28 & 0x1F)))) {
                rsilent |= bit;
            } else {
                rsilent &= ~bit;
                rendered++;
                int32_t l, rr, d;
                slot_render(ch, sf, t, l, rr, d);
                mix[4 * i] += l;
                mix[4 * i + 1] += rr;
                if (!no_dsp) adv_bus[i][chr(ch, 0x20) & 0xF] += d;
            }
        }
    }
    if (c.stop_in && --c.stop_in == 0) { slot_stop(c); stops_pending--; }
    /* a quiet slot is clocked only on KYONEX samples, where its clocks are no-ops unless the key event moved it */
    const bool q0 = (quiet & bit) != 0;
    if (c.keyed || c.keyed_off) c.keyed = c.keyed_off = false;   /* each lasts one sample */
    if (kx) {
        const bool on = (chr(ch, 0x00) & 0x4000) != 0;
        if (on && c.AEG.state == EG_RELEASE) { key_on(ch); c.keyed = true; c.keyed_fetch = true; }
        if (!on && c.AEG.state != EG_RELEASE) { key_off(ch); c.keyed_off = true; }
    }
    const uint32_t md = (mdec - 2) & 0xFFFF;
    if ((md & 1) == eg_par && (kx || !q0)) {
        eg_cnt = (eg_K - (md >> 1)) & 0x3FFF;
        if (((eg_cnt - 1) & c.env_skip) == 0) {
            if (!(c.AEG.off && c.AEG.a >= 0x3FF)) aeg_clock(ch);
            if (c.FEG.dir || c.FEG.passed || c.keyed_off) feg_clock(ch);
            if (slot_quiet(ch, render)) quiet |= bit;
            else c.env_skip = env_skip_mask(ch);
        }
    }
    if (c.keyed || c.keyed_off) quiet &= ~bit;
}

#ifdef CAIQUE_RAM_HOOKS
#define CAIQUE_LINE(a) caique_line(a)
#else
#define CAIQUE_LINE(a) (ram + ((a) & ~127u))
#endif

/* send_level() with the level taken apart once per slot, branch-free per sample (SPU: no branch prediction) */
struct SendLv { int32_t odd, on; uint32_t sh; };
static inline SendLv send_lv(uint32_t L) {
    const uint32_t n = 15 - L;
    SendLv s;
    s.odd = (n & 1) ? -1 : 0;
    s.on = L ? -1 : 0;
    s.sh = 2 + (n >> 1);
    return s;
}
static inline int32_t send_apply(int32_t v, const SendLv &s) { return ((v * 4 - (v & s.odd)) >> s.sh) & s.on; }

typedef int32_t mix4 __attribute__((vector_size(16), may_alias));

/* slot_sample() over [i, n) for a slot with no key/stop flag, noise or KYONEX, state in locals (R: render into mix).
 * Returns after a one-shot end, an armed stop, or once the slot is quiet (render: and render-silent). */
template <bool R>
uint32_t AicaModel::slot_fast(int ch, uint32_t i, const AdvCtx &x, int32_t *mix, uint32_t &rendered) {
    Slot &c = slot[ch];
    const uint64_t bit = 1ull << ch;
    const uint16_t r0 = chr(ch, 0x00), r14 = chr(ch, 0x14), r18 = chr(ch, 0x18), r1c = chr(ch, 0x1C);
    const int f = (r0 >> 7) & 3;
    const bool LPCTL = (r0 >> 9) & 1, LPSLNK = (r14 >> 14) & 1;
    const uint32_t LSA = c.LSA, LEA = c.LEA, sa = sa_addr(ch), plfos = (r1c >> 5) & 7;
    const int32_t m0 = 1024 + (int32_t)(r18 & 0x3FF);
    const int osh = (((r18 >> 11) & 8) ? (int)((r18 >> 11) & 0xF) - 16 : (int)((r18 >> 11) & 0xF)) + 4;
    const uint16_t r24 = R ? chr(ch, 0x24) : 0, r28 = R ? chr(ch, 0x28) : 0;
    const bool lpoff = (r28 >> 5) & 1, voff = (r28 >> 6) & 1;
    const uint32_t q = r28 & 0x1F, tl4 = ((r28 >> 8) & 0xFF) * 4, alfos = r1c & 7;
    const SendLv dlv = send_lv((r24 >> 8) & 0xF), slv = send_lv(15 - (r24 & 0xF));   /* DIPAN 0xF: side level 0 */
    const int32_t pm = (r24 & 0x10) ? -1 : 0;   /* direct to the left, side to the right */
    bool en = c.enabled, in_loop = c.in_loop, fix = c.lpf_fix, out = false;
    bool qb = (quiet & bit) != 0, rs = (rsilent & bit) != 0;
    uint32_t CA = c.CA, step = c.step, fix_v = c.fix_v, fix_q = c.fix_q, vo = c.FEG.vo, rn = 0;
    int32_t s0 = c.s0, s1 = c.s1, quant = c.adpcm_quant, L = c.lpf_low, B = c.lpf_band;
    const int32_t qd = lpf_q128[q];
    uint32_t kv = 0x10000;   /* the FEG value kk, ksh were made for */
    int32_t kk = 0;
    int ksh = 0;
    /* loop invariants, and the envelope fields only a clock or an LPSLNK attack end changes */
    const uint32_t n = x.n, t0 = x.t0, mdec0 = x.mdec0, eK = eg_K, ep = eg_par, urate = c.update_rate;
    const bool lfo_on = plfos || (R && alfos);   /* PLFO, or ALFO when rendering (never the noise waveforms here) */
    uint32_t aeg_a = c.AEG.a, fev = c.FEG.v, es = c.env_skip;
    EgState ast = c.AEG.state;
    uint32_t lb = 1;   /* base of the wave RAM line in lp; 1 = none */
    const uint8_t *lp = 0;
    auto rd8 = [&](uint32_t a) -> uint32_t {
        if (__builtin_expect((a & ~127u) != lb, 0)) { lp = CAIQUE_LINE(a); lb = a & ~127u; }
        return lp[(a & 127) ^ CAIQUE_SWZ];
    };
    auto decode = [&](uint32_t ca, bool last) {   /* decode_sample() for formats 0..3 */
        if (__builtin_expect(f >= 2, 1)) {
            const uint32_t sf = (ca & 1) * 4;
            int32_t qq = quant;
            s0 = decode_adpcm((rd8((sa + (ca >> 1)) & (RAM_SIZE - 1)) >> sf) & 0xF, s0, qq);
            quant = qq;
            /* the next-sample preview; s1 is 0 after a step that is not the last (decoded anyway: no branch) */
            s1 = decode_adpcm((rd8((sa + ((ca + 1) >> 1)) & (RAM_SIZE - 1)) >> (4 - sf)) & 0xF, s0, qq) & -(int32_t)last;
        } else if (f == 0) {
            const uint32_t a0 = (sa + 2 * ca) & (RAM_SIZE - 2), a1 = (sa + 2 * ca + 2) & (RAM_SIZE - 2);
            s0 = (int16_t)(rd8(a0) | rd8(a0 + 1) << 8);
            s1 = (int16_t)(rd8(a1) | rd8(a1 + 1) << 8);
        } else {
            s0 = (int8_t)rd8((sa + ca) & (RAM_SIZE - 1)) * 256;
            s1 = (int8_t)rd8((sa + ca + 1) & (RAM_SIZE - 1)) * 256;
        }
    };
    for (; i < n && !out; i++) {
        const uint32_t t = t0 + i;
        if (__builtin_expect(lfo_on, 0)) lfo_tick(ch, t);   /* the LFO before this sample's tick, for PLFO and ALFO */
        if (__builtin_expect(en, 1)) {   /* stream_step() */
            uint32_t inc = urate;
            if (__builtin_expect(plfos != 0, 0)) {   /* PLFOWS 0..2 here */
                const int32_t w = (int8_t)c.lfo.plfo, mod = (w & ~1) >> (7 - plfos), m = m0 + ((m0 * mod) >> 10);
                inc = (uint32_t)(osh >= 0 ? m << osh : m >> -osh);
            }
            step += inc;
            uint32_t ip = step >> 14;
            step &= 0x3FFF;
            if (__builtin_expect(ip != 0, 1)) {
                /* the first CA where the loop logic acts (<= 0xFFFF: no wrap); selects, no branches */
                const int32_t il = -(int32_t)in_loop;
                uint32_t ev = (LEA & il) | (LSA & ~il);
                const int32_t ls = -(int32_t)(LPSLNK & (ast == EG_ATTACK) & (LSA < ev));
                ev = (LSA & ls) | (ev & ~ls);
                const bool clean = CA + ip < ev;
                /* PCM and the control pass only need the last position */
                const uint32_t sk = (ip - 1) & -(uint32_t)(clean & !(R && f >= 2));
                CA += sk;
                ip -= sk;
                do {
                    ip--;
                    uint32_t ca = (CA + 1) & 0xFFFF;
                    bool rst = false;
                    if (__builtin_expect(!clean, 0)) {
                        const uint32_t ca_t = f == 3 ? ca & ~3u : ca;
                        if (LPSLNK && ast == EG_ATTACK && ca >= LSA) { ast = c.AEG.state = EG_DECAY1; es = c.env_skip = 0; }
                        const bool armed = in_loop;
                        if (ca >= LSA) in_loop = true;
                        if (R && f >= 2 && !armed && ca == LSA)   /* adpcm_op()'s OP_LSA */
                            lsa_state[ch] = (uint32_t)quant << 16 | ((uint32_t)s0 & 0xFFFF);
                        if (ca_t >= LEA && armed) {
                            c.looped = true;
                            const uint32_t lea = f == 3 && stream_lsa ? (LEA + 3) & ~3u : LEA;   /* as stream_step() */
                            ca = (uint32_t)((int32_t)ca - ((int32_t)lea - (int32_t)LSA)) & 0xFFFF;
                            if (LPCTL) rst = f == 2;
                            else {   /* one-shot end: CA reads LEA now, 0 from the next sample */
                                CA = (CA + 1) & 0xFFFF;
                                en = c.enabled = false;
                                c.ca_clr = true;
                                es = c.env_skip = 0;
                                out = true;
                                break;
                            }
                        }
                    }
                    CA = ca;
                    if (R && __builtin_expect((f >= 2) | (ip == 0), 1)) {
                        if (__builtin_expect(rst, 0)) {   /* adpcm_op()'s OP_LOOP */
                            const uint32_t v = adpcm_restore ? lsa_state[ch] : LSA_KEYON;
                            quant = (int32_t)(v >> 16);
                            s0 = (int16_t)v;
                        }
                        decode(CA, ip == 0);
                    }
                } while (__builtin_expect(ip != 0, 0));
            }
        }
        if (R) {   /* slot_control() + slot_render(), no noise, no DSP send */
            const uint32_t fv = fev;
            vo = fv;
            rs = (!en) & (lpoff | (fix & (L == 0) & (fix_v == fv) & (fix_q == q)));
            if (__builtin_expect(!rs, 1)) {
                rn++;
                int32_t s16 = en ? s0 * 16 + (mul32x16(s1 - s0, (step >> 8) & 63) >> 2) : 0;
                if (__builtin_expect(!lpoff, 1)) {
                    const int32_t x8 = s16 >> 1;
                    if (__builtin_expect(!((x8 == 0) & fix & (fix_v == fv) & (fix_q == q)), 1)) {
                        const int32_t L0 = L, B0 = B;
                        if (__builtin_expect(fv != kv, 0)) { lpf_ksh(fv, kk, ksh); kv = fv; }
                        if (!lpf32k(L, B, kk, ksh, qd, x8)) lpf64(L, B, fv, q, x8);
                        fix = (x8 == 0) & (B == B0) & (L == L0);
                        fix_v = fv;
                        fix_q = q;
                    }
                    s16 = clampi((int32_t)((0u - (uint32_t)L) << 1), -524288, 524287);
                }
                int32_t V16 = s16;
                if (__builtin_expect(!voff, 1)) {
                    uint32_t alfo = 0;
                    if (__builtin_expect(alfos != 0, 0))   /* ALFOWS 0..2 here; lfo_tick() synced it */
                        alfo = (c.lfo.alfo_w & 0xFEu) >> (7 - alfos);
                    V16 = att_apply(s16, tl4 + aeg_a + alfo) & ~15;
                }
                const int32_t dir = send_apply(V16 >> 4, dlv), side = send_apply(dir, slv);
                /* lanes 2, 3 feed slot_fast_dsp() (A3); in a row shared by all slots they are never read */
                const mix4 lr = {(dir & pm) | (side & ~pm), (side & pm) | (dir & ~pm), V16, (int32_t)CA};
                *(mix4 *)(mix + 4 * i) += lr;
            }
        }
        const uint32_t md = (mdec0 - i - 2) & 0xFFFF;   /* the envelope for the next sample */
        const uint32_t ec = (eK - (md >> 1)) & 0x3FFF;
        if (__builtin_expect(((md & 1) == ep) & !qb & (((ec - 1) & es) == 0), 0)) {
            eg_cnt = ec;
            if (!(c.AEG.off && c.AEG.a >= 0x3FF)) aeg_clock(ch);
            if (c.FEG.dir || c.FEG.passed) feg_clock(ch);
            if (R) c.FEG.vo = (uint16_t)vo;   /* slot_quiet() reads it */
            if (slot_quiet(ch, R)) { quiet |= bit; qb = true; }
            else c.env_skip = env_skip_mask(ch);
            if (c.stop_in) out = true;
            aeg_a = c.AEG.a;
            fev = c.FEG.v;
            ast = c.AEG.state;
            es = c.env_skip;
        }
        if (__builtin_expect(qb & (!R | rs), 0)) out = true;   /* quiet (render: and silent): static */
    }
    c.CA = CA;
    c.step = step;
    c.in_loop = in_loop;
    if (R) {
        rsilent = rs ? rsilent | bit : rsilent & ~bit;
        rendered += rn;
        c.s0 = s0;
        c.s1 = s1;
        c.adpcm_quant = quant;
        c.lpf_low = L;
        c.lpf_band = B;
        c.lpf_fix = fix;
        c.fix_v = (uint16_t)fix_v;
        c.fix_q = (uint8_t)fix_q;
        c.FEG.vo = (uint16_t)vo;
    }
    return i;
}

/* 5.13 A3: slot_fast<true> into adv_row, then the slot's DSP inputs: its bus send and, if the program reads it, its fetch
 * claim. Inside slot_fast, slot_quiet() reaches the claim test only after a sample with no fetch: run()'s claim is 0. */
uint32_t AicaModel::slot_fast_dsp(int ch, uint32_t i0, const AdvCtx &x, int32_t *mix, uint32_t &rendered) {
    const Slot &c = slot[ch];
    const bool en0 = c.enabled;   /* only a one-shot end clears it, and that ends slot_fast */
    uint32_t ca_prev = c.CA;
    coll_claim[ch] = false;
    const uint32_t i1 = slot_fast<true>(ch, i0, x, &adv_row[0][0], rendered);
    if (en0 && ((coll_need >> ch) & 1) && i1 > i0) {
        /* slot_fetch(ch, true, false, ca_old, ca) inline: never noise here; ADPCM fetches on a word change, none at OCT 3..7.
         * Every sample rendered, so its row has its CA, but a one-shot end's last, whose CA slot_fast left in c.CA. */
        if (c.ca_clr) adv_row[i1 - 1][3] = (int32_t)c.CA;
        const int f = (chr(ch, 0x00) >> 7) & 3;
        const uint32_t sa = sa_addr(ch), fs = f == 0 ? 0 : f == 1 ? 1 : 2;   /* fetch byte = sa + (2 (CA + 1) >> fs) */
        const bool adpcm = f >= 2, adp_nf = ((chr(ch, 0x18) >> 11) & 15) - 3u <= 4u;
        uint32_t lb = 1;   /* base of the wave RAM line in lp; 1 = none */
        const uint8_t *lp = 0;
        uint32_t i = i0;
        if (ch < 7 && i == 0) { ca_prev = (uint32_t)adv_row[0][3]; i = 1; }   /* sample 0: the last round's preview */
        for (const int r0 = ch < 7 ? -1 : 0; i < i1; i++) {
            const uint32_t ca = (uint32_t)adv_row[i][3], w1 = (sa + (ca >> 1)) >> 1;
            const bool same = w1 == (sa + (ca_prev >> 1)) >> 1 && (sa + ((ca + 1) >> 1)) >> 1 == w1;
            ca_prev = ca;
            if (__builtin_expect((uint32_t)!adpcm | ((uint32_t)!adp_nf & (uint32_t)!same), 1)) {
                const uint32_t b = (sa + ((2 * (ca + 1)) >> fs)) & (RAM_SIZE - 2);
                if ((b & ~127u) != lb) { lp = CAIQUE_LINE(b); lb = b & ~127u; }
                adv_cc[i + r0][ch] = true;
                adv_cw[i + r0][ch] = (uint16_t)(lp[(b & 127) ^ CAIQUE_SWZ] | lp[((b & 127) + 1) ^ CAIQUE_SWZ] << 8);
            }
        }
    }
    /* the l, r lanes into the round's mix and the bus send (IMXL 0 sends 0); the rows left zero */
    const uint16_t r20 = chr(ch, 0x20);
    const SendLv ilv = send_lv((r20 >> 4) & 0xF);
    int32_t *const busp = &adv_bus[0][r20 & 0xF];
    const mix4 lr_mask = {-1, -1, 0, 0}, zero = {0, 0, 0, 0};
    for (uint32_t i = i0; i < i1; i++) {
        const mix4 row = *(const mix4 *)adv_row[i];   /* a sample with no render left zeros: V16 0, send 0 */
        *(mix4 *)adv_row[i] = zero;
        *(mix4 *)(mix + 4 * i) += row & lr_mask;
        busp[16 * i] += send_apply(row[2], ilv);
    }
    return i1;
}

typedef uint32_t umix4 __attribute__((vector_size(16), may_alias));

/* run()'s part between the slots and the output stage: bus sums into MIXS_bank, the claims, the DSP */
void AicaModel::dsp_sample(uint32_t i) {
    const uint64_t smp = adv_s0 + i;
    const mix4 zero = {0, 0, 0, 0};
    mix4 *bus = (mix4 *)adv_bus[i], *bank = (mix4 *)MIXS_bank[smp & 1];
    const mix4 *bw = (const mix4 *)adv_busw;
    for (int k = 0; k < 4; k++) {   /* the 20-bit sums wrap (tests/sgc_mix); a bus no slot points at keeps its value */
        const mix4 v = (mix4)((umix4)bus[k] << 12) >> 12;
        bank[k] = (v & bw[k]) | (bank[k] & ~bw[k]);
        bus[k] = zero;
    }
    if (coll_need) {
        mix4 *cc = (mix4 *)adv_cc[i];
        const mix4 *cw = (const mix4 *)adv_cw[i];
        for (int k = 0; k < 4; k++) { ((mix4 *)coll_claim)[k] = cc[k]; cc[k] = zero; }
        for (int k = 0; k < 8; k++) ((mix4 *)coll_word)[k] = cw[k];
    }
    CAIQUE_PROF_AT(3, 0);
    const uint32_t mdec = (adv_mdec0 - i) & 0xFFFF;
#ifdef CAIQUE_DSP_HOOK
    if (!caique_dsp_hook(this, smp, mdec))
#endif
        dsp_step(smp, mdec);
    CAIQUE_PROF_AT(4, 0);
}

#ifndef CAIQUE_ADV_RENDER_ONLY
/* slot_fast<false> without PLFO: the walk in closed form to the next clock the env_skip mask (2^m - 1) lets through,
 * as eg_cnt advances 1 per clock. A loop event in reach (LSA, LEA, LPSLNK) leaves the round to slot_fast<false>. */
uint32_t AicaModel::slot_ctl_fast(int ch, uint32_t i, const AdvCtx &x, uint32_t &rendered) {
    Slot &c = slot[ch];
    const uint64_t bit = 1ull << ch;
    if ((chr(ch, 0x1C) >> 5) & 7) return slot_fast<false>(ch, i, x, 0, rendered);   /* PLFO: the step changes per sample */
    const bool LPSLNK = (chr(ch, 0x14) >> 14) & 1;
    const uint32_t n = x.n;
    while (i < n) {
        if (quiet & bit) return i;
        /* the first sample j >= i whose envelope pass clocks: parity of MDEC_CT, then eg_cnt = 1 mod (env_skip + 1) */
        const uint32_t j0 = i + ((x.mdec0 + eg_par - i) & 1);
        const uint32_t ec0 = (eg_K - (((x.mdec0 - j0 - 2) & 0xFFFF) >> 1)) & 0x3FFF;
        const uint32_t j = j0 + 2 * ((1u - ec0) & c.env_skip);
        const uint32_t last = j < n ? j : n - 1;   /* walk samples i..last */
        if (c.enabled) {
            const uint32_t tot = c.step + (last - i + 1) * c.update_rate;   /* < 2^29: 64 x (2047 << 11) + 2^14 */
            uint32_t ev = c.in_loop ? c.LEA : c.LSA;
            if (LPSLNK && c.AEG.state == EG_ATTACK && c.LSA < ev) ev = c.LSA;
            if (c.CA + (tot >> 14) >= ev) return slot_fast<false>(ch, i, x, 0, rendered);
            c.CA += tot >> 14;
            c.step = tot & 0x3FFF;
        }
        if (j >= n) return n;
        eg_cnt = (eg_K - (((x.mdec0 - j - 2) & 0xFFFF) >> 1)) & 0x3FFF;   /* sample j's envelope pass */
        if (!(c.AEG.off && c.AEG.a >= 0x3FF)) aeg_clock(ch);
        if (c.FEG.dir || c.FEG.passed) feg_clock(ch);
        if (slot_quiet(ch, false)) quiet |= bit;
        else c.env_skip = env_skip_mask(ch);
        i = j + 1;
        if (c.stop_in) return i;   /* the stop is the generic path's */
    }
    return i;
}
#endif

void AicaModel::snap(CtlSnap &o) {
    for (int ch = 0; ch < 64; ch++) lfo_sync(ch, (uint32_t)samples);
    o.sample = (uint32_t)samples;
    o.lfsr = lfsr;
    o.mdec = MDEC_CT;
    o.eg_cnt = eg_cnt;
    for (int ch = 0; ch < 64; ch++) {
        const Slot &c = slot[ch];
        SlotSnap &s = o.s[ch];
        s.CA = c.CA;
        s.step = c.step;
        s.a = c.AEG.a;
        s.fv = c.FEG.v;
        s.aeg = (uint8_t)c.AEG.state;
        s.feg = (uint8_t)c.FEG.state;
        s.flags = (uint8_t)(c.enabled | (c.AEG.off << 1) | (c.keyed_fetch << 2) | ((c.stop_in != 0) << 3) |
                            (c.in_loop << 4) | (c.ca_clr << 5) | (c.FEG.passed << 6) | ((c.FEG.dir < 0) << 7));
        s.lfo = c.lfo.state;
        s.lfo_cnt = c.lfo.counter;
    }
}

uint32_t AicaModel::snap_hash(const CtlSnap &o) {
    const uint32_t *w = (const uint32_t *)&o;
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < sizeof(CtlSnap) / 4; i++) h = (h ^ w[i]) * 16777619u;
    return h;
}

/* Register interface */
void AicaModel::write(uint32_t off, uint32_t val) {
    off &= 0x7FFC;
    uint16_t v = (uint16_t)val;
    if (off < 0x2000) {
        int ch = off >> 7;
        uint32_t o = off & 0x7F;
        uint16_t m = chan_mask(o);
        if (o == 0x20) { isel_cnt[reg[off >> 2] & 0xF]--; isel_cnt[v & 0xF]++; }
        reg[off >> 2] = (uint16_t)((v & m) | (o == 0 ? (v & 0x8000) : 0));
        quiet &= ~(1ull << ch);
        slot[ch].env_skip = 0;
        slot_regwrite(ch, o);
        return;
    }
    if (off >= 0x3000 && off < 0x3200) { reg[off >> 2] = v & 0xFFF8; return; }
    if (off >= 0x4000 && off < 0x4400) {
        int i = (off - 0x4000) >> 3;
        if (off & 4) TEMP[i] = sext((int32_t)((v << 8) | (TEMP[i] & 0xFF)), 24);
        else TEMP[i] = (TEMP[i] & ~0xFF) | (v & 0xFF);
        return;
    }
    if (off >= 0x4400 && off < 0x4500) {
        int i = (off - 0x4400) >> 3;
        if (off & 4) MEMS[i] = sext((int32_t)((v << 8) | (MEMS[i] & 0xFF)), 24); /* low byte not CPU-writable */
        return;
    }
    if (off >= 0x4500 && off < 0x4580) {
        int i = (off - 0x4500) >> 3;
        int32_t &b = MIXS_bank[samples & 1][i];   /* the bank the DSP reads on the next sample; alternate samples */
        if (off & 4) b = sext((int32_t)((v << 4) | (b & 0xF)), 20);
        else b = (b & ~0xF) | (v & 0xF);
    }
    if (off >= 0x4580 && off < 0x45C0) { EFREG[(off - 0x4580) >> 2] = (int16_t)v; return; }
    if (off >= 0x45C0) return;
    const uint16_t old = reg[off >> 2];
    reg[off >> 2] = v;
    if (off >= 0x3400 && off < 0x3C00) {
        mpro_nz += (v != 0) - (old != 0);
        const uint32_t s = (off - 0x3400) >> 4;
        const uint16_t w[4] = {r(0x3400 + 16 * s), r(0x3404 + 16 * s), r(0x3408 + 16 * s), r(0x340C + 16 * s)};
        dsp_inst_t d;
        dsp_decode(w, &d);
        dsp_op_set(&prog[s], &d);
        if (!(s & 1)) {   /* even step: it reads the claim of slot (s + 14) / 2 */
            const uint64_t b = 1ull << (((s + 14) >> 1) & 63);
            coll_need = (prog[s].MWT | prog[s].MRD) ? coll_need | b : coll_need & ~b;
        }
    }
    switch (off) {   /* timers and interrupt registers: as cycle-model/aica_model.cpp reg_write */
    case 0x2890: case 0x2894: case 0x2898: { const int t = (off - 0x2890) >> 2; irq.cnt[t] = (uint8_t)v; irq.pre[t] = (v >> 8) & 7; break; }
    case 0x289C: irq.scieb = v & 0x7FF; break;
    case 0x28A0: irq.scipd |= v & 0x20; break;
    case 0x28A4: irq.scipd &= ~v; break;
    case 0x28B4: irq.mcieb = v & 0x7FF; break;
    case 0x28B8: irq.mcipd |= v & 0x20; break;
    case 0x28BC: irq.mcipd &= ~v; break;
    default: break;
    }
}

uint32_t AicaModel::ram_read32(uint32_t off) {
    off &= RAM_SIZE - 4;
    uint32_t v = rd16(off) | ((uint32_t)rd16(off + 2) << 16);
    memval = (uint16_t)(v >> 16); /* 16-bit memory bus: the upper half is read last and stays in the read latch */
    return v;
}

uint32_t AicaModel::read(uint32_t off) {
    off &= 0x7FFC;
    if (off < 0x2000) return reg[off >> 2] & chan_mask(off & 0x7F);
    switch (off) {
    case 0x2800: return 0x0010; /* VER = 1; MVOL/DAC18B/MEM8MB/MONO write-only */
    case 0x2804: return 0;
    case 0x2808: return 0x0900;
    case 0x2810: {
        int ch = (r(0x280C) >> 8) & 0x3F;
        Slot &c = slot[ch];
        uint32_t v;
        if ((r(0x280C) >> 14) & 1) /* AFSEL: filter envelope */
            v = ((c.looped ? 1u : 0u) << 15) | ((uint32_t)c.FEG.state << 13) | c.FEG.v;
        else
            v = ((c.looped ? 1u : 0u) << 15) | ((uint32_t)c.AEG.state << 13) | (c.AEG.off ? 0x1FFFu : c.AEG.a);
        c.looped = false;
        return v;
    }
    case 0x2814: return slot[(r(0x280C) >> 8) & 0x3F].CA & 0xFFFF;
    case 0x2890: case 0x2894: case 0x2898: return 0;
    case 0x289C: return irq.scieb;
    case 0x28A0: return irq.scipd;
    case 0x28B4: return irq.mcieb;
    case 0x28B8: return irq.mcipd;
    case 0x28A4: case 0x28A8: case 0x28AC: case 0x28B0: case 0x28BC: return 0;
    }
    if (off >= 0x4000 && off < 0x4400) {
        int i = (off - 0x4000) >> 3;
        return (off & 4) ? ((TEMP[i] >> 8) & 0xFFFF) : (TEMP[i] & 0xFF);
    }
    if (off >= 0x4400 && off < 0x4500) {
        int i = (off - 0x4400) >> 3;
        return (off & 4) ? ((MEMS[i] >> 8) & 0xFFFF) : (MEMS[i] & 0xFF);
    }
    if (off >= 0x4500 && off < 0x4580) {
        /* the CPU reads the bank of the current sample parity, where its own writes go (tests/mixs_rd) */
        int i = (off - 0x4500) >> 3;
        int32_t v = MIXS_bank[samples & 1][i];
        return (off & 4) ? ((v >> 4) & 0xFFFF) : (v & 0xF);
    }
    if (off >= 0x4580 && off < 0x45C0) return EFREG[(off - 0x4580) >> 2] & 0xFFFF;
    if (off >= 0x45C0 && off < 0x45C8) return 0;
    if (off >= 0x4600) return 0;   /* never written */
    return reg[off >> 2];
}

} // namespace caique
