#include "types.h"
#include <string.h>

#include "maple_if.h"

#include "config/config.h"

#include "dc/sh4/intc.h"
#include "dc/mem/sb.h"
#include "dc/mem/sh4_mem.h"
#include "plugins/plugin_manager.h"
#include "dc/asic/asic.h"
#include "dc/maple/maple_helper.h"
#include "dc/sh4/rec_v2/jit_dma.h"
#include "ps3/ps3_stall.h"

maple_device* MapleDevices[4][6];
s32 maple_dma_pending=0;

// Plan section 7, bundle 16 (ps3_presets.cpp): 0 is the old path in each one. Read at each DMA.
extern int g_maple_delay_preset, g_maple_speed_preset, g_maple_ddt_preset, g_maple_guard_preset, g_maple_frame_preset;

// P29: replies that wait for the end interrupt, as native u32 words: the devices' XOR-3 byte writes need them.
#define MAPLE_OUT_WORDS   8192
#define MAPLE_OUT_FRAMES  128
#define MAPLE_FRAME_WORDS 256
struct MapleOut { u32 addr, first, words; };
static u32 s_out_buf[MAPLE_OUT_WORDS];
static MapleOut s_out[MAPLE_OUT_FRAMES];
static u32 s_out_n, s_out_words;
// P68: a frame with SB_MMSEL 0, with each word swapped.
static u32 s_in_buf[MAPLE_FRAME_WORDS];
// P68: a reply had no RAM address; the overrun interrupt fires at the DMA's end.
static bool s_overrun;

static inline u32 maple_swap32(u32 v)
{
	return (v<<24) | ((v&0xFF00)<<8) | ((v>>8)&0xFF00) | (v>>24);
}

// Maple host controller: direct processing, async interrupt handling (devices: maple_devs, setup: maple_cfg).
// Missing hardware details such as proper trigger handling and DMA continuation on suspend.

void maple_DoDma();

// Hackish: no delay or stop/start; DDT is only hacked far enough for WinCE. Delayed maple DMA could
// replace it.
bool maple_ddt_pending_reset=false;
void maple_vblank()
{
	if (SB_MDEN &1)
	{
		if (SB_MDTSEL&1)
		{
			if (maple_ddt_pending_reset)
			{
				//printf("DDT vblank ; reset pending\n");
			}
			else
			{
				//printf("DDT vblank\n");
				// P9: the DMA wants SB_MDST set (its verify hangs without it); Flycast clears it at the end interrupt.
				if (g_maple_ddt_preset)
					SB_MDST = 1;
				maple_DoDma();
				if (!g_maple_ddt_preset)
					SB_MDST = 0;
				if ((SB_MSYS>>12)&1)
				{
					maple_ddt_pending_reset=true;
				}
			}
		}
		else
		{
			maple_ddt_pending_reset=false;
		}
	}
}
void maple_SB_MSHTCL_Write(u32 data)
{
	if (data&1)
		maple_ddt_pending_reset=false;
}
void maple_SB_MDST_Write(u32 data)
{
	if (data & 0x1)
	{
		if (SB_MDEN &1)
		{
			SB_MDST=1;
			maple_DoDma();
		}
	}
}

bool IsOnSh4Ram(u32 addr)
{
	if (((addr>>26)&0x7)==3)
	{
		if ((((addr>>29) &0x7)!=7))
		{
			return true;
		}
	}

	return false;
}

// P29: the kept replies reach RAM.
static void maple_copy_replies()
{
	for (u32 i=0;i<s_out_n;i++)
	{
		memcpy(GetMemPtr(s_out[i].addr,s_out[i].words*4),&s_out_buf[s_out[i].first],s_out[i].words*4);
		rdv_DmaWrite(s_out[i].addr,s_out[i].words*4,JDMA_MAPLE);
	}
	s_out_n=s_out_words=0;
}

// The end of a DMA (Flycast maple_schd): the kept replies reach RAM, or an aborted DMA drops them.
static void maple_end_replies(bool copy)
{
	if (copy)
	{
		maple_copy_replies();
		if (s_overrun)
			asic_RaiseInterrupt(holly_MAPLE_OVERRUN);
	}
	s_out_n=s_out_words=0;
	s_overrun=false;
}

