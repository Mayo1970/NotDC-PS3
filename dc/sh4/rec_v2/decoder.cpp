// Ugly, hacky, bad code. It decodes sh4 opcodes too.

#include "types.h"
#include "decoder.h"
#include "shil.h"
#include "ngen.h"
#include "dc/sh4/sh4_opcode_list.h"
#include "dc/sh4/sh4_registers.h"
#include "dc/mem/sh4_mem.h"
#include "decoder_opcodes.h"
#include "idle_loop.h"
#include "idle_hash.h"
#include "div32_match.h"

#define NullAddress 0xFFFFFFFF

static shil_param mk_imm(u32 immv)
{
	return shil_param(FMT_IMM,immv);
}
static shil_param mk_reg(Sh4RegType reg)
{
	return shil_param(reg);
}
static shil_param mk_regi(int reg)
{
	return mk_reg((Sh4RegType)reg);
}
enum NextDecoderOperation
{
	NDO_NextOp,		//pc+=2
	NDO_End,		//End the block, Type = BlockEndType
	NDO_Delayslot,	//pc+=2, NextOp=DelayOp
	NDO_Jump,		//pc=JumpAddr,NextOp=JumpOp
};

void DecodedBlock::Setup(u32 rpc)
{
	start=rpc;
	cycles=0;
	opcodes=0;

	BranchBlock=0xFFFFFFFF;
	NextBlock=0xFFFFFFFF;

	BlockType=BET_SCL_Intr;
	oplist.clear();

	has_store=false;
	idle_loop=false;
	idle_hash=false;
	cur_pc=rpc;
	cur_slot=0;
	has_fpu=false;
}

// MMU JIT (P38, sh4_mmu_jit.cpp): a block stops at its 1 KB page and reads its code there at the physical address
// (rdv_CompilePCMmu sets the delta); SR.FD with an FPU op in the block stops the decode, so FPSCR comes first.
extern bool g_jit_mmu;
bool jx_fpu_op(u32 op);
u32 g_dec_code_delta;
bool g_dec_fpu_disabled;
static inline u32 dec_read16(u32 pc) { return ReadMem16(pc + g_dec_code_delta); }

DecodedBlock block;

// Plan section 7, bundle 8. JIT RM 2/3 (P57), JIT PR MOVES (5.39), JIT IDLE RULE (P71): 0 x30, 1 Reicast's x3, 2 none.
extern "C" int get_jit_rm_any_preset();
extern "C" int get_jit_pr_moves_preset();
extern "C" int get_jit_idle_rule_preset();
// Since boot, for the [jit8] line: PR=1 moves kept native, blocks the idle rule billed, blocks with RM 2 or 3.
u32 g_dec_pr_moves, g_dec_rule_blocks, g_dec_rm23_blocks;
// JIT IDLE HASH (bundle 8, way B): 0 off, 1 our list, 2 also Reicast's. Blocks that end the slice, since boot.
extern "C" int get_jit_idle_hash_preset();
u32 g_dec_hash_blocks;

struct
{
	NextDecoderOperation NextOp;
	NextDecoderOperation DelayOp;
	NextDecoderOperation JumpOp;
	u32 JumpAddr;
	u32 NextAddr;
	BlockEndType BlockType;

	struct
	{
		bool FPR64;	//64 bit fpu opcodes
		bool FSZ64;	//64 bit fpu moves
		bool RoundToZero;	//false -> Round to nearest.
		u32 rpc;
		bool is_delayslot;
	}cpu;

	ngen_features ngen;

	struct
	{
		bool has_readm;
		bool has_writem;
		bool has_fpu;
	} info;

	void Setup(u32 rpc,fpscr_type fpu_cfg)
	{
		cpu.rpc=rpc;
		cpu.is_delayslot=false;
		cpu.FPR64=fpu_cfg.PR;
		cpu.FSZ64=fpu_cfg.SZ;
		cpu.RoundToZero=fpu_cfg.RM==1;
		// P57: WinCE games set RM 3. With JIT RM 2/3 it compiles as round to nearest, as SetFloatStatusReg() runs it.
		if (!get_jit_rm_any_preset())
		{
			verify(fpu_cfg.RM<2);
		}
		else if (fpu_cfg.RM>=2)
			g_dec_rm23_blocks++;
		//what about fp/fs ?

		NextOp=NDO_NextOp;
		BlockType=BET_SCL_Intr;
		JumpAddr=0xFFFFFFFF;
		NextAddr=0xFFFFFFFF;

		info.has_readm=false;
		info.has_writem=false;
		info.has_fpu=false;
	}
} state;

extern "C" int get_jit_pcrel_preset();
extern "C" int get_jit_fschg_preset();
extern "C" int get_jit_ifb_mask();
extern "C" int get_jit_ifb_group();
extern "C" int get_jit_ifb_lomask();

// PC-relative reads (mova 0xC7xx, mov.w 0x9xxx, mov.l 0xDxxx) bake their address at decode time, so a wrong PC
// or &~3 shows only at run time. JITPCREL sends them to the interpreter to test that.
static bool dec_is_pcrel(u32 op)
{
	const u32 hi = op >> 12;
	return hi == 0x9 || hi == 0xD || (op & 0xFF00) == 0xC700;
}

static void dec_fallback(u32 op)
{
	shil_opcode opcd;
	opcd.op=shop_ifb;

	opcd.rs1=shil_param(FMT_IMM,OpDesc[op]->NeedPC());

	opcd.rs2=shil_param(FMT_IMM,state.cpu.rpc+2);
	opcd.rs3=shil_param(FMT_IMM,op);
	opcd.guest_pc=block.cur_pc;
	opcd.delay_slot=block.cur_slot;
	block.oplist.push_back(opcd);
}

// JIT_NEWOPS: nine opcodes whose handlers existed but no table row reached (eight rec_oph, plus stc SR via
// dec_generic's DM_ReadSRF). Both dispatch paths check this list, so with the preset off all nine fall back.
extern "C" int get_jit_newops_preset();

static bool dec_is_newop(u32 op)
{
	switch(op&0xF0FF)
	{
	case 0x0002:	//stc SR,<REG_N>   (via dec_generic's DM_ReadSRF, not a rec_oph)
	case 0x4003:	//stc.l SR,@-<REG_N>
	case 0x4007:	//ldc.l @<REG_N>+,SR
	case 0x400E:	//ldc <REG_N>,SR
	case 0x401B:	//tas.b @<REG_N>
	case 0x4024:	//rotcl <REG_N>
	case 0x4025:	//rotcr <REG_N>
	case 0x4066:	//lds.l @<REG_N>+,FPSCR
	case 0x406A:	//lds <REG_N>,FPSCR
		return true;
	}
	return false;
}

