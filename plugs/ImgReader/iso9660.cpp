#include "iso9660.h"
#include <unistd.h> // For usleep
#include <ctype.h>

// Defined in ps3_presets.cpp
extern "C" int get_debug_loop();
extern "C" int get_gdi_parser_preset();   // P23
extern "C" int get_gdi_paths_preset();    // P57
extern "C" int get_toc_fields_preset();   // P23

bool inbios=true;
FILE* f_1=0;
FILE* f_2=0;



u8 isotemshit[5000];
struct file_TrackInfo
{
	FILE* f;
	u32 FAD;
	u32 SectorSize;
	u32 ctrl;
	s32 offset;

	bool ReadSector(u8 * buff,u32 sector,u32 secsz)
	{
		if (sector>=FAD)
		{
			if (SectorSize==0 || f==0)
				printf("Read from missing sector %d\n",sector);
			else
			{
				u8* ptr=isotemshit;
				s32 off2=(sector-FAD)*SectorSize + offset;
				if (off2>=0)
				{
					fseek(f,off2,SEEK_SET);
					fread(ptr,SectorSize,1,f);
				}
				else
				{
					fseek(f,0,SEEK_SET);
					fread(ptr,SectorSize-off2,1,f);
					ptr+=off2;
				}
			//	printf("readed %d bytes from file 0x%X , converting to %d [sec %d]\n",
			//		SectorSize,f,secsz,sector);
				ConvertSector(ptr,buff,SectorSize,secsz,sector);
				if (sector==45000)
					PatchRegion_0(buff,secsz);
				if (sector==45006)
					PatchRegion_6(buff,secsz);
			}
			return true;
		}
		return false;
	}
};

file_TrackInfo iso_tracks[101];
TocInfo gdi_toc;
SessionInfo gdi_ses;
u32 iso_tc=0;
// P23: the FAD after track 2's last sector, from its file size; 0 = not known.
static u32 gdi_sd_leadout;

void iso_ReadSSect(u8* p_out,u32 sector,u32 secsz)
{
	for (s32 i=(s32)iso_tc-1;i>=0;i--)
	{
		if (iso_tracks[i].ReadSector(p_out,sector,secsz))
			break;
	}
}


void rss(u8* buff,u32 ss,FILE* file)
{
	fseek(file,ss*2352+0x10,SEEK_SET);
	fread(buff,2048,1,file);
}
void iso_DriveReadSector(u8 * buff,u32 StartSector,u32 SectorCount,u32 secsz)
{

  // Debug loop preset: log every ISO read
  if(get_debug_loop() == 1){
	  printf("GDR(ISO)->Read : Sector %d , size %d , mode %d \n",StartSector,SectorCount,secsz);
  }
	if (StartSector>150)
		StartSector-=150;
	while(SectorCount--)
	{
		iso_ReadSSect(buff,StartSector,secsz);
		buff+=secsz;
		StartSector++;
	}
	return;
}
void iso_DriveGetTocInfo(TocInfo* toc,DiskArea area)
{
	memcpy(toc,&gdi_toc,sizeof(TocInfo));
	if(area==SingleDensity)
	{
		// P23 (Flycast e592650af): the low-density area ends where track 2 does, not at a fixed FAD.
		toc->LeadOut.FAD=(get_toc_fields_preset() && gdi_sd_leadout) ? gdi_sd_leadout : 13085;
		toc->FistTrack=1;
		toc->LastTrack=2;

		for (int tr=2;tr<99;tr++)
		{
			toc->tracks[tr].FAD=0xFFFFFFF;
			toc->tracks[tr].Addr=0xFF;
			toc->tracks[tr].Control=0xFF;
		}
	}
	else
	{
		toc->LeadOut.FAD=549300;
		toc->FistTrack=3;
		toc->LastTrack=gdi_toc.LastTrack;

		for (int tr=0;tr<2;tr++)
		{
			toc->tracks[tr].FAD=0xFFFFFFF;
			toc->tracks[tr].Addr=0xFF;
			toc->tracks[tr].Control=0xFF;
		}
	}
}
void iso_GetSessionsInfo(SessionInfo* sessions)
{
	memcpy(sessions,&gdi_ses,sizeof(SessionInfo));
}
//TODO : fix up
u32 iso_DriveGetDiscType()
{
	if (iso_tc==0)
		return NoDisk;
	else
		return GdRom;
}

// P23 (Flycast gdi.cpp): a track name in quotes may hold spaces. False at the file end or on an open quote.
static bool gdi_read_name(FILE* t,char* out,size_t len)
{
	int c;
	do
		c=fgetc(t);
	while (c!=EOF && isspace(c));
	if (c==EOF)
		return false;
	const bool quoted=c=='"';
	if (quoted)
		c=fgetc(t);
	size_t n=0;
	while (c!=EOF && (quoted ? c!='"' : !isspace(c)))
	{
		if (n+1<len)
			out[n++]=(char)c;
		c=fgetc(t);
	}
	out[n]=0;
	return !(quoted && c==EOF);
}

