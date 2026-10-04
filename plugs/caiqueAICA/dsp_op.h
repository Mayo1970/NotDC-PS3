/* The model's decoded MPRO step (5.13 A3): dsp_inst_t with one byte per field, 3 KB a program (the SPU's LS is short).
 * dsp_inst_t stays as in caique, whose test cases include their own dsp_asm.h with the same guard. */
#ifndef CAIQUE_DSP_OP_H
#define CAIQUE_DSP_OP_H
#include <stdint.h>
#include "dsp_asm.h"

typedef struct {   /* every field fits 7 bits */
    uint8_t TRA, TWT, TWA;
    uint8_t XSEL, YSEL, IRA, IWT, IWA;
    uint8_t TABLE, MWT, MRD, EWT, EWA, ADRL, FRCL, SHIFT, YRL, NEGB, ZERO, BSEL;
    uint8_t NOFL, MASA, ADREB, NXADR;
} dsp_op_t;

static inline void dsp_op_set(dsp_op_t *o, const dsp_inst_t *i) {
    o->TRA = (uint8_t)i->TRA; o->TWT = (uint8_t)i->TWT; o->TWA = (uint8_t)i->TWA;
    o->XSEL = (uint8_t)i->XSEL; o->YSEL = (uint8_t)i->YSEL; o->IRA = (uint8_t)i->IRA; o->IWT = (uint8_t)i->IWT;
    o->IWA = (uint8_t)i->IWA;
    o->TABLE = (uint8_t)i->TABLE; o->MWT = (uint8_t)i->MWT; o->MRD = (uint8_t)i->MRD; o->EWT = (uint8_t)i->EWT;
    o->EWA = (uint8_t)i->EWA; o->ADRL = (uint8_t)i->ADRL; o->FRCL = (uint8_t)i->FRCL; o->SHIFT = (uint8_t)i->SHIFT;
    o->YRL = (uint8_t)i->YRL; o->NEGB = (uint8_t)i->NEGB; o->ZERO = (uint8_t)i->ZERO; o->BSEL = (uint8_t)i->BSEL;
    o->NOFL = (uint8_t)i->NOFL; o->MASA = (uint8_t)i->MASA; o->ADREB = (uint8_t)i->ADREB; o->NXADR = (uint8_t)i->NXADR;
}
#endif
