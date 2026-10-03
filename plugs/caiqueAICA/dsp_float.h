/* AICA DSP 16-bit memory float (caique-rtl 5aa2376, MIT, see LICENSE).
 * Measured: tests/dsp_basic, dsp_unpack (all 65536 words), dsp_pack (all 2^24 inputs). */
#ifndef CAIQUE_DSP_FLOAT_H
#define CAIQUE_DSP_FLOAT_H
#include <stdint.h>

/* 24-bit -> float: sign, 4-bit exponent = leading sign copies (max 12), 11 mantissa bits */
static inline uint16_t dsp_pack(int32_t val) {
    int sign = (val >> 23) & 1;
    uint32_t temp = (uint32_t)(val ^ (val << 1)) & 0xFFFFFF;
    int exponent = 0;
    for (int k = 0; k < 12; k++) {
        if (temp & 0x800000) break;
        temp <<= 1;
        exponent++;
    }
    if (exponent < 12) val = (int32_t)(((uint32_t)val << exponent) & 0x3FFFFF);
    else val = (int32_t)((uint32_t)val << 11);
    val >>= 11;
    val &= 0x7FF;
    return (uint16_t)(val | (sign << 15) | (exponent << 11));
}

/* float -> 24-bit. Exponents above 11 act as 11 with no hidden bit: bit 22 is the sign (minicast/MAME put 0) */
static inline int32_t dsp_unpack(uint16_t f) {
    int sign = (f >> 15) & 1, exponent = (f >> 11) & 0xF, mantissa = f & 0x7FF;
    int32_t v = (sign << 23) | (mantissa << 11);
    if (exponent > 11) {
        exponent = 11;
        v |= sign << 22;
    } else {
        v |= (sign ^ 1) << 22;
    }
    v = (int32_t)((uint32_t)v << 8) >> 8;
    return v >> exponent;
}
#endif