bool load_gdi(char* file_)
{
	char file[512];
	strcpy(file,file_);
	memset(&gdi_toc,0xFFFFFFFF,sizeof(gdi_toc));
	memset(&gdi_ses,0xFFFFFFFF,sizeof(gdi_ses));
	FILE* t=fopen(file,"rb");
	if (!t)
		return false;
	
	iso_tc=0;
	gdi_sd_leadout=0;

	fscanf(t,"%d\r\n",&iso_tc);
	if (iso_tc==0 || iso_tc>99)
	{
		iso_tc=0;
		fclose(t);
		return false;
	}

	printf("\nGDI : %d tracks\n",iso_tc);

	char temp[512];
	char path[512];
	strcpy(path,file);
	size_t len=strlen(file);
	while (len>2)
	{
		if (path[len]=='/' || path[len]=='\\')
			break;
		len--;
	}
	len++;
	char* pathptr=&path[len];
	u32 TRACK=0,FADS=0,CTRL=0,SSIZE=0;
	s32 OFFSET=0;
	for (u32 i=0;i<iso_tc;i++)
	{

		//TRACK FADS CTRL SSIZE file OFFSET

		if (!get_gdi_parser_preset())
			fscanf(t,"%d %d %d %d %s %d\r\n",&TRACK,&FADS,&CTRL,&SSIZE,temp,&OFFSET);
		else if (fscanf(t,"%d %d %d %d",&TRACK,&FADS,&CTRL,&SSIZE)!=4 || !gdi_read_name(t,temp,sizeof(temp)) ||
		         fscanf(t,"%d",&OFFSET)!=1)
		{
			// P23: the old line read went on with stale fields, and the boot failed later with no message.
			printf("GDI : line %d does not parse\n",i+2);
			fclose(t);
			iso_term();
			return false;
		}
		printf("file %s[%d] : FAD:%d,CTRL : %d, SSIZE :%d,OFFSET:%d\n",temp,TRACK,FADS,CTRL,SSIZE,OFFSET);

		iso_tracks[i].f=0;
		if (SSIZE!=0)
		{
			strcpy(pathptr,temp);
			// P57: a GDI written on Windows may name its tracks with '\'.
			if (get_gdi_paths_preset())
				for (char* p=pathptr;*p;p++)
					if (*p=='\\')
						*p='/';
			iso_tracks[i].f=fopen(path,"rb");
			if (!iso_tracks[i].f)
				printf("GDI : cannot open track file %s\n",path);
			// P23: where track 2 ends, for the low-density lead-out.
			else if (TRACK==2)
			{
				fseek(iso_tracks[i].f,0,SEEK_END);
				const long size=ftell(iso_tracks[i].f);
				if (size>OFFSET)
					gdi_sd_leadout=FADS+150+(u32)((size-OFFSET)/SSIZE);
			}
		}

		iso_tracks[i].FAD=FADS;
		iso_tracks[i].offset=OFFSET;
		iso_tracks[i].SectorSize=SSIZE;
		iso_tracks[i].ctrl=CTRL;

		gdi_toc.tracks[i].Addr=0;
		gdi_toc.tracks[i].Control=CTRL;
		gdi_toc.tracks[i].FAD=FADS+150;
	}


	gdi_toc.LastTrack=iso_tc;

	gdi_ses.SessionCount=2;
	//session 1 : start @ track 1, and its fad
	gdi_ses.SessionStart[0]=1;
	gdi_ses.SessionFAD[0]=gdi_toc.tracks[0].FAD;


	//session 2 : start @ track 3, and its fad
	// P23: index 1. The old index 0 gives REQ_SES session 1 at track 3 and session 2 at 0xFF.
	gdi_ses.SessionStart[get_gdi_parser_preset() ? 1 : 0]=3;
	gdi_ses.SessionFAD[1]=gdi_toc.tracks[2].FAD;
	gdi_ses.SessionsEndFAD=549300;

	fclose(t);
	return true;
}
bool iso_init(char* file)
{
	size_t len=strlen(file);
	if (len>4)
	{
		// Case-insensitive: the PS3 browser lists whatever the card holds,
		// and a ".GDI" would otherwise reach no reader at all.
		if (strcasecmp( &file[len-4],".gdi")==0)
		{
			return load_gdi(file);
		}
	}
	return false;
}

// P34: a disc swap opens the next image, so the track files close here.
void iso_term()
{
	for (u32 i=0;i<iso_tc;i++)
		if (iso_tracks[i].f)
		{
			fclose(iso_tracks[i].f);
			iso_tracks[i].f=0;
		}
	iso_tc=0;
}

