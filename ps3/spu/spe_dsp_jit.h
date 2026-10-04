#pragma once
// OPTIMIZATION_PLAN 5.13 A1b: AICA DSP recompiler for the SPU. Portable C, so the PC test
// (misc/dspjit_check) runs the same emitter and executes its output in a small SPU simulator.
#include <stdint.h>
#ifndef CAIQUE_DSP_OP_H   // the PPU build gets it through aica_model.h, without plugs/caiqueAICA on the path
#include "dsp_op.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

// One value per quadword, in w[0] (the SPU preferred slot): loads need no rotate.
typedef struct
{
	uint32_t w[4];
} __attribute__((aligned(16))) DspQ;

// Per-sample state of the generated code, at a fixed LS address. In: all but pend_out.
// Out: acc, frc, y, adrs, memval, and pend_out when DspJitInfo.pend_out is set.
typedef struct
{
	DspQ acc, frc, y, adrs, memval;  // frc = sext13(FRC_REG), adrs = sext12(ADRS_REG)
	DspQ pend_m, pend_v;             // a read of the last sample lands at step 1: mask, value
	DspQ nofl0, nofl1;               // masks: the NOFL steps 0 and 1 unpack with (nofl_pipe[1], nofl_pipe[0])
	DspQ mdec, m4, mixb;             // MDEC_CT, (MDEC_CT & 127) * 4, address of MIXS_bank[(smp - 1) & 1]
	DspQ pend_out;
} DspJitCtx;

// LS addresses the code uses. temp, ring and coef/madrs are 16-aligned; ring has one spare quadword
// after ring_bytes for dropped stores. coef[s] = sext(COEF >> 3, 13), madrs[m] = MADRS, one per quadword.
typedef struct
{
	uint32_t ctx, temp, mems, efreg, exts, coll_claim, coll_word, ring, coef, madrs;
	uint32_t rd_fn, wr_fn;  // uint32_t rd(uint32_t ADDR); void wr(uint32_t ADDR, uint32_t v): interpreter memory path
} DspJitLayout;

typedef struct
{
	uint32_t words;     // instructions emitted
	uint32_t live;      // steps that emitted code
	uint32_t slow;      // memory accesses through rd_fn / wr_fn
	uint8_t pend_out;   // the read of step 127 lands at step 1 of the next sample
	uint8_t nofl126, nofl127;
	uint8_t has_slow;
} DspJitInfo;

// 0 = ok, -1 = buffer too small, -2 = emitter limit hit (a bug)
int dspjit_compile(const DspJitLayout* L, const dsp_op_t* prog, uint32_t rbl, uint32_t ring_bytes,
                   uint32_t* buf, uint32_t cap, DspJitInfo* info);

// ISA probe forms: r13 = OP(r10, r11, r12 | imm), with r10..r12 loaded from in_ls, in_ls+16, in_ls+32.
enum
{
	DJF_A, DJF_SF, DJF_AND, DJF_OR, DJF_XOR, DJF_ANDC, DJF_EQV, DJF_CEQ, DJF_CGT, DJF_CLGT,
	DJF_SHL, DJF_ROTM, DJF_ROTMA, DJF_ROTQBY, DJF_CWX, DJF_CHX, DJF_MPY, DJF_CLZ, DJF_XSHW,
	DJF_SELB, DJF_SHUFB,
	DJF_SHLI, DJF_ROTMI, DJF_ROTMAI, DJF_ROTQBYI, DJF_CWD,
	DJF_AI, DJF_SFI, DJF_ANDI, DJF_ORI, DJF_CGTI,
	DJF_IL, DJF_ILHU, DJF_IOHL, DJF_ILA,
	DJF_COUNT
};
// Immediate range of each form (lo, hi); register forms have 0, 0. Right shifts take the count (n >= 0).
extern const int32_t dspjit_form_imm[DJF_COUNT][2];
// In the header so the PPU can name a failing form without linking the emitter
static const char* const dspjit_form_names[DJF_COUNT] = {
	"a", "sf", "and", "or", "xor", "andc", "eqv", "ceq", "cgt", "clgt",
	"shl", "rotm", "rotma", "rotqby", "cwx", "chx", "mpy", "clz", "xshw",
	"selb", "shufb",
	"shli", "rotmi", "rotmai", "rotqbyi", "cwd",
	"ai", "sfi", "andi", "ori", "cgti",
	"il", "ilhu", "iohl", "ila",
};
// Words emitted, or -1. The snippet is a leaf: it returns with bi $0.
int dspjit_probe(uint32_t* buf, uint32_t cap, int form, uint32_t in_ls, uint32_t out_ls, int imm);

#ifdef __cplusplus
}
#endif