// JIT_CARRY: addc/subc/negc/addv/subv/div1, cmp/str, xtrct, swap.b, clrmac as native shil ops; off sends them
// back to the interpreter. shop_div1 also adds the q^M^carry step the interpreter's div1 lacks.
extern "C" int get_jit_carry_preset();

static bool dec_is_carryop(u32 op)
{
	switch(op&0xF00F)
	{
	case 0x300E:	//addc <REG_M>,<REG_N>
	case 0x300F:	//addv <REG_M>,<REG_N>
	case 0x300A:	//subc <REG_M>,<REG_N>
	case 0x300B:	//subv <REG_M>,<REG_N>
	case 0x3004:	//div1 <REG_M>,<REG_N>
	case 0x600A:	//negc <REG_M>,<REG_N>
	case 0x6008:	//swap.b <REG_M>,<REG_N>
	case 0x200C:	//cmp/str <REG_M>,<REG_N>
	case 0x200D:	//xtrct <REG_M>,<REG_N>
		return true;
	}
	return op==0x0028;	//clrmac
}

// JIT_MAC: mac.l / mac.w natively, apart from jit_carry because they touch memory (bisect them alone). Their
// reads go through fastmem, and SR.S saturation works (the interpreter's mac.l dies on it, mac.w skips it).
extern "C" int get_jit_mac_preset();

static bool dec_is_macop(u32 op)
{
	//mac.l @<REG_M>+,@<REG_N>+ / mac.w @<REG_M>+,@<REG_N>+
	return (op&0xF00F)==0x000F || (op&0xF00F)==0x400F;
}

// Plan 5.39 (Flycast 62085539a): FPU ops that do not read PR, so a PR=1 block keeps them native. fneg is in Flycast's
// list; ours takes it with Interp Fixes FIXED (P32), when the fallback also flips FR(n).
extern int g_interp_fix_preset;
static bool dec_is_pr_free(u32 op)
{
	const u32 lo=op&0xF;
	//fmov forms 0xFnm6-0xFnmC / fsts FPUL,<FREG_N> 0xFn0D, flds <FREG_N>,FPUL 0xFn1D
	return (lo>=6 && lo<=0xC) || (op&0xEF)==0x0D || (g_interp_fix_preset && (op&0xFF)==0x4D);
}

#if 1

static void dec_DynamicSet(u32 regbase,u32 offs=0)
{
	if (offs==0)
		block.Emit(shop_jdyn,reg_pc_dyn,mk_reg((Sh4RegType)regbase));
	else
		block.Emit(shop_jdyn,reg_pc_dyn,mk_reg((Sh4RegType)regbase),mk_imm(offs));
}

static void dec_End(u32 dst,BlockEndType flags,bool delay)
{
	if (state.ngen.OnlyDynamicEnds && flags == BET_StaticJump)
	{
		block.Emit(shop_mov32,mk_reg(reg_nextpc),mk_imm(dst));
		dec_DynamicSet(reg_nextpc);
		dec_End(NullAddress,BET_DynamicJump,delay);
		return;
	}

	if (state.ngen.OnlyDynamicEnds)
	{
		verify(flags == BET_DynamicJump);
	}

	state.BlockType=flags;
	state.NextOp=delay?NDO_Delayslot:NDO_End;
	state.DelayOp=NDO_End;
	state.JumpAddr=dst;
	state.NextAddr=state.cpu.rpc+2+(delay?2:0);
}

#define GetN(str) ((str>>8) & 0xf)
#define GetM(str) ((str>>4) & 0xf)
#define GetImm4(str) ((str>>0) & 0xf)
#define GetImm8(str) ((str>>0) & 0xff)
#define GetSImm8(str) ((s8)((str>>0) & 0xff))
#define GetImm12(str) ((str>>0) & 0xfff)
#define GetSImm12(str) (((s16)((GetImm12(str))<<4))>>4)


#define SR_STATUS_MASK 0x700083F2
#define SR_T_MASK 1

static u32 dec_jump_simm8(u32 op)
{
	return state.cpu.rpc + GetSImm8(op)*2 + 4;
}
static u32 dec_jump_simm12(u32 op)
{
	return state.cpu.rpc + GetSImm12(op)*2 + 4;
}
static u32 dec_set_pr()
{
	u32 retaddr=state.cpu.rpc + 4;
	block.Emit(shop_mov32,reg_pr,mk_imm(retaddr));
	return retaddr;
}
static void dec_write_sr(shil_param src)
{
	block.Emit(shop_and,mk_reg(reg_sr_status),src,mk_imm(SR_STATUS_MASK));
	block.Emit(shop_and,mk_reg(reg_sr_T),src,mk_imm(SR_T_MASK));
}
//bf <bdisp8>
sh4dec(i1000_1011_iiii_iiii)
{
	dec_End(dec_jump_simm8(op),BET_Cond_0,false);
}
//bf.s <bdisp8>
sh4dec(i1000_1111_iiii_iiii)
{
	block.Emit(shop_jcond,reg_pc_dyn,reg_sr_T);
	dec_End(dec_jump_simm8(op),BET_Cond_0,true);
}
//bt <bdisp8>
sh4dec(i1000_1001_iiii_iiii)
{
	dec_End(dec_jump_simm8(op),BET_Cond_1,false);
}
//bt.s <bdisp8>
sh4dec(i1000_1101_iiii_iiii)
{
	block.Emit(shop_jcond,reg_pc_dyn,reg_sr_T);
	dec_End(dec_jump_simm8(op),BET_Cond_1,true);
}
//bra <bdisp12>
sh4dec(i1010_iiii_iiii_iiii)
{
	dec_End(dec_jump_simm12(op),BET_StaticJump,true);
}
//braf <REG_N>
sh4dec(i0000_nnnn_0010_0011)
{
	u32 n = GetN(op);

	dec_DynamicSet(reg_r0+n,state.cpu.rpc + 4);
	dec_End(NullAddress,BET_DynamicJump,true);
}
//jmp @<REG_N>
sh4dec(i0100_nnnn_0010_1011)
{
	u32 n = GetN(op);

	dec_DynamicSet(reg_r0+n);
	dec_End(NullAddress,BET_DynamicJump,true);
}
//bsr <bdisp12>
sh4dec(i1011_iiii_iiii_iiii)
{
	//TODO: set PR
	dec_set_pr();
	dec_End(dec_jump_simm12(op),BET_StaticCall,true);
}
//bsrf <REG_N>
sh4dec(i0000_nnnn_0000_0011)
{
	u32 n = GetN(op);
	//TODO: set PR
	u32 retaddr=dec_set_pr();
	dec_DynamicSet(reg_r0+n,retaddr);
	dec_End(NullAddress,BET_DynamicCall,true);
}
//jsr @<REG_N>
sh4dec(i0100_nnnn_0000_1011) 
{
	u32 n = GetN(op);

	//TODO: Set pr
	dec_set_pr();
	dec_DynamicSet(reg_r0+n);
	dec_End(NullAddress,BET_DynamicCall,true);
}
//rts
sh4dec(i0000_0000_0000_1011)
{
	dec_DynamicSet(reg_pr);
	dec_End(NullAddress,BET_DynamicRet,true);
}
//rte
sh4dec(i0000_0000_0010_1011)
{
	//TODO: Write SR, Check intr
	dec_write_sr(reg_ssr);
	block.Emit(shop_sync_sr);
	dec_DynamicSet(reg_spc);
	dec_End(NullAddress,BET_DynamicIntr,true);
}
//trapa #<imm>
sh4dec(i1100_0011_iiii_iiii)
{
	//TODO: ifb
	dec_fallback(op);
	dec_DynamicSet(reg_nextpc);
	dec_End(NullAddress,BET_DynamicJump,false);
}
//sleep
sh4dec(i0000_0000_0001_1011)
{
	//TODO: ifb
	dec_fallback(op);
	dec_DynamicSet(reg_nextpc);
	dec_End(NullAddress,BET_DynamicJump,false);
}

