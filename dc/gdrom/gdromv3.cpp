#include "gdromv3.h"

#include "plugins/plugin_manager.h"
#include "dc/mem/sh4_mem.h"
#include "dc/mem/memutil.h"
#include "dc/mem/sb.h"
#include "dc/sh4/dmac.h"
#include "dc/sh4/intc.h"
#include "dc/sh4/sh4_registers.h"
#include "dc/asic/asic.h"
#include "dc/sh4/sh4_sched.h"   // SCHED preset: deadline-ordered GD-DMA completion
#include "dc/sh4/rec_v2/jit_dma.h"
#include "ps3/ps3_stall.h"
#include "ps3/ps3_gdasync.h"
#include "ps3_timebase.h"

// SCHED preset: the GD-DMA end IRQ goes through the deadline scheduler,
// so it fires in order with the other DMA/render completions.
extern "C" int get_sched_preset();

// Scheduler id for the deferred GD-DMA end IRQ; registered in gdrom_reg_Init().
static int gdrom_dma_schid = -1;

// Only needs to be >0 so the completion goes through the ordered queue; the value is not critical.
#define GDROM_DMA_IRQ_DELAY 200

static int gdrom_dma_end_cb(int tag, int sch_cycl, int jitter)
{
	asic_RaiseInterrupt(holly_GDROM_DMA);
	return 0;   // single-shot
}

extern "C" int get_debug_loop();
extern "C" int get_debug_gdrom();

// Plan section 7, bundle 2: each item has its own option on the GD-ROM page; 0 is the old path.
extern "C" {
int get_gd_status_preset();     // P21: 1 = a data read stops CD-DA, 2 = also Flycast dev's status timing
int get_gd_position_preset();   // P47: 1 = REQ_STAT position bytes, 2 = GET_SCD format 0 too
int get_gd_scd_q_preset();      // P62: GET_SCD format 1 layout, replies cut at the asked length
int get_cdda_rules_preset();    // P61: CD-DA end, resume and repeat rules; 2 = also the stop at the lead-out (P82)
int get_gd_speed_preset();      // P22: 1 = DMA paced as the drive
int get_gd_timing_preset();     // 1 = the SH4 waits for a late disc read; 2, 3 = also a 100, 330 ms seek (DMA reads)
int get_gd_dma_fix_preset();    // P22 SB_GDLEN 0, P9 DMA clamp
int get_cdda_miss_preset();     // 5.34: 1 = read-ahead from CD_PLAY, 2 = a miss plays silence, 3 = loop start read ahead
int get_gd_cmd_fix_preset();    // P22: ATA and SPI command hangs
int get_gd_set_mode_preset();   // P48
int get_disc_swap_preset();     // P34
int get_holly_regs_preset();    // bundle 20, P68: the ATA soft reset's register values
}

enum gd_states
{
	//Generic
	gds_waitcmd,
	gds_procata,
	gds_waitpacket,
	gds_procpacket,
	gds_pio_send_data,
	gds_pio_get_data,
	gds_pio_end,
	gds_procpacketdone,

	//Command spec.
	gds_readsector_pio,
	gds_readsector_dma,
	gds_process_set_mode,
};

struct
{
	u32 start_sector;
	u32 remaining_sectors;
	u32 sector_type;
} read_params;

struct
{
	u32 index;
	union
	{
		u16 data_16[6]; //*WATCH* this is in native-endian form ..
		u8 data_8[12];
		//Spi command structs
		union
		{
			struct
			{
				u8 cc;

#if HOST_ENDIAN==ENDIAN_LITTLE
				u8 prmtype	: 1 ;
				u8 expdtype	: 3 ;
				//	u8 datasel	: 4 ;
				u8 other	: 1 ;
				u8 data		: 1 ;
				u8 subh		: 1 ;
				u8 head		: 1 ;
#else
				u8 head		: 1 ;
				u8 subh		: 1 ;
				u8 data		: 1 ;
				u8 other	: 1 ;
				u8 expdtype	: 3 ;
				u8 prmtype	: 1 ;
#endif

				u8 block[10];
			};

			struct
			{
				u8 b[12];
			};
		}GDReadBlock;
	};
} packet_cmd;

//Buffer for sector reads [ dma ]
struct
{
	u32 cache_index;
	u32 cache_size;
	u8 cache[2352*32];	//up to 32 sectors
} read_buff;

//pio buffer
struct
{
	gd_states next_state;
	u32 index;
	u32 size;
	u16 data[0x10000>>1];	//64 kb
} pio_buff;

u32 set_mode_offset;
struct
{
	u8 command;
} ata_cmd;

// P21, P61: the CD-DA state as Flycast's cdda_t. playing is true in CDDA_PLAYING only; the old paths test it.
enum { CDDA_NOINFO, CDDA_PLAYING, CDDA_PAUSED, CDDA_TERMINATED };

struct
{
	bool playing;
	u32 status;
	u32 repeats;
	union
	{
		u32 FAD;
		struct
		{
			u8 B0;		//msb
			u8 B1;		//midle byte
			u8 B2;		//lsb
		};
	}CurrAddr,EndAddr,StartAddr;
} cdda;


gd_states gd_state;
DiscType gd_disk_type;
// Reset -> gds_waitcmd; an ATA command (code in ata_cmd) is processed there.
// SPI packet: gds_waitpacket -> gds_procpacket -> command-specific states.
u32 data_write_mode=0;

//Registers
	u32 DriveSel;
	GD_ErrRegT Error;
	GD_InterruptReasonT IntReason;
	GD_FeaturesT Features;
	GD_SecCountT SecCount;
	GD_SecNumbT SecNumber;

	GD_StatusT GDStatus;

	union
	{
		struct
		{
		u8 low;
		u8 hi;
		};
		//u16 full;
		u16 Get() { return low | (hi<<8); }
		void Set(u16 v) { low=(u8)v; hi=v>>8; }
	} ByteCount;

//end

void nilprintf(...){}

#define printf_rm nilprintf
#define printf_ata nilprintf
#define printf_spi nilprintf
#define printf_spicmd printf

// P3: a disc read on the SH4 thread, timed per caller for the [gdr] line.
static void gd_read(int who,u8* buff,u32 fad,u32 count,u32 secsz)
{
	const u64 t0=gettime();
	libGDR_ReadSector(buff,fad,count,secsz);
	gda_count(who,count,gettime()-t0);
}

static void cdda_set(u32 st)
{
	cdda.status=st;
	cdda.playing=st==CDDA_PLAYING;
}

// P21 step 2 (Flycast dev cddaTransition): the status shows transient, then state after ms of emulated time.
static int gd_trans_next=-1;
static s64 gd_trans_cycles;

static void cdda_transition(int state,u32 ms,int transient)
{
	SecNumber.Status=transient;
	gd_trans_next=state;
	gd_trans_cycles=(s64)(SH4_CLOCK_EFF/1000)*ms;
}

// GD-ROM Speed REAL: SH4 cycles until the next DMA chunk is due.
static s64 gd_dma_wait;

// GD-ROM Timing SEEK: SH4 cycles until a DMA read that does not go on from the last read has its data, and the FAD
// where the last read ended (none after a reset).
static s64 gd_seek_wait;
static u32 gd_head_fad=0xFFFFFFFF;

// P34: sense key, ASC and ASCQ as Flycast's; only a disc swap sets them, and they act only after one.
static int sns_key,sns_asc,sns_ascq;
static bool gd_swapped;
static s64 gd_swap_cycles;

// CD-DA Miss + SILENCE: pulls in a row that the ring did not have; past the limit the SH4 thread reads again.
#define CDDA_HOLD_MAX 150
static u32 cdda_held;

