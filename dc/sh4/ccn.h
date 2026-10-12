#pragma once

#include "types.h"

// CCN: the SH7091's cache and TLB controller (on-chip MMU and cache control registers).

void ccn_Init();
void ccn_Reset(bool Manual);
void ccn_Term();

// PTEH, Page Table Entry High (0xFF000000): VPN in bits 31-10, ASID in bits 7-0.
union CCN_PTEH_type
{
    struct {
#if HOST_ENDIAN == ENDIAN_LITTLE
        u32 ASID : 8;   // bits  7- 0 — Address Space ID
        u32 res  : 2;   // bits  9- 8 — reserved
        u32 VPN  : 22;  // bits 31-10 — Virtual Page Number
#else
        u32 VPN:22; //10-31 VPN
        u32 res:2;  //8,9 reserved
		    u32 ASID:8; //0-7 ASID
#endif
    };
    u32 reg_data;
};

// PTEL, Page Table Entry Low (0xFF000004): PPN in bits 28-10; PR protection, SZ page size, C cacheable, D dirty,
// SH shared, WT write-through, V valid.
union CCN_PTEL_type
{
	struct
	{
#if HOST_ENDIAN==ENDIAN_LITTLE
		u32 WT:1;
		u32 SH:1;
		u32 D :1;
		u32 C :1;
		
		u32 SZ0:1;
		u32 PR :2;
		u32 SZ1:1;

		u32 V:1;
		u32 res_0:1;
		u32 PPN:19;//PPN 10-28
		u32 res_1:3;
#else
		u32 res_1:3;
		u32 PPN:19;//PPN 10-28
		u32 res_0:1;
		u32 V:1;

		u32 SZ1:1;
		u32 PR :2;
		u32 SZ0:1;

		u32 C :1;
		u32 D :1;
		u32 SH:1;
		u32 WT:1;
#endif
	};
	u32 reg_data;
};

// MMUCR, MMU Control Register (0xFF000010). The bitfield has 28 bits, so on big endian only LRUI is at its bit:
// AT reads bit 4, TI bit 6, URC bits 19-14 (P58). MMUCR Bits OLD reads it; FIXED reads the masks below.
union CCN_MMUCR_type
{
	struct
	{
#if HOST_ENDIAN==ENDIAN_LITTLE
		u32 AT:1;
		u32 res:1;
		u32 TI:1;
		u32 res_2:5;
		u32 SV:1;
		u32 SQMD:1;
		u32 URC:6;
		u32 URB:6;
		u32 LRUI:6;
#else
		u32 LRUI:6;
		u32 URB:6;
		u32 URC:6;
		u32 SQMD:1;
		u32 SV:1;
		u32 res_2:5;
		u32 TI:1;
		u32 res:1;
		u32 AT:1;
#endif
	};
	u32 reg_data;
};

// The manual's MMUCR fields: AT bit 0, TI bit 2, SV bit 8, SQMD bit 9, URC bits 15-10, URB 23-18, LRUI 31-26.
#define MMUCR_AT(v)    ((v) & 1u)
#define MMUCR_TI(v)    (((v) >> 2) & 1u)
#define MMUCR_SV(v)    (((v) >> 8) & 1u)
#define MMUCR_SQMD(v)  (((v) >> 9) & 1u)
#define MMUCR_URC(v)   (((v) >> 10) & 63u)
#define MMUCR_URB(v)   (((v) >> 18) & 63u)
#define MMUCR_LRUI(v)  (((v) >> 26) & 63u)
#define MMUCR_TI_BIT   0x00000004u
// The bits a write keeps (Flycast CCN_MMUCR_write, P78).
#define MMUCR_WMASK    0xFCFCFF05u

union CCN_PTEA_type
{
	struct
	{
#if HOST_ENDIAN==ENDIAN_LITTLE
		u32 SA:3;
		u32 TC:1;
		u32 res:28;
#else
		u32 res:28;
		u32 TC:1;
		u32 SA:3;
#endif
	};
	u32 reg_data;
};

// CCR, Cache Control Register (0xFF00001C): OCE/ICE cache enables, OCI/ICI invalidate (self-clearing), ORA = the
// operand cache as RAM, OIX/IIX index modes, WT/CB write-through and copy-back.
union CCN_CCR_type
{
	struct
	{
#if HOST_ENDIAN==ENDIAN_LITTLE
		u32 OCE:1;
		u32 WT:1;
		u32 CB:1;
		u32 OCI:1;
		u32 res:1;
		u32 ORA:1;
		u32 res_1:1;
		u32 OIX:1;
		u32 ICE:1;
		u32 res_2:2;
		u32 ICI:1;
		u32 res_3:3;
		u32 IIX:1;
		u32 res_4:16;
#else
		u32 res_4:16;
		u32 IIX:1;
		u32 res_3:3;
		u32 ICI:1;
		u32 res_2:2;
		u32 ICE:1;
		u32 OIX:1;
		u32 res_1:1;
		u32 ORA:1;
		u32 res:1;
		u32 OCI:1;
		u32 CB:1;
		u32 WT:1;
		u32 OCE:1;
#endif
	};
	u32 reg_data;
};

union CCN_QACR_type
{
	struct
	{
#if HOST_ENDIAN==ENDIAN_LITTLE
		u32 res:2;
		u32 Area:3;
		u32 res_1:27;
#else
		u32 res_1:27;
		u32 Area:3;
		u32 res:2;
#endif
	};
	u32 reg_data;
};


//Types
extern CCN_PTEH_type CCN_PTEH;
extern CCN_PTEL_type CCN_PTEL;
extern u32 CCN_TTB;
extern u32 CCN_TEA;
extern CCN_MMUCR_type CCN_MMUCR;
extern u8 CCN_BASRA;
extern u8 CCN_BASRB;
extern CCN_CCR_type CCN_CCR;
extern u32 CCN_TRA;
extern u32 CCN_EXPEVT;
extern u32 CCN_INTEVT;
extern CCN_PTEA_type CCN_PTEA;
extern CCN_QACR_type CCN_QACR0;
extern CCN_QACR_type CCN_QACR1;

// MMUCR Bits (plan section 7, bundle 13): 0 = the bitfield as it compiles, 1 = the manual's bits.
extern int g_mmu_bits_preset;
static inline u32 mmucr_at()  { return g_mmu_bits_preset ? MMUCR_AT(CCN_MMUCR.reg_data)  : CCN_MMUCR.AT; }
static inline u32 mmucr_urc() { return g_mmu_bits_preset ? MMUCR_URC(CCN_MMUCR.reg_data) : CCN_MMUCR.URC; }
static inline u32 mmucr_urb() { return g_mmu_bits_preset ? MMUCR_URB(CCN_MMUCR.reg_data) : CCN_MMUCR.URB; }