//illegal instruction handler
void dec_illegalOp(u32 op)
{
	printf("WARNING: Illegal instruction %04x at pc %08x\n", op, state.cpu.rpc);
	dec_fallback(op);
	dec_DynamicSet(reg_nextpc);
	dec_End(NullAddress, BET_DynamicJump, false);
}

//ldc.l @<REG_N>+,SR
sh4dec(i0100_nnnn_0000_0111)
{
	u32 n = GetN(op);
	shil_param rn = mk_regi(reg_r0 + n);
	
	// Read from memory into temp
	state.info.has_readm = true;
	block.Emit(shop_readm, reg_temp, rn, shil_param(), 4);
	
	// Increment pointer
	block.Emit(shop_add, rn, rn, mk_imm(4));
	
	// Write temp to SR
	dec_write_sr(reg_temp);
	block.Emit(shop_sync_sr);
	
	// Check interrupts
	if (!state.cpu.is_delayslot)
		dec_End(state.cpu.rpc + 2, BET_StaticIntr, false);
}

//ldc <REG_N>,SR
sh4dec(i0100_nnnn_0000_1110)
{
	u32 n = GetN(op);
	
	// Write SR from register
	dec_write_sr(mk_regi(reg_r0 + n));
	block.Emit(shop_sync_sr);
	
	// Check for interrupts if not in delay slot
	if (!state.cpu.is_delayslot)
		dec_End(state.cpu.rpc + 2, BET_StaticIntr, false);
}

//nop !
sh4dec(i0000_0000_0000_1001)
{
}

//fschg
sh4dec(i1111_0011_1111_1101)
{
	//fpscr.SZ is bit 20
	block.Emit(shop_xor,reg_fpscr,reg_fpscr,mk_imm(1<<20));
	state.cpu.FSZ64=!state.cpu.FSZ64;
	// Plan 5.37 (JIT FSCHG 2, as Flycast): the xor is all of it. old_fpscr keeps a stale SZ, which only JIT FRCHG's
	// test reads (it leaves SZ out); the block goes on, as the decoder follows SZ itself.
	if (get_jit_fschg_preset()>=2)
		return;
	// SZ only (flags = SYNC_FPSCR_SZ_ONLY): JIT_FSCHG skips UpdateFPSCR(); T&L loops run ~1.3M fschg/s and
	// almost none change FP banks. frchg and the ldc..FPSCR forms must not set it.
	block.Emit(shop_sync_fpscr,shil_param(),shil_param(),shil_param(),SYNC_FPSCR_SZ_ONLY);
	
	if (!state.cpu.is_delayslot)
		dec_End(state.cpu.rpc + 2, BET_StaticJump, false);
}

//frchg
sh4dec(i1111_1011_1111_1101)
{
	//fpscr.FR is bit 21
	block.Emit(shop_xor, reg_fpscr, reg_fpscr, mk_imm(1 << 21));
	
	// Si shop_sync_fpscr existe:
	block.Emit(shop_sync_fpscr);
	// SINON utilisez:
	// dec_fallback(op);
	
	if (!state.cpu.is_delayslot)
		dec_End(state.cpu.rpc + 2, BET_StaticJump, false);
}

//rotcl <REG_N>
sh4dec(i0100_nnnn_0010_0100)
{
	u32 n = GetN(op);
	shil_param rn = mk_regi(reg_r0 + n);
	
	// Save old T
	block.Emit(shop_mov32, reg_temp, reg_sr_T);
	
	// Set T to MSB (bit 31)
	block.Emit(shop_shr, reg_sr_T, rn, mk_imm(31));
	
	// Shift left
	block.Emit(shop_shl, rn, rn, mk_imm(1));
	
	// OR in old T (at bit 0)
	block.Emit(shop_or, rn, rn, reg_temp);
}

//rotcr <REG_N>
sh4dec(i0100_nnnn_0010_0101)
{
	u32 n = GetN(op);
	shil_param rn = mk_regi(reg_r0 + n);
	
	// Save old T shifted to bit 31
	block.Emit(shop_shl, reg_temp, reg_sr_T, mk_imm(31));
	
	// Set T to LSB (bit 0)
	block.Emit(shop_and, reg_sr_T, rn, mk_imm(1));
	
	// Shift right
	block.Emit(shop_shr, rn, rn, mk_imm(1));
	
	// OR in old T (at bit 31)
	block.Emit(shop_or, rn, rn, reg_temp);
}

//tas.b @<REG_N>
sh4dec(i0100_nnnn_0001_1011)
{
	u32 n = GetN(op);
	shil_param rn = mk_regi(reg_r0 + n);
	
	// Read byte
	state.info.has_readm = true;
	block.Emit(shop_readm, reg_temp, rn, shil_param(), 1);
	
	// Set T flag if zero
	block.Emit(shop_seteq, reg_sr_T, reg_temp, mk_imm(0));
	
	// Set bit 7
	block.Emit(shop_or, reg_temp, reg_temp, mk_imm(0x80));
	
	// Write back
	state.info.has_writem = true;
	block.Emit(shop_writem, shil_param(), rn, reg_temp, 1);
}