void FASTCALL gdrom_get_cdda(s16* sector)
{
	//silence ! :p
	if (cdda.playing)
	{
		// P82 (Flycast): the play ends at the lead-out, in PAUSE, with the position on the last sector it played.
		if (get_cdda_rules_preset()>=2 && cdda.CurrAddr.FAD>=libGDR_GetLeadOut())
		{
			printf("[cdda] play stopped at the lead-out, FAD %u\n",cdda.CurrAddr.FAD);
			cdda.CurrAddr.FAD--;
			cdda_set(CDDA_TERMINATED);
			SecNumber.Status=GD_PAUSE;
			cdda_held=0;
			gda_cdda_next(false,0);
			memset(sector,0,2352);
			return;
		}
		// P3 + CD-DA: the worker's read-ahead has the sector most of the time.
		if (!gda_cdda((u8*)sector,cdda.CurrAddr.FAD))
		{
			// CD-DA Miss + SILENCE: the sector waits for the worker, so the SH4 thread does not stall on the disc.
			if (get_cdda_miss_preset()>=2 && gda_cdda_on() && cdda_held<CDDA_HOLD_MAX)
			{
				cdda_held++;
				memset(sector,0,2352);
				gda_cdda_next(true,cdda.CurrAddr.FAD);
				return;
			}
			gd_read(GDR_CDDA,(u8*)sector,cdda.CurrAddr.FAD,1,2352);
		}
		cdda_held=0;
		cdda.CurrAddr.FAD++;
		// P61: >= also ends a play that started at or past its end; the drive then stays in PAUSE (Flycast).
		const bool rules=get_cdda_rules_preset()!=0;
		if (rules ? cdda.CurrAddr.FAD>=cdda.EndAddr.FAD : cdda.CurrAddr.FAD==cdda.EndAddr.FAD)
		{
			if (cdda.repeats==0)
			{
				//stop
				cdda_set(CDDA_TERMINATED);
				SecNumber.Status=rules ? GD_PAUSE : GD_STANDBY;
			}
			else
			{
				//Repeat ;)
				if (cdda.repeats!=0xf)
					cdda.repeats--;
				cdda.CurrAddr.FAD=cdda.StartAddr.FAD;
			}
		}
		// CD-DA Miss + LOOP AHEAD: a play that turns back gets its loop start read before the turn.
		gda_cdda_loop(get_cdda_miss_preset()>=3 && cdda.playing && cdda.repeats ? cdda.StartAddr.FAD : GDA_NO_LOOP,
		              cdda.EndAddr.FAD);
		gda_cdda_next(cdda.playing,cdda.CurrAddr.FAD);
	}
	else
	{
		memset(sector,0,2352);
	}
}
void gd_spi_pio_end(u8* buffer,u32 len,gd_states next_state=gds_pio_end);
void gd_process_spi_cmd();
void gd_process_ata_cmd();
void FillReadBuffer()
{
	read_buff.cache_index=0;
	u32 count = read_params.remaining_sectors;
	if (count>32)
		count=32;

	read_buff.cache_size=count*read_params.sector_type;

	// P3: with GD-ROM Async the worker reads it, and UpdateGDRom holds the DMA until it lands.
	if (gda_async())
		gda_fill(read_buff.cache,read_params.start_sector,count,read_params.sector_type);
	else
		gd_read(GDR_DMA,read_buff.cache,read_params.start_sector,count,read_params.sector_type);
	read_params.start_sector+=count;
	read_params.remaining_sectors-=count;
}

void gd_set_state(gd_states state)
{
	gd_states prev=gd_state;
	gd_state=state;
	switch(state)
	{
		case gds_waitcmd:
			GDStatus.DRDY=1;	//Can accept ata cmd :)
			GDStatus.BSY=0;		//Does not access command block
			break;

		case gds_procata:
			//verify(prev==gds_waitcmd);	//validate the previus cmd ;)

			GDStatus.DRDY=0;	//can't accept ata cmd
			GDStatus.BSY=1;		//accessing command block to process cmd
			gd_process_ata_cmd();
			break;

		case gds_waitpacket:
			verify(prev==gds_procata);	//validate the previus cmd ;)

			//prepare for packet cmd
			packet_cmd.index=0;

			//Set CoD, clear BSY and IO
			IntReason.CoD=1;
			GDStatus.BSY = 0;
			IntReason.IO=0;

			//Make DRQ valid
			GDStatus.DRQ = 1;

			//ATA can optionaly raise the interrupt ...
			//RaiseInterrupt(holly_GDROM_CMD);
			break;

		case gds_procpacket:
			verify(prev==gds_waitpacket);	//validate the previus state ;)

			GDStatus.DRQ=0;		//can't accept ata cmd
			GDStatus.BSY=1;		//accessing command block to process cmd
			gd_process_spi_cmd();
			break;
			//yep , get/set are the same !
		case gds_pio_get_data:
		case gds_pio_send_data:
			//	When preparations are complete, the following steps are carried out at the device.
			//(1)	Number of bytes to be read is set in "Byte Count" register.
			ByteCount.Set((u16)(pio_buff.size<<1));
			//(2)	IO bit is set and CoD bit is cleared.
			IntReason.IO=1;
			IntReason.CoD=0;
			//(3)	DRQ bit is set, BSY bit is cleared.
			GDStatus.DRQ=1;
			GDStatus.BSY=0;
			//(4)	INTRQ is set, and a host interrupt is issued.
			asic_RaiseInterrupt(holly_GDROM_CMD);
			// Byte count is normally the register value at command time; it can also be a buffered total.
			break;

		case gds_readsector_pio:
			{
				// More data to send: set BSY and repeat the sequence from step 7.
				GDStatus.BSY=1;

				u32 sector_count = read_params.remaining_sectors;
				gd_states next_state=gds_pio_end;

				if (sector_count>27)
				{
					sector_count=27;
					next_state=gds_readsector_pio;
				}
				else if (get_gd_status_preset()>=2)
					cdda_transition(GD_PAUSE,67,GD_PLAY);   // P21: PLAY for 67 ms after a data read
				gd_read(GDR_PIO,(u8*)&pio_buff.data[0],read_params.start_sector,sector_count,
											read_params.sector_type);
				read_params.start_sector+=sector_count;
				read_params.remaining_sectors-=sector_count;

				gd_spi_pio_end(0,sector_count*read_params.sector_type,next_state);
			}
			break;

		case gds_readsector_dma:
			FillReadBuffer();
			break;

		case gds_pio_end:

			GDStatus.DRQ=0;//all data is sent !

			gd_set_state(gds_procpacketdone);
			break;

		case gds_procpacketdone:
			// Step 7: write the final status (IO, CoD, DRDY set; BSY, DRQ clear), then raise INTRQ.
			//Set IO, CoD, DRDY
			GDStatus.DRDY=1;
			IntReason.CoD=1;
			IntReason.IO=1;

			//Clear DRQ,BSY
			GDStatus.DRQ=0;
			GDStatus.BSY=0;
			//Make INTRQ valid
			asic_RaiseInterrupt(holly_GDROM_CMD);

			//command finished !
			gd_set_state(gds_waitcmd);
			break;

		case gds_process_set_mode:
			// P48: REQ_MODE reads at the byte offset, so the write goes there too; the *2 is from Reicast's u16 table.
			memcpy(&gd_data_0x11[get_gd_set_mode_preset() ? set_mode_offset : set_mode_offset*2],pio_buff.data ,pio_buff.size<<1);
			//end pio transfer ;)
			gd_set_state(gds_pio_end);
			break;
		default :
			die("Unhandled gdrom state ...");
			break;
	}
}


// With no disc, a real drive reports BUSY while it spins up after power-on.
// The BIOS plays the intro only if its first drive check sees a status below 6.
#define GD_SPINUP_SECONDS 3
static s64 gd_spinup_cycles = 0;

void gd_setdisc()
{
	gd_trans_next=-1;   // P21: a pending status change does not outlive the disc state
	// P61 (Flycast): a new disc state stops CD-DA.
	if (get_cdda_rules_preset())
	{
		cdda_set(CDDA_NOINFO);
		gda_cdda_next(false,0);
	}
	gd_disk_type = (DiscType)libGDR_GetDiscType();

	switch(gd_disk_type)
	{
	case NoDisk:
		SecNumber.Status = (gd_spinup_cycles > 0) ? GD_BUSY : GD_NODISC;
		//GDStatus.BSY=0;
		//GDStatus.DRDY=1;
		break;
	case Open:
		SecNumber.Status = GD_OPEN;
		//GDStatus.BSY=0;
		//GDStatus.DRDY=1;
		break;
	case Busy:
		SecNumber.Status = GD_BUSY;
		//GDStatus.BSY=1;
		//GDStatus.DRDY=0;
		break;
	default :
		// PAUSE (disc ready) for any disc: the BIOS needs it to issue SPI_CD_READ;
		// with STANDBY it loops without booting.
		SecNumber.Status = GD_PAUSE;
		//GDStatus.BSY=0;
		//GDStatus.DRDY=1;
		break;
	}
	// P34: the busy second of a disc swap is not over.
	if (gd_swap_cycles>0)
		SecNumber.Status = GD_BUSY;

	SecNumber.DiscFormat=gd_disk_type>>4;
}
void gd_reset()
{
	gda_wait();   // P3: a fill in flight lands before the drive resets
	gd_seek_wait=0;
	gd_head_fad=0xFFFFFFFF;
	//Reset the drive
	gd_setdisc();
	gd_set_state(gds_waitcmd);
}
u32 GetFAD(u8* data,bool msf)
{
	if( msf )
	{
		if (get_debug_gdrom()) printf("GDROM: MSF FORMAT\n");
		return ((data[0]*60*75) + (data[1]*75) + (data[2]));
	}
	else
	{
		return (data[0]<<16) | (data[1]<<8) | (data[2]);
	}
}
//disk changes ect
void NotifyEvent_gdrom(u32 info,void* param)
{
	if (info == DiskChange)
		gd_setdisc();
}