u32 dmacount=0;
void maple_DoDma()
{
	StallScope st_(ST_MAPLE);
	verify(SB_MDEN &1)
	verify(SB_MDST &1)

#if debug_maple
	printf("Maple: DoMapleDma\n");
#endif
	const bool delay=g_maple_delay_preset!=0, guard=g_maple_guard_preset!=0, frame_rules=g_maple_frame_preset!=0;
	// P68: SB_MMSEL 0 sends each word of a frame MSB first.
	const bool swap_msb=frame_rules && SB_MMSEL==0;
	u32 addr = SB_MDSTAR;
	u32 xfer_count=0;
	// P29: the bytes to the devices and the bytes back, each frame with its start, parity and stop bytes.
	u32 xfer_in=0, xfer_out=0;
	bool last = false;
	while (last != true)
	{
		dmacount++;
		u32 header_1 = ReadMem32_nommu(addr);
		u32 header_2 = ReadMem32_nommu(addr + 4) &0x1FFFFFE0;

		last = (header_1 >> 31) == 1;//is last transfer ?
		u32 plen = (header_1 & 0xFF )+1;//transfer length
		u32 maple_op=(header_1>>8)&7;
		xfer_count+=plen*4;

		//this is kinda wrong .. but meh
		//really need to properly process the commands at some point
		if (maple_op==0)
		{
			const bool on_ram=IsOnSh4Ram(header_2);
			if (!on_ram)
			{
				printf("MAPLE ERROR : DESTINATION NOT ON SH4 RAM 0x%X\n",header_2);
				header_2&=0xFFFFFF;
				header_2|=(3<<26);
			}
			// P68: with the guard, a reply with no RAM address is not written; before, it went to a masked address.
			const bool drop=!on_ram && guard;
			u32* p_out=(u32*)GetMemPtr(header_2,4);
			u32 outlen=0;

			u32* p_data =(u32*) GetMemPtr(addr + 8,(plen)*sizeof(u32));
			// P68: a command table outside RAM stops the DMA; without the guard the null pointer is read.
			if (!p_data && guard)
			{
				printf("MAPLE ERROR : COMMAND TABLE NOT ON SH4 RAM 0x%X\n",addr);
				SB_MDST=0;
				maple_end_replies(false);
				return;
			}
			// The swap is on the native u32 values, never on the XOR-3 byte view.
			if (swap_msb)
			{
				for (u32 i=0;i<plen;i++)
					s_in_buf[i]=maple_swap32(p_data[i]);
				p_data=s_in_buf;
			}

			//Command code
			u32 command=p_data[0] &0xFF;
			//Recipient address
			u32 reci=(p_data[0] >> 8) & 0xFF;//0-5;
			u32 port=maple_GetPort(reci);
			u32 bus=maple_GetBusId(reci);
			// P68: an address with no unit bit is the main unit; before, sub-unit 1 took it.
			if (frame_rules && !(reci&0x3F))
				port=5;
			//Sender address
			u32 send=(p_data[0] >> 16) & 0xFF;
			//Number of additional words in frame
			u32 inlen=(p_data[0]>>24) & 0xFF;
			u32 resp=0;
			inlen*=4;

			// The old path writes the reply straight into RAM. A row that changes the reply builds it in the pool's tail.
			const bool direct=!delay && !drop && !swap_msb;
			u32* out=p_out;
			u32 words=1;
			if (!direct)
			{
				if (s_out_n==MAPLE_OUT_FRAMES || s_out_words+MAPLE_FRAME_WORDS>MAPLE_OUT_WORDS)
				{
					printf("[maple] reply pool full: %u replies written before the end interrupt\n",s_out_n);
					maple_copy_replies();
				}
				out=&s_out_buf[s_out_words];
			}

			if (MapleDevices[bus][5] && MapleDevices[bus][port])
			{
				resp=MapleDevices[bus][port]->Dma(command,&p_data[1],inlen,&out[1],outlen);

				if(reci&0x20)
					reci|=maple_GetAttachedDevices(bus);

				verify(u8(outlen/4)*4==outlen);
				out[0]=(resp<<0)|(send<<8)|(reci<<16)|((outlen/4)<<24);
				xfer_count+=outlen+4;
				xfer_in+=plen*4+3;
				xfer_out+=outlen+4+3;
				words=outlen/4+1;
			}
			else
			{
				outlen=4;
				out[0]=0xFFFFFFFF;
			}

			if (!direct)
			{
				if (swap_msb)
					for (u32 i=0;i<words;i++)
						out[i]=maple_swap32(out[i]);
				if (drop)
					s_overrun=true;
				else if (delay)
				{
					s_out[s_out_n].addr=header_2;
					s_out[s_out_n].first=s_out_words;
					s_out[s_out_n].words=words;
					s_out_n++;
					s_out_words+=words;
				}
				else
					memcpy(p_out,out,words*4);
			}
			// The reply is in RAM now, unless it was dropped or waits for the end interrupt.
			if (!drop && !delay)
				rdv_DmaWrite(header_2,words*4,JDMA_MAPLE);

			//goto next command
			addr += 2 * 4 + plen * 4;
		}
		else
		{
			// P29: a reset pattern, and an SDCKB occupy to a plugged bus, are one byte on the wire.
			if (maple_op==3 || (maple_op==2 && MapleDevices[(header_1>>16)&3][5]))
				xfer_in++;
			addr += 1 * 4;
		}
	}

	//printf("Maple XFER size %d bytes - %.2f ms\n",xfer_count,xfer_count*100.0f/(2*1024*1024/8));
	if (g_maple_speed_preset)
	{
		// P29, Flycast d84108de3: 2 Mb/s to the devices, 740 Kb/s back; one second at most, one cycle at least.
		u64 cycles=(u64)SH4_CLOCK_EFF*xfer_in/(2000000/8)+(u64)SH4_CLOCK_EFF*xfer_out/(740000/8);
		if (cycles>SH4_CLOCK_EFF)
			cycles=SH4_CLOCK_EFF;
		maple_dma_pending=cycles ? (s32)cycles : 1;
	}
	else
		maple_dma_pending=xfer_count*(SH4_CLOCK_EFF/(2*1024*1024/8));
}

