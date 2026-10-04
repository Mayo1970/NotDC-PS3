// OPTIMIZATION_PLAN 5.13 A2 high voices: SPU simulator, written from the SPU ISA 1.2 (encodings checked against
// spu-objdump). Timing: in-order, even/odd dual issue, CBEA latencies, 18-cycle penalty for a branch not hinted.
#include "spusim.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define LSLR 0x3FFFFu
#define BR_PENALTY 18
#define HINT_LEAD 11      // a hint issued closer than this to its branch does not help
#define DEC_DIV 40        // SPU cycles per decrementer tick (3.2 GHz / 79.8 MHz)

enum { EVEN = 0, ODD = 1 };

enum
{
	X_BAD = 0,
	X_LQX, X_STQX, X_CBX, X_CHX, X_CWX, X_CDX, X_AH, X_A, X_SFH, X_SF, X_ADDX, X_CG, X_CGX, X_SFX, X_BG, X_BGX,
	X_MPY, X_MPYU, X_MPYH, X_MPYS, X_MPYHH, X_MPYHHA, X_MPYHHU, X_MPYHHAU, X_CLZ, X_CNTB, X_FSMB, X_FSMH, X_FSM,
	X_GBB, X_GBH, X_GB, X_AVGB, X_ABSDB, X_SUMB, X_XSBH, X_XSHW, X_XSWD, X_AND, X_ANDC, X_OR, X_ORC, X_ORX,
	X_XOR, X_NAND, X_NOR, X_EQV, X_SHLH, X_SHL, X_SHLQBI, X_SHLQBY, X_SHLQBYBI, X_ROTH, X_ROT, X_ROTQBY,
	X_ROTQBYBI, X_ROTQBI, X_ROTHM, X_ROTM, X_ROTQMBY, X_ROTQMBYBI, X_ROTQMBI, X_ROTMAH, X_ROTMA, X_HEQ, X_HGT,
	X_HLGT, X_CEQB, X_CEQH, X_CEQ, X_CGTB, X_CGTH, X_CGT, X_CLGTB, X_CLGTH, X_CLGT, X_BI, X_IRET, X_BISLED,
	X_BISL, X_BIZ, X_BINZ, X_BIHZ, X_BIHNZ, X_HBR, X_STOP, X_STOPD, X_LNOP, X_NOP, X_SYNC, X_DSYNC,
	X_RDCH, X_RCHCNT, X_WRCH, X_FA, X_FS, X_FM, X_FCEQ, X_FCGT,
	X_SHLHI, X_SHLI, X_SHLQBII, X_SHLQBYI, X_ROTHI, X_ROTI, X_ROTQBYI, X_ROTQBII, X_ROTHMI, X_ROTMI, X_ROTQMBYI,
	X_ROTQMBII, X_ROTMAHI, X_ROTMAI, X_CBD, X_CHD, X_CWD, X_CDD,
	X_CSFLT, X_CFLTS, X_CUFLT, X_CFLTU,
	X_LQD, X_STQD, X_AI, X_AHI, X_SFI, X_SFHI, X_MPYI, X_MPYUI, X_ANDBI, X_ANDHI, X_ANDI, X_ORBI, X_ORHI, X_ORI,
	X_XORBI, X_XORHI, X_XORI, X_HEQI, X_HGTI, X_HLGTI, X_CEQBI, X_CEQHI, X_CEQI, X_CGTBI, X_CGTHI, X_CGTI,
	X_CLGTBI, X_CLGTHI, X_CLGTI,
	X_BRA, X_BR, X_BRASL, X_BRSL, X_BRNZ, X_BRZ, X_BRHNZ, X_BRHZ, X_LQA, X_LQR, X_STQA, X_STQR, X_ILH, X_ILHU,
	X_IL, X_IOHL, X_FSMBI,
	X_ILA, X_HBRA, X_HBRR,
	X_SELB, X_SHUFB, X_MPYA, X_FNMS, X_FMA, X_FMS,
	X_COUNT
};

// Register use: what a form reads and writes (dependency tracking)
enum { F_RR, F_RRT, F_RI7, F_RI8, F_RI10, F_RI10T, F_RI16, F_RI16T, F_RI18, F_RRR, F_NONE };

typedef struct
{
	const char* name;
	int x, width, code, pipe, lat, form;
	int rd_rt, wr_rt;   // reads RT as a source; writes RT
} OpInfo;