//This handles the work of setting up the pio regs/state :)
void gd_spi_pio_end(u8* buffer,u32 len,gd_states next_state)
{
	verify(len<0xFFFF);
	pio_buff.index=0;
	pio_buff.size=len>>1;
	pio_buff.next_state=next_state;
	if (buffer!=0)
		memcpy(pio_buff.data,buffer,len);
	if (len==0)
		gd_set_state(next_state);
	else
		gd_set_state(gds_pio_send_data);
}
void gd_spi_pio_read_end(u32 len,gd_states next_state)
{
	verify(len<0xFFFF);
	pio_buff.index=0;
	pio_buff.size=len>>1;
	pio_buff.next_state=next_state;
	if (len==0)
		gd_set_state(next_state);
	else
		gd_set_state(gds_pio_get_data);
}
void gd_process_ata_cmd()
{
	//Any ata cmd clears these bits , unless aborted/error :p
	Error.ABRT=0;
	// P34 (Flycast): a sense key other than none, aborted or unit attention keeps CHECK set.
	GDStatus.CHECK=(sns_key==0 || sns_key==0xB || sns_key==6) ? 0 : 1;

	switch(ata_cmd.command)
	{
	case ATA_NOP:
		printf_ata("ATA_NOP\n");
		// Set ABRT and the status error bit, clear BSY, assert INTRQ.

		Error.ABRT=1;
		if (gd_swapped)
			Error.Sense=sns_key;
		GDStatus.BSY=0;
		GDStatus.CHECK=1;
		// P34 (Flycast cb33387ed): a NOP also ends a data phase (Sentimental Graffiti 2 disc switch).
		if (get_disc_swap_preset())
			GDStatus.DRQ=0;

		asic_RaiseInterrupt(holly_GDROM_CMD);
		gd_set_state(gds_waitcmd);
		break;

	case ATA_SOFT_RESET:
		{
			printf_ata("ATA_SOFT_RESET\n");
			//DRV -> preserved -> wtf is it anyway ?
			gd_reset();
			sns_key=0;
			// Holly Regs FLYCAST (P68, Flycast 959b6344b): the values DC Checker reads after a soft reset.
			if (get_holly_regs_preset())
			{
				GDStatus.full = 0;
				Error.full = 1;
				SecNumber.Status = GD_PAUSE;
				IntReason.full = 1;
				ByteCount.low = 0x14;
				ByteCount.hi = 0xEB;
			}
		}
		break;

	case ATA_EXEC_DIAG:
		printf_ata("ATA_EXEC_DIAG\n");
		// P22: Flycast completes it. With BSY left set and no interrupt, the guest waits for ever.
		if (get_gd_cmd_fix_preset())
		{
			Error.full=1;	// no error
			sns_key=0;
			GDStatus.BSY=0;
			GDStatus.CHECK=1;
			asic_RaiseInterrupt(holly_GDROM_CMD);
			gd_set_state(gds_waitcmd);
		}
		else if (get_debug_gdrom()) printf("ATA_EXEC_DIAG -- not implemented\n");
		break;

	case ATA_SPI_PACKET:
		printf_ata("ATA_SPI_PACKET\n");
		gd_set_state(gds_waitpacket);
		break;

	// IDENTIFY PACKET DEVICE (0xA1): drive id block over PIO (NullDC PSP port).
	// It was a no-op with no completion IRQ, so a guest waiting on it hung.
	case ATA_IDENTIFY_DEV:
		printf_ata("ATA_IDENTIFY_DEV\n");
		GDStatus.BSY = 0;
		// P22: the whole 0x50-byte block, as Flycast; the packet bytes here are left from an older SPI command.
		if (get_gd_cmd_fix_preset())
			gd_spi_pio_end(&reply_a1[0], 0x50);
		else
			gd_spi_pio_end(&reply_a1[packet_cmd.data_8[2] & ~1], packet_cmd.data_8[4]);
		break;

	// IDENTIFY DEVICE (0xEC) on ATAPI aborts and leaves the ATAPI signature (SecCount=1,
	// SecNumber=1, ByteCount=0xEB14) with ABRT+CHECK; it used to hit die() (NullDC PSP port).
	case ATA_IDENTIFY:
		printf_ata("ATA_IDENTIFY\n");
		// Set the ATAPI signature
		DriveSel &= 0xf0;

		SecCount.full  = 1;
		SecNumber.full = 1;
		ByteCount.low  = 0x14;
		ByteCount.hi   = 0xeb;

		// Command aborted — this is the correct ATAPI response to 0xEC
		Error.full = 0x4;

		GDStatus.full  = 0;
		GDStatus.DRDY  = 1;
		GDStatus.CHECK = 1;

		asic_RaiseInterrupt(holly_GDROM_CMD);
		gd_set_state(gds_waitcmd);
		break;

	case ATA_SET_FEATURES:
		printf_ata("ATA_SET_FEATURES\n");

		//Set features sets :
		//Error : ABRT
		Error.ABRT=0;	//command was not aborted ;) [hopefully ...]

		//status : DRDY , DSC , DF , CHECK
		//DRDY is set on state change
		GDStatus.DSC=0;
		GDStatus.DF=0;
		GDStatus.CHECK=0;
		asic_RaiseInterrupt(holly_GDROM_CMD);  //???
		gd_set_state(gds_waitcmd);
		break;

	default:
		// P22: Flycast aborts an unknown ATA command with an interrupt; die() never returns.
		if (get_gd_cmd_fix_preset())
		{
			printf("GDROM: unknown ATA command %02X, aborted\n",ata_cmd.command);
			Error.ABRT=1;
			Error.Sense=5;	// illegal request
			GDStatus.BSY=0;
			GDStatus.CHECK=1;
			asic_RaiseInterrupt(holly_GDROM_CMD);
			gd_set_state(gds_waitcmd);
		}
		else
			die("Unkown ATA command..");
		break;
	};
}

// P47: the CD-DA position while it plays or is paused, else the last sector a data read took (Flycast).
static u32 gd_cur_fad()
{
	return (cdda.status==CDDA_PLAYING || cdda.status==CDDA_PAUSED) ? cdda.CurrAddr.FAD : read_params.start_sector-1;
}

static u8 gd_ctrl_adr(u32 track)
{
	u8 adr,ctrl;
	libGDR_GetTrackAdrAndControl(track,&adr,&ctrl);
	return (u8)((ctrl<<4)|adr);
}

// GET_SCD byte 1. P21 step 2 and P61 take it from the CD-DA state, as Flycast; else it follows the drive status.
static u8 gd_audio_status()
{
	if (get_gd_status_preset()>=2 || get_cdda_rules_preset())
	{
		switch (cdda.status)
		{
		case CDDA_PLAYING:    return 0x11;
		case CDDA_PAUSED:     return 0x12;
		case CDDA_TERMINATED: return 0x13;
		default:              return 0x15;
		}
	}
	if (SecNumber.Status==GD_STANDBY)
		return 0x13;	// audio playback ended normally
	if (SecNumber.Status==GD_PAUSE)
		return 0x15;	// no audio status (data disc, ready to read)
	if (SecNumber.Status==GD_PLAY)
		return 0x11;	// audio playback in progress
	return cdda.playing ? 0x11 : 0x15;
}

