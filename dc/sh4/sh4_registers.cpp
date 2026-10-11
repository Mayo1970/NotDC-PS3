// Sh4 register storage/functions/utilities

#include "types.h"
#include "sh4_registers.h"
#include "dc/sh4/rec_v2/ccall_census.h"
#include "intc.h"

ALIGN(64) Sh4Context Sh4cntx;

INLINE void ChangeGPR()
{
	u32 temp[8];
	for (int i=0;i<8;i++)
	{
		temp[i]=r[i];
		r[i]=r_bank[i];
		r_bank[i]=temp[i];
	}
}

INLINE void ChangeFP()
{
	u32 temp[16];
	for (int i=0;i<16;i++)
	{
		temp[i]=fr_hex[i];
		fr_hex[i]=xf_hex[i];
		xf_hex[i]=temp[i];
	}
}

//called when sr is changed and we must check for reg banks ect.. , returns true if interrupts got
bool UpdateSR()
{
	CCALL(CC_SYNC_SR);
	if (sr.MD)
	{
		if (old_sr.RB !=sr.RB)
			ChangeGPR();//bank change
	}
	else
	{
		if (sr.RB)
		{
			printf("UpdateSR MD=0;RB=1 , this must not happen\n");
			sr.RB =0;//error - must allways be 0
			if (old_sr.RB)
				ChangeGPR();//switch
		}
		else
		{
			if (old_sr.RB)
				ChangeGPR();//switch
		}
	}

	old_sr.status=sr.status;

	return SRdecode();
}

extern "C" int get_fpu_dn_preset();

//make x86 and sh4 float status registers match ;)
static u32 old_rm=0xFF;
static u32 old_dn=0xFF;
void SetFloatStatusReg()
{
	if ((old_rm!=fpscr.RM) || (old_dn!=fpscr.DN) || (old_rm==0xFF )|| (old_dn==0xFF))
	{
		old_rm=fpscr.RM ;
		old_dn=fpscr.DN ;

		//TODO: Implement this (needed for SOTB)
#if HOST_ARCH==ARCH_X86
		u32 temp=0x1f80;	//no flush to zero && round to nearest

		if (fpscr.RM==1)	//if round to 0 , set the flag
			temp|=(3<<13);

		if (fpscr.DN)		//denormals are considered 0
			temp|=(1<<15);

		#if HOST_OS==OS_WINDOWS
		_asm { ldmxcsr temp; }
		#elif defined(__GNUC__)
		__asm__ volatile("ldmxcsr %0" :: "m"(temp));
		#endif
#elif HOST_CPU==CPU_PPC_CELL
		// FPSCR field 7 = XE,NI,RN. RN=01 is toward zero: the BIOS runs with RM=1, nearest skips the boot animation.
		// FPU DN (plan 5.40) adds NI (4) while DN is set. ps3_rend.cpp restores nearest, NI clear, while drawing.
		const bool ni=fpscr.DN && get_fpu_dn_preset();
		if (fpscr.RM==1)
		{
			if (ni)
				__asm__ volatile("mtfsfi 7,5" ::: "memory");
			else
				__asm__ volatile("mtfsfi 7,1" ::: "memory");
		}
		else if (ni)
			__asm__ volatile("mtfsfi 7,4" ::: "memory");
		else
			__asm__ volatile("mtfsfi 7,0" ::: "memory");
#endif
	}
}
//called when fpscr is changed and we must check for reg banks ect..
void UpdateFPSCR()
{
	CCALL(CC_SYNC_FPSCR);
	if (fpscr.FR !=old_fpscr.FR)
		ChangeFP();//fpu bank change
	old_fpscr=fpscr;
	SetFloatStatusReg();//ensure they are on sync :)
}


u32* Sh4_int_GetRegisterPtr(Sh4RegType reg)
{
	if ((reg>=reg_r0) && (reg<=reg_r15))
	{
		return &r[reg-reg_r0];
	}
	else if ((reg>=reg_r0_Bank) && (reg<=reg_r7_Bank))
	{
		return &r_bank[reg-reg_r0_Bank];
	}
	else if ((reg>=reg_fr_0) && (reg<=reg_fr_15))
	{
		return &fr_hex[reg-reg_fr_0];
	}
	else if ((reg>=reg_xf_0) && (reg<=reg_xf_15))
	{
		return &xf_hex[reg-reg_xf_0];
	}
	else
	{
		switch(reg)
		{
		case reg_gbr :
			return &gbr;
		case reg_vbr :
			return &vbr;

		case reg_ssr :
			return &ssr;

		case reg_spc :
			return &spc;

		case reg_sgr :
			return &sgr;

		case reg_dbr :
			return &dbr;

		case reg_mach :
			return &mach;

		case reg_macl :
			return &macl;

		case reg_pr :
			return &pr;

		case reg_fpul :
			return &fpul;


		case reg_nextpc :
			return &next_pc;

		case reg_sr_status :
			return &sr.status;

		case reg_sr_T :
			return &sr.T;

		case reg_fpscr :
			return &fpscr.full;

		case reg_temp :
			return &Sh4cntx.jit_temp;

		case reg_temp2 :
			return &Sh4cntx.jit_temp2;

		default:
			// EMUERROR2("Unkown register Id %d",reg);
			// printf instead of EMUERROR because tiny freeze otherwise
			printf("Unkown register Id %d\n",reg);
			die("invalid reg");
			return nullptr;
		}
	}
}

u32 Sh4Context::offset(u32 sh4_reg)
{
	void* addr=Sh4_int_GetRegisterPtr((Sh4RegType)sh4_reg);
	u32 offs=(u8*)addr-(u8*)&Sh4cntx;
	verify(offs<sizeof(Sh4cntx));

	return offs;
}