void maple_Update(u32 cycles)
{
	if (maple_dma_pending>0)
	{
		maple_dma_pending-=cycles;
		if (SB_MDEN&1)
		{
			if (maple_dma_pending<=0)
			{
				maple_end_replies(true);
				SB_MDST=0;
				asic_RaiseInterrupt(holly_MAPLE_DMA);
			}
		}
		else
		{
			printf("WARNING: MAPLE DMA ABORT\n");
			if (maple_dma_pending<=0)
				maple_end_replies(false);
			SB_MDST=0;	//I really wonder what this means, can the dma be continued ?
		}
	}
}

//Init registers :)
void maple_Init()
{
	sb_regs[(SB_MDST_addr-SB_BASE)>>2].flags=REG_32BIT_READWRITE | REG_READ_DATA;
	sb_regs[(SB_MDST_addr-SB_BASE)>>2].writeFunction=maple_SB_MDST_Write;

	sb_regs[(SB_MSHTCL_addr-SB_BASE)>>2].flags=REG_32BIT_READWRITE;
	sb_regs[(SB_MSHTCL_addr-SB_BASE)>>2].writeFunction=maple_SB_MSHTCL_Write;
}

void maple_Reset(bool Manual)
{
	maple_ddt_pending_reset=false;
	maple_dma_pending=0;
	maple_end_replies(false);
	SB_MDTSEL	= 0x00000000;
	SB_MDEN	= 0x00000000;
	SB_MDST	= 0x00000000;
	SB_MSYS	= 0x3A980000;
	SB_MSHTCL	= 0x00000000;
	SB_MDAPRO = 0x00007F00;
	SB_MMSEL	= 0x00000001;
}

void maple_Term()
{

}