void gd_process_spi_cmd()
{
	printf_spi("SPI cmd %02x;",packet_cmd.data_8[0]);
	printf_spi("params: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x \n",
		packet_cmd.data_8[0], packet_cmd.data_8[1], packet_cmd.data_8[2], packet_cmd.data_8[3], packet_cmd.data_8[4], packet_cmd.data_8[5],
		packet_cmd.data_8[6], packet_cmd.data_8[7], packet_cmd.data_8[8], packet_cmd.data_8[9], packet_cmd.data_8[10], packet_cmd.data_8[11] );

	// P34 (Flycast): CHECK stays set while a sense key waits for REQ_ERROR.
	GDStatus.CHECK=(sns_key==0 || sns_key==0xB) ? 0 : 1;

	switch(packet_cmd.data_8[0])
	{
	case SPI_TEST_UNIT	:
    if(get_debug_loop() == 1){
		  printf_spicmd("SPI_TEST_UNIT\n");
    }

		GDStatus.CHECK=SecNumber.Status==GD_BUSY;	//drive is ready ;)

		gd_set_state(gds_procpacketdone);
		break;

	case SPI_REQ_MODE:
		printf_spicmd("SPI_REQ_MODE\n");
		gd_spi_pio_end((u8*)&gd_data_0x11[packet_cmd.data_8[2]&~1],packet_cmd.data_8[4]);
		break;

	// Sector size comes from the data-select flags, not a fixed 2048.
	case SPI_CD_READ:
	case SPI_CD_READ2:
		{
#define readcmd packet_cmd.GDReadBlock

			// head+subh+data with expdtype 3 = header + subheader + data (2340 bytes, Mode2/CDI).
			// A fixed 2048 gives misframed data and the game stops drawing (NullDC PSP port).
			u32 sector_type=2048;
			if (readcmd.head==1 && readcmd.subh==1 && readcmd.data==1 &&
			    readcmd.expdtype==3 && readcmd.other==0)
			{
				sector_type=2340;
			}
			else if (readcmd.other==1 || readcmd.expdtype==1)
			{
				// Flycast e94b26f1d: expected data type CD-DA reads the raw 2352-byte sector.
				sector_type=2352;
			}
			else if (get_debug_gdrom() && (readcmd.head || readcmd.subh || readcmd.other || (!readcmd.data)))
			{
				printf("GDROM: unsupported CD_READ flags head=%u subh=%u data=%u "
				       "other=%u expdtype=%u - falling back to 2048-byte sectors\n",
				       (u32)readcmd.head, (u32)readcmd.subh, (u32)readcmd.data,
				       (u32)readcmd.other, (u32)readcmd.expdtype);
			}

			u32 start_sector = GetFAD(&readcmd.b[2],readcmd.prmtype);

			u32 sector_count = (readcmd.b[8]<<16) | (readcmd.b[9]<<8) | (readcmd.b[10]);
			// P22 (Flycast): CD_READ2 has its length in bytes 6-7.
			if (packet_cmd.data_8[0]==SPI_CD_READ2 && get_gd_cmd_fix_preset())
				sector_count=(readcmd.b[6]<<8) | readcmd.b[7];

			// P21: a data read stops CD-DA (Hydro Thunder, NFL Blitz 2001). Step 2: the status is PLAY while it runs.
			if (get_gd_status_preset())
			{
				cdda_set(CDDA_NOINFO);
				gda_cdda_next(false,0);
				gd_trans_next=-1;
				if (get_gd_status_preset()>=2)
					SecNumber.Status=GD_PLAY;
				else if (SecNumber.Status==GD_PLAY)
					SecNumber.Status=GD_PAUSE;
			}

			gda_wait();   // P3: a fill still in flight (an aborted read) lands before read_buff is armed again
			// GD-ROM Timing SEEK: the head moves unless the read goes on from the last one. DMA reads only.
			gd_seek_wait=0;
			const int timing=get_gd_timing_preset();
			if (timing>=2 && Features.CDRead.DMA==1 && start_sector!=gd_head_fad)
			{
				const u32 seek_ms=timing==2 ? 100 : 330;
				gd_seek_wait=(s64)(SH4_CLOCK_EFF/1000)*seek_ms;
				gda_count(GDR_SEEK,0,ps3_timebase_hz/1000*seek_ms);
			}
			gd_head_fad=start_sector+sector_count;
			read_params.start_sector=start_sector;
			read_params.remaining_sectors=sector_count;
			read_params.sector_type = sector_type;//yeah i know , not realy many types supported ...
      if (get_debug_gdrom() && get_debug_loop()) {
			  printf_spicmd("SPI_CD_READ sec=%d sz=%d/%d dma=%d\n",read_params.start_sector,read_params.remaining_sectors,read_params.sector_type,Features.CDRead.DMA);
      }
			if (get_debug_gdrom()) printf("[GDR] SPI_CD_READ: FAD=%d count=%d dma=%d\n",read_params.start_sector,read_params.remaining_sectors,Features.CDRead.DMA);
			if (Features.CDRead.DMA==1)
			{
				gd_set_state(gds_readsector_dma);
			}
			else
			{
				gd_set_state(gds_readsector_pio);
			}
		}
		break;

	case SPI_GET_TOC:
		{
			printf_spicmd("SPI_GET_TOC\n");
			//printf("SPI_GET_TOC - %d\n",(packet_cmd.data_8[4]) | (packet_cmd.data_8[3]<<8) );
			u32 toc_gd[102];

			//toc - dd/sd
			libGDR_GetToc(&toc_gd[0],packet_cmd.data_8[1]&0x1);

			u32 toc_len=(packet_cmd.data_8[4]) | (packet_cmd.data_8[3]<<8);
			// P22 (Flycast): no more than the TOC holds.
			if (get_gd_cmd_fix_preset() && toc_len>sizeof(toc_gd))
				toc_len=sizeof(toc_gd);
			gd_spi_pio_end((u8*)&toc_gd[0],toc_len);
		}
		break;

		//mount/map drive ? some kind of reset/unlock ??
		//seems like a non data command :)
	case 0x70:
		printf_spicmd("SPI : unkown ? [0x70]\n");
		/*GDStatus.full=0x50; //FIXME
		RaiseInterrupt(holly_GDROM_CMD);*/

		gd_set_state(gds_procpacketdone);
		break;
	case 0x71:
		{
			printf_spicmd("SPI : unkown ? [0x71]\n");
			if (get_debug_gdrom()) printf("SPI : unkown ? [0x71]\n");

			gd_spi_pio_end((u8*)&gd_data_0x71[0],gd_data_0x71_len);//uCount

			// Rez reads the status after this vendor command to gate its streamer; a stale value stalls it.
			// As in the PSP port: GdRom/CdRom_XA -> PAUSE, else STANDBY.
			SecNumber.Status = ((gd_disk_type == GdRom) || (gd_disk_type == CdRom_XA))
			                 ? GD_PAUSE : GD_STANDBY;
		}
		break;
	case SPI_SET_MODE:
		{
			printf_spicmd("SPI_SET_MODE\n");
			u32 Offset = packet_cmd.data_8[2];
			u32 Count = packet_cmd.data_8[4];
			// P22 (Flycast): a longer write stops at the 10 writable bytes; the verify hangs on it.
			if (get_gd_cmd_fix_preset())
				Count=Offset<10 ? min(Count,10-Offset) : 0;
			else
				verify((Offset+Count)<11);	//cant set write olny things :P
			set_mode_offset=Offset;
			gd_spi_pio_read_end(Count,gds_process_set_mode);
		}

		break;

	// SPI_CD_READ2 is handled with SPI_CD_READ above; here it returned no data.

	case SPI_REQ_STAT:
		{
			printf_spicmd("SPI_REQ_STAT\n");
			if (get_debug_gdrom()) printf("GDROM: Unhandled Sega SPI frame: SPI_REQ_STAT\n");
			u8 stat[10];

			//0	0	0	0	0	STATUS
			stat[0]=SecNumber.Status;	//low nibble
			//1	Disc Format	Repeat Count
			stat[1]=(u8)(SecNumber.DiscFormat<<4) | (cdda.repeats);
			//2	Address	Control
			stat[2]=0x4;
			//3	TNO
			stat[3]=1;  // Track 1 (data track)
			//4	X
			stat[4]=0;
			//5	FAD
			stat[5]=cdda.CurrAddr.B0;
			//6	FAD
			stat[6]=cdda.CurrAddr.B1;
			//7	FAD
			stat[7]=cdda.CurrAddr.B2;
			//8	Max Read Error Retry Times
			stat[8]=0;
			//9	0	0	0	0	0	0	0	0
			stat[9]=0;
			// P47 (Flycast e5344d37d): the FAD by shifts, the track and control/ADR from the TOC, index 1. The
			// union bytes above are FAD >> 8 on a big-endian host.
			if (get_gd_position_preset())
			{
				u32 elapsed;
				const u32 fad=gd_cur_fad();
				const u32 track=libGDR_GetTrackNumber(fad,&elapsed);
				stat[2]=gd_ctrl_adr(track);
				stat[3]=(u8)track;
				stat[4]=1;
				stat[5]=(u8)(fad>>16);
				stat[6]=(u8)(fad>>8);
				stat[7]=(u8)fad;
			}


			verify((packet_cmd.data_8[2]+packet_cmd.data_8[4])<11);
			gd_spi_pio_end(&stat[packet_cmd.data_8[2]],packet_cmd.data_8[4]);
		}
		break;

	case SPI_REQ_ERROR:
		printf_spicmd("SPI_REQ_ERROR\n");
		if (get_debug_gdrom()) printf("GDROM: Unhandled Sega SPI frame: SPI_REQ_ERROR\n");

		u8 resp[10];
		resp[0]=0xF0;
		resp[1]=0;
		resp[2]= SecNumber.Status==GD_BUSY ? 2:0;//sense
		resp[3]=0;
		resp[4]=resp[5]=resp[6]=resp[7]=0; //Command Specific Information
		resp[8]=0;//Additional Sense Code
		resp[9]=0;//Additional Sense Code Qualifier
		// P34 (Flycast): after a disc swap the reply is the sense the swap left, and reading it clears it.
		if (gd_swapped)
		{
			resp[2]=(u8)sns_key;
			resp[8]=(u8)sns_asc;
			resp[9]=(u8)sns_ascq;
		}

		gd_spi_pio_end(resp,packet_cmd.data_8[4]);
		if (gd_swapped)
		{
			sns_key=sns_asc=sns_ascq=0;
			GDStatus.CHECK=0;
		}
		break;

	case SPI_REQ_SES:
		printf_spicmd("SPI_REQ_SES\n");

		u8 ses_inf[6];
		libGDR_GetSessionInfo(ses_inf,packet_cmd.data_8[2]);
		ses_inf[0]=SecNumber.Status;
		if (get_debug_gdrom()) {
			printf("[GDR] SPI_REQ_SES: ses=%u status=0x%02X disc_fmt=%u cnt=%u resp=[%02X %02X %02X %02X %02X %02X]\n",
			       (unsigned int)packet_cmd.data_8[2], (unsigned int)SecNumber.Status, (unsigned int)SecNumber.DiscFormat,
			       (unsigned int)ses_inf[3],
			       (unsigned int)ses_inf[0],(unsigned int)ses_inf[1],(unsigned int)ses_inf[2],
			       (unsigned int)ses_inf[3],(unsigned int)ses_inf[4],(unsigned int)ses_inf[5]);
		}
		gd_spi_pio_end((u8*)&ses_inf[0], 6);  // always send full 6-byte session response
		break;

	case SPI_CD_OPEN:
		printf_spicmd("SPI_CD_OPEN\n");
		if (get_debug_gdrom()) printf("GDROM: Unhandled Sega SPI frame: SPI_CD_OPEN\n");


		gd_set_state(gds_procpacketdone);
		break;

	case SPI_CD_PLAY:
		{
			printf_spicmd("SPI_CD_PLAY\n");
			if (get_debug_gdrom()) printf("GDROM: Unhandled Sega SPI frame: SPI_CD_PLAY\n");
			//cdda.CurrAddr.FAD=60000;

			u32 param_type=packet_cmd.data_8[1]&0x7;
			const bool rules=get_cdda_rules_preset()!=0, timing=get_gd_status_preset()>=2;
			bool play=true;      // old: every form starts the audio at once
			int next=GD_PLAY;
			if (get_debug_gdrom()) printf("param_type=%d\n",param_type);
			if (param_type==1 || param_type==2)
			{
				const bool msf=param_type==2;
				cdda.StartAddr.FAD=cdda.CurrAddr.FAD=GetFAD(&packet_cmd.data_8[2],msf);
				cdda.EndAddr.FAD=GetFAD(&packet_cmd.data_8[8],msf);
				// P61 (Flycast): an end of 0 is the end of the disc, from REQ_SES session 0.
				if (rules && cdda.EndAddr.FAD==0)
				{
					u8 ses_inf[6]={0};
					libGDR_GetSessionInfo(ses_inf,0);
					cdda.EndAddr.FAD=(ses_inf[3]<<16) | (ses_inf[4]<<8) | ses_inf[5];
				}
				if (!timing)
					GDStatus.DSC=1;	//we did the seek xD lol
			}
			else if (param_type==7)
			{
				//Resume from previus pos :)
				// P21 step 2, P61 (Flycast): only a paused play resumes; past its end it stops.
				if (rules || timing)
				{
					play=cdda.status==CDDA_PAUSED;
					if (play && cdda.CurrAddr.FAD>cdda.EndAddr.FAD)
						next=GD_STANDBY;
				}
			}
			else
			{
				die("SPI_CD_SEEK  : not known parameter..");
			}
			// P61: only the two start forms carry a repeat count.
			if (!rules || param_type!=7)
				cdda.repeats=packet_cmd.data_8[6]&0xF;
			if (play)
			{
				// P21 step 2 (Flycast dev): CD-DA starts 5 ms after the command; a resume shows BUSY for 20 ms.
				if (timing && param_type==7)
					cdda_transition(next,20,GD_BUSY);
				else if (timing)
					cdda_transition(GD_PLAY,5,SecNumber.Status);
				else
				{
					cdda_set(next==GD_PLAY ? CDDA_PLAYING : CDDA_TERMINATED);
					SecNumber.Status=next;
				}
				// CD-DA Miss: the worker reads the start of the play now, not after the first miss.
				if (next==GD_PLAY && get_cdda_miss_preset())
					gda_cdda_seek(cdda.CurrAddr.FAD);
			}
			if (get_debug_gdrom()) printf("cdda.StartAddr=%d\n",cdda.StartAddr.FAD);
			if (get_debug_gdrom()) printf("cdda.EndAddr=%d\n",cdda.EndAddr.FAD);
			if (get_debug_gdrom()) printf("cdda.repeats=%d\n",cdda.repeats);
			if (get_debug_gdrom()) printf("cdda.playing=%d\n",cdda.playing);
			if (get_debug_gdrom()) printf("cdda.CurrAddr=%d\n",cdda.CurrAddr.FAD);

			gd_set_state(gds_procpacketdone);
		}
		break;

	case SPI_CD_SEEK:
		{
			printf_spicmd("SPI_CD_SEEK\n");
			if (get_debug_gdrom()) printf("GDROM: Unhandled Sega SPI frame: SPI_CD_SEEK\n");

			// P21 step 2 (Flycast dev): SEEK with DSC clear for 20 ms, then PAUSE; a stop is BUSY for 5 ms, then
			// STANDBY. The play state changes when the time is up.
			const bool timing=get_gd_status_preset()>=2;
			if (!timing)
			{
				SecNumber.Status=GD_PAUSE;
				// As Flycast master: a seek pauses a play; any other CD-DA state stays.
				if (cdda.status==CDDA_PLAYING)
					cdda_set(CDDA_PAUSED);
			}

			u32 param_type=packet_cmd.data_8[1]&0x7;
			if (get_debug_gdrom()) printf("param_type=%d\n",param_type);
			if (param_type==1 || param_type==2)
			{
				cdda.StartAddr.FAD=cdda.CurrAddr.FAD=GetFAD(&packet_cmd.data_8[2],param_type==2);
				GDStatus.DSC=timing ? 0 : 1;
				if (timing)
					cdda_transition(GD_PAUSE,20,GD_SEEK);
			}
			else if (param_type==3)
			{
				//stop audio , goto home
				cdda.StartAddr.FAD=cdda.CurrAddr.FAD=150;
				GDStatus.DSC=timing ? 0 : 1;
				if (timing)
					cdda_transition(GD_STANDBY,5,GD_BUSY);
				else
				{
					SecNumber.Status=GD_STANDBY;
					cdda_set(CDDA_NOINFO);
				}
			}
			else if (param_type==4)
			{
				//pause audio -- nothing more (nullDC-v1, Reicast, Flycast): a resume continues from here
				if (timing)
				{
					SecNumber.Status=GD_PAUSE;
					cdda_set(CDDA_PAUSED);
					gd_trans_next=-1;
				}
			}
			else
			{
				die("SPI_CD_SEEK  : not known parameter..");
			}

			if (get_debug_gdrom()) printf("cdda.StartAddr=%d\n",cdda.StartAddr.FAD);
			if (get_debug_gdrom()) printf("cdda.EndAddr=%d\n",cdda.EndAddr.FAD);
			if (get_debug_gdrom()) printf("cdda.repeats=%d\n",cdda.repeats);
			if (get_debug_gdrom()) printf("cdda.playing=%d\n",cdda.playing);
			if (get_debug_gdrom()) printf("cdda.CurrAddr=%d\n",cdda.CurrAddr.FAD);
			if (get_debug_gdrom()) printf("[GDR] SPI_CD_SEEK done: Status=0x%02X (%s) DSC=%d\n",SecNumber.Status,SecNumber.Status==GD_STANDBY?"STANDBY":SecNumber.Status==GD_PAUSE?"PAUSE":SecNumber.Status==GD_PLAY?"PLAY":"?",GDStatus.DSC);


			gd_set_state(gds_procpacketdone);
		}
		break;

	case SPI_CD_SCAN:
		printf_spicmd("SPI_CD_SCAN\n");
		if (get_debug_gdrom()) printf("GDROM: Unhandled Sega SPI frame: SPI_CD_SCAN\n");


		gd_set_state(gds_procpacketdone);
		break;

	case SPI_GET_SCD:
		{
			u32 format;
			format=packet_cmd.data_8[1]&0xF;
			u32 alloc_len = (packet_cmd.data_8[3]<<8)|packet_cmd.data_8[4];
			if (get_debug_loop()) printf("[GDR] SPI_GET_SCD fmt=%u alloc=%u status=0x%02X subc_info[1] will be 0x%02X\n",
				format, alloc_len, (u32)SecNumber.Status, (u32)gd_audio_status());
			u32 sz;
			u8 subc_info[100];
		memset(subc_info, 0, 100);  // zero all; BIOS requests 100 bytes, we fill 18, rest must be 0


			//0	Reserved
			subc_info[0]=0;
			//1	Audio status
			subc_info[1]=gd_audio_status();

			if (format==0)
			{
				// Format 0: current position, 4-byte header + 14 bytes of data.
				// Times are BCD MSF (minutes, seconds, frames at 75 fps).
				u32 absFAD = cdda.CurrAddr.FAD ? cdda.CurrAddr.FAD : 150;
				u32 relFAD = (absFAD >= 150) ? (absFAD - 150) : 0;
				u8 ctrl_adr=0x14;  // CTRL=4 data track (low nibble), ADR=1 current position (high nibble)
				u8 tno=0x01;       // TNO = track 1
				// P47 level 2 (Flycast): the position REQ_STAT sends, its track and control/ADR, the time inside
				// that track.
				if (get_gd_position_preset()>=2)
				{
					absFAD=gd_cur_fad();
					const u32 track=libGDR_GetTrackNumber(absFAD,&relFAD);
					ctrl_adr=gd_ctrl_adr(track);
					tno=(u8)(track<100 ? ((track/10)<<4)|(track%10) : track);
				}
				// Convert to MSF (BCD)
				u32 absFrames = absFAD % 75;
				u32 absSec    = (absFAD / 75) % 60;
				u32 absMin    = (absFAD / 75) / 60;
				u32 relFrames = relFAD % 75;
				u32 relSec    = (relFAD / 75) % 60;
				u32 relMin    = (relFAD / 75) / 60;
				#define TOBCD(x) ((((x)/10)<<4)|((x)%10))
				subc_info[2]=0x00;
				subc_info[3]=0x0E;  // data length = 14 bytes
				subc_info[4]=ctrl_adr;
				subc_info[5]=tno;
				subc_info[6]=0x01;  // Index = 1
				subc_info[7]=(u8)TOBCD(relMin);    // Relative Min (BCD)
				subc_info[8]=(u8)TOBCD(relSec);    // Relative Sec (BCD)
				subc_info[9]=(u8)TOBCD(relFrames); // Relative Frame (BCD)
				subc_info[10]=0x00;
				subc_info[11]=(u8)TOBCD(absMin);    // Absolute Min (BCD)
				subc_info[12]=(u8)TOBCD(absSec);    // Absolute Sec (BCD)
				subc_info[13]=(u8)TOBCD(absFrames); // Absolute Frame (BCD)
				#undef TOBCD
				sz=alloc_len;  // send exactly what BIOS requested (100 bytes); bytes 18+ are zero-padded
				// P62 (Flycast 959b6344b): no more than the buffer holds.
				if (get_gd_scd_q_preset() && sz>sizeof(subc_info))
					sz=sizeof(subc_info);
				if (get_debug_loop()) printf("[GDR] SPI_GET_SCD fmt=0: bytes[0..13]=%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X sz=%u\n",
					subc_info[0],subc_info[1],subc_info[2],subc_info[3],subc_info[4],subc_info[5],subc_info[6],
					subc_info[7],subc_info[8],subc_info[9],subc_info[10],subc_info[11],subc_info[12],subc_info[13],sz);
			}
			else if (get_gd_scd_q_preset())
			{
				// P62 (Flycast): 14 bytes. The Q data follows the length: control/ADR, track, index, the FADs
				// since the track start, 0, the FAD. The old layout has control/ADR twice and cuts the FAD.
				u32 elapsed;
				const u32 fad=gd_cur_fad();
				const u32 track=libGDR_GetTrackNumber(fad,&elapsed);
				subc_info[2]=0;
				subc_info[3]=0xE;
				subc_info[4]=gd_ctrl_adr(track);
				subc_info[5]=(u8)track;
				subc_info[6]=1;
				subc_info[7]=(u8)(elapsed>>16);
				subc_info[8]=(u8)(elapsed>>8);
				subc_info[9]=(u8)elapsed;
				subc_info[10]=0;
				subc_info[11]=(u8)(fad>>16);
				subc_info[12]=(u8)(fad>>8);
				subc_info[13]=(u8)fad;
				sz=min((u32)14,alloc_len);
			}
			else
			{
				//2	DATA Length MSB (0 = 0h)
				subc_info[2]=0;
				//3	DATA Length LSB (14 = Eh)
				subc_info[3]=0xE;
				//4	Control	ADR
				subc_info[4]=0x14;	//CTRL=4(data) in low nibble, ADR=1 in high nibble
				//5-13	DATA-Q
				u8* subc=&subc_info[5];
				// DATA-Q layout for ADR = 1; byte 0 = Control | ADR.
					subc[0]=0x14;//Control(data track)|ADR=1
				//1	TNO
					subc[1]=1;//Track number - track 1 (data track at FAD 150)
				//2	X
				subc[2]=1;//gap #1 (main track)
				//3-5	Elapsed FAD within track
				u32 FAD_el=cdda.CurrAddr.FAD-cdda.StartAddr.FAD;
				subc[3]=(u8)(FAD_el>>16);
				subc[4]=(u8)(FAD_el>>8);
				subc[5]=(u8)(FAD_el>>0);
				//6	0	0	0	0	0	0	0	0
				subc[6]=0;//
				//7-9	???
					subc[7]=(u8)(cdda.CurrAddr.FAD>>16);  // abs FAD MSB
					subc[8]=(u8)(cdda.CurrAddr.FAD>>8);
					subc[9]=(u8)(cdda.CurrAddr.FAD>>0);  // abs FAD LSB
				//we'l see about em ;p
				if (get_debug_loop()) printf("[GDR] SPI_GET_SCD fmt=%u Status=0x%02X TNO=%u X=%u FAD_el=%u absFAD=%u\n",format,SecNumber.Status,subc[1],subc[2],FAD_el,cdda.CurrAddr.FAD);
				sz=12;
				//printf("NON raw subcode read -- partialy wrong\n");
			}

			gd_spi_pio_end((u8*)&subc_info[0],sz);
		}
		break;

	default:
		if (get_debug_gdrom()) printf("GDROM: Unhandled Sega SPI frame: %X\n", packet_cmd.data_8[0]);

		gd_set_state(gds_procpacketdone);
		break;
	}
}
//Read handler
u32 ReadMem_gdrom(u32 Addr, u32 sz)
{
	switch (Addr)
	{
	// DSC (bit 4) reads as set, but P21 step 2 shows the real bit: it is clear while a seek runs.
		//cancel interrupt
	case GD_STATUS_Read :
		asic_CancelInterrupt(holly_GDROM_CMD);	//Clear INTRQ signal
		// P22 (Flycast c3d954263, DreamShell): there is no slave drive.
		if ((DriveSel&0x10) && get_gd_cmd_fix_preset())
			return 0;
		printf_rm("GDROM: STATUS [cancel int](v=%X)\n",GDStatus.full);
		return GDStatus.full | (get_gd_status_preset()>=2 ? 0 : (1<<4));

	case GD_ALTSTAT_Read:
		printf_rm("GDROM: Read From AltStatus (v=%X)\n",GDStatus.full);
		return GDStatus.full | (get_gd_status_preset()>=2 ? 0 : (1<<4));

	case GD_BYCTLLO	:
		printf_rm("GDROM: Read From GD_BYCTLLO\n");
		return ByteCount.low;

	case GD_BYCTLHI	:
		printf_rm("GDROM: Read From GD_BYCTLHI\n");
		return ByteCount.hi;


	case GD_DATA:
		if(2!=sz)
			if (get_debug_gdrom()) printf("GDROM: Bad size on DATA REG Read\n");

		if (gd_state == gds_pio_send_data)
		{
			if (pio_buff.index==pio_buff.size)
			{ if (get_debug_gdrom()) printf("GDROM: Illegal Read From DATA (underflow)\n"); }
			else
			{
				u32 rv= HOST_TO_LE16(pio_buff.data[pio_buff.index]);
				pio_buff.index+=1;
				ByteCount.Set(ByteCount.Get()-2);
				if (pio_buff.index==pio_buff.size)
				{
					verify(pio_buff.next_state != gds_pio_send_data);
					//end of pio transfer !
					gd_set_state(pio_buff.next_state);
				}
				return rv;
			}

		}
		else
			if (get_debug_gdrom()) printf("GDROM: Illegal Read From DATA (wrong mode)\n");

		return 0;

	case GD_DRVSEL:
		printf_rm("GDROM: Read From DriveSel\n");
		return DriveSel;

	case GD_ERROR_Read:
		printf_rm("GDROM: Read from ERROR Register\n");
		if (gd_swapped)
			Error.Sense=sns_key;   // P34 (Flycast)
		return Error.full;

	case GD_IREASON_Read:
		printf_rm("GDROM: Read from INTREASON Register\n");
		return IntReason.full;

	case GD_SECTNUM:
		printf_rm("GDROM: Read from SecNumber Register (v=%X)\n", SecNumber.full);
		return SecNumber.full;

	default:
		if (get_debug_gdrom()) printf("GDROM: Unhandled Read From %X sz:%X\n",Addr,sz);
		return 0;
	}
}

