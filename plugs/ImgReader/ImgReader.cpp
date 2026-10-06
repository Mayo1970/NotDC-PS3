// nullGDR.cpp : Defines the entry point for the DLL application.
//

#include "ImgReader.h"
#include <stdio.h>
#include <string.h>
#include "common.h"
#include "ps3/ps3_stall.h"
#include "ps3/ps3_gdasync.h"

extern "C" int get_debug_loop();
extern "C" int get_debug_gdrom();

void FASTCALL libGDR_ReadSubChannel(u8 * buff, u32 format, u32 len)
{
	//printf("libGDR_ReadSubChannel\n");
}

// Any thread (P3: the GD-ROM worker too): the drive's FILE and buffers are shared, so the read holds the disc lock.
void libGDR_ReadSectorLocked(u8 * buff,u32 StartSector,u32 SectorCount,u32 secsz)
{
	gda_lock();
	// Flycast e94b26f1d: a FAD no track covers reads as zeros, not stale buffer bytes (NBA Hoopz voices).
	memset(buff, 0, SectorCount * secsz);
	if (CurrDrive)
		CurrDrive->ReadSector(buff,StartSector,SectorCount,secsz);
	gda_unlock();
}

void FASTCALL libGDR_ReadSector(u8 * buff,u32 StartSector,u32 SectorCount,u32 secsz)
{
	StallScope st_(ST_GDREAD);
  if (get_debug_gdrom() && get_debug_loop()) {
	  printf("[GDR] libGDR_ReadSector FAD=%u count=%u secsz=%u\n", StartSector, SectorCount, secsz);
  }
	libGDR_ReadSectorLocked(buff,StartSector,SectorCount,secsz);
}

void FASTCALL libGDR_GetToc(u32* toc,u32 area)
{
	if (CurrDrive)
	{
		gda_lock();
		GetDriveToc(toc,(DiskArea)area);
		gda_unlock();
	}
}
//TODO : fix up
u32 FASTCALL libGDR_GetDiscType()
{
	if (CurrDrive)
		return CurrDrive->GetDiscType();
	else
		return NoDisk;
}

void FASTCALL libGDR_GetSessionInfo(u8* out,u8 ses)
{
	gda_lock();
	GetDriveSessionInfo(out,ses);
	gda_unlock();
}


//called when plugin is used by emu (you should do first time init here)
s32 FASTCALL libGDR_Load()
{
	return rv_ok;	
}

//called when plugin is unloaded by emu , olny if dcInitGDR is called (eg , not called to enumerate plugins)
void FASTCALL libGDR_Unload()
{
	
}

//It's suposed to reset everything (if not a manual reset)
void FASTCALL libGDR_Reset(bool Manual)
{

}

//called when entering sh4 thread , from the new thread context (for any thread speciacific init)
s32 FASTCALL libGDR_Init(gdr_init_params* prm)
{
	if (!InitDrive())
		return rv_serror;
	NotifyEvent_gdrom(DiskChange,0);
	gda_start();
	return rv_ok;
}

//called when exiting from sh4 thread , from the new thread context (for any thread speciacific de init) :P
void FASTCALL libGDR_Term()
{
	gda_stop();
	TermDrive();
}