//stc.l SR,@-<REG_N>
sh4dec(i0100_nnnn_0000_0011)
{
	u32 n = GetN(op);
	
	// Combine SR status and T flag into temp
	block.Emit(shop_mov32, reg_temp, reg_sr_status);
	block.Emit(shop_or, reg_temp, reg_temp, reg_sr_T);
	
	// Write to memory with predecrement
	shil_param rn = mk_regi(reg_r0 + n);
	state.info.has_writem = true;
	block.Emit(shop_writem, shil_param(), rn, reg_temp, 4, mk_imm(-4));
	
	// Decrement pointer
	block.Emit(shop_add, rn, rn, mk_imm(-4));
}

//ldc <REG_N>,FPSCR
sh4dec(i0100_nnnn_0110_1010)
{
	u32 n = GetN(op);
	
	// Load FPSCR from register
	block.Emit(shop_mov32, reg_fpscr, mk_regi(reg_r0 + n));
	
	// Sync FPSCR
	// Si shop_sync_fpscr existe:
	block.Emit(shop_sync_fpscr);
	// SINON:
	// dec_fallback(op);
	
	if (!state.cpu.is_delayslot)
		dec_End(state.cpu.rpc + 2, BET_StaticJump, false);
}

//ldc.l @<REG_N>+,FPSCR
sh4dec(i0100_nnnn_0110_0110)
{
	u32 n = GetN(op);
	shil_param rn = mk_regi(reg_r0 + n);
	
	// Read from memory
	state.info.has_readm = true;
	block.Emit(shop_readm, reg_fpscr, rn, shil_param(), 4);
	
	// Increment pointer
	block.Emit(shop_add, rn, rn, mk_imm(4));
	
	// Sync FPSCR
	// IF shop_sync_fpscr exist :
	block.Emit(shop_sync_fpscr);
	// ELSE:
	// dec_fallback(op);
	
	if (!state.cpu.is_delayslot)
		dec_End(state.cpu.rpc + 2, BET_StaticJump, false);
}


#endif

const Sh4RegType SREGS[] =
{
	reg_mach,
	reg_macl,
	reg_pr,
	reg_sgr,
	NoReg,
	reg_fpul,
	reg_fpscr,
	NoReg,

	NoReg,
	NoReg,
	NoReg,
	NoReg,
	NoReg,
	NoReg,
	NoReg,
	reg_dbr,
};

const Sh4RegType CREGS[] =
{
	reg_sr,
	reg_gbr,
	reg_vbr,
	reg_ssr,
	reg_spc,
	NoReg,
	NoReg,
	NoReg,

	reg_r0_Bank,
	reg_r1_Bank,
	reg_r2_Bank,
	reg_r3_Bank,
	reg_r4_Bank,
	reg_r5_Bank,
	reg_r6_Bank,
	reg_r7_Bank,
};

void dec_param(DecParam p,shil_param& r1,shil_param& r2, u32 op)
{
	switch(p)
	{
		//constants
	case PRM_PC_D8_x2:
		r1=mk_imm((state.cpu.rpc+4)+(GetImm8(op)<<1));
		break;

	case PRM_PC_D8_x4:
		r1=mk_imm(((state.cpu.rpc+4)&0xFFFFFFFC)+(GetImm8(op)<<2));
		break;
	
	case PRM_ZERO:
		r1= mk_imm(0);
		break;

	case PRM_ONE:
		r1= mk_imm(1);
		break;

	case PRM_TWO:
		r1= mk_imm(2);
		break;

	case PRM_TWO_INV:
		r1= mk_imm(~2);
		break;

	case PRM_ONE_F32:
		r1= mk_imm(0x3f800000);
		break;

	//imms
	case PRM_SIMM8:
		r1=mk_imm(GetSImm8(op));
		break;
	case PRM_UIMM8:
		r1=mk_imm(GetImm8(op));
		break;

	//direct registers
	case PRM_R0:
		r1=mk_reg(reg_r0);
		break;

	case PRM_RN:
		r1=mk_regi(reg_r0+GetN(op));
		break;

	case PRM_RM:
		r1=mk_regi(reg_r0+GetM(op));
		break;

	case PRM_FRN_SZ:
		if (state.cpu.FSZ64)
		{
			int rx=GetN(op)/2;
			if (GetN(op)&1)
				rx+=regv_xd_0;
			else
				rx+=regv_dr_0;

			r1=mk_regi(rx);
			break;
		}
		FALLTHROUGH;	//32bit size: same register as PRM_FRN
	case PRM_FRN:
		r1=mk_regi(reg_fr_0+GetN(op));
		break;

	case PRM_FRM_SZ:
		if (state.cpu.FSZ64)
		{
			int rx=GetM(op)/2;
			if (GetM(op)&1)
				rx+=regv_xd_0;
			else
				rx+=regv_dr_0;

			r1=mk_regi(rx);
			break;
		}
		FALLTHROUGH;	//32bit size: same register as PRM_FRM
	case PRM_FRM:
		r1=mk_regi(reg_fr_0+GetM(op));
		break;

	case PRM_FPUL:
		r1=mk_regi(reg_fpul);
		break;

	case PRM_FPN:	//float pair, 3 bits
		r1=mk_regi(regv_dr_0+GetN(op)/2);
		break;

	case PRM_FVN:	//float quad, 2 bits
		r1=mk_regi(regv_fv_0+GetN(op)/4);
		break;

	case PRM_FVM:	//float quad, 2 bits
		r1=mk_regi(regv_fv_0+(GetN(op)&0x3));
		break;

	case PRM_XMTRX:	//float matrix, 0 bits
		r1=mk_regi(regv_xmtrx);
		break;

	case PRM_FRM_FR0:
		r1=mk_regi(reg_fr_0+GetM(op));
		r2=mk_regi(reg_fr_0);
		break;

	case PRM_SR_T:
		r1=mk_regi(reg_sr_T);
		break;

	case PRM_SR_STATUS:
		r1=mk_regi(reg_sr_status);
		break;

	case PRM_SREG:	//FPUL/FPSCR/MACH/MACL/PR/DBR/SGR
		r1=mk_regi(SREGS[GetM(op)]);
		break;
	case PRM_CREG:	//SR/GBR/VBR/SSR/SPC/<RM_BANK>
		r1=mk_regi(CREGS[GetM(op)]);
		break;
	
	//reg/imm reg/reg
	case PRM_RN_D4_x1:
	case PRM_RN_D4_x2:
	case PRM_RN_D4_x4:
		{
			u32 shft=p-PRM_RN_D4_x1;
			r1=mk_regi(reg_r0+GetN(op));
			r2=mk_imm(GetImm4(op)<<shft);
		}
		break;

	case PRM_RN_R0:
		r1=mk_regi(reg_r0+GetN(op));
		r2=mk_regi(reg_r0);
		break;

	case PRM_RM_D4_x1:
	case PRM_RM_D4_x2:
	case PRM_RM_D4_x4:
		{
			u32 shft=p-PRM_RM_D4_x1;
			r1=mk_regi(reg_r0+GetM(op));
			r2=mk_imm(GetImm4(op)<<shft);
		}
		break;

	case PRM_RM_R0:
		r1=mk_regi(reg_r0+GetM(op));
		r2=mk_regi(reg_r0);
		break;

	case PRM_GBR_D8_x1:
	case PRM_GBR_D8_x2:
	case PRM_GBR_D8_x4:
		{
			u32 shft=p-PRM_GBR_D8_x1;
			r1=mk_regi(reg_gbr);
			r2=mk_imm(GetImm8(op)<<shft);
		}
		break;

	default:
		die("Nop supported param used");
	}
}