//Write Handler
void WriteMem_gdrom(u32 Addr, u32 data, u32 sz)
{
	switch(Addr)
	{
	case GD_BYCTLLO:
		printf_rm("GDROM: Write to GD_BYCTLLO = %X sz:%X\n",data,sz);
		ByteCount.low =(u8) data;
		break;

	case GD_BYCTLHI:
		printf_rm("GDROM: Write to GD_BYCTLHI = %X sz:%X\n",data,sz);
		ByteCount.hi =(u8) data;
		break;

	case GD_DATA:
		{
			if(2!=sz)
				if (get_debug_gdrom()) printf("GDROM: Bad size on DATA REG\n");
			if (gd_state == gds_waitpacket)
			{
				packet_cmd.data_16[packet_cmd.index]=HOST_TO_LE16((u16)data);
				packet_cmd.index+=1;
				if (packet_cmd.index==6)
					gd_set_state(gds_procpacket);
			}
			else if (gd_state == gds_pio_get_data)
			{
				pio_buff.data[pio_buff.index]=HOST_TO_LE16((u16)data);
				pio_buff.index+=1;
				if (pio_buff.size==pio_buff.index)
				{
					verify(pio_buff.next_state!=gds_pio_get_data);
					gd_set_state(pio_buff.next_state);
				}
			}
			else
				if (get_debug_gdrom()) printf("GDROM: Illegal Write to DATA\n");
			return;
		}
	case GD_DEVCTRL_Write:
		if (get_debug_gdrom()) printf("GDROM: Write GD_DEVCTRL ( Not implemented on dc)\n");
		break;

	case GD_DRVSEL:
		if (get_debug_gdrom()) printf("GDROM: Write to GD_DRVSEL\n");
		// P22 (Flycast): only the low 5 bits take a write; bit 4 selects the slave drive.
		DriveSel = get_gd_cmd_fix_preset() ? (0xa0 | (data&0x1f)) : data;
		break;


		// Feature 3 + Set Features selects PIO or DMA; the mode is in the Sector Count register.

	case GD_FEATURES_Write:
		printf_rm("GDROM: Write to GD_FEATURES\n");
		Features.full =(u8) data;
		break;

	case GD_SECTCNT_Write:
		if (get_debug_gdrom()) printf("GDROM: Write to SecCount = %X\n", data);
		SecCount.full =(u8) data;
		break;

	case GD_SECTNUM	:
		if (get_debug_gdrom()) printf("GDROM: Write to SecNum; not possible = %X\n", data);
		break;

	case GD_COMMAND_Write:
		// P22 (Flycast): a write of the wrong size or to the slave drive does nothing; the verify hangs.
		if (get_gd_cmd_fix_preset())
		{
			if (sz!=1 || (DriveSel&0x10))
				break;
		}
		else
			verify(sz==1);
		if ((data !=ATA_NOP) && (data != ATA_SOFT_RESET))
			verify(gd_state==gds_waitcmd);
		//printf("\nGDROM:\tCOMMAND: %X !\n", data);
		ata_cmd.command=(u8)data;
		gd_set_state(gds_procata);
		break;

	default:
		if (get_debug_gdrom()) printf("\nGDROM:\tUnhandled Write to %X <= %X sz:%X\n",Addr,data,sz);
		break;
	}
}

