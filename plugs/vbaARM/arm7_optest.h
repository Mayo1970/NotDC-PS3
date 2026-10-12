// ARM7 op tests for every engine: Flycast tests/src/AicaArmTest.cpp line by line (P40), then the STR / STRB / unaligned
// STR cases of plan 6.8. The includer gives an Io (registers, flags, memory, run); a7t_run plays the script on it.
#pragma once
#include <stdint.h>
#include <stdio.h>

#define A7T_BASE 0x1000u   // PrepareOps: the ops start here; a `b .` follows them

enum
{
	A7T_END, A7T_GROUP, A7T_OP, A7T_REG, A7T_FSET, A7T_FCLR, A7T_FPUT, A7T_MEM, A7T_RUN,
	A7T_XREG, A7T_XNZCV, A7T_XMEM, A7T_XPC, A7T_XLOAD, A7T_XMEM2
};

static const char* const a7t_groups[] = {
	"Arithmetic", "Logic", "Operand2Imm", "Operand2ShiftImm", "Operand2RegShift", "Carry", "Memory", "PcRelative",
	"Conditional", "Jump", "LdmStm", "RegAlloc", "ConditionRegAlloc", "Str"
};

#define tG(id)          A7T_GROUP, id,                    // TEST_F: the core resets
#define tOP(x)          A7T_OP, 0, 1, x,                  // PrepareOp
#define tJOP(x)         A7T_OP, 1, 1, x,                  // PrepareOp of a PC write: one step in both passes
#define tOPS(n, ...)    A7T_OP, 0, n, __VA_ARGS__,        // PrepareOps
#define tR(n, v)        A7T_REG, n, (uint32_t)(v),
#define tFS(m)          A7T_FSET, m,
#define tFC             A7T_FCLR,                         // ResetNZCV
#define tFP(m)          A7T_FPUT, m,
#define tM(a, v)        A7T_MEM, a, v,
#define tRUN            A7T_RUN,
#define xR(n, v)        A7T_XREG, n, (uint32_t)(v),
#define xF(m)           A7T_XNZCV, m,
#define xM(a, v)        A7T_XMEM, a, v,
#define xPC(v)          A7T_XPC, v,
#define xL(n, a, rot)   A7T_XLOAD, n, a, rot,             // reg n == the word at a, rotated right by rot
#define xM2(a, off, on) A7T_XMEM2, a, off, on,            // the word at a, by "ARM7 STR Align"
#define fN 0x80000000u
#define fZ 0x40000000u
#define fC 0x20000000u
#define fV 0x10000000u
#define fALL 0xF0000000u