// Division idiom: DIV0U/S + 32 x "ROTCL reg1; DIV1 reg2,reg3". A full match (score 65) becomes one divide
// (div32_match.h, JIT DIV32 picks the form) and skips the 64 guest opcodes; anything else stays as decoded.
extern "C" int get_jit_div32_preset();

#define DIV1_KEY      0x3004
#define ROTCL_KEY     0x4024
#define DIV_MASK_N    0xF0FF	// masks out the n field
#define DIV_MASK_N_M  0xF00F	// masks out both n and m fields

static Sh4RegType div_som_reg1;
static Sh4RegType div_som_reg2;
static Sh4RegType div_som_reg3;

static u32 MatchDiv32(u32 pc, Sh4RegType& reg1, Sh4RegType& reg2, Sh4RegType& reg3)
{
	u32 v_pc = pc;
	u32 match = 1;
	for (int i = 0; i < 32; i++)
	{
		u16 opcode = dec_read16(v_pc);
		v_pc += 2;
		if ((opcode & DIV_MASK_N) == ROTCL_KEY)
		{
			if (reg1 == NoReg)
				reg1 = (Sh4RegType)GetN(opcode);
			else if (reg1 != (Sh4RegType)GetN(opcode))
				break;
			match++;
		}
		else
			break;

		opcode = dec_read16(v_pc);
		v_pc += 2;
		if ((opcode & DIV_MASK_N_M) == DIV1_KEY)
		{
			if (reg2 == NoReg)
				reg2 = (Sh4RegType)GetM(opcode);
			else if (reg2 != (Sh4RegType)GetM(opcode))
				break;

			if (reg2 == reg1)
				break;

			if (reg3 == NoReg)
				reg3 = (Sh4RegType)GetN(opcode);
			else if (reg3 != (Sh4RegType)GetN(opcode))
				break;

			if (reg3 == reg1)
				break;

			// div1 Rn,Rn divides by a changing value, not by a fixed divisor.
			if (reg3 == reg2)
				break;

			match++;
		}
		else
			break;
	}

	return match;
}

// How far the last MatchDiv32 got before it broke, for the [DIV1] probe below.
static u32 s_div_match_len = 0;

// MMU JIT: the 64 ops the match skips must stay in the block's 1 KB page.
static bool div_past_page(u32 pc)
{
	return g_jit_mmu && (pc & 0x3FF) + 130 > 0x400;
}

static bool MatchDiv32u(u32 op, u32 pc)
{
	if (settings.dynarec.safemode || div_past_page(pc))
		return false;

	div_som_reg1 = NoReg;
	div_som_reg2 = NoReg;
	div_som_reg3 = NoReg;

	// 1 (div0u) + 32 rotcl + 32 div1 = 65 on a perfect match
	s_div_match_len = MatchDiv32(pc + 2, div_som_reg1, div_som_reg2, div_som_reg3);
	return s_div_match_len == 65;
}

static bool MatchDiv32s(u32 op, u32 pc)
{
	if (settings.dynarec.safemode || div_past_page(pc))
		return false;

	// DIV0S Rm,Rn fixes the divisor / working-remainder registers up front.
	div_som_reg1 = NoReg;
	div_som_reg2 = (Sh4RegType)GetM(op);
	div_som_reg3 = (Sh4RegType)GetN(op);

	s_div_match_len = MatchDiv32(pc + 2, div_som_reg1, div_som_reg2, div_som_reg3);
	return s_div_match_len == 65;
}

// [DIV1] probe (IFB Probe preset): Castlevania runs ~11.5k div1/s through ifb, so its divisions miss MatchDiv32.
// Dumps the code around the first sites and how far the matcher got.
extern "C" int get_ifb_probe_preset();

// One line per site, matches too (it shows whether the 32-step loops collapse); div0 and div1 budgets apart.
// Sites below 0x8C010000 (the BIOS, IP.BIN) are skipped: boot code filled all 16 slots (plan 5.38).
#define DIV_SITE_MAX 16
#define DIV_SITE_MIN 0x8C010000
static u32 s_div0_sites[DIV_SITE_MAX];
static u32 s_div1_sites[DIV_SITE_MAX];
static u32 s_div0_site_n = 0;
static u32 s_div1_site_n = 0;

// Returns true the first time this pc is seen (and records it).
static bool dec_div_site_new(u32* tab, u32& n, u32 pc)
{
	for (u32 i=0;i<n;i++)
		if (tab[i]==pc)
			return false;
	if (n>=DIV_SITE_MAX)
		return false;
	tab[n++]=pc;
	return true;
}

// Every div0u/div0s the decoder sees, matched or not.
static void dec_div0_probe(u32 pc, bool is_unsigned, bool matched)
{
	if (!get_ifb_probe_preset() || pc < DIV_SITE_MIN)
		return;
	if (!dec_div_site_new(s_div0_sites, s_div0_site_n, pc))
		return;

	if (matched)
	{
		printf("[DIV] div0%c pc=%08X MATCHED 65/65 -> native divide (reg1=%d reg2=%d reg3=%d)\n",
			is_unsigned ? 'u' : 's', pc,
			(int)div_som_reg1, (int)div_som_reg2, (int)div_som_reg3);
	}
	else
	{
		printf("[DIV] div0%c pc=%08X failed at %u/65 -> next op %04X (%s)\n",
			is_unsigned ? 'u' : 's', pc, s_div_match_len,
			dec_read16(pc+2), OpDesc[dec_read16(pc+2)]->diss);
	}
}