// P22 (Flycast d0d971254): 0 in SB_GDLEN is the full 32 MB (Pro Pinball Trilogy).
static u32 gd_dma_total()
{
	return (SB_GDLEN==0 && get_gd_dma_fix_preset()) ? 0x02000000 : SB_GDLEN;
}

// GD-ROM Speed REAL (Flycast getGDROMTicks): 10 KB chunks at 1.8 MB/s, a last small chunk at 25 MB/s.
#define GD_DMA_CHUNK 10240
#define GD_DMA_NOW   448     // Flycast's SH4_TIMESLICE: a shorter wait moves the data inside the SB_GDST write
static u32 gd_dma_ticks()
{
	if (!(SB_GDST&1))
		return 0;
	const u32 left=gd_dma_total()-SB_GDLEND;
	if (left>GD_DMA_CHUNK)
		return (u32)((u64)SH4_CLOCK_EFF*GD_DMA_CHUNK/1800000);
	return (u32)((u64)SH4_CLOCK_EFF*left/25000000);
}

//is this needed ?
void UpdateGDRom()
{
	StallScope st_(ST_GDDMA);
	if(!(SB_GDST&1) || !(SB_GDEN &1))
		return;
	// GD-ROM Timing SEEK: the head is still on its way.
	if (gd_seek_wait>0)
		return;
	// P3: read_buff is the worker's until its fill lands; the DMA goes on at a later update. GD-ROM Timing FIXED:
	// the SH4 waits for it here, so the game never sees the disc's time.
	if (gda_busy())
	{
		if (!get_gd_timing_preset())
			return;
		gda_wait();
	}
	const bool real=get_gd_speed_preset()!=0, fix=get_gd_dma_fix_preset()!=0;
	// GD-ROM Speed REAL: the next chunk is not due.
	if (real && gd_dma_wait>0)
		return;
	// P9 (Flycast): a DMA with no read behind it waits for the next read. A disc swap also leaves one.
	if ((fix || gd_swapped) && read_buff.cache_size==read_buff.cache_index && read_params.remaining_sectors==0)
		return;

	//SB_GDST=0;

	//TODO : Fix dmaor
	u32 dmaor	= DMAC_DMAOR.full;

	u32	src		= SB_GDSTARD,
		len		= gd_dma_total()-SB_GDLEND ;

	len=min(len,(u32)(real ? GD_DMA_CHUNK : 32000));
	// do we need to do this for gdrom dma ?
	if(0x8201 != (dmaor &DMAOR_MASK)) {
		if (get_debug_gdrom()) printf("\n!\tGDROM: DMAOR has invalid settings (%X) !\n", dmaor);
		//return;
	}
	// With the fix a chunk may stop inside a 32-byte unit, so the register is tested, as Flycast does.
	if((fix ? SB_GDLEN : len) & 0x1F) {
		if (get_debug_gdrom()) printf("\n!\tGDROM: SB_GDLEN has invalid size (%X) !\n", len);
		return;
	}

	if(0 == len)
	{
		if (get_debug_gdrom()) printf("\n!\tGDROM: Len: %X, Abnormal Termination !\n", len);
	}
	u32 len_backup=len;
	if( 1 == SB_GDDIR )
	{
		while(len)
		{
			u32 buff_size =read_buff.cache_size - read_buff.cache_index;
			if (buff_size==0)
			{
				// P9 (Reicast, Flycast): a DMA longer than its read stops where the read ends; the verify hangs.
				if (fix && read_params.remaining_sectors==0)
					break;
				verify(read_params.remaining_sectors>0);
				//buffer is empty , fill it :)
				FillReadBuffer();
				// P3: the rest waits for the fill (FIXED: here). Only full 32-sector batches end early, so len stays
				// 32-aligned.
				if (gda_busy())
				{
					if (!get_gd_timing_preset())
						break;
					gda_wait();
				}
			}

			//transfer up to len bytes
			if (buff_size>len)
			{
				buff_size=len;
			}
			//WriteMemBlock_nommu_ptr(src,(u32*)&read_buff.cache[read_buff.cache_index], buff_size);
			//Can't use it due to endianess mess :d
			{
				u32* pdata=(u32*)&read_buff.cache[read_buff.cache_index];
				for (u32 i=0;i<buff_size;i+=4)
				{
					WriteMem32_nommu(src+i,HOST_TO_LE32(pdata[i>>2]));
				}
			}

			read_buff.cache_index+=buff_size;
			src+=buff_size;
			len-=buff_size;
		}
		rdv_DmaWrite(SB_GDSTARD,len_backup-len,JDMA_GDROM);
	}
	else
	{
		msgboxf("GDROM: SB_GDDIR %X (TO AICA WAVE MEM?)",MBX_ICONERROR, SB_GDDIR);
		len=0;   // counted as moved, as before
	}

	//SB_GDLEN = 0x00000000; //13/5/2k7 -> acording to docs these regs are not updated by hardware
	//SB_GDSTAR = (src + len_backup);

	// P3: only the bytes that moved; a fill in flight ends the transfer early.
	const u32 moved=len_backup-len;
	SB_GDLEND+= moved;
	// P22 (Flycast): the counter has bits 5-24, so the 32 MB of SB_GDLEN 0 end at 0.
	if (fix)
		SB_GDLEND&=0x01ffffe0;
	SB_GDSTARD+= moved;//(src + len_backup)&0x1FFFFFFF;

	if (SB_GDLEND==SB_GDLEN && (moved || !fix))
	{
		//printf("Streamed GDMA end - %d bytes trasnfered\n",SB_GDLEND);
		SB_GDST=0; // cleared now so UpdateGDRom re-entry cannot fire the end IRQ twice
		// SCHED preset: the end IRQ goes through the deadline queue; otherwise it is raised inline.
		if (get_sched_preset() && gdrom_dma_schid != -1)
			sh4_sched_request(gdrom_dma_schid, GDROM_DMA_IRQ_DELAY);
		else
			asic_RaiseInterrupt(holly_GDROM_DMA);
	}
	//Readed ALL sectors
	if (read_params.remaining_sectors==0)
	{
		u32 buff_size =read_buff.cache_size - read_buff.cache_index;
		//And all buffer :p
		if (buff_size==0)
		{
			// P9 (Flycast): the DMA may run on into the next read; the verify hangs on that.
			if (!fix && !gd_swapped)
			{
				verify(!(SB_GDST&1));
			}
			gd_set_state(gds_procpacketdone);
			// P21 step 2 (Flycast dev): PLAY for 67 ms after a data read, then PAUSE.
			if (get_gd_status_preset()>=2)
				cdda_transition(GD_PAUSE,67,GD_PLAY);
		}
	}
	if (real && moved)
		gd_dma_wait=gd_dma_ticks();
}
//Dma Start
void GDROM_DmaStart(u32 data)
{
	if (SB_GDEN==0)
	{
		if (get_debug_gdrom()) printf("Invalid GD-DMA start, SB_GDEN=0.Ingoring it.\n");
		return;
	}
	SB_GDST|=data&1;

	if (SB_GDST==1)
	{
		SB_GDSTARD=SB_GDSTAR;
		SB_GDLEND=0;
		//printf("Streamed GDMA start\n");
		// GD-ROM Speed REAL (Flycast): only a transfer shorter than a timeslice moves inside this write.
		gd_dma_wait=0;
		if (get_gd_speed_preset())
		{
			const u32 ticks=gd_dma_ticks();
			if (ticks>=GD_DMA_NOW)
			{
				gd_dma_wait=ticks;
				return;
			}
		}
		UpdateGDRom();
	}
}