// width: opcode bits (11 RR/RI7, 10 RI8, 8 RI10, 9 RI16, 7 RI18, 4 RRR). lat: result latency in cycles.
static const OpInfo k_ops[] = {
	{"lqx", X_LQX, 11, 0x1C4, ODD, 6, F_RR, 0, 1}, {"stqx", X_STQX, 11, 0x144, ODD, 6, F_RR, 1, 0},
	{"cbx", X_CBX, 11, 0x1D4, ODD, 4, F_RR, 0, 1}, {"chx", X_CHX, 11, 0x1D5, ODD, 4, F_RR, 0, 1},
	{"cwx", X_CWX, 11, 0x1D6, ODD, 4, F_RR, 0, 1}, {"cdx", X_CDX, 11, 0x1D7, ODD, 4, F_RR, 0, 1},
	{"ah", X_AH, 11, 0x0C8, EVEN, 2, F_RR, 0, 1}, {"a", X_A, 11, 0x0C0, EVEN, 2, F_RR, 0, 1},
	{"sfh", X_SFH, 11, 0x048, EVEN, 2, F_RR, 0, 1}, {"sf", X_SF, 11, 0x040, EVEN, 2, F_RR, 0, 1},
	{"addx", X_ADDX, 11, 0x340, EVEN, 2, F_RR, 1, 1}, {"cg", X_CG, 11, 0x0C2, EVEN, 2, F_RR, 0, 1},
	{"cgx", X_CGX, 11, 0x342, EVEN, 2, F_RR, 1, 1}, {"sfx", X_SFX, 11, 0x341, EVEN, 2, F_RR, 1, 1},
	{"bg", X_BG, 11, 0x042, EVEN, 2, F_RR, 0, 1}, {"bgx", X_BGX, 11, 0x343, EVEN, 2, F_RR, 1, 1},
	{"mpy", X_MPY, 11, 0x3C4, EVEN, 7, F_RR, 0, 1}, {"mpyu", X_MPYU, 11, 0x3CC, EVEN, 7, F_RR, 0, 1},
	{"mpyh", X_MPYH, 11, 0x3C5, EVEN, 7, F_RR, 0, 1}, {"mpys", X_MPYS, 11, 0x3C7, EVEN, 7, F_RR, 0, 1},
	{"mpyhh", X_MPYHH, 11, 0x3C6, EVEN, 7, F_RR, 0, 1}, {"mpyhha", X_MPYHHA, 11, 0x346, EVEN, 7, F_RR, 1, 1},
	{"mpyhhu", X_MPYHHU, 11, 0x3CE, EVEN, 7, F_RR, 0, 1}, {"mpyhhau", X_MPYHHAU, 11, 0x34E, EVEN, 7, F_RR, 1, 1},
	{"clz", X_CLZ, 11, 0x2A5, EVEN, 2, F_RR, 0, 1}, {"cntb", X_CNTB, 11, 0x2B4, EVEN, 4, F_RR, 0, 1},
	{"fsmb", X_FSMB, 11, 0x1B6, ODD, 4, F_RR, 0, 1}, {"fsmh", X_FSMH, 11, 0x1B5, ODD, 4, F_RR, 0, 1},
	{"fsm", X_FSM, 11, 0x1B4, ODD, 4, F_RR, 0, 1}, {"gbb", X_GBB, 11, 0x1B2, ODD, 4, F_RR, 0, 1},
	{"gbh", X_GBH, 11, 0x1B1, ODD, 4, F_RR, 0, 1}, {"gb", X_GB, 11, 0x1B0, ODD, 4, F_RR, 0, 1},
	{"avgb", X_AVGB, 11, 0x0D3, EVEN, 4, F_RR, 0, 1}, {"absdb", X_ABSDB, 11, 0x053, EVEN, 4, F_RR, 0, 1},
	{"sumb", X_SUMB, 11, 0x253, EVEN, 4, F_RR, 0, 1}, {"xsbh", X_XSBH, 11, 0x2B6, EVEN, 2, F_RR, 0, 1},
	{"xshw", X_XSHW, 11, 0x2AE, EVEN, 2, F_RR, 0, 1}, {"xswd", X_XSWD, 11, 0x2A6, EVEN, 2, F_RR, 0, 1},
	{"and", X_AND, 11, 0x0C1, EVEN, 2, F_RR, 0, 1}, {"andc", X_ANDC, 11, 0x2C1, EVEN, 2, F_RR, 0, 1},
	{"or", X_OR, 11, 0x041, EVEN, 2, F_RR, 0, 1}, {"orc", X_ORC, 11, 0x2C9, EVEN, 2, F_RR, 0, 1},
	{"orx", X_ORX, 11, 0x1F0, ODD, 4, F_RR, 0, 1}, {"xor", X_XOR, 11, 0x241, EVEN, 2, F_RR, 0, 1},
	{"nand", X_NAND, 11, 0x0C9, EVEN, 2, F_RR, 0, 1}, {"nor", X_NOR, 11, 0x049, EVEN, 2, F_RR, 0, 1},
	{"eqv", X_EQV, 11, 0x249, EVEN, 2, F_RR, 0, 1}, {"shlh", X_SHLH, 11, 0x05F, EVEN, 4, F_RR, 0, 1},
	{"shl", X_SHL, 11, 0x05B, EVEN, 4, F_RR, 0, 1}, {"shlqbi", X_SHLQBI, 11, 0x1DB, ODD, 4, F_RR, 0, 1},
	{"shlqby", X_SHLQBY, 11, 0x1DF, ODD, 4, F_RR, 0, 1}, {"shlqbybi", X_SHLQBYBI, 11, 0x1CF, ODD, 4, F_RR, 0, 1},
	{"roth", X_ROTH, 11, 0x05C, EVEN, 4, F_RR, 0, 1}, {"rot", X_ROT, 11, 0x058, EVEN, 4, F_RR, 0, 1},
	{"rotqby", X_ROTQBY, 11, 0x1DC, ODD, 4, F_RR, 0, 1}, {"rotqbybi", X_ROTQBYBI, 11, 0x1CC, ODD, 4, F_RR, 0, 1},
	{"rotqbi", X_ROTQBI, 11, 0x1D8, ODD, 4, F_RR, 0, 1}, {"rothm", X_ROTHM, 11, 0x05D, EVEN, 4, F_RR, 0, 1},
	{"rotm", X_ROTM, 11, 0x059, EVEN, 4, F_RR, 0, 1}, {"rotqmby", X_ROTQMBY, 11, 0x1DD, ODD, 4, F_RR, 0, 1},
	{"rotqmbybi", X_ROTQMBYBI, 11, 0x1CD, ODD, 4, F_RR, 0, 1}, {"rotqmbi", X_ROTQMBI, 11, 0x1D9, ODD, 4, F_RR, 0, 1},
	{"rotmah", X_ROTMAH, 11, 0x05E, EVEN, 4, F_RR, 0, 1}, {"rotma", X_ROTMA, 11, 0x05A, EVEN, 4, F_RR, 0, 1},
	{"heq", X_HEQ, 11, 0x3D8, EVEN, 2, F_RR, 0, 0}, {"hgt", X_HGT, 11, 0x258, EVEN, 2, F_RR, 0, 0},
	{"hlgt", X_HLGT, 11, 0x2D8, EVEN, 2, F_RR, 0, 0}, {"ceqb", X_CEQB, 11, 0x3D0, EVEN, 2, F_RR, 0, 1},
	{"ceqh", X_CEQH, 11, 0x3C8, EVEN, 2, F_RR, 0, 1}, {"ceq", X_CEQ, 11, 0x3C0, EVEN, 2, F_RR, 0, 1},
	{"cgtb", X_CGTB, 11, 0x250, EVEN, 2, F_RR, 0, 1}, {"cgth", X_CGTH, 11, 0x248, EVEN, 2, F_RR, 0, 1},
	{"cgt", X_CGT, 11, 0x240, EVEN, 2, F_RR, 0, 1}, {"clgtb", X_CLGTB, 11, 0x2D0, EVEN, 2, F_RR, 0, 1},
	{"clgth", X_CLGTH, 11, 0x2C8, EVEN, 2, F_RR, 0, 1}, {"clgt", X_CLGT, 11, 0x2C0, EVEN, 2, F_RR, 0, 1},
	{"bi", X_BI, 11, 0x1A8, ODD, 0, F_RR, 0, 0}, {"iret", X_IRET, 11, 0x1AA, ODD, 0, F_RR, 0, 0},
	{"bisled", X_BISLED, 11, 0x1AB, ODD, 4, F_RR, 0, 1}, {"bisl", X_BISL, 11, 0x1A9, ODD, 4, F_RR, 0, 1},
	{"biz", X_BIZ, 11, 0x128, ODD, 0, F_RR, 1, 0}, {"binz", X_BINZ, 11, 0x129, ODD, 0, F_RR, 1, 0},
	{"bihz", X_BIHZ, 11, 0x12A, ODD, 0, F_RR, 1, 0}, {"bihnz", X_BIHNZ, 11, 0x12B, ODD, 0, F_RR, 1, 0},
	{"hbr", X_HBR, 11, 0x1AC, ODD, 0, F_RR, 0, 0}, {"stop", X_STOP, 11, 0x000, ODD, 0, F_NONE, 0, 0},
	{"stopd", X_STOPD, 11, 0x140, ODD, 0, F_NONE, 0, 0}, {"lnop", X_LNOP, 11, 0x001, ODD, 0, F_NONE, 0, 0},
	{"nop", X_NOP, 11, 0x201, EVEN, 0, F_NONE, 0, 0}, {"sync", X_SYNC, 11, 0x002, ODD, 0, F_NONE, 0, 0},
	{"dsync", X_DSYNC, 11, 0x003, ODD, 0, F_NONE, 0, 0}, {"rdch", X_RDCH, 11, 0x00D, ODD, 6, F_NONE, 0, 1},
	{"rchcnt", X_RCHCNT, 11, 0x00F, ODD, 6, F_NONE, 0, 1}, {"wrch", X_WRCH, 11, 0x10D, ODD, 6, F_NONE, 1, 0},
	{"fa", X_FA, 11, 0x2C4, EVEN, 6, F_RR, 0, 1}, {"fs", X_FS, 11, 0x2C5, EVEN, 6, F_RR, 0, 1},
	{"fm", X_FM, 11, 0x2C6, EVEN, 6, F_RR, 0, 1}, {"fceq", X_FCEQ, 11, 0x3C2, EVEN, 2, F_RR, 0, 1},
	{"fcgt", X_FCGT, 11, 0x2C2, EVEN, 2, F_RR, 0, 1},
	{"shlhi", X_SHLHI, 11, 0x07F, EVEN, 4, F_RI7, 0, 1}, {"shli", X_SHLI, 11, 0x07B, EVEN, 4, F_RI7, 0, 1},
	{"shlqbii", X_SHLQBII, 11, 0x1FB, ODD, 4, F_RI7, 0, 1}, {"shlqbyi", X_SHLQBYI, 11, 0x1FF, ODD, 4, F_RI7, 0, 1},
	{"rothi", X_ROTHI, 11, 0x07C, EVEN, 4, F_RI7, 0, 1}, {"roti", X_ROTI, 11, 0x078, EVEN, 4, F_RI7, 0, 1},
	{"rotqbyi", X_ROTQBYI, 11, 0x1FC, ODD, 4, F_RI7, 0, 1}, {"rotqbii", X_ROTQBII, 11, 0x1F8, ODD, 4, F_RI7, 0, 1},
	{"rothmi", X_ROTHMI, 11, 0x07D, EVEN, 4, F_RI7, 0, 1}, {"rotmi", X_ROTMI, 11, 0x079, EVEN, 4, F_RI7, 0, 1},
	{"rotqmbyi", X_ROTQMBYI, 11, 0x1FD, ODD, 4, F_RI7, 0, 1}, {"rotqmbii", X_ROTQMBII, 11, 0x1F9, ODD, 4, F_RI7, 0, 1},
	{"rotmahi", X_ROTMAHI, 11, 0x07E, EVEN, 4, F_RI7, 0, 1}, {"rotmai", X_ROTMAI, 11, 0x07A, EVEN, 4, F_RI7, 0, 1},
	{"cbd", X_CBD, 11, 0x1F4, ODD, 4, F_RI7, 0, 1}, {"chd", X_CHD, 11, 0x1F5, ODD, 4, F_RI7, 0, 1},
	{"cwd", X_CWD, 11, 0x1F6, ODD, 4, F_RI7, 0, 1}, {"cdd", X_CDD, 11, 0x1F7, ODD, 4, F_RI7, 0, 1},
	{"csflt", X_CSFLT, 10, 0x1DA, EVEN, 7, F_RI8, 0, 1}, {"cflts", X_CFLTS, 10, 0x1D8, EVEN, 7, F_RI8, 0, 1},
	{"cuflt", X_CUFLT, 10, 0x1DB, EVEN, 7, F_RI8, 0, 1}, {"cfltu", X_CFLTU, 10, 0x1D9, EVEN, 7, F_RI8, 0, 1},
	{"lqd", X_LQD, 8, 0x34, ODD, 6, F_RI10, 0, 1}, {"stqd", X_STQD, 8, 0x24, ODD, 6, F_RI10, 1, 0},
	{"ai", X_AI, 8, 0x1C, EVEN, 2, F_RI10, 0, 1}, {"ahi", X_AHI, 8, 0x1D, EVEN, 2, F_RI10, 0, 1},
	{"sfi", X_SFI, 8, 0x0C, EVEN, 2, F_RI10, 0, 1}, {"sfhi", X_SFHI, 8, 0x0D, EVEN, 2, F_RI10, 0, 1},
	{"mpyi", X_MPYI, 8, 0x74, EVEN, 7, F_RI10, 0, 1}, {"mpyui", X_MPYUI, 8, 0x75, EVEN, 7, F_RI10, 0, 1},
	{"andbi", X_ANDBI, 8, 0x16, EVEN, 2, F_RI10, 0, 1}, {"andhi", X_ANDHI, 8, 0x15, EVEN, 2, F_RI10, 0, 1},
	{"andi", X_ANDI, 8, 0x14, EVEN, 2, F_RI10, 0, 1}, {"orbi", X_ORBI, 8, 0x06, EVEN, 2, F_RI10, 0, 1},
	{"orhi", X_ORHI, 8, 0x05, EVEN, 2, F_RI10, 0, 1}, {"ori", X_ORI, 8, 0x04, EVEN, 2, F_RI10, 0, 1},
	{"xorbi", X_XORBI, 8, 0x46, EVEN, 2, F_RI10, 0, 1}, {"xorhi", X_XORHI, 8, 0x45, EVEN, 2, F_RI10, 0, 1},
	{"xori", X_XORI, 8, 0x44, EVEN, 2, F_RI10, 0, 1}, {"heqi", X_HEQI, 8, 0x7F, EVEN, 2, F_RI10, 0, 0},
	{"hgti", X_HGTI, 8, 0x4F, EVEN, 2, F_RI10, 0, 0}, {"hlgti", X_HLGTI, 8, 0x5F, EVEN, 2, F_RI10, 0, 0},
	{"ceqbi", X_CEQBI, 8, 0x7E, EVEN, 2, F_RI10, 0, 1}, {"ceqhi", X_CEQHI, 8, 0x7D, EVEN, 2, F_RI10, 0, 1},
	{"ceqi", X_CEQI, 8, 0x7C, EVEN, 2, F_RI10, 0, 1}, {"cgtbi", X_CGTBI, 8, 0x4E, EVEN, 2, F_RI10, 0, 1},
	{"cgthi", X_CGTHI, 8, 0x4D, EVEN, 2, F_RI10, 0, 1}, {"cgti", X_CGTI, 8, 0x4C, EVEN, 2, F_RI10, 0, 1},
	{"clgtbi", X_CLGTBI, 8, 0x5E, EVEN, 2, F_RI10, 0, 1}, {"clgthi", X_CLGTHI, 8, 0x5D, EVEN, 2, F_RI10, 0, 1},
	{"clgti", X_CLGTI, 8, 0x5C, EVEN, 2, F_RI10, 0, 1},
	{"bra", X_BRA, 9, 0x060, ODD, 0, F_RI16, 0, 0}, {"br", X_BR, 9, 0x064, ODD, 0, F_RI16, 0, 0},
	{"brasl", X_BRASL, 9, 0x062, ODD, 4, F_RI16, 0, 1}, {"brsl", X_BRSL, 9, 0x066, ODD, 4, F_RI16, 0, 1},
	{"brnz", X_BRNZ, 9, 0x042, ODD, 0, F_RI16, 1, 0}, {"brz", X_BRZ, 9, 0x040, ODD, 0, F_RI16, 1, 0},
	{"brhnz", X_BRHNZ, 9, 0x046, ODD, 0, F_RI16, 1, 0}, {"brhz", X_BRHZ, 9, 0x044, ODD, 0, F_RI16, 1, 0},
	{"lqa", X_LQA, 9, 0x061, ODD, 6, F_RI16, 0, 1}, {"lqr", X_LQR, 9, 0x067, ODD, 6, F_RI16, 0, 1},
	{"stqa", X_STQA, 9, 0x041, ODD, 6, F_RI16, 1, 0}, {"stqr", X_STQR, 9, 0x047, ODD, 6, F_RI16, 1, 0},
	{"ilh", X_ILH, 9, 0x083, EVEN, 2, F_RI16, 0, 1}, {"ilhu", X_ILHU, 9, 0x082, EVEN, 2, F_RI16, 0, 1},
	{"il", X_IL, 9, 0x081, EVEN, 2, F_RI16, 0, 1}, {"iohl", X_IOHL, 9, 0x0C1, EVEN, 2, F_RI16, 1, 1},
	{"fsmbi", X_FSMBI, 9, 0x065, ODD, 4, F_RI16, 0, 1},
	{"ila", X_ILA, 7, 0x21, EVEN, 2, F_RI18, 0, 1}, {"hbra", X_HBRA, 7, 0x08, ODD, 0, F_NONE, 0, 0},
	{"hbrr", X_HBRR, 7, 0x09, ODD, 0, F_NONE, 0, 0},
	{"selb", X_SELB, 4, 0x8, EVEN, 2, F_RRR, 0, 1}, {"shufb", X_SHUFB, 4, 0xB, ODD, 4, F_RRR, 0, 1},
	{"mpya", X_MPYA, 4, 0xC, EVEN, 7, F_RRR, 0, 1}, {"fnms", X_FNMS, 4, 0xD, EVEN, 6, F_RRR, 0, 1},
	{"fma", X_FMA, 4, 0xE, EVEN, 6, F_RRR, 0, 1}, {"fms", X_FMS, 4, 0xF, EVEN, 6, F_RRR, 0, 1},
};