// A div1 about to take the interpreter path.
static void dec_div1_probe(u32 op, u32 pc)
{
	if ((op & DIV_MASK_N_M) != DIV1_KEY || !get_ifb_probe_preset() || pc < DIV_SITE_MIN)
		return;
	if (!dec_div_site_new(s_div1_sites, s_div1_site_n, pc))
		return;

	printf("[DIV] div1 ifb pc=%08X (prev %04X, next %04X)\n",
		pc, dec_read16(pc-2), dec_read16(pc+2));
}

bool dec_generic(u32 op)
{
	DecMode mode;DecParam d;DecParam s;shilop natop;u32 e;
	if (OpDesc[op]->decode==0)
		return false;

	// JIT_NEWOPS gate for its generic-decoder opcode (stc SR,<REG_N> -> DM_ReadSRF); off falls back as before.
	if (dec_is_newop(op) && !get_jit_newops_preset())
		return false;

	// JIT_CARRY gate -- see dec_is_carryop above.
	if (dec_is_carryop(op) && !get_jit_carry_preset())
		return false;

	// JIT_MAC gate -- see dec_is_macop above. MMU JIT: the interpreter's mac moves Rn and Rm only after both reads.
	if (dec_is_macop(op) && (!get_jit_mac_preset() || g_jit_mmu))
		return false;

	u64 inf=OpDesc[op]->decode;

	e=(u32)(inf>>32);
	mode=(DecMode)((inf>>24)&0xFF);
	d=(DecParam)((inf>>16)&0xFF);
	s=(DecParam)((inf>>8)&0xFF);
	natop=(shilop)((inf>>0)&0xFF);

	//if ((op&0xF00F)>=0x4005 && op<0x8000)
	{
	//	return false;
	}

	bool transfer_64=false;
	if (op>=0xF000)
	{
		state.info.has_fpu=true;
		//return false;//fpu off for now
		if (state.cpu.FPR64 /*|| state.cpu.FSZ64*/)
		{
			if (!get_jit_pr_moves_preset() || !dec_is_pr_free(op))
				return false;
			g_dec_pr_moves++;
		}

		if (state.cpu.FSZ64 && (d==PRM_FRN_SZ || d==PRM_FRM_SZ || s==PRM_FRN_SZ || s==PRM_FRM_SZ))
		{
			transfer_64=true;
		}
	}

	shil_param rs1,rs2,rs3,rd;

	dec_param(s,rs2,rs3,op);
	dec_param(d,rs1,rs3,op);

	switch(mode)
	{
	case DM_ReadSRF:
		// Read SR with flags combined
		block.Emit(shop_mov32, rs1, reg_sr_status);
		block.Emit(shop_or, rs1, rs1, reg_sr_T);
		break;
	
	case DM_ADC:
		// Add/subtract with carry
		block.Emit(natop, rs1, rs1, rs2, 0, mk_reg(reg_sr_T), mk_reg(reg_sr_T));
		break;
	
	case DM_NEGC:
		// Negate with carry: 0 - src - T
		block.Emit(natop, rs1, rs2, mk_reg(reg_sr_T), 0, shil_param(), mk_reg(reg_sr_T));
		break;

	case DM_OVF:
		// addv/subv: value -> rd, signed overflow -> T. No T input.
		block.Emit(natop, rs1, rs1, rs2, 0, shil_param(), mk_reg(reg_sr_T));
		break;

	case DM_MAC:
		// mac.l/mac.w @Rm+,@Rn+ (e = 4 or 2): plain readm (fastmem; size 2 sign-extends), Rn first as the manual says.
		// No has_readm on purpose: `mac.l @Rm+,@Rn+; dt Rk; bf loop` is work, not the x30 idle rule's spin.
		block.Emit(shop_readm, mk_reg(reg_temp),  rs1, shil_param(), e);
		block.Emit(shop_add,   rs1, rs1, mk_imm(e));
		block.Emit(shop_readm, mk_reg(reg_temp2), rs2, shil_param(), e);
		block.Emit(shop_add,   rs2, rs2, mk_imm(e));
		block.Emit(natop, mk_reg(reg_macl), mk_reg(reg_temp), mk_reg(reg_temp2),
		           0, mk_reg(reg_sr_status), mk_reg(reg_mach));
		break;

	case DM_XTRCT:
		// xtrct Rm,Rn: Rn = (Rn >> 16) | (Rm << 16), with reg_temp because rd aliases rs1.
		block.Emit(shop_shr, mk_reg(reg_temp), rs1, mk_imm(16));
		block.Emit(shop_shl, rs1, rs2, mk_imm(16));
		block.Emit(shop_or,  rs1, rs1, mk_reg(reg_temp));
		break;

	case DM_CLRMAC:
		block.Emit(shop_mov32, mk_reg(reg_mach), mk_imm(0));
		block.Emit(shop_mov32, mk_reg(reg_macl), mk_imm(0));
		break;

	case DM_WriteTOp:
		block.Emit(natop,reg_sr_T,rs1,rs2);
		break;

	case DM_DT:
		verify(natop==shop_sub);
		block.Emit(natop,rs1,rs1,rs2);
		block.Emit(shop_seteq,mk_reg(reg_sr_T),rs1,mk_imm(0));
		break;

	case DM_Shift:
		if (natop==shop_shl && e==1)
			block.Emit(shop_shr,mk_reg(reg_sr_T),rs1,mk_imm(31));
		else if (e==1)
			block.Emit(shop_and,mk_reg(reg_sr_T),rs1,mk_imm(1));

		block.Emit(natop,rs1,rs1,mk_imm(e));
		break;

	case DM_Rot:
		if (!(((s32)e>=0?e:-e)&0x1000))
		{
			if ((s32)e<0)
			{
				//left rorate
				block.Emit(shop_shr,mk_reg(reg_sr_T),rs2,mk_imm(31));
				e=-e;
			}
			else
			{
				//right rotate
				block.Emit(shop_and,mk_reg(reg_sr_T),rs2,mk_imm(1));
			}
		}
		e&=31;

		block.Emit(natop,rs1,rs2,mk_imm(e));
		break;

	case DM_BinaryOp://d=d op s
		if (e&1)
			block.Emit(natop,rs1,rs1,rs2,0,rs3);
		else
			block.Emit(natop,shil_param(),rs1,rs2,0,rs3);
		break;

	case DM_UnaryOp: //d= op s
		if (transfer_64 && natop==shop_mov32) 
			natop=shop_mov64;

		if (natop==shop_cvt_i2f_n && state.cpu.RoundToZero)
			natop=shop_cvt_i2f_z;

		if (e&1)
			block.Emit(natop,shil_param(),rs1);
		else
			block.Emit(natop,rs1,rs2);
		break;

	case DM_WriteM:	//write(d,s)
		{
			//0 has no effect, so get rid of it
			if (rs3.is_imm() && rs3._imm==0)
				rs3=shil_param();

			state.info.has_writem=true;
			if (transfer_64) e=(s32)e*2;
			bool update_after=false;
			if ((s32)e<0)
			{
				// MMU JIT: Rn moves after the write, so a write that faults leaves it as it was (Flycast).
				if (rs1._reg!=rs2._reg && !g_jit_mmu)	//reg shoudn't be updated if its writen
				{
					block.Emit(shop_sub,rs1,rs1,mk_imm(-e));
				}
				else
				{
					verify(rs3.is_null());
					rs3=mk_imm(e);
					update_after=true;
				}
			}

			block.Emit(shop_writem,shil_param(),rs1,rs2,(s32)e<0?-e:e,rs3);

			if (update_after)
			{
				block.Emit(shop_sub,rs1,rs1,mk_imm(-e));
			}
		}
		break;

	case DM_ReadM:
		//0 has no effect, so get rid of it
		if (rs3.is_imm() && rs3._imm==0)
				rs3=shil_param();

		state.info.has_readm=true;
		if (transfer_64) e=(s32)e*2;

		block.Emit(shop_readm,rs1,rs2,shil_param(),(s32)e<0?-e:e,rs3);
		if ((s32)e<0)
		{
			if (rs1._reg!=rs2._reg)//the reg shoudn't be updated if it was just readed
				block.Emit(shop_add,rs2,rs2,mk_imm(-e));
		}
		break;

	case DM_fiprOp:
		{
			shil_param rdd=mk_regi(rs1._reg+3);
			block.Emit(natop,rdd,rs1,rs2);
		}
		break;

	case DM_EXTOP:
		{
			block.Emit(natop,rs1,rs2,mk_imm(e==1?0xFF:0xFFFF));
		}
		break;
	
	case DM_MUL:
		{
			shilop op;
			shil_param rd=mk_reg(reg_macl);
			shil_param rd2=shil_param();

			switch((s32)e)
			{
				case 16:	op=shop_mul_u16; break;
				case -16:	op=shop_mul_s16; break;

				case -32:	op=shop_mul_i32; break;

				case 64:	op=shop_mul_u64; rd2 = mk_reg(reg_mach); break;
				case -64:	op=shop_mul_s64; rd2 = mk_reg(reg_mach); break;

				default:
					die("DM_MUL: Failed to classify opcode");
			}

			block.Emit(op,rd,rs1,rs2,0,shil_param(),rd2);
		}
		break;

	case DM_DIV0:
		{
			// The full ROTCL/DIV1 idiom becomes one divide; unmatched div0u/div0s keep the SR code below. Never in
			// a delay slot: a match skips 128 bytes of guest code.
			bool matched = !state.cpu.is_delayslot &&
				(e==1 ? MatchDiv32u(op, state.cpu.rpc)
				      : MatchDiv32s(op, state.cpu.rpc));

			// s_div_match_len is only meaningful if the matcher actually ran
			if (!state.cpu.is_delayslot)
				dec_div0_probe(state.cpu.rpc, e==1, matched);

			if (matched)
			{
				div32_emit(block, e==1, get_jit_div32_preset()!=0, div_som_reg1, div_som_reg2, div_som_reg3);

				// Skip the 64 matched opcodes and bill them here (the decode loop never sees them; the idle rule counts them).
				state.cpu.rpc += 128;
				block.cycles  += CPU_RATIO*64;
				block.opcodes += 64;
			}
			else if (e==1)
			{
				//crear QM (bits 8,9)
				u32 qm=(1<<8)|(1<<9);
				block.Emit(shop_and,mk_reg(reg_sr_status),mk_reg(reg_sr_status),mk_imm(~qm));
				//clear T !
				block.Emit(shop_mov32,mk_reg(reg_sr_T),mk_imm(0));
			}
			else
			{
				//sr.Q=r[n]>>31; sr.M=r[m]>>31; sr.T=sr.M^sr.Q -- nasty, as there is no temp reg

				//Clear Q & M
				block.Emit(shop_and,mk_reg(reg_sr_status),mk_reg(reg_sr_status),mk_imm(~((1<<8)|(1<<9))));

				//sr.Q=r[n]>>31;
				block.Emit(shop_sar,mk_reg(reg_sr_T),rs1,mk_imm(31));
				block.Emit(shop_and,mk_reg(reg_sr_T),mk_reg(reg_sr_T),mk_imm(1<<8));
				block.Emit(shop_or,mk_reg(reg_sr_status),mk_reg(reg_sr_status),mk_reg(reg_sr_T));

				//sr.M=r[m]>>31;
				block.Emit(shop_sar,mk_reg(reg_sr_T),rs2,mk_imm(31));
				block.Emit(shop_and,mk_reg(reg_sr_T),mk_reg(reg_sr_T),mk_imm(1<<9));
				block.Emit(shop_or,mk_reg(reg_sr_status),mk_reg(reg_sr_status),mk_reg(reg_sr_T));

				//sr.T=sr.M^sr.Q;
				block.Emit(shop_xor,mk_reg(reg_sr_T),rs1,rs2);
				block.Emit(shop_shr,mk_reg(reg_sr_T),mk_reg(reg_sr_T),mk_imm(31));
			}
		}
		break;

	default:
		verify(false);
	}

	return true;
}