void GDROM_DmaEnable(u32 data)
{
	SB_GDEN=data&1;
	if (SB_GDEN==0 && SB_GDST==1)
	{
		if (get_debug_gdrom()) printf("GD-DMA aborted\n");
		SB_GDST=0;
	}
}
//Init/Term/Res
void gdrom_reg_Init()
{
	sb_regs[(SB_GDST_addr-SB_BASE)>>2].flags=REG_32BIT_READWRITE | REG_READ_DATA;
	sb_regs[(SB_GDST_addr-SB_BASE)>>2].writeFunction=GDROM_DmaStart;

	sb_regs[(SB_GDEN_addr-SB_BASE)>>2].flags=REG_32BIT_READWRITE | REG_READ_DATA;
	sb_regs[(SB_GDEN_addr-SB_BASE)>>2].writeFunction=GDROM_DmaEnable;

	// SCHED preset: register the deferred GD-DMA end IRQ callback once. Safe to
	// register even when the preset is off — it is only ever armed under it.
	if (gdrom_dma_schid == -1)
		gdrom_dma_schid = sh4_sched_register(0, gdrom_dma_end_cb);
}
void gdrom_reg_Term()
{

}

void gdrom_reg_Reset(bool Manual)
{
	gda_wait();   // P3: as gd_reset
	gd_trans_next=-1;
	gd_dma_wait=0;
	gd_seek_wait=0;
	gd_head_fad=0xFFFFFFFF;
	gd_spinup_cycles = (s64)SH4_CLOCK_EFF * GD_SPINUP_SECONDS;
	if (gd_disk_type == NoDisk)
		SecNumber.Status = GD_BUSY;
}