static const OpInfo* s_dec[2048];

static void build_decoder(void)
{
	if (s_dec[0])
		return;
	for (size_t i = 0; i < sizeof(k_ops) / sizeof(k_ops[0]); i++)
	{
		const OpInfo* o = &k_ops[i];
		const int free_bits = 11 - o->width;
		for (int k = 0; k < (1 << free_bits); k++)
		{
			const int idx = (o->code << free_bits) | k;
			if (s_dec[idx] && s_dec[idx] != o)
			{
				fprintf(stderr, "spusim: opcode clash %s / %s at %03X\n", s_dec[idx]->name, o->name, idx);
				exit(2);
			}
			s_dec[idx] = o;
		}
	}
}

// ---- symbols (for the profile) ----
typedef struct { uint32_t addr, size; char name[64]; } Sym;
static Sym* s_syms;
static int s_nsyms;

static int sym_cmp(const void* a, const void* b)
{
	const Sym *x = (const Sym*)a, *y = (const Sym*)b;
	return x->addr < y->addr ? -1 : x->addr > y->addr;
}

const char* spusim_sym(uint32_t pc)
{
	int lo = 0, hi = s_nsyms - 1, best = -1;
	while (lo <= hi)
	{
		const int m = (lo + hi) / 2;
		if (s_syms[m].addr <= pc) { best = m; lo = m + 1; }
		else hi = m - 1;
	}
	return best >= 0 ? s_syms[best].name : "?";
}

static uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint16_t be16(const uint8_t* p) { return (uint16_t)(p[0] << 8 | p[1]); }