int stuffcmp(const void* p1,const void* p2)
{
	sh4_opcodelistentry* a=(sh4_opcodelistentry*)p1;
	sh4_opcodelistentry* b=(sh4_opcodelistentry*)p2;

	return b->fallbacks-a->fallbacks;
}

// JIT IDLE HASH: true = this block ends the timeslice. Level 1 acts on our list and only logs a block of
// Reicast's list; level 2 acts on both. The first hits of each list go to the log in hash()'s full form.
static bool dec_idle_hash(int level)
{
	if (block.opcodes==0 || block.opcodes>IDLE_HASH_MAX_OPS)
		return false;
	u8 code[IDLE_HASH_MAX_OPS];
	for (u32 i=0;i<block.opcodes;i++)
		code[i]=ReadMem8(block.start+i);
	char full[IDLE_HASH_STR];
	const int list=idle_hash_listed(block.start,block.opcodes,code,full);
	if (list==IDLE_LIST_NONE)
		return false;
	const bool act=list==IDLE_LIST_OWN || level>=2;
	// A log share per list: Reicast's entries hit dozens of blocks in a boot and must not hide ours.
	static u32 logged[2];
	u32& lines=logged[list==IDLE_LIST_OWN];
	if (lines<24)
	{
		lines++;
		printf("[idlehash] %s %s: %s\n",full,list==IDLE_LIST_OWN?"our list":"Reicast's list",
		       act?"ends the slice":"log only");
	}
	if (act)
		g_dec_hash_blocks++;
	return act;
}