// Every SlowUpdate: the spin-up, and the emulated-time waits of P21 step 2, GD-ROM Speed REAL and Timing, and P34.
void gdrom_spinup_tick(u32 cycles)
{
	if (gd_dma_wait>0)
		gd_dma_wait-=cycles;
	if (gd_seek_wait>0)
		gd_seek_wait-=cycles;

	// P21 step 2 (Flycast dev statusSched): the status and the CD-DA state a command asked for.
	if (gd_trans_next!=-1 && (gd_trans_cycles-=cycles)<=0)
	{
		GDStatus.DSC=1;
		if (gd_trans_next==GD_PAUSE && cdda.status==CDDA_PLAYING)
			cdda_set(CDDA_PAUSED);
		else if (gd_trans_next==GD_STANDBY)
			cdda_set(CDDA_TERMINATED);
		else if (gd_trans_next==GD_PLAY)
			cdda_set(CDDA_PLAYING);
		SecNumber.Status=gd_trans_next;
		gd_trans_next=-1;
	}

	// P34 (Flycast discSwapCallback): the busy second is over; unit attention, medium changed or none.
	if (gd_swap_cycles>0 && (gd_swap_cycles-=cycles)<=0)
	{
		sns_key=6;
		sns_asc=libGDR_GetDiscType()==NoDisk ? 0x29 : 0x28;
		sns_ascq=0;
		gd_setdisc();
		printf("[swap] drive ready: status %u, disc format %u\n",(u32)SecNumber.Status,(u32)SecNumber.DiscFormat);
	}

	if (gd_spinup_cycles <= 0)
		return;
	gd_spinup_cycles -= cycles;
	if (gd_spinup_cycles <= 0 && gd_disk_type == NoDisk && SecNumber.Status == GD_BUSY)
		SecNumber.Status = GD_NODISC;
}

// P34 (Flycast insertDisk): the next image opens, then the drive is BUSY with sense 2/04/01 for 1 s.
bool gdrom_disc_swap(const char* path)
{
	const bool ok=libGDR_SwapDisc(path);
	// Fourth review (Reicast c050ee8f): the old disc's command, buffers and CD-DA do not survive the change.
	memset(&read_params,0,sizeof(read_params));
	memset(&packet_cmd,0,sizeof(packet_cmd));
	set_mode_offset=0;
	read_buff.cache_index=read_buff.cache_size=0;
	pio_buff.index=pio_buff.size=0;
	ata_cmd.command=0;
	memset(&cdda,0,sizeof(cdda));
	cdda_held=0;
	gd_trans_next=-1;
	gd_dma_wait=0;
	gd_seek_wait=0;
	gd_head_fad=0xFFFFFFFF;

	gd_swapped=true;
	sns_key=2;
	sns_asc=4;
	sns_ascq=1;
	gd_swap_cycles=(s64)SH4_CLOCK_EFF;
	SecNumber.Status=GD_BUSY;
	printf("[swap] %s: %s\n",path,ok ? "open, drive busy for 1 s" : "no reader took it, no disc");
	return ok;
}
