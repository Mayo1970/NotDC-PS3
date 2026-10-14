#pragma once
#include "dc/sh4/sh4_if.h"

struct shil_opcode;
typedef void shil_chfp(shil_opcode* op);
extern shil_chfp* shil_chf[];

// shil_opcode::flags bit for shop_sync_fpscr, set by `fschg`: it flips FPSCR.SZ (bit 20) only, so there is no FR bank
// swap or RM/DN host-FPSCR update and a backend may skip UpdateFPSCR(). frchg and ldc/ldc.l to FPSCR must NOT set it.
#define SYNC_FPSCR_SZ_ONLY 1

enum shil_param_type
{
    // 2 bits used for base type
    FMT_NULL        = 0,
    FMT_IMM         = 1,
    FMT_I32         = 2,
    FMT_F32         = 3,
    FMT_F64         = 4,
    FMT_V4          = 5,
    FMT_V16         = 6,

    FMT_REG_BASE    = FMT_I32,
    FMT_VECTOR_BASE = FMT_V4,

    FMT_MASK        = 0xFFFF,
};

// formats: 16u 16s 32u 32s, 32f, 64f; param types: r32, r64


#define SHIL_MODE 0
#include "shil_canonical.h"

// Returns a pointer to the physical register storage for a given register index.
//this should be really removed ...
u32* GetRegPtr(u32 reg);

// mac.l / mac.w reference (JIT_MAC preset): SH-4 manual pseudocode, saturation included; writes MACH/MACL. The OLD
// accumulator is passed in, so a backend can emit S==0 inline and call this only to redo S==1 (ps3_driver.cpp does).
extern "C" void sh4_mac_l(u32 a, u32 b, u32 oldl, u32 oldh, u32 srs);
extern "C" void sh4_mac_w(u32 a, u32 b, u32 oldl, u32 oldh, u32 srs);

struct shil_param
{
    shil_param()
        : _imm(0xFFFFFFFF), type(FMT_NULL)
    {}

    // Raw type + immediate/register index. Register types (>= FMT_REG_BASE) delegate to the Sh4RegType constructor
    // (the original placement-new then overwrote _imm, losing that setup); FMT_NULL/FMT_IMM store the raw value.
    shil_param(u32 paramType, u32 imm)
    {
        if (paramType >= FMT_REG_BASE)
        {
            // Treat imm as a Sh4RegType
            *this = shil_param(static_cast<Sh4RegType>(imm));
            // The type inferred from the register wins over paramType (no caller override such as forcing
            // FMT_I32): the Sh4RegType constructor already picks the right one.
        }
        else
        {
            type  = paramType;
            _imm  = imm;
        }
    }

    // Implicit conversion from Sh4RegType is intentional — the entire
    // decoder passes bare register enum values as shil_param arguments.
    shil_param(Sh4RegType reg)
        : _imm(0xFFFFFFFF), type(FMT_NULL)
    {
        if (reg >= reg_fr_0 && reg <= reg_xf_15)
        {
            type = FMT_F32;
            _reg = reg;
        }
        else if (reg >= regv_dr_0 && reg <= regv_dr_14)
        {
            type = FMT_F64;
            _imm = (reg - regv_dr_0) * 2 + reg_fr_0;
        }
        else if (reg >= regv_xd_0 && reg <= regv_xd_14)
        {
            type = FMT_F64;
            _imm = (reg - regv_xd_0) * 2 + reg_xf_0;
        }
        else if (reg >= regv_fv_0 && reg <= regv_fv_12)
        {
            type = FMT_V4;
            _imm = (reg - regv_fv_0) * 4 + reg_fr_0;
        }
        else if (reg == regv_xmtrx)
        {
            type = FMT_V16;
            _imm = reg_xf_0;
        }
        else
        {
            type = FMT_I32;
            _reg = reg;
        }
    }

    union
    {
        u32        _imm;
        Sh4RegType _reg;
    };
    u32 type;

    // ---- Type predicates ----
    bool is_null()  const { return type == FMT_NULL; }
    bool is_imm()   const { return type == FMT_IMM;  }
    bool is_reg()   const { return type >= FMT_REG_BASE; }

    bool is_r32i()  const { return type == FMT_I32; }
    bool is_r32f()  const { return type == FMT_F32; }
    bool is_r64f()  const { return type == FMT_F64; }

    // Returns the vector width (4 or 16) if this is a vector register, else 0.
    u32  is_r32fv() const { return (type >= FMT_VECTOR_BASE) ? static_cast<u32>(count()) : 0u; }

    bool is_r32()   const { return is_r32i() || is_r32f(); }
    bool is_r64()   const { return is_r64f(); }

    bool is_vector() const { return type >= FMT_VECTOR_BASE; }

    // ---- Immediate range helpers ----
    bool is_imm_s8()  const { return is_imm() && is_s8(_imm);  }
    bool is_imm_u8()  const { return is_imm() && is_u8(_imm);  }
    bool is_imm_s16() const { return is_imm() && is_s16(_imm); }
    bool is_imm_u16() const { return is_imm() && is_u16(_imm); }

    // ---- Register pointer / offset ----
    u32* reg_ptr() const { verify(is_reg()); return GetRegPtr(_reg); }
    u32  reg_ofs() const
    {
        verify(is_reg());
        return static_cast<u32>(
            reinterpret_cast<u8*>(GetRegPtr(_reg)) -
            reinterpret_cast<u8*>(GetRegPtr(reg_xf_0))
        );
    }

    // ---- Number of 32-bit SH4 registers this param spans ----
    int count() const
    {
        switch (type)
        {
            case FMT_F64:  return 2;
            case FMT_V4:   return 4;
            case FMT_V16:  return 16;
            default:       return 1;
        }
    }
};

struct shil_opcode
{
    shilop op;
    u32    Flow;
    u32    flags;

    shil_param rd, rd2;
    shil_param rs1, rs2, rs3;

    // MMU JIT (P38): the SPC of an exception in this op (a delay slot's is its branch), and whether it is a slot.
    u32    guest_pc;
    u32    delay_slot;
};