DecodedBlock* dec_DecodeBlock(u32 startpc,fpscr_type fpu_cfg,u32 max_cycles)
{
	block.Setup(startpc);
	state.Setup(startpc,fpu_cfg);
	g_dec_fpu_disabled=false;
#ifndef HOST_NO_REC
	ngen_GetFeatures(&state.ngen);
#endif
	for(;;)
	{
		switch(state.NextOp)
		{
		case NDO_Delayslot:
			state.NextOp=state.DelayOp;
			state.cpu.is_delayslot=true;
			FALLTHROUGH;	//there is no break here by design
		case NDO_NextOp:
			{
				// An MMU block's access is ~30 PPC words: 96 ops stay inside ngen_Compile's 16 KB and the cold branches' reach.
				const bool page_end=g_jit_mmu && state.cpu.rpc!=block.start && ((state.cpu.rpc&0x3FF)==0 || block.opcodes>=96);
				if ((block.cycles>=max_cycles || page_end) && !state.cpu.is_delayslot)
				{
					dec_End(state.cpu.rpc,BET_StaticJump,false);
				}
				else
				{
					u32 op=dec_read16(state.cpu.rpc);
					block.opcodes++;
					if (op>=0xF000)
						block.cycles+=0;
					else
						block.cycles+=CPU_RATIO;
					block.cur_pc=state.cpu.is_delayslot ? state.cpu.rpc-2 : state.cpu.rpc;
					block.cur_slot=state.cpu.is_delayslot;
					if (g_jit_mmu && jx_fpu_op(op))
					{
						if (sr.FD)
						{
							g_dec_fpu_disabled=true;
							return 0;
						}
						block.has_fpu=true;
					}

					RecOpCallFP* rec_oph=OpDesc[op]->rec_oph;
					if (rec_oph && dec_is_newop(op) && !get_jit_newops_preset())
						rec_oph=0;

					// JitIfbMask bisect: a whole top-nibble group to the interpreter, block ends untouched (unlike JitSafe
					// and JitDynEnds). JitIfbGroup + JitIfbLoMask sub-bisect one group by low nibble.
					const u32 hi=op>>12;
					// MMU JIT: rte (its slot runs with the new SR), pref (the UTLB) and a branch whose slot is in the next
					// page run on the MMU interpreter.
					const bool mmu_ifb=g_jit_mmu && (op==0x002B || (op&0xF0FF)==0x0083 ||
					    (OpDesc[op]->SetPC() && ((state.cpu.rpc+2)&0x3FF)==0));
					const bool force_ifb=((get_jit_ifb_mask()>>hi)&1)
					    || ((int)hi==get_jit_ifb_group()
					        && ((get_jit_ifb_lomask()>>(op&0xF))&1)) || mmu_ifb;
					if (force_ifb)
						rec_oph=0;

					if (state.ngen.OnlyDynamicEnds || !rec_oph)
					{
						if (state.ngen.InterpreterFallback || force_ifb
						    || (get_jit_pcrel_preset() && dec_is_pcrel(op))
						    || !dec_generic(op))
						{
							dec_div1_probe(op,state.cpu.rpc);
							dec_fallback(op);
							if (OpDesc[op]->SetPC())
							{
								dec_DynamicSet(reg_nextpc);
								dec_End(NullAddress,BET_DynamicJump,false);
							}
							if (OpDesc[op]->SetFPSCR() && !state.cpu.is_delayslot)
							{
								dec_End(state.cpu.rpc+2,BET_StaticJump,false);
							}
						}
					}
					else
					{
						rec_oph(op);
					}

					state.cpu.rpc+=2;
				}
			}
			break;

		case NDO_Jump:
			state.NextOp=state.JumpOp;
			state.cpu.rpc=state.JumpAddr;
			break;

		case NDO_End:
			goto _end;
		}
	}

_end:
	block.sh4_code_size=state.cpu.rpc-block.start;
	block.NextBlock=state.NextAddr;
	block.BranchBlock=state.JumpAddr;
	block.BlockType=state.BlockType;
	block.has_store=idle_block_stores(block);
	const int idle_hash_level=get_jit_idle_hash_preset();
	block.idle_hash=idle_hash_level && !g_jit_mmu && dec_idle_hash(idle_hash_level);

	//cycle tricks
	{
		// P71 JIT IDLE RULE: the x30 also bills work blocks. 1 = Reicast's x3 (a bt block only when it branches
		// back), 2 = no cycle trick at all (Flycast). idle_loop is set at every level, for JIT IDLE SKIP.
		const int idle_rule=get_jit_idle_rule_preset();
		const u32 plain_cycles=block.cycles;

		//Small-n-simple idle loop detector :p
		if (state.info.has_readm && !state.info.has_writem && !state.info.has_fpu && block.opcodes<6)
		{
			if (block.BlockType==BET_Cond_0 || block.BlockType==BET_Cond_1)
			{
				if (idle_rule==0)
					block.cycles*=30;
				else if (idle_rule==1 && (block.BlockType==BET_Cond_0 || block.BranchBlock<=block.start))
					block.cycles*=3;
				// Plan 5.25: with JIT IDLE SKIP, this backward branch may end the timeslice.
				block.idle_loop=block.BranchBlock<=block.start && idle_turn_is_pure(block);
			}

			if (block.BranchBlock==block.start && idle_rule<2)
			{
				block.cycles*=10;
			}
		}

		//if in syscalls area (ip.bin etc) skip fast :p
		if ((block.start&0x1FFF0000)==0x0C000000 && idle_rule<2)
		{
			if (block.start&0x8000)
			{
				//ip.bin (boot loader/img etc)
				block.cycles*=15;
			}
			else
			{
				//syscalls
				block.cycles*=5;
			}
			
		}
		//make sure we don't use wayy-too-many cycles ;p
		block.cycles=min(block.cycles,max_cycles);
		if (block.cycles>plain_cycles)
			g_dec_rule_blocks++;
	}

	static int cc=0;
	cc++;
	if ((cc&31)==0 && HOST_OS==OS_WINDOWS)
	{
		vector<sh4_opcodelistentry> stuff;
		for (u32 i=0;opcodes[i].oph;i++)
		{
			if (opcodes[i].fallbacks)
				stuff.push_back(opcodes[i]);
		}
		if (stuff.size() && 0)
		{
			printf("Compile stats:\n");
			qsort(&stuff[0],stuff.size(),sizeof(stuff[0]),stuffcmp);
		}
	}
	return &block;
}
void dec_Cleanup()
{
	block.oplist.clear();
}

