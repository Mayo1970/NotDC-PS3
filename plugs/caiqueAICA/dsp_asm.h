/* AICA DSP instruction encode/decode (caique-rtl 5aa2376, MIT, see LICENSE). One step = 64 bits in MPRO,
 * 0x703400 + step*16 + k*4; field positions as minicast DecodeInst, confirmed by the caique hardware tests. */
#ifndef CAIQUE_DSP_ASM_H
#define CAIQUE_DSP_ASM_H
#include <stdint.h>

typedef struct {
    unsigned TRA, TWT, TWA;
    unsigned XSEL, YSEL, IRA, IWT, IWA;
    unsigned TABLE, MWT, MRD, EWT, EWA, ADRL, FRCL, SHIFT, YRL, NEGB, ZERO, BSEL;
    unsigned NOFL, MASA, ADREB, NXADR;
} dsp_inst_t;

static inline void dsp_encode(const dsp_inst_t *i, uint16_t w[4]) {
    w[0] = (uint16_t)(((i->TRA & 0x7F) << 9) | ((i->TWT & 1) << 8) | ((i->TWA & 0x7F) << 1));
    w[1] = (uint16_t)(((i->XSEL & 1) << 15) | ((i->YSEL & 3) << 13) | ((i->IRA & 0x3F) << 7) | ((i->IWT & 1) << 6) |
                      ((i->IWA & 0x1F) << 1));
    w[2] = (uint16_t)(((i->TABLE & 1) << 15) | ((i->MWT & 1) << 14) | ((i->MRD & 1) << 13) | ((i->EWT & 1) << 12) |
                      ((i->EWA & 0xF) << 8) | ((i->ADRL & 1) << 7) | ((i->FRCL & 1) << 6) | ((i->SHIFT & 3) << 4) |
                      ((i->YRL & 1) << 3) | ((i->NEGB & 1) << 2) | ((i->ZERO & 1) << 1) | (i->BSEL & 1));
    w[3] = (uint16_t)(((i->NOFL & 1) << 15) | ((i->MASA & 0x3F) << 9) | ((i->ADREB & 1) << 8) | ((i->NXADR & 1) << 7));
}

static inline void dsp_decode(const uint16_t w[4], dsp_inst_t *i) {
    i->TRA = (w[0] >> 9) & 0x7F; i->TWT = (w[0] >> 8) & 1; i->TWA = (w[0] >> 1) & 0x7F;
    i->XSEL = (w[1] >> 15) & 1; i->YSEL = (w[1] >> 13) & 3; i->IRA = (w[1] >> 7) & 0x3F;
    i->IWT = (w[1] >> 6) & 1; i->IWA = (w[1] >> 1) & 0x1F;
    i->TABLE = (w[2] >> 15) & 1; i->MWT = (w[2] >> 14) & 1; i->MRD = (w[2] >> 13) & 1; i->EWT = (w[2] >> 12) & 1;
    i->EWA = (w[2] >> 8) & 0xF; i->ADRL = (w[2] >> 7) & 1; i->FRCL = (w[2] >> 6) & 1; i->SHIFT = (w[2] >> 4) & 3;
    i->YRL = (w[2] >> 3) & 1; i->NEGB = (w[2] >> 2) & 1; i->ZERO = (w[2] >> 1) & 1; i->BSEL = w[2] & 1;
    i->NOFL = (w[3] >> 15) & 1; i->MASA = (w[3] >> 9) & 0x3F; i->ADREB = (w[3] >> 8) & 1; i->NXADR = (w[3] >> 7) & 1;
}

#define IRA_MEMS(n) (n)          /* 0x00-0x1F */
#define IRA_MIXS(n) (0x20 + (n)) /* 0x20-0x2F */
#define IRA_EXTS(n) (0x30 + (n)) /* 0x30-0x31 */

/* COEF register value (bits 15:3) for a 13-bit signed coefficient */
#define COEF_REG(c13) ((uint16_t)(((unsigned)(c13) & 0x1FFF) << 3))

#endif