uint32_t spusim_load_elf(SpuSim* s, const char* path)
{
	FILE* f = fopen(path, "rb");
	if (!f)
		return 0;
	fseek(f, 0, SEEK_END);
	const long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	uint8_t* e = (uint8_t*)malloc((size_t)n);
	if (fread(e, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(e); return 0; }
	fclose(f);
	const uint32_t entry = be32(e + 24), phoff = be32(e + 28), shoff = be32(e + 32);
	const uint16_t phentsize = be16(e + 42), phnum = be16(e + 44), shentsize = be16(e + 46), shnum = be16(e + 48);
	for (int i = 0; i < phnum; i++)
	{
		const uint8_t* p = e + phoff + i * phentsize;
		if (be32(p) != 1)
			continue;
		const uint32_t off = be32(p + 4), va = be32(p + 8), fsz = be32(p + 16), msz = be32(p + 20);
		if (va + msz > sizeof(s->ls)) { free(e); return 0; }
		memcpy(s->ls + va, e + off, fsz);
		memset(s->ls + va + fsz, 0, msz - fsz);
	}
	s_nsyms = 0;
	for (int i = 0; i < shnum; i++)
	{
		const uint8_t* sh = e + shoff + i * shentsize;
		if (be32(sh + 4) != 2)   // SHT_SYMTAB
			continue;
		const uint8_t* str = e + be32(e + shoff + be32(sh + 24) * shentsize + 16);
		const uint32_t off = be32(sh + 16), sz = be32(sh + 20), es = be32(sh + 36);
		s_syms = (Sym*)realloc(s_syms, sizeof(Sym) * (sz / es));
		for (uint32_t k = 0; k < sz / es; k++)
		{
			const uint8_t* y = e + off + k * es;
			if ((y[12] & 0xF) != 2)   // STT_FUNC
				continue;
			Sym* d = &s_syms[s_nsyms++];
			d->addr = be32(y + 4);
			d->size = be32(y + 8);
			snprintf(d->name, sizeof(d->name), "%s", (const char*)str + be32(y));
		}
	}
	qsort(s_syms, (size_t)s_nsyms, sizeof(Sym), sym_cmp);
	free(e);
	return entry;
}

// ---- quadword helpers (w[0] = bytes 0..3, big-endian) ----
static uint8_t qb(const SpuQ* q, int i) { return (uint8_t)(q->w[i >> 2] >> (24 - 8 * (i & 3))); }
static void set_qb(SpuQ* q, int i, uint8_t v)
{
	const int sh = 24 - 8 * (i & 3);
	q->w[i >> 2] = (q->w[i >> 2] & ~(0xFFu << sh)) | ((uint32_t)v << sh);
}
static uint16_t qh(const SpuQ* q, int i) { return (uint16_t)(q->w[i >> 1] >> (16 - 16 * (i & 1))); }
static void set_qh(SpuQ* q, int i, uint16_t v)
{
	const int sh = 16 - 16 * (i & 1);
	q->w[i >> 1] = (q->w[i >> 1] & ~(0xFFFFu << sh)) | ((uint32_t)v << sh);
}
static int32_t sext(uint32_t v, int bits) { return (int32_t)(v << (32 - bits)) >> (32 - bits); }
static SpuQ splat(uint32_t v) { SpuQ q = {{v, v, v, v}}; return q; }

static SpuQ ld(const SpuSim* s, uint32_t a)
{
	SpuQ q;
	a &= LSLR & ~15u;
	for (int i = 0; i < 4; i++)
		q.w[i] = be32(s->ls + a + 4 * i);
	return q;
}
static void st(SpuSim* s, uint32_t a, const SpuQ* q)
{
	a &= LSLR & ~15u;
	for (int i = 0; i < 4; i++)
	{
		uint8_t* p = s->ls + a + 4 * i;
		p[0] = (uint8_t)(q->w[i] >> 24); p[1] = (uint8_t)(q->w[i] >> 16); p[2] = (uint8_t)(q->w[i] >> 8); p[3] = (uint8_t)q->w[i];
	}
}

static SpuQ ins_mask(uint32_t t, int bytes)
{
	SpuQ m;
	for (int i = 0; i < 16; i++)
		set_qb(&m, i, (uint8_t)(0x10 + i));
	const int base = bytes == 1 ? 3 : bytes == 2 ? 2 : 0;
	for (int i = 0; i < bytes; i++)
		set_qb(&m, (int)t + i, (uint8_t)(base + i));
	return m;
}

static SpuQ rot_bytes(const SpuQ* a, uint32_t n)
{
	SpuQ t;
	for (int i = 0; i < 16; i++)
		set_qb(&t, i, qb(a, (int)((i + n) & 15)));
	return t;
}
static SpuQ shl_bytes(const SpuQ* a, uint32_t n)
{
	SpuQ t;
	for (int i = 0; i < 16; i++)
		set_qb(&t, i, i + n < 16 ? qb(a, (int)(i + n)) : 0);
	return t;
}
static SpuQ shr_bytes(const SpuQ* a, uint32_t n)
{
	SpuQ t;
	for (int i = 0; i < 16; i++)
		set_qb(&t, i, (int)i - (int)n >= 0 ? qb(a, (int)(i - n)) : 0);
	return t;
}
// whole-quadword bit shifts by 0..7
static SpuQ shl_bits(const SpuQ* a, uint32_t n)
{
	SpuQ t;
	for (int i = 0; i < 4; i++)
		t.w[i] = n ? (a->w[i] << n) | (i < 3 ? a->w[i + 1] >> (32 - n) : 0) : a->w[i];
	return t;
}
static SpuQ rot_bits(const SpuQ* a, uint32_t n)
{
	SpuQ t;
	for (int i = 0; i < 4; i++)
		t.w[i] = n ? (a->w[i] << n) | (a->w[(i + 1) & 3] >> (32 - n)) : a->w[i];
	return t;
}
static SpuQ shr_bits(const SpuQ* a, uint32_t n)
{
	SpuQ t;
	for (int i = 0; i < 4; i++)
		t.w[i] = n ? (a->w[i] >> n) | (i > 0 ? a->w[i - 1] << (32 - n) : 0) : a->w[i];
	return t;
}

// SPU single precision: denormals are zero, results truncate toward zero, no inf/NaN (saturate)
static float f_in(uint32_t w)
{
	if (((w >> 23) & 0xFF) == 0)
		return 0.0f;
	float f;
	memcpy(&f, &w, 4);
	return f;
}
static uint32_t f_out(double d)
{
	if (d == 0.0 || fabs(d) < 1.1754943508222875e-38)
		return d < 0 ? 0x80000000u : 0;
	float f = (float)d;
	if (fabs((double)f) > fabs(d))   // rounded away from zero: step back one ulp
	{
		uint32_t w;
		memcpy(&w, &f, 4);
		w--;
		memcpy(&f, &w, 4);
	}
	uint32_t w;
	memcpy(&w, &f, 4);
	if (((w >> 23) & 0xFF) == 0xFF)
		w = (w & 0x80000000u) | 0x7FFFFFFFu;
	return w;
}

void spusim_init(SpuSim* s, SpuEaFn ea, void* ctx)
{
	build_decoder();
	memset(s, 0, sizeof(*s));
	s->ea = ea;
	s->ctx = ctx;
	s->dma_lat = 500;
	s->prev_pc = 0xFFFFFFFFu;
	s->prev_rt = -1;
}

void spusim_llr_lost(SpuSim* s)
{
	if (s->llr_on)
	{
		s->llr_on = 0;
		s->ev_pend |= 0x400;   // MFC_LLR_LOST_EVENT
	}
}

static int sim_fault(SpuSim* s, uint32_t w, const char* why)
{
	s->fault = 1;
	snprintf(s->msg, sizeof(s->msg), "%s: insn %08X at pc %05X (%s)", why, w, s->pc, spusim_sym(s->pc));
	return SPU_RUN_FAULT;
}

static uint32_t dec_now(const SpuSim* s) { return s->dec_val - (uint32_t)((s->cycle - s->dec_cycle) / DEC_DIV); }

static int do_dma(SpuSim* s, uint32_t cmd)
{
	const uint32_t tag = s->tagid & 31, base = cmd & ~0x7u & 0xFF;
	const uint64_t ea = ((uint64_t)s->eah << 32) | s->eal;
	uint64_t done = s->cycle + s->dma_lat;
	s->dmas++;
	if (base == 0xD0 || base == 0xB0 || base == 0xB4 || base == 0xB8)   // getllar, putlluc, putllc, putqlluc
	{
		uint8_t* m = s->ea(s->ctx, ea & ~127ull, 128);
		if (!m)
			return 1;
		const uint32_t lsa = s->lsa & LSLR & ~127u;
		if (base == 0xD0)
		{
			memcpy(s->ls + lsa, m, 128);
			s->llr_ea = ea & ~127ull;
			s->llr_on = 1;
			s->atomic_stat = 4;
		}
		else if (base == 0xB4)
		{
			if (s->llr_on && s->llr_ea == (ea & ~127ull)) { memcpy(m, s->ls + lsa, 128); s->atomic_stat = 0; }
			else s->atomic_stat = 1;
			s->llr_on = 0;
		}
		else
		{
			memcpy(m, s->ls + lsa, 128);
			s->atomic_stat = 2;
		}
		return 0;
	}
	const int put = (base & 0xE0) == 0x20, get = (base & 0xE0) == 0x40, list = (cmd & 4) != 0;
	if (!put && !get)
		return 1;
	if (!list)
	{
		const uint32_t lsa = s->lsa & LSLR, size = s->size;
		if (size > 16384 || ((lsa ^ (uint32_t)ea) & 15) || (size >= 16 && (size & 15)) ||
		    (size < 16 && size != 1 && size != 2 && size != 4 && size != 8) || (size < 16 && ((lsa | (uint32_t)ea) & (size - 1))))
			return 1;
		uint8_t* m = s->ea(s->ctx, ea, size);
		if (!m)
			return 1;
		if (put) memcpy(m, s->ls + lsa, size);
		else memcpy(s->ls + lsa, m, size);
		s->dma_bytes += size;
		done += size > 128 ? (size - 128) / 16 : 0;
	}
	else
	{
		uint32_t lsa = s->lsa & LSLR;
		const uint32_t la = s->eal & LSLR & ~7u, n = s->size / 8;
		for (uint32_t i = 0; i < n; i++)
		{
			const uint32_t w0 = be32(s->ls + la + 8 * i), eal = be32(s->ls + la + 8 * i + 4), size = w0 & 0x7FFF;
			const uint64_t eai = ((uint64_t)s->eah << 32) | eal;
			if (size > 16384 || ((lsa ^ eal) & 15))
				return 1;
			uint8_t* m = s->ea(s->ctx, eai, size);
			if (!m)
				return 1;
			if (put) memcpy(m, s->ls + lsa, size);
			else memcpy(s->ls + lsa, m, size);
			s->dma_bytes += size;
			lsa = (lsa + size) & LSLR;
			done += 16 + size / 16;
		}
	}
	if (done > s->tag_done[tag])
		s->tag_done[tag] = done;
	return 0;
}

// Channel read; returns 1 when the read would block (nothing pending), 2 on a fault.
static int ch_read(SpuSim* s, uint32_t ch, uint32_t* v, uint64_t* wait_until)
{
	*wait_until = 0;
	switch (ch)
	{
	case 0:   // SPU_RdEventStat
	{
		if (s->dec_armed && (int32_t)dec_now(s) < 0)   // fires once per decrementer write
		{
			s->ev_pend |= 0x20;
			s->dec_armed = 0;
		}
		const uint32_t e = s->ev_pend & s->ev_mask;
		if (!e)
			return 1;
		*v = e;
		return 0;
	}
	case 8: *v = dec_now(s); return 0;   // SPU_RdDec
	case 11: *v = s->ev_mask; return 0;
	case 12: *v = s->tag_mask; return 0;
	case 24:   // MFC_RdTagStat
	{
		uint64_t t = 0;
		uint32_t done = 0;
		for (int i = 0; i < 32; i++)
			if (s->tag_mask & (1u << i))
			{
				if (s->tag_done[i] > t) t = s->tag_done[i];
				done |= 1u << i;
			}
		if (s->tag_upd != 0)
			*wait_until = t;
		*v = done;
		return 0;
	}
	case 27: *v = s->atomic_stat; return 0;   // MFC_RdAtomicStat
	default: return 2;
	}
}

static int ch_write(SpuSim* s, uint32_t ch, uint32_t v)
{
	switch (ch)
	{
	case 1: s->ev_mask = v; return 0;
	case 2: s->ev_pend &= ~v; return 0;
	case 7: s->dec_val = v; s->dec_cycle = s->cycle; s->dec_armed = 1; return 0;
	case 16: s->lsa = v; return 0;
	case 17: s->eah = v; return 0;
	case 18: s->eal = v; return 0;
	case 19: s->size = v & 0xFFFF; return 0;
	case 20: s->tagid = v; return 0;
	case 21: return do_dma(s, v & 0xFFFF);
	case 22: s->tag_mask = v; return 0;
	case 23: s->tag_upd = v; return 0;
	case 28: s->out_mbox = v; return 0;
	case 30: s->out_mbox = v; return 0;
	default: return 1;
	}
}

static int branch_hinted(const SpuSim* s, uint32_t pc, uint32_t tgt, uint64_t issue)
{
	return s->hint_on && s->hint_br == pc && s->hint_tgt == tgt && issue >= s->hint_cycle + HINT_LEAD;
}

int spusim_run(SpuSim* s, uint64_t max_insns)
{
	for (uint64_t n = 0; n < max_insns; n++)
	{
		const uint32_t pc = s->pc & LSLR & ~3u;
		const uint32_t w = be32(s->ls + pc);
		const OpInfo* o = s_dec[w >> 21];
		if (!o)
			return sim_fault(s, w, "unknown instruction");
		int rt = w & 0x7F;
		const int ra = (w >> 7) & 0x7F, rb = (w >> 14) & 0x7F;
		int rc = -1;
		if (o->x == X_RDCH && ra == 0)   // blocking event read: hand control back before any timing state moves
		{
			uint32_t v;
			uint64_t wu;
			if (ch_read(s, 0, &v, &wu) == 1)
			{
				s->pair_ok = 0;
				return SPU_RUN_BLOCKED;
			}
		}
		if (o->form == F_RRR)
		{
			rc = rt;
			rt = (w >> 21) & 0x7F;
		}

		// ---- timing: earliest issue after operands and the in-order rule ----
		uint64_t t = s->next_min;
		const int uses_ra = o->form == F_RR || o->form == F_RI7 || o->form == F_RI8 || o->form == F_RI10 || o->form == F_RRR;
		const int uses_rb = o->form == F_RR || o->form == F_RRR;
		int srcs[4], ns = 0;
		if (uses_ra) srcs[ns++] = ra;
		if (uses_rb && !(o->x >= X_BI && o->x <= X_BIHNZ) && o->x != X_HBR) srcs[ns++] = rb;
		if (rc >= 0) srcs[ns++] = rc;
		if (o->rd_rt) srcs[ns++] = rt;
		if (o->x == X_RDCH || o->x == X_RCHCNT || o->x == X_WRCH)   // channel number is in ra's field
			ns = o->rd_rt ? 1 : 0, srcs[0] = rt;
		int dep_prev = 0;
		for (int i = 0; i < ns; i++)
		{
			if (s->ready[srcs[i]] > t) t = s->ready[srcs[i]];
			if (srcs[i] == s->prev_rt) dep_prev = 1;
		}
		if (s->pair_ok && s->prev_pc == pc - 4 && (pc & 4) && o->pipe == ODD && s->prev_pipe == EVEN && !dep_prev && t <= s->cycle)
		{
			t = s->cycle;
			s->dual++;
		}
		else if (t < s->cycle + 1 && s->insns)
			t = s->cycle + 1;
		const uint64_t prev_cycle = s->cycle;
		s->cycle = t;
		s->prev_pc = pc;
		s->prev_pipe = o->pipe;
		s->prev_rt = o->wr_rt ? rt : -1;
		s->pair_ok = 1;
		s->insns++;

		uint32_t npc = (pc + 4) & LSLR;
		int taken = 0;
		uint32_t tgt = 0;
		SpuQ R = s->r[rt];
		const SpuQ A = s->r[ra], B = s->r[rb];
		const SpuQ C = rc >= 0 ? s->r[rc] : splat(0);
		const SpuQ T = s->r[rt];
		const int32_t i7 = sext((w >> 14) & 0x7F, 7);
		const int32_t i10 = sext((w >> 14) & 0x3FF, 10);
		const uint32_t i16 = (w >> 7) & 0xFFFF;
		const int32_t si16 = sext(i16, 16);
		uint64_t extra_wait = 0;
		int write = o->wr_rt;

		switch (o->x)
		{
		case X_LQX: R = ld(s, A.w[0] + B.w[0]); break;
		case X_STQX: st(s, A.w[0] + B.w[0], &T); break;
		case X_CBX: R = ins_mask((A.w[0] + B.w[0]) & 0xF, 1); break;
		case X_CHX: R = ins_mask((A.w[0] + B.w[0]) & 0xE, 2); break;
		case X_CWX: R = ins_mask((A.w[0] + B.w[0]) & 0xC, 4); break;
		case X_CDX: R = ins_mask((A.w[0] + B.w[0]) & 0x8, 8); break;
		case X_CBD: R = ins_mask((A.w[0] + (uint32_t)i7) & 0xF, 1); break;
		case X_CHD: R = ins_mask((A.w[0] + (uint32_t)i7) & 0xE, 2); break;
		case X_CWD: R = ins_mask((A.w[0] + (uint32_t)i7) & 0xC, 4); break;
		case X_CDD: R = ins_mask((A.w[0] + (uint32_t)i7) & 0x8, 8); break;
		case X_AH: for (int i = 0; i < 8; i++) set_qh(&R, i, (uint16_t)(qh(&A, i) + qh(&B, i))); break;
		case X_A: for (int i = 0; i < 4; i++) R.w[i] = A.w[i] + B.w[i]; break;
		case X_SFH: for (int i = 0; i < 8; i++) set_qh(&R, i, (uint16_t)(qh(&B, i) - qh(&A, i))); break;
		case X_SF: for (int i = 0; i < 4; i++) R.w[i] = B.w[i] - A.w[i]; break;
		case X_ADDX: for (int i = 0; i < 4; i++) R.w[i] = A.w[i] + B.w[i] + (T.w[i] & 1); break;
		case X_CG: for (int i = 0; i < 4; i++) R.w[i] = (uint32_t)(((uint64_t)A.w[i] + B.w[i]) >> 32); break;
		case X_CGX: for (int i = 0; i < 4; i++) R.w[i] = (uint32_t)(((uint64_t)A.w[i] + B.w[i] + (T.w[i] & 1)) >> 32); break;
		case X_SFX: for (int i = 0; i < 4; i++) R.w[i] = B.w[i] + ~A.w[i] + (T.w[i] & 1); break;
		case X_BG: for (int i = 0; i < 4; i++) R.w[i] = B.w[i] >= A.w[i]; break;
		case X_BGX:
			for (int i = 0; i < 4; i++)
				R.w[i] = (T.w[i] & 1) ? B.w[i] >= A.w[i] : B.w[i] > A.w[i];
			break;
		case X_MPY: for (int i = 0; i < 4; i++) R.w[i] = (uint32_t)((int32_t)(int16_t)A.w[i] * (int32_t)(int16_t)B.w[i]); break;
		case X_MPYU: for (int i = 0; i < 4; i++) R.w[i] = (A.w[i] & 0xFFFF) * (B.w[i] & 0xFFFF); break;
		case X_MPYH: for (int i = 0; i < 4; i++) R.w[i] = ((A.w[i] >> 16) * (B.w[i] & 0xFFFF)) << 16; break;
		case X_MPYS:
			for (int i = 0; i < 4; i++)
				R.w[i] = (uint32_t)(((int32_t)(int16_t)A.w[i] * (int32_t)(int16_t)B.w[i]) >> 16);
			break;
		case X_MPYHH: for (int i = 0; i < 4; i++) R.w[i] = (uint32_t)((int32_t)(int16_t)(A.w[i] >> 16) * (int32_t)(int16_t)(B.w[i] >> 16)); break;
		case X_MPYHHA: for (int i = 0; i < 4; i++) R.w[i] = T.w[i] + (uint32_t)((int32_t)(int16_t)(A.w[i] >> 16) * (int32_t)(int16_t)(B.w[i] >> 16)); break;
		case X_MPYHHU: for (int i = 0; i < 4; i++) R.w[i] = (A.w[i] >> 16) * (B.w[i] >> 16); break;
		case X_MPYHHAU: for (int i = 0; i < 4; i++) R.w[i] = T.w[i] + (A.w[i] >> 16) * (B.w[i] >> 16); break;
		case X_MPYA:
			for (int i = 0; i < 4; i++)
				R.w[i] = (uint32_t)((int32_t)(int16_t)A.w[i] * (int32_t)(int16_t)B.w[i]) + C.w[i];
			break;
		case X_MPYI: for (int i = 0; i < 4; i++) R.w[i] = (uint32_t)((int32_t)(int16_t)A.w[i] * (int32_t)(int16_t)i10); break;
		case X_MPYUI: for (int i = 0; i < 4; i++) R.w[i] = (A.w[i] & 0xFFFF) * ((uint32_t)i10 & 0xFFFF); break;
		case X_CLZ: for (int i = 0; i < 4; i++) R.w[i] = A.w[i] ? (uint32_t)__builtin_clz(A.w[i]) : 32; break;
		case X_CNTB: for (int i = 0; i < 16; i++) set_qb(&R, i, (uint8_t)__builtin_popcount(qb(&A, i))); break;
		case X_FSMB: for (int i = 0; i < 16; i++) set_qb(&R, i, (A.w[0] >> (15 - i)) & 1 ? 0xFF : 0); break;
		case X_FSMH: for (int i = 0; i < 8; i++) set_qh(&R, i, (A.w[0] >> (7 - i)) & 1 ? 0xFFFF : 0); break;
		case X_FSM: for (int i = 0; i < 4; i++) R.w[i] = (A.w[0] >> (3 - i)) & 1 ? ~0u : 0; break;
		case X_FSMBI: for (int i = 0; i < 16; i++) set_qb(&R, i, (i16 >> (15 - i)) & 1 ? 0xFF : 0); break;
		case X_GBB:
		{
			uint32_t v = 0;
			for (int i = 0; i < 16; i++) v = (v << 1) | (qb(&A, i) & 1);
			R = splat(0); R.w[0] = v;
			break;
		}
		case X_GBH:
		{
			uint32_t v = 0;
			for (int i = 0; i < 8; i++) v = (v << 1) | (qh(&A, i) & 1);
			R = splat(0); R.w[0] = v;
			break;
		}
		case X_GB:
		{
			uint32_t v = 0;
			for (int i = 0; i < 4; i++) v = (v << 1) | (A.w[i] & 1);
			R = splat(0); R.w[0] = v;
			break;
		}
		case X_AVGB: for (int i = 0; i < 16; i++) set_qb(&R, i, (uint8_t)((qb(&A, i) + qb(&B, i) + 1) >> 1)); break;
		case X_ABSDB:
			for (int i = 0; i < 16; i++)
			{
				const int d = (int)qb(&B, i) - (int)qb(&A, i);
				set_qb(&R, i, (uint8_t)(d < 0 ? -d : d));
			}
			break;
		case X_SUMB:
			for (int i = 0; i < 4; i++)
			{
				uint32_t sa = 0, sb = 0;
				for (int k = 0; k < 4; k++) { sa += qb(&A, 4 * i + k); sb += qb(&B, 4 * i + k); }
				R.w[i] = (sb << 16) | (sa & 0xFFFF);
			}
			break;
		case X_XSBH: for (int i = 0; i < 8; i++) set_qh(&R, i, (uint16_t)(int16_t)(int8_t)qh(&A, i)); break;
		case X_XSHW: for (int i = 0; i < 4; i++) R.w[i] = (uint32_t)(int32_t)(int16_t)A.w[i]; break;
		case X_XSWD:
			for (int i = 0; i < 2; i++)
			{
				R.w[2 * i + 1] = A.w[2 * i + 1];
				R.w[2 * i] = (A.w[2 * i + 1] & 0x80000000u) ? ~0u : 0;
			}
			break;
		case X_AND: for (int i = 0; i < 4; i++) R.w[i] = A.w[i] & B.w[i]; break;
		case X_ANDC: for (int i = 0; i < 4; i++) R.w[i] = A.w[i] & ~B.w[i]; break;
		case X_OR: for (int i = 0; i < 4; i++) R.w[i] = A.w[i] | B.w[i]; break;
		case X_ORC: for (int i = 0; i < 4; i++) R.w[i] = A.w[i] | ~B.w[i]; break;
		case X_ORX: R = splat(0); R.w[0] = A.w[0] | A.w[1] | A.w[2] | A.w[3]; break;
		case X_XOR: for (int i = 0; i < 4; i++) R.w[i] = A.w[i] ^ B.w[i]; break;
		case X_NAND: for (int i = 0; i < 4; i++) R.w[i] = ~(A.w[i] & B.w[i]); break;
		case X_NOR: for (int i = 0; i < 4; i++) R.w[i] = ~(A.w[i] | B.w[i]); break;
		case X_EQV: for (int i = 0; i < 4; i++) R.w[i] = ~(A.w[i] ^ B.w[i]); break;
		case X_ANDBI: for (int i = 0; i < 16; i++) set_qb(&R, i, qb(&A, i) & (uint8_t)i10); break;
		case X_ANDHI: for (int i = 0; i < 8; i++) set_qh(&R, i, qh(&A, i) & (uint16_t)i10); break;
		case X_ANDI: for (int i = 0; i < 4; i++) R.w[i] = A.w[i] & (uint32_t)i10; break;
		case X_ORBI: for (int i = 0; i < 16; i++) set_qb(&R, i, qb(&A, i) | (uint8_t)i10); break;
		case X_ORHI: for (int i = 0; i < 8; i++) set_qh(&R, i, qh(&A, i) | (uint16_t)i10); break;
		case X_ORI: for (int i = 0; i < 4; i++) R.w[i] = A.w[i] | (uint32_t)i10; break;
		case X_XORBI: for (int i = 0; i < 16; i++) set_qb(&R, i, qb(&A, i) ^ (uint8_t)i10); break;
		case X_XORHI: for (int i = 0; i < 8; i++) set_qh(&R, i, qh(&A, i) ^ (uint16_t)i10); break;
		case X_XORI: for (int i = 0; i < 4; i++) R.w[i] = A.w[i] ^ (uint32_t)i10; break;
		case X_SHLH:
			for (int i = 0; i < 8; i++) { const uint32_t c = qh(&B, i) & 0x1F; set_qh(&R, i, c < 16 ? (uint16_t)(qh(&A, i) << c) : 0); }
			break;
		case X_SHLHI:
			for (int i = 0; i < 8; i++) { const uint32_t c = (uint32_t)i7 & 0x1F; set_qh(&R, i, c < 16 ? (uint16_t)(qh(&A, i) << c) : 0); }
			break;
		case X_SHL: for (int i = 0; i < 4; i++) { const uint32_t c = B.w[i] & 0x3F; R.w[i] = c < 32 ? A.w[i] << c : 0; } break;
		case X_SHLI: for (int i = 0; i < 4; i++) { const uint32_t c = (uint32_t)i7 & 0x3F; R.w[i] = c < 32 ? A.w[i] << c : 0; } break;
		case X_ROTH: for (int i = 0; i < 8; i++) { const uint32_t c = qh(&B, i) & 0xF; const uint16_t v = qh(&A, i); set_qh(&R, i, c ? (uint16_t)((v << c) | (v >> (16 - c))) : v); } break;
		case X_ROTHI: for (int i = 0; i < 8; i++) { const uint32_t c = (uint32_t)i7 & 0xF; const uint16_t v = qh(&A, i); set_qh(&R, i, c ? (uint16_t)((v << c) | (v >> (16 - c))) : v); } break;
		case X_ROT: for (int i = 0; i < 4; i++) { const uint32_t c = B.w[i] & 0x1F; R.w[i] = c ? (A.w[i] << c) | (A.w[i] >> (32 - c)) : A.w[i]; } break;
		case X_ROTI: for (int i = 0; i < 4; i++) { const uint32_t c = (uint32_t)i7 & 0x1F; R.w[i] = c ? (A.w[i] << c) | (A.w[i] >> (32 - c)) : A.w[i]; } break;
		case X_ROTHM: for (int i = 0; i < 8; i++) { const uint32_t c = (0 - qh(&B, i)) & 0x1F; set_qh(&R, i, c < 16 ? (uint16_t)(qh(&A, i) >> c) : 0); } break;
		case X_ROTHMI: for (int i = 0; i < 8; i++) { const uint32_t c = (0 - (uint32_t)i7) & 0x1F; set_qh(&R, i, c < 16 ? (uint16_t)(qh(&A, i) >> c) : 0); } break;
		case X_ROTM: for (int i = 0; i < 4; i++) { const uint32_t c = (0 - B.w[i]) & 0x3F; R.w[i] = c < 32 ? A.w[i] >> c : 0; } break;
		case X_ROTMI: for (int i = 0; i < 4; i++) { const uint32_t c = (0 - (uint32_t)i7) & 0x3F; R.w[i] = c < 32 ? A.w[i] >> c : 0; } break;
		case X_ROTMAH: for (int i = 0; i < 8; i++) { const uint32_t c = (0 - qh(&B, i)) & 0x1F; set_qh(&R, i, (uint16_t)((int16_t)qh(&A, i) >> (c < 16 ? c : 15))); } break;
		case X_ROTMAHI: for (int i = 0; i < 8; i++) { const uint32_t c = (0 - (uint32_t)i7) & 0x1F; set_qh(&R, i, (uint16_t)((int16_t)qh(&A, i) >> (c < 16 ? c : 15))); } break;
		case X_ROTMA: for (int i = 0; i < 4; i++) { const uint32_t c = (0 - B.w[i]) & 0x3F; R.w[i] = (uint32_t)((int32_t)A.w[i] >> (c < 32 ? c : 31)); } break;
		case X_ROTMAI: for (int i = 0; i < 4; i++) { const uint32_t c = (0 - (uint32_t)i7) & 0x3F; R.w[i] = (uint32_t)((int32_t)A.w[i] >> (c < 32 ? c : 31)); } break;
		case X_SHLQBI: R = shl_bits(&A, B.w[0] & 7); break;
		case X_SHLQBII: R = shl_bits(&A, (uint32_t)i7 & 7); break;
		case X_SHLQBY: R = shl_bytes(&A, B.w[0] & 0x1F); break;
		case X_SHLQBYI: R = shl_bytes(&A, (uint32_t)i7 & 0x1F); break;
		case X_SHLQBYBI: R = shl_bytes(&A, (B.w[0] >> 3) & 0x1F); break;
		case X_ROTQBY: R = rot_bytes(&A, B.w[0] & 0xF); break;
		case X_ROTQBYI: R = rot_bytes(&A, (uint32_t)i7 & 0xF); break;
		case X_ROTQBYBI: R = rot_bytes(&A, (B.w[0] >> 3) & 0xF); break;
		case X_ROTQBI: R = rot_bits(&A, B.w[0] & 7); break;
		case X_ROTQBII: R = rot_bits(&A, (uint32_t)i7 & 7); break;
		case X_ROTQMBY: R = shr_bytes(&A, (0 - B.w[0]) & 0x1F); break;
		case X_ROTQMBYI: R = shr_bytes(&A, (0 - (uint32_t)i7) & 0x1F); break;
		case X_ROTQMBYBI: R = shr_bytes(&A, (0 - (B.w[0] >> 3)) & 0x1F); break;
		case X_ROTQMBI: R = shr_bits(&A, (0 - B.w[0]) & 7); break;
		case X_ROTQMBII: R = shr_bits(&A, (0 - (uint32_t)i7) & 7); break;
		case X_HEQ: if (A.w[0] == B.w[0]) return sim_fault(s, w, "halt (heq)"); break;
		case X_HGT: if ((int32_t)A.w[0] > (int32_t)B.w[0]) return sim_fault(s, w, "halt (hgt)"); break;
		case X_HLGT: if (A.w[0] > B.w[0]) return sim_fault(s, w, "halt (hlgt)"); break;
		case X_HEQI: if ((int32_t)A.w[0] == i10) return sim_fault(s, w, "halt (heqi)"); break;
		case X_HGTI: if ((int32_t)A.w[0] > i10) return sim_fault(s, w, "halt (hgti)"); break;
		case X_HLGTI: if (A.w[0] > (uint32_t)i10) return sim_fault(s, w, "halt (hlgti)"); break;
		case X_CEQB: for (int i = 0; i < 16; i++) set_qb(&R, i, qb(&A, i) == qb(&B, i) ? 0xFF : 0); break;
		case X_CEQH: for (int i = 0; i < 8; i++) set_qh(&R, i, qh(&A, i) == qh(&B, i) ? 0xFFFF : 0); break;
		case X_CEQ: for (int i = 0; i < 4; i++) R.w[i] = A.w[i] == B.w[i] ? ~0u : 0; break;
		case X_CGTB: for (int i = 0; i < 16; i++) set_qb(&R, i, (int8_t)qb(&A, i) > (int8_t)qb(&B, i) ? 0xFF : 0); break;
		case X_CGTH: for (int i = 0; i < 8; i++) set_qh(&R, i, (int16_t)qh(&A, i) > (int16_t)qh(&B, i) ? 0xFFFF : 0); break;
		case X_CGT: for (int i = 0; i < 4; i++) R.w[i] = (int32_t)A.w[i] > (int32_t)B.w[i] ? ~0u : 0; break;
		case X_CLGTB: for (int i = 0; i < 16; i++) set_qb(&R, i, qb(&A, i) > qb(&B, i) ? 0xFF : 0); break;
		case X_CLGTH: for (int i = 0; i < 8; i++) set_qh(&R, i, qh(&A, i) > qh(&B, i) ? 0xFFFF : 0); break;
		case X_CLGT: for (int i = 0; i < 4; i++) R.w[i] = A.w[i] > B.w[i] ? ~0u : 0; break;
		case X_CEQBI: for (int i = 0; i < 16; i++) set_qb(&R, i, qb(&A, i) == (uint8_t)i10 ? 0xFF : 0); break;
		case X_CEQHI: for (int i = 0; i < 8; i++) set_qh(&R, i, qh(&A, i) == (uint16_t)i10 ? 0xFFFF : 0); break;
		case X_CEQI: for (int i = 0; i < 4; i++) R.w[i] = (int32_t)A.w[i] == i10 ? ~0u : 0; break;
		case X_CGTBI: for (int i = 0; i < 16; i++) set_qb(&R, i, (int8_t)qb(&A, i) > (int8_t)(uint8_t)i10 ? 0xFF : 0); break;
		case X_CGTHI: for (int i = 0; i < 8; i++) set_qh(&R, i, (int16_t)qh(&A, i) > (int16_t)i10 ? 0xFFFF : 0); break;
		case X_CGTI: for (int i = 0; i < 4; i++) R.w[i] = (int32_t)A.w[i] > i10 ? ~0u : 0; break;
		case X_CLGTBI: for (int i = 0; i < 16; i++) set_qb(&R, i, qb(&A, i) > (uint8_t)i10 ? 0xFF : 0); break;
		case X_CLGTHI: for (int i = 0; i < 8; i++) set_qh(&R, i, qh(&A, i) > (uint16_t)i10 ? 0xFFFF : 0); break;
		case X_CLGTI: for (int i = 0; i < 4; i++) R.w[i] = A.w[i] > (uint32_t)i10 ? ~0u : 0; break;
		case X_AI: for (int i = 0; i < 4; i++) R.w[i] = A.w[i] + (uint32_t)i10; break;
		case X_AHI: for (int i = 0; i < 8; i++) set_qh(&R, i, (uint16_t)(qh(&A, i) + i10)); break;
		case X_SFI: for (int i = 0; i < 4; i++) R.w[i] = (uint32_t)i10 - A.w[i]; break;
		case X_SFHI: for (int i = 0; i < 8; i++) set_qh(&R, i, (uint16_t)(i10 - qh(&A, i))); break;
		case X_LQD: R = ld(s, A.w[0] + ((uint32_t)i10 << 4)); break;
		case X_STQD: st(s, A.w[0] + ((uint32_t)i10 << 4), &T); break;
		case X_LQA: R = ld(s, (uint32_t)si16 << 2); break;
		case X_STQA: st(s, (uint32_t)si16 << 2, &T); break;
		case X_LQR: R = ld(s, pc + ((uint32_t)si16 << 2)); break;
		case X_STQR: st(s, pc + ((uint32_t)si16 << 2), &T); break;
		case X_IL: R = splat((uint32_t)si16); break;
		case X_ILH: for (int i = 0; i < 8; i++) set_qh(&R, i, (uint16_t)i16); break;
		case X_ILHU: R = splat(i16 << 16); break;
		case X_IOHL: for (int i = 0; i < 4; i++) R.w[i] = T.w[i] | i16; break;
		case X_ILA: R = splat((w >> 7) & 0x3FFFF); break;
		case X_SELB: for (int i = 0; i < 4; i++) R.w[i] = (C.w[i] & B.w[i]) | (~C.w[i] & A.w[i]); break;
		case X_SHUFB:
			for (int i = 0; i < 16; i++)
			{
				const uint8_t c = qb(&C, i);
				uint8_t v;
				if ((c & 0xC0) == 0x80) v = 0x00;
				else if ((c & 0xE0) == 0xC0) v = 0xFF;
				else if ((c & 0xE0) == 0xE0) v = 0x80;
				else v = (c & 0x10) ? qb(&B, c & 15) : qb(&A, c & 15);
				set_qb(&R, i, v);
			}
			break;
		case X_FA: for (int i = 0; i < 4; i++) R.w[i] = f_out((double)f_in(A.w[i]) + f_in(B.w[i])); break;
		case X_FS: for (int i = 0; i < 4; i++) R.w[i] = f_out((double)f_in(A.w[i]) - f_in(B.w[i])); break;
		case X_FM: for (int i = 0; i < 4; i++) R.w[i] = f_out((double)f_in(A.w[i]) * f_in(B.w[i])); break;
		case X_FMA: for (int i = 0; i < 4; i++) R.w[i] = f_out((double)f_in(A.w[i]) * f_in(B.w[i]) + f_in(C.w[i])); break;
		case X_FMS: for (int i = 0; i < 4; i++) R.w[i] = f_out((double)f_in(A.w[i]) * f_in(B.w[i]) - f_in(C.w[i])); break;
		case X_FNMS: for (int i = 0; i < 4; i++) R.w[i] = f_out(f_in(C.w[i]) - (double)f_in(A.w[i]) * f_in(B.w[i])); break;
		case X_FCEQ: for (int i = 0; i < 4; i++) R.w[i] = f_in(A.w[i]) == f_in(B.w[i]) ? ~0u : 0; break;
		case X_FCGT: for (int i = 0; i < 4; i++) R.w[i] = f_in(A.w[i]) > f_in(B.w[i]) ? ~0u : 0; break;
		case X_CSFLT:
		{
			const int sc = 155 - (int)((w >> 14) & 0xFF);
			for (int i = 0; i < 4; i++) R.w[i] = f_out(ldexp((double)(int32_t)A.w[i], -sc));
			break;
		}
		case X_CUFLT:
		{
			const int sc = 155 - (int)((w >> 14) & 0xFF);
			for (int i = 0; i < 4; i++) R.w[i] = f_out(ldexp((double)A.w[i], -sc));
			break;
		}
		case X_CFLTS:
		{
			const int sc = 173 - (int)((w >> 14) & 0xFF);
			for (int i = 0; i < 4; i++)
			{
				const double d = ldexp((double)f_in(A.w[i]), sc);
				R.w[i] = d >= 2147483647.0 ? 0x7FFFFFFFu : d <= -2147483648.0 ? 0x80000000u : (uint32_t)(int32_t)d;
			}
			break;
		}
		case X_CFLTU:
		{
			const int sc = 173 - (int)((w >> 14) & 0xFF);
			for (int i = 0; i < 4; i++)
			{
				const double d = ldexp((double)f_in(A.w[i]), sc);
				R.w[i] = d >= 4294967295.0 ? 0xFFFFFFFFu : d <= 0 ? 0 : (uint32_t)d;
			}
			break;
		}
		case X_BR: taken = 1; tgt = (pc + ((uint32_t)si16 << 2)) & LSLR; break;
		case X_BRA: taken = 1; tgt = ((uint32_t)si16 << 2) & LSLR; break;
		case X_BRSL: taken = 1; tgt = (pc + ((uint32_t)si16 << 2)) & LSLR; R = splat(0); R.w[0] = npc; break;
		case X_BRASL: taken = 1; tgt = ((uint32_t)si16 << 2) & LSLR; R = splat(0); R.w[0] = npc; break;
		case X_BRZ: taken = T.w[0] == 0; tgt = (pc + ((uint32_t)si16 << 2)) & LSLR; break;
		case X_BRNZ: taken = T.w[0] != 0; tgt = (pc + ((uint32_t)si16 << 2)) & LSLR; break;
		case X_BRHZ: taken = (T.w[0] & 0xFFFF) == 0; tgt = (pc + ((uint32_t)si16 << 2)) & LSLR; break;
		case X_BRHNZ: taken = (T.w[0] & 0xFFFF) != 0; tgt = (pc + ((uint32_t)si16 << 2)) & LSLR; break;
		case X_BI: case X_IRET: taken = 1; tgt = A.w[0] & LSLR & ~3u; break;
		case X_BISL: case X_BISLED: taken = 1; tgt = A.w[0] & LSLR & ~3u; R = splat(0); R.w[0] = npc; break;
		case X_BIZ: taken = T.w[0] == 0; tgt = A.w[0] & LSLR & ~3u; break;
		case X_BINZ: taken = T.w[0] != 0; tgt = A.w[0] & LSLR & ~3u; break;
		case X_BIHZ: taken = (T.w[0] & 0xFFFF) == 0; tgt = A.w[0] & LSLR & ~3u; break;
		case X_BIHNZ: taken = (T.w[0] & 0xFFFF) != 0; tgt = A.w[0] & LSLR & ~3u; break;
		case X_HBR:
			if (!((w >> 20) & 1))   // P bit set = hbrp (prefetch only)
			{
				const int32_t ro = sext(((w >> 14) & 3) << 7 | (w & 0x7F), 9);
				s->hint_br = (pc + ((uint32_t)ro << 2)) & LSLR;
				s->hint_tgt = A.w[0] & LSLR & ~3u;
				s->hint_cycle = s->cycle;
				s->hint_on = 1;
			}
			break;
		case X_HBRA: case X_HBRR:
		{
			const int32_t ro = sext(((w >> 23) & 3) << 7 | (w & 0x7F), 9);
			s->hint_br = (pc + ((uint32_t)ro << 2)) & LSLR;
			s->hint_tgt = o->x == X_HBRA ? (((uint32_t)si16 << 2) & LSLR) : ((pc + ((uint32_t)si16 << 2)) & LSLR);
			s->hint_cycle = s->cycle;
			s->hint_on = 1;
			break;
		}
		case X_LNOP: case X_NOP: case X_SYNC: case X_DSYNC: break;
		case X_STOP: case X_STOPD:
			s->stop_code = w & 0x3FFF;
			s->pc = npc;
			return SPU_RUN_STOP;
		case X_RDCH: case X_RCHCNT:
		{
			const uint32_t ch = ra;
			uint32_t v = 0;
			if (o->x == X_RCHCNT)
			{
				if (ch == 0)
				{
					uint64_t wu;
					v = ch_read(s, 0, &v, &wu) == 0;
				}
				else
					v = ch == 29 ? 0 : 1;
			}
			else
			{
				uint64_t wu;
				const int r = ch_read(s, ch, &v, &wu);
				if (r == 1)
					return sim_fault(s, w, "blocking channel read");
				if (r == 2)
					return sim_fault(s, w, "unsupported channel read");
				if (wu > s->cycle + 6)
					extra_wait = wu - (s->cycle + 6);
			}
			R = splat(0);
			R.w[0] = v;
			break;
		}
		case X_WRCH:
			if (ch_write(s, ra, T.w[0]))
				return sim_fault(s, w, "unsupported channel write or bad DMA");
			break;
		default: return sim_fault(s, w, "unimplemented");
		}

		if (write)
		{
			s->r[rt] = R;
			s->ready[rt] = s->cycle + (uint64_t)o->lat + extra_wait;
		}
		if (extra_wait)
		{
			s->dma_wait += extra_wait;
			s->cycle += extra_wait;
		}
		if (s->prof)   // cycles per word; a mispredict penalty goes to its branch, not to the target
		{
			uint64_t d = s->cycle - prev_cycle;
			if (s->pen)
			{
				const uint64_t k = d < s->pen ? d : s->pen;
				s->prof[s->pen_pc >> 2] += k;
				d -= k;
				s->pen = 0;
			}
			s->prof[pc >> 2] += d;
		}
		s->next_min = s->cycle;
		const int is_branch = (o->x >= X_BRA && o->x <= X_BRHZ) || (o->x >= X_BI && o->x <= X_BIHNZ);
		if (is_branch)
		{
			const int hinted = branch_hinted(s, pc, tgt, s->cycle);
			if (taken)
			{
				npc = tgt;
				s->pair_ok = 0;
				if (!hinted)
				{
					s->br_miss++;
					s->next_min = s->cycle + BR_PENALTY;
					s->pen = BR_PENALTY;
					s->pen_pc = pc;
					if (s->brmiss) s->brmiss[pc >> 2]++;
				}
			}
			else if (hinted)
			{
				s->br_miss++;
				s->next_min = s->cycle + BR_PENALTY;
				s->pen = BR_PENALTY;
				s->pen_pc = pc;
				if (s->brmiss) s->brmiss[pc >> 2]++;
			}
		}
		s->pc = npc;
	}
	return SPU_RUN_LIMIT;
}

typedef struct { const char* name; uint64_t cyc; } ProfRow;
static int prof_cmp(const void* a, const void* b)
{
	const ProfRow *x = (const ProfRow*)a, *y = (const ProfRow*)b;
	return x->cyc < y->cyc ? 1 : x->cyc > y->cyc ? -1 : 0;
}

void spusim_print_profile(const SpuSim* s, uint64_t total, int top)
{
	if (!s->prof || !s_nsyms)
		return;
	ProfRow* rows = (ProfRow*)calloc((size_t)s_nsyms + 1, sizeof(ProfRow));
	for (uint32_t i = 0; i < 0x10000; i++)
	{
		if (!s->prof[i])
			continue;
		const uint32_t pc = i << 2;
		int lo = 0, hi = s_nsyms - 1, best = -1;
		while (lo <= hi)
		{
			const int m = (lo + hi) / 2;
			if (s_syms[m].addr <= pc) { best = m; lo = m + 1; }
			else hi = m - 1;
		}
		ProfRow* r = &rows[best >= 0 ? best : s_nsyms];
		r->name = best >= 0 ? s_syms[best].name : "?";
		r->cyc += s->prof[i];
	}
	qsort(rows, (size_t)s_nsyms + 1, sizeof(ProfRow), prof_cmp);
	for (int i = 0; i < top && rows[i].cyc; i++)
		printf("  %6.2f%%  %12llu  %s\n", total ? rows[i].cyc * 100.0 / total : 0.0, (unsigned long long)rows[i].cyc, rows[i].name);
	free(rows);
}

static const SpuSim* s_hot_sim;
static int hot_cmp(const void* a, const void* b)
{
	const uint64_t x = s_hot_sim->prof[*(const uint32_t*)a], y = s_hot_sim->prof[*(const uint32_t*)b];
	return x < y ? 1 : x > y ? -1 : 0;
}

void spusim_print_hot(const SpuSim* s, const char* fn, int top)
{
	if (!s->prof)
		return;
	uint32_t* idx = (uint32_t*)malloc(0x10000 * sizeof(uint32_t));
	uint32_t n = 0;
	uint64_t tot = 0, miss = 0;
	for (uint32_t i = 0; i < 0x10000; i++)
		if (s->prof[i] && strstr(spusim_sym(i << 2), fn))
		{
			idx[n++] = i;
			tot += s->prof[i];
			if (s->brmiss) miss += s->brmiss[i];
		}
	s_hot_sim = s;
	qsort(idx, n, sizeof(uint32_t), hot_cmp);
	printf("hot words of *%s*: %llu cycles, %llu mispredicts\n", fn, (unsigned long long)tot, (unsigned long long)miss);
	for (uint32_t k = 0; k < n && (int)k < top; k++)
		printf("  %05X  %10llu cyc  %8u miss\n", idx[k] << 2, (unsigned long long)s->prof[idx[k]], s->brmiss ? s->brmiss[idx[k]] : 0);
	free(idx);
}