static const uint32_t a7t_script[] = {
	tG(0)   // ArithmeticOpsTest
	tOP(0xe0810002) tR(0, 0) tR(1, 0xbaad0000) tR(2, 0x0000cafe) tFS(fALL) tRUN xR(0, 0xbaadcafe) xF(fALL)   // add r0, r1, r2
	tOP(0xe0810000) tR(0, 11) tR(1, 22) tRUN xR(0, 33)                                                       // add r0, r1, r0
	tOP(0xe0410002) tR(0, 0) tR(1, 0xbaadcafe) tR(2, 0x0000cafe) tRUN xR(0, 0xbaad0000) xF(fALL)             // sub r0, r1, r2
	tOP(0xe0410000) tR(0, 11) tR(1, 22) tRUN xR(0, 11)                                                       // sub r0, r1, r0
	tOP(0xe0910002) tR(0, 0) tR(1, 0x80000000) tR(2, 0x80000000) tRUN xR(0, 0) xF(fZ | fC | fV)              // adds r0, r1, r2
	tOP(0xe0510002) tR(0, 0) tR(1, 0xbaadcafe) tR(2, 0x0000cafe) tRUN xR(0, 0xbaad0000) xF(fN | fC)          // subs r0, r1, r2
	tR(0, 0) tR(1, 0xbaadcafe) tR(2, 0xbaadcafe) tRUN xR(0, 0) xF(fZ | fC)
	tR(0, 0) tR(1, 0x80000000) tR(2, 1) tRUN xR(0, 0x7fffffff) xF(fC | fV)
	tR(0, 0) tR(1, 0xffffffff) tR(2, 0xffffffff) tRUN xR(0, 0) xF(fZ | fC)
	tOP(0xe0b10002) tFC tR(0, 0) tR(1, 1) tR(2, 2) tRUN xR(0, 3) xF(0)                                       // adcs r0, r1, r2
	tFS(fC) tR(0, 0) tRUN xR(0, 4) xF(0)
	tOP(0xe0d22002) tFC tFS(fC) tR(2, 0xFFFFFFFF) tRUN xR(2, 0) xF(fZ | fC)                                  // sbcs r2, r2, r2
	tOP(0xe2d22000) tFC tR(2, 0) tRUN xR(2, -1) xF(fN)                                                       // sbcs r2, r2, #0
	tOP(0xe0d10002) tFC tR(0, 0) tR(1, 1) tR(2, 2) tRUN xR(0, -2) xF(fN)                                     // sbcs r0, r1, r2
	tOP(0xe0710002) tR(0, 0) tR(1, 1) tR(2, 2) tRUN xR(0, 1) xF(fC)                                          // rsbs r0, r1, r2
	tOP(0xe0f10002) tFC tR(0, 0) tR(1, 1) tR(2, 2) tRUN xR(0, 0) xF(fZ | fC)                                 // rscs r0, r1, r2
	tFS(fC) tR(0, 0) tR(1, 1) tR(2, 2) tRUN xR(0, 1) xF(fC)
	tOP(0xe1500001) tFC tR(0, 2) tR(1, 1) tRUN xF(fC)                                                        // cmp r0, r1
	tOP(0xe1700001) tFC tR(0, 2) tR(1, -1) tRUN xF(fC)                                                       // cmn r0, r1

	tG(1)   // LogicOpsTest
	tOP(0xe1a00001) tR(0, 0) tR(1, 0xbaadcafe) tRUN xR(0, 0xbaadcafe)                        // mov r0, r1
	tOP(0xe3c100ff) tR(0, 0) tR(1, 0xffff) tRUN xR(0, 0xff00)                                // bic r0, r1, #0xff
	tOP(0xe0010002) tR(0, 0) tR(1, 0xff0f) tR(2, 0xf0f0) tRUN xR(0, 0xf000)                  // and r0, r1, r2
	tOP(0xe0010000) tR(0, 0xf0f0f0f0) tR(1, 0xffffffff) tRUN xR(0, 0xf0f0f0f0)               // and r0, r1, r0
	tOP(0xe1a00251) tR(0, 0) tR(1, 0xfffffff8) tR(2, 2) tRUN xR(0, 0xfffffffe)               // asr r0, r1, r2
	tOP(0xe1a00241) tR(0, 0) tR(1, 0x0ffffff0) tRUN xR(0, 0x00ffffff)                        // asr r0, r1, #4
	tOP(0xe0210002) tR(0, 0) tR(1, 0xffff0000) tR(2, 0xf0f0f0f0) tRUN xR(0, 0x0f0ff0f0)      // eor r0, r1, r2
	tOP(0xe0210000) tR(0, 0xf0f0f0f0) tR(1, 0xffffffff) tRUN xR(0, 0x0f0f0f0f)               // eor r0, r1, r0
	tOP(0xe1810000) tR(0, 0xf0f0f0f0) tR(1, 0x0f0f0f0f) tRUN xR(0, 0xffffffff)               // orr r0, r1, r0
	tOP(0xe1a00211) tR(0, 0) tR(1, 1) tR(2, 8) tRUN xR(0, 0x100)                             // lsl r0, r1, r2
	tOP(0xe1a00231) tR(0, 0) tR(1, 0x100) tR(2, 4) tRUN xR(0, 0x10)                          // lsr r0, r1, r2
	tOP(0xe1a00271) tR(0, 0) tR(1, 0x12345678) tR(2, 16) tRUN xR(0, 0x56781234)              // ror r0, r1, r2
	tOP(0xe1300001) tFC tR(0, 0) tR(1, 1) tRUN xF(0)                                         // teq r0, r1
	tFS(fC | fV) tR(0, 1) tR(1, 1) tRUN xF(fZ | fC | fV)
	tOP(0xe1100001) tFC tR(0, 1) tR(1, 2) tRUN xF(fZ)                                        // tst r0, r1
	tFS(fC | fV) tR(0, 3) tR(1, 2) tRUN xF(fC | fV)

	tG(2)   // Operand2ImmTest
	tOP(0xe2810003) tR(0, 0) tR(1, 1) tRUN xR(0, 4)            // add r0, r1, #3
	tOP(0xe2a10003) tFC tR(0, 0) tR(1, 2) tRUN xR(0, 5)        // adc r0, r1, #3
	tOP(0xe2410003) tR(0, 0) tR(1, 7) tRUN xR(0, 4)            // sub r0, r1, #3
	tOP(0xe2610003) tR(0, 0) tR(1, 1) tRUN xR(0, 2)            // rsb r0, r1, #3
	tOP(0xe2c10003) tFC tR(0, 0) tR(1, 10) tRUN xR(0, 6)       // sbc r0, r1, #3
	tOP(0xe2e10010) tFC tR(0, 0) tR(1, 6) tRUN xR(0, 9)        // rsc r0, r1, #16
	tOP(0xe3500010) tFC tR(0, 16) tRUN xF(fZ | fC)             // cmp r0, #16
	tOP(0xe3700001) tFC tR(0, 0xffffffff) tRUN xF(fZ | fC)     // cmn r0, #1
	tOP(0xe2010001) tR(0, 0) tR(1, 3) tRUN xR(0, 1)            // and r0, r1, #1
	tOP(0xe3810001) tR(0, 0) tR(1, 2) tRUN xR(0, 3)            // orr r0, r1, #1
	tOP(0xe2210001) tR(0, 0) tR(1, 3) tRUN xR(0, 2)            // eor r0, r1, #1
	tOP(0xe3c10001) tR(0, 0) tR(1, 3) tRUN xR(0, 2)            // bic r0, r1, #1
	tOP(0xe3100001) tFC tR(0, 2) tRUN xF(fZ)                   // tst r0, #1
	tFC tR(0, 1) tRUN xF(0)
	tOP(0xe3300001) tFC tR(0, 1) tRUN xF(fZ)                   // teq r0, #1
	tFC tR(0, 2) tRUN xF(0)
	tOP(0xe2522001) tFC tR(2, 1) tRUN xF(fZ | fC)              // subs r2, #1
	tFC tR(2, 2) tRUN xF(fC)
	tFC tR(2, 0) tRUN xF(fN)

	tG(3)   // Operand2ShiftImmTest
	tOP(0xe0810202) tR(0, 0) tR(1, 1) tR(2, 2) tRUN xR(0, 0x21)                               // add r0, r1, r2, LSL #4
	tOP(0xe0810142) tR(0, 0) tR(1, 1) tR(2, -8) tRUN xR(0, -1)                                // add r0, r1, r2, ASR #2
	tOP(0xe08100a2) tR(0, 0) tR(1, 1) tR(2, 0x80000000) tRUN xR(0, 0x40000001)                // add r0, r1, r2, LSR #1
	tOP(0xe0810862) tR(0, 0) tR(1, 1) tR(2, 0x56771234) tRUN xR(0, 0x12345678)                // add r0, r1, r2, ROR #16
	tOP(0xe0810062) tFC tR(0, 0) tR(1, 1) tR(2, 0x22222221) tRUN xR(0, 0x11111111) xF(0)      // add r0, r1, r2, RRX
	tOP(0xe1910062) tFS(fC) tR(0, 0) tR(1, 1) tR(2, 0x22222221) tRUN xR(0, 0x91111111) xF(fN | fC)   // orrs r0, r1, r2, RRX
	tOP(0xe3b00102) tFC tR(0, 0) tRUN xR(0, 0x80000000) xF(fN | fC)                           // movs r0, #0x80000000
	tOP(0xe3f00000) tFC tR(0, 0) tRUN xR(0, 0xffffffff) xF(fN)                                // mvns r0, #0

	tG(4)   // Operand2RegShiftTest
	tOP(0xe1810312) tR(0, 0) tR(1, 1) tR(2, 0x10) tR(3, 8) tRUN xR(0, 0x1001)                 // orr r0, r1, r2, LSL r3
	tOP(0xe1810352) tR(0, 0) tR(1, 1) tR(2, 0x80000000) tR(3, 30) tRUN xR(0, 0xffffffff)      // orr r0, r1, r2, ASR r3
	tOP(0xe1810332) tR(0, 0) tR(1, 1) tR(2, 0x80000000) tR(3, 30) tRUN xR(0, 3)               // orr r0, r1, r2, LSR r3
	tOP(0xe1810372) tR(0, 0) tR(1, 1) tR(2, 0x43208765) tR(3, 16) tRUN xR(0, 0x87654321)      // orr r0, r1, r2, ROR r3
	tOP(0xe1b0431f) tFC tR(3, 0) tR(4, 0) tRUN xR(4, A7T_BASE + 12) xF(0)                     // movs r4, r15, lsl r3
	tOP(0xe1b0400f) tFC tR(3, 0) tR(4, 0) tRUN xR(4, A7T_BASE + 8) xF(0)                      // movs r4, r15, lsl #0

	tG(5)   // CarryTest
	tOP(0xe1910022) tFC tR(0, 0) tR(1, 1) tR(2, 0x80000000) tRUN xR(0, 1) xF(fC)                // orrs r0, r1, r2, LSR #32
	tOP(0xe1910822) tFC tR(0, 0) tR(1, 1) tR(2, 0x80008000) tRUN xR(0, 0x8001) xF(fC)           // orrs r0, r1, r2, LSR #16
	tOP(0xe1910042) tFC tR(0, 0) tR(1, 1) tR(2, 0x80000000) tRUN xR(0, 0xffffffff) xF(fN | fC)  // orrs r0, r1, r2, ASR #32
	tOP(0xe1910842) tFC tR(0, 0) tR(1, 1) tR(2, 0x00008000) tRUN xR(0, 1) xF(fC)                // orrs r0, r1, r2, ASR #16
	tFC tR(0, 0) tR(1, 1) tR(2, 0x00004000) tRUN xR(0, 1) xF(0)
	tOP(0xe1910802) tFC tR(0, 0) tR(1, 1) tR(2, 0x00010001) tRUN xR(0, 0x10001) xF(fC)          // orrs r0, r1, r2, LSL #16

	tG(6)   // MemoryTest: the word at 0x1004 is the `b .` after the op
	tOP(0xe5910004) tR(0, 0) tR(1, 0x10000) tM(0x10004, 0xbaadcafe) tRUN xR(0, 0xbaadcafe)                  // ldr r0, [r1, #4]
	tOP(0xe5010008) tR(0, 0xbaadcafe) tR(1, 0x10008) tM(0x10000, 0) tRUN xM(0x10000, 0xbaadcafe)            // str r0, [r1, #-8]
	tOP(0xe5b10004) tR(0, 0) tR(1, 0x10000) tM(0x10004, 0xbaadcafe) tRUN xR(0, 0xbaadcafe) xR(1, 0x10004)   // ldr r0, [r1, #4]!
	tOP(0xe4110004) tR(0, 0) tR(1, 0x10004) tM(0x10004, 0xbaadcafe) tRUN xR(0, 0xbaadcafe) xR(1, 0x10000)   // ldr r0, [r1], #-4
	tOP(0xe4900004) tR(0, 0x10004) tM(0x10004, 0xbaadcafe) tRUN xR(0, 0xbaadcafe)                           // ldr r0, [r0], #4
	tOP(0xe5b00004) tR(0, 0x10000) tM(0x10000, 0xbaadcafe) tRUN xR(0, 0xbaadcafe)                           // ldr r0, [r0, #4]!
	tOP(0xe51f0004) tR(0, 0) tRUN xL(0, 0x1004, 0)                                                          // ldr r0, [r15, #-4]
	tOP(0xe79000a4) tR(0, 0x1003) tR(4, 2) tRUN xL(0, 0x1004, 0)                                            // ldr r0, [r0, r4, LSR #1]
	tOP(0xe7900084) tR(0, 0x1002) tR(4, 1) tRUN xL(0, 0x1004, 0)                                            // ldr r0, [r0, r4, LSL #1]
	tOP(0xe7900024) tR(0, 0x1004) tR(4, 0x12345678) tRUN xL(0, 0x1004, 0) xR(4, 0x12345678)                 // ldr r0, [r0, r4, LSR #32]
	tOP(0xe7900044) tR(0, 0x1005) tR(4, 0x80000000) tRUN xL(0, 0x1004, 0) xR(4, 0x80000000)                 // ldr r0, [r0, r4, ASR #32]
	tOP(0xe7920002) tR(0, 5) tR(2, 0x1004 / 2) tRUN xL(0, 0x1004, 0) xR(2, 0x1004 / 2)                      // ldr r0, [r2, r2]
	tOP(0xe7920063) tFC tFS(fC) tR(0, 5) tR(2, 0x1006) tR(3, -4) tRUN xL(0, 0x1004, 0) xR(2, 0x1006) xR(3, -4)   // ldr r0, [r2, r3, rrx]
	tOP(0xe7900002) tR(0, 0x1004) tR(2, 2) tRUN xL(0, 0x1004, 16) xR(2, 2)                                  // ldr r0, [r0, r2] unaligned
	tOP(0xe69000a4) tR(0, 0x1004) tR(4, 2) tRUN xL(0, 0x1004, 0) xR(4, 2)                                   // ldr r0, [r0], r4, lsr #1
	tOP(0xe6920104) tR(0, 0) tR(2, 0x1004) tR(4, 2) tRUN xL(0, 0x1004, 0) xR(2, 0x1004 + 8) xR(4, 2)        // ldr r0, [r2], r4, lsl #2
	tOP(0xe6920000) tR(0, 123) tR(2, 0x1004) tRUN xL(0, 0x1004, 0) xR(2, 0x1004 + 123)                      // ldr r0, [r2], r0
	tOP(0xe6920043) tR(0, 0) tR(2, 0x1004) tR(3, 0xc0000000) tRUN xL(0, 0x1004, 0) xR(2, 0x1004 - 1) xR(3, 0xc0000000)   // ldr r0, [r2], r3, asr #32
	tOP(0xe6920063) tFC tFS(fC) tR(0, 5) tR(2, 0x1004) tR(3, 0xfffffffc) tRUN xL(0, 0x1004, 0) xR(2, 0x1002) xF(fC) xR(3, 0xfffffffc)   // ldr r0, [r2], r3, rrx
	tOP(0xe6900002) tR(0, 0x1006) tR(2, 1) tRUN xL(0, 0x1004, 16) xR(2, 1)                                  // ldr r0, [r0], r2 unaligned
	tOP(0x04910004) tFC tR(0, 0) tR(1, 0x1004) tRUN xR(0, 0) xR(1, 0x1004)                                  // ldreq r0, [r1], #4, false

	tG(7)   // PcRelativeTest
	tOP(0xe38f0010) tR(0, 0) tRUN xR(0, A7T_BASE + 8 + 16)               // orr r0, r15, #16
	tOP(0xe180011f) tR(0, 0) tR(1, 1) tRUN xR(0, (A7T_BASE + 12) << 1)   // orr r0, r15, LSL r1
	tOP(0xe28f5c7d) tR(5, 0) tRUN xR(5, 32000 + A7T_BASE + 8)            // add r5, r15, #32000

	tG(8)   // ConditionalTest: mov<cc> r0, r1
	tOP(0x01a00001) tFC tR(0, 0) tR(1, 1) tRUN xR(0, 0) tFS(fZ) tRUN xR(0, 1)                                 // eq
	tOP(0x11a00001) tFC tR(0, 0) tR(1, 1) tRUN xR(0, 1) tFS(fZ) tR(0, 0) tRUN xR(0, 0)                        // ne
	tOP(0x21a00001) tFC tR(0, 0) tR(1, 1) tRUN xR(0, 0) tFS(fC) tRUN xR(0, 1)                                 // cs
	tOP(0x31a00001) tFC tR(0, 0) tR(1, 1) tRUN xR(0, 1) tFS(fC) tR(0, 0) tRUN xR(0, 0)                        // cc
	tOP(0x41a00001) tFC tR(0, 0) tR(1, 1) tRUN xR(0, 0) tFS(fN) tRUN xR(0, 1)                                 // mi
	tOP(0x51a00001) tFC tR(0, 0) tR(1, 1) tRUN xR(0, 1) tFS(fN) tR(0, 0) tRUN xR(0, 0)                        // pl
	tOP(0x61a00001) tFC tR(0, 0) tR(1, 1) tRUN xR(0, 0) tFS(fV) tRUN xR(0, 1)                                 // vs
	tOP(0x71a00001) tFC tR(0, 0) tR(1, 1) tRUN xR(0, 1) tFS(fV) tR(0, 0) tRUN xR(0, 0)                        // vc
	tOP(0x81a00001) tFC tR(0, 0) tR(1, 1) tRUN xR(0, 0) tFS(fC) tRUN xR(0, 1) tFS(fZ) tR(0, 0) tRUN xR(0, 0)  // hi
	tOP(0x91a00001) tFC tR(0, 0) tR(1, 1) tRUN xR(0, 1) tFS(fC) tR(0, 0) tRUN xR(0, 0) tFP(fZ) tR(0, 0) tRUN xR(0, 1)   // ls
	tOP(0xa1a00001) tFC tR(0, 0) tR(1, 1) tRUN xR(0, 1) tFS(fN) tR(0, 0) tRUN xR(0, 0) tFS(fV) tRUN xR(0, 1)  // ge
	tOP(0xb1a00001) tFC tR(0, 0) tR(1, 1) tRUN xR(0, 0) tFS(fN) tRUN xR(0, 1) tFS(fV) tR(0, 0) tRUN xR(0, 0)  // lt
	tOP(0xc1a00001) tFC tR(0, 0) tR(1, 1) tRUN xR(0, 1) tFC tFS(fZ) tR(0, 0) tRUN xR(0, 0)                    // gt
	tFC tFS(fV) tRUN xR(0, 0) tFS(fN) tRUN xR(0, 1)
	tOP(0xd1a00001) tFC tR(0, 0) tR(1, 1) tRUN xR(0, 0) tFS(fZ) tRUN xR(0, 1)                                 // le
	tFC tR(0, 0) tFS(fV) tRUN xR(0, 1) tR(0, 0) tFS(fN) tRUN xR(0, 0)

	tG(9)   // JumpTest
	tJOP(0xea00003e) tRUN xPC(0x1100)                                                   // b +248
	tJOP(0xeb00003e) tR(14, 0) tRUN xPC(0x1100) xR(14, 0x1004)                          // bl +248
	tJOP(0xe1a0f000) tR(0, 0x100) tRUN xPC(0x100)                                       // mov pc, r0
	tJOP(0xc1a0f000) tFC tFS(fN) tR(0, 0x100) tRUN xPC(0x1004) tFC tRUN xPC(0x100)      // movgt pc, r0
	tJOP(0xe590f000) tR(0, 0x10000) tM(0x10000, 0xbaadcafc) tRUN xPC(0xbaadcafc)        // ldr r15, [r0]
	tJOP(0x1b00003e) tFC tR(14, 0) tRUN xPC(0x1100) xR(14, 0x1004)                      // blne +248
	tFC tFS(fZ) tR(14, 0) tRUN xPC(0x1004) xR(14, 0)

	tG(10)   // LdmStmTest
	tJOP(0xe8bd8000) tR(13, 0x1100) tM(0x1100, 0x1234) tRUN xPC(0x1234)                 // ldm sp!, {pc}
	tOP(0xe92d8000) tR(13, 0x1104) tRUN xR(13, 0x1100) xM(0x1100, A7T_BASE + 12)        // stmdb sp!, {pc}

	tG(11)   // RegAllocTest: mov r0-r6, #0-6, then add rN, rN, rN+1 (r6: r0)
	tOPS(14, 0xe3a00000, 0xe3a01001, 0xe3a02002, 0xe3a03003, 0xe3a04004, 0xe3a05005, 0xe3a06006, 0xe0800001, 0xe0811002,
	     0xe0822003, 0xe0833004, 0xe0844005, 0xe0855006, 0xe0866000)
	tR(0, 0) tR(1, 0) tR(2, 0) tR(3, 0) tR(4, 0) tR(5, 0) tR(6, 0) tR(7, 0) tR(8, 0) tR(9, 0) tR(10, 0) tR(11, 0) tR(12, 0)
	tR(13, 0) tR(14, 0) tRUN xR(0, 1) xR(1, 3) xR(2, 5) xR(3, 7) xR(4, 9) xR(5, 11) xR(6, 7)

	tG(12)   // ConditionRegAllocTest
	tOPS(2, 0x03a0004d, 0xe1a01000) tR(0, 22) tR(1, 22) tFC tRUN xR(0, 22) xR(1, 22)                // moveq r0, #77; mov r1, r0
	tOPS(2, 0x01a01000, 0xe1a02000) tR(0, 22) tR(1, 0) tR(2, 0) tFC tRUN xR(0, 22) xR(1, 0) xR(2, 22)   // moveq r1, r0; mov r2, r0

	tG(13)   // Plan 6.8, not Flycast's: STR and STRB forms, one byte lane each, then unaligned STR
	tOP(0xE5810000) tR(0, 0xbaadcafe) tR(1, 0x10100) tM(0x10100, 0x11111111) tRUN xM(0x10100, 0xbaadcafe) xR(1, 0x10100)   // str r0, [r1]
	tOP(0xE5A10004) tR(0, 0x01020304) tR(1, 0x10100) tM(0x10104, 0) tRUN xM(0x10104, 0x01020304) xR(1, 0x10104)            // str r0, [r1, #4]!
	tOP(0xE4810004) tR(0, 0x0a0b0c0d) tR(1, 0x10100) tRUN xM(0x10100, 0x0a0b0c0d) xR(1, 0x10104)                           // str r0, [r1], #4
	tOP(0xE5210004) tR(0, 0x55667788) tR(1, 0x10108) tRUN xM(0x10104, 0x55667788) xR(1, 0x10104)                           // str r0, [r1, #-4]!
	tOP(0xE4010004) tR(0, 0x99aabbcc) tR(1, 0x10108) tM(0x10108, 0) tRUN xM(0x10108, 0x99aabbcc) xR(1, 0x10104)            // str r0, [r1], #-4
	tOP(0xE7810002) tR(0, 0x13572468) tR(1, 0x10100) tR(2, 0x10) tM(0x10110, 0) tRUN xM(0x10110, 0x13572468) xR(1, 0x10100) xR(2, 0x10)   // str r0, [r1, r2]
	tOP(0xE7010002) tR(0, 0x24681357) tR(1, 0x10120) tR(2, 0x10) tRUN xM(0x10110, 0x24681357) xR(1, 0x10120)               // str r0, [r1, -r2]
	tOP(0xE7810102) tR(0, 0xcafe0001) tR(1, 0x10100) tR(2, 5) tM(0x10114, 0) tRUN xM(0x10114, 0xcafe0001)                  // str r0, [r1, r2, lsl #2]
	tOP(0xE6810102) tR(0, 0xcafe0002) tR(1, 0x10118) tR(2, 2) tRUN xM(0x10118, 0xcafe0002) xR(1, 0x10120)                  // str r0, [r1], r2, lsl #2
	tOP(0xE7810062) tFC tFS(fC) tR(0, 0xcafe0003) tR(1, 0x10124) tR(2, 0xfffffff8) tM(0x10120, 0) tRUN xM(0x10120, 0xcafe0003) xF(fC)   // str r0, [r1, r2, rrx]
	tOP(0x04810004) tFC tR(0, 0xdddddddd) tR(1, 0x10100) tM(0x10100, 0x11111111) tRUN xM(0x10100, 0x11111111) xR(1, 0x10100)   // streq r0, [r1], #4, false
	tOP(0xE5C10000) tR(0, 0x123456AB) tR(1, 0x10100) tM(0x10100, 0x11223344) tRUN xM(0x10100, 0x112233AB)                  // strb r0, [r1]
	tOP(0xE5C10001) tM(0x10100, 0x11223344) tRUN xM(0x10100, 0x1122AB44)                                                   // strb r0, [r1, #1]
	tOP(0xE5C10002) tM(0x10100, 0x11223344) tRUN xM(0x10100, 0x11AB3344)                                                   // strb r0, [r1, #2]
	tOP(0xE5C10003) tM(0x10100, 0x11223344) tRUN xM(0x10100, 0xAB223344)                                                   // strb r0, [r1, #3]
	tOP(0xE4C10001) tR(1, 0x10102) tM(0x10100, 0x11223344) tRUN xM(0x10100, 0x11AB3344) xR(1, 0x10103)                     // strb r0, [r1], #1
	tOP(0xE5610001) tR(1, 0x10102) tM(0x10100, 0x11223344) tRUN xM(0x10100, 0x1122AB44) xR(1, 0x10101)                     // strb r0, [r1, #-1]!
	tOPS(2, 0xE5C10003, 0xE5D12003) tR(0, 0xC7) tR(1, 0x10100) tR(2, 0) tM(0x10100, 0) tRUN xR(2, 0xC7) xM(0x10100, 0xC7000000)   // strb r0, [r1, #3]; ldrb r2, [r1, #3]
	tOP(0xE5810000) tR(0, 0xA1B2C3D4) tR(1, 0x10101) tM(0x10100, 0x11223344) tM(0x10104, 0x55667788) tRUN                  // str r0, [r1], r1 & 3 = 1
	xM2(0x10100, 0xB2C3D444, 0xA1B2C3D4) xM2(0x10104, 0x556677A1, 0x55667788) xR(1, 0x10101)
	tR(1, 0x10102) tM(0x10100, 0x11223344) tM(0x10104, 0x55667788) tRUN                                                    // r1 & 3 = 2
	xM2(0x10100, 0xC3D43344, 0xA1B2C3D4) xM2(0x10104, 0x5566A1B2, 0x55667788)
	tR(1, 0x10103) tM(0x10100, 0x11223344) tM(0x10104, 0x55667788) tRUN                                                    // r1 & 3 = 3
	xM2(0x10100, 0xD4223344, 0xA1B2C3D4) xM2(0x10104, 0x55A1B2C3, 0x55667788)
	tOP(0xE5A10002) tR(0, 0xA1B2C3D4) tR(1, 0x10100) tM(0x10100, 0x11223344) tM(0x10104, 0x55667788) tRUN                  // str r0, [r1, #2]!
	xM2(0x10100, 0xC3D43344, 0xA1B2C3D4) xM2(0x10104, 0x5566A1B2, 0x55667788) xR(1, 0x10102)

	A7T_END
};

#undef tG
#undef tOP
#undef tJOP
#undef tOPS
#undef tR
#undef tFS
#undef tFC
#undef tFP
#undef tM
#undef tRUN
#undef xR
#undef xF
#undef xM
#undef xPC
#undef xL
#undef xM2
#undef fN
#undef fZ
#undef fC
#undef fV
#undef fALL

struct A7TStats { uint32_t runs, checks, fails; };

// One pass. Step: one instruction per io.run(1). Block: the ops in one slice, which ends in the `b .` (a PC write
// is stepped in both). Prints the first 8 failures after `tag`.
template<class Io>
static void a7t_run(Io& io, bool block, const char* tag, A7TStats& st)
{
	const uint32_t* p = a7t_script;
	uint32_t n_ops = 0, jump = 0, group = 0, run_no = 0, op0 = 0;
	auto check = [&](const char* what, uint32_t idx, uint32_t got, uint32_t want) {
		st.checks++;
		if (got == want)
			return;
		if (st.fails++ < 8)
			printf("%s FAIL %s run %u, op %08X, %s: %s %X = %08X, want %08X\n", tag, a7t_groups[group], run_no, op0,
			       block ? "block" : "step", what, idx, got, want);
	};
	for (;;)
	{
		switch (*p++)
		{
		case A7T_END:
			return;
		case A7T_GROUP:
			group = *p++;
			run_no = 0;
			io.reset();
			break;
		case A7T_OP:
			jump = *p++;
			n_ops = *p++;
			op0 = p[0];
			for (uint32_t i = 0; i < n_ops; i++)
				io.wr32(A7T_BASE + 4 * i, *p++);
			io.wr32(A7T_BASE + 4 * n_ops, 0xEAFFFFFEu);
			io.flush();
			break;
		case A7T_REG:
			io.set_reg((int)p[0], p[1]);
			p += 2;
			break;
		case A7T_FSET:
			io.set_nzcv(io.nzcv() | *p++);
			break;
		case A7T_FCLR:
			io.set_nzcv(0);
			break;
		case A7T_FPUT:
			io.set_nzcv(*p++);
			break;
		case A7T_MEM:
			io.wr32(p[0], p[1]);
			p += 2;
			break;
		case A7T_RUN:
			run_no++;
			st.runs++;
			io.set_pc(A7T_BASE);
			if (block && !jump)
				io.run(64 * n_ops);
			else
				for (uint32_t i = 0; i < n_ops; i++)
					io.run(1);
			if (!jump)
				check("pc", 0, io.pc(), A7T_BASE + 4 * n_ops);
			break;
		case A7T_XREG:
			check("r", p[0], io.reg((int)p[0]), p[1]);
			p += 2;
			break;
		case A7T_XNZCV:
			check("nzcv", 0, io.nzcv(), *p);
			p++;
			break;
		case A7T_XMEM:
			check("mem", p[0], io.rd32(p[0]), p[1]);
			p += 2;
			break;
		case A7T_XPC:
			check("pc", 0, io.pc(), *p);
			p++;
			break;
		case A7T_XLOAD:
		{
			const uint32_t w = io.rd32(p[1]), rot = p[2];
			check("r", p[0], io.reg((int)p[0]), rot ? (w >> rot) | (w << (32 - rot)) : w);
			p += 3;
			break;
		}
		case A7T_XMEM2:
			check("mem", p[0], io.rd32(p[0]), io.str_align() ? p[2] : p[1]);
			p += 3;
			break;
		}
	}
}
