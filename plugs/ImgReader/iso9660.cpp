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
// A .cue of a CD: its disc type and the FAD after its last sector. 0: a GD-ROM (GDI, or a cue with the HD area).
static u32 cue_cd;
static u32 cue_leadout;

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
	if (cue_cd)
	{
		// As the CHD reader: a CD has no high-density area; track 1 keeps ConvToc() inside the array.
		if (area==DoubleDensity)
		{
			memset(toc,0xFF,sizeof(TocInfo));
			toc->FistTrack=1;
			toc->LastTrack=1;
			toc->LeadOut.FAD=cue_leadout;
			toc->LeadOut.Control=0x04;
			toc->LeadOut.Addr=0;
			toc->LeadOut.Session=0;
		}
		return;
	}
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
		return cue_cd ? cue_cd : GdRom;
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

static u32 cue_sector_size(const char* type)
{
	if (!strcasecmp(type,"AUDIO") || !strcasecmp(type,"CDG"))
		return 2352;
	const char* s=strchr(type,'/');
	const u32 n=s ? (u32)atoi(s+1) : 0;
	return (n==2048 || n==2336 || n==2352) ? n : 0;
}

static bool cue_fail(FILE* t,const char* why,u32 line)
{
	printf("CUE : line %u: %s\n",line,why);
	fclose(t);
	iso_term();
	return false;
}

// Bin/cue as Flycast's cue.cpp: a Redump GD-ROM (REM HIGH-DENSITY AREA) or a CD; REM SESSION 02 starts session 2.
// Each track reads from its FILE at the INDEX 01 sector; the sectors before it are its pregap.
bool load_cue(char* file)
{
	FILE* t=fopen(file,"rb");
	if (!t)
		return false;
	memset(&gdi_toc,0xFF,sizeof(gdi_toc));
	memset(&gdi_ses,0,sizeof(gdi_ses));
	gdi_sd_leadout=0;
	iso_tc=0;
	cue_cd=CdRom;

	char path[512],line[512],tok[16],type[16]="";
	snprintf(path,sizeof(path),"%s",file);
	char* name=strrchr(path,'/');
	name=name ? name+1 : path;
	u32 fad=150,file_fad=0,ses=0,secsize=0,ln=0;
	long file_size=0;
	bool have_file=false,ses_first=false;
	while (fgets(line,sizeof(line),t))
	{
		ln++;
		char* p=line;
		if ((u8)p[0]==0xEF && (u8)p[1]==0xBB && (u8)p[2]==0xBF)
			p+=3;
		if (sscanf(p,"%15s",tok)!=1)
			continue;
		p=strstr(p,tok)+strlen(tok);
		if (!strcasecmp(tok,"REM"))
		{
			u32 n=0;
			if (sscanf(p,"%15s %u",tok,&n)<1)
				continue;
			if (!strcasecmp(tok,"SESSION"))
			{
				if (n==0 || n==ses)
					continue;
				ses=n;
				// Session 1 lead-out 01:30:00, session 2 lead-in 01:00:00, pregap 00:02:00.
				if (ses==2)
				{
					fad+=6750+4500+150;
					ses_first=true;
				}
				if (gdi_ses.SessionCount<99)
				{
					gdi_ses.SessionStart[gdi_ses.SessionCount]=iso_tc+1;
					gdi_ses.SessionFAD[gdi_ses.SessionCount]=fad;
					gdi_ses.SessionCount++;
				}
			}
			else if (!strcasecmp(tok,"HIGH-DENSITY"))
			{
				gdi_sd_leadout=fad;
				fad=45000+150;
				cue_cd=0;
			}
			else if (strcasecmp(tok,"SINGLE-DENSITY"))
				continue;
			have_file=false;
			secsize=0;
		}
		else if (!strcasecmp(tok,"FILE"))
		{
			while (*p==' ' || *p=='\t')
				p++;
			char* fn=p;
			char* q;
			if (*p=='"')
				q=strchr(fn=p+1,'"');
			else
				q=p+strcspn(p," \t\r\n");
			if (!q || q==fn)
				return cue_fail(t,"no file name",ln);
			if (*q)
				*q++=0;
			if (sscanf(q,"%15s",tok)!=1 || strcasecmp(tok,"BINARY"))
				return cue_fail(t,"FILE is not BINARY",ln);
			snprintf(name,sizeof(path)-(name-path),"%s",fn);
			for (char* c=name;*c;c++)
				if (*c=='\\')
					*c='/';
			FILE* b=fopen(path,"rb");
			if (!b)
			{
				printf("CUE : cannot open track file %s\n",path);
				return cue_fail(t,"FILE not found",ln);
			}
			fseek(b,0,SEEK_END);
			file_size=ftell(b);
			fclose(b);
			file_fad=fad;
			have_file=true;
			secsize=0;
		}
		else if (!strcasecmp(tok,"TRACK"))
		{
			u32 n=0;
			if (!have_file || sscanf(p,"%u %15s",&n,type)!=2 || n==0 || n>99)
				return cue_fail(t,"bad TRACK",ln);
			const bool first=secsize==0;
			secsize=cue_sector_size(type);
			if (!secsize)
				return cue_fail(t,"unknown track type",ln);
			// Several tracks of one FILE: its size counts once.
			if (first)
				fad+=(u32)(file_size/secsize);
		}
		else if (!strcasecmp(tok,"INDEX"))
		{
			u32 idx=0;
			int m=0,s=0,f=0;
			if (sscanf(p,"%u %d:%d:%d",&idx,&m,&s,&f)!=4 || idx!=1)
				continue;
			if (!secsize || iso_tc>=99)
				return cue_fail(t,"INDEX 01 without a TRACK, or more than 99 tracks",ln);
			const u32 at=(u32)(f+75*(s+60*m));
			// The session gap holds this track's pregap: the sectors before INDEX 01 do not count twice.
			if (ses_first && at>0)
			{
				const u32 pregap=at<150 ? at : 150;
				file_fad-=pregap;
				fad-=pregap;
			}
			ses_first=false;
			file_TrackInfo& tr=iso_tracks[iso_tc];
			tr.f=fopen(path,"rb");
			tr.FAD=file_fad+at-150;
			tr.offset=(s32)(at*secsize);
			tr.SectorSize=secsize;
			tr.ctrl=(!strcasecmp(type,"AUDIO") || !strcasecmp(type,"CDG")) ? 0 : 4;
			gdi_toc.tracks[iso_tc].FAD=file_fad+at;
			gdi_toc.tracks[iso_tc].Control=tr.ctrl;
			gdi_toc.tracks[iso_tc].Addr=1;
			gdi_toc.tracks[iso_tc].Session=ses ? ses : 1;
			iso_tc++;
		}
	}
	fclose(t);

	if (!cue_cd)
	{
		// Flycast's checks: tracks 1 and 3 data, track 2 audio, track 3 at the high-density area's start.
		if (iso_tc<3 || gdi_toc.tracks[0].Control!=4 || gdi_toc.tracks[1].Control!=0 ||
		    gdi_toc.tracks[2].Control!=4 || gdi_toc.tracks[2].FAD!=45150)
		{
			printf("CUE : not a GD-ROM track layout\n");
			iso_term();
			return false;
		}
		gdi_ses.SessionCount=2;
		gdi_ses.SessionStart[0]=1;
		gdi_ses.SessionFAD[0]=gdi_toc.tracks[0].FAD;
		gdi_ses.SessionStart[1]=3;
		gdi_ses.SessionFAD[1]=gdi_toc.tracks[2].FAD;
		gdi_ses.SessionsEndFAD=549300;
	}
	else
	{
		if (!iso_tc)
		{
			printf("CUE : no track\n");
			return false;
		}
		// Flycast: REM SESSION makes a CD-ROM XA.
		if (!ses)
		{
			gdi_ses.SessionCount=1;
			gdi_ses.SessionStart[0]=1;
			gdi_ses.SessionFAD[0]=gdi_toc.tracks[0].FAD;
		}
		else
			cue_cd=CdRom_XA;
		cue_leadout=fad;
		gdi_ses.SessionsEndFAD=fad;
		gdi_toc.LeadOut.FAD=fad;
		gdi_toc.LeadOut.Control=4;
		gdi_toc.LeadOut.Addr=1;
		gdi_toc.LeadOut.Session=0;
	}
	gdi_toc.FistTrack=1;
	gdi_toc.LastTrack=iso_tc;
	printf("CUE : %s, %u tracks, %u session(s), last track at FAD %u, lead-out %u\n",cue_cd ? "CD-ROM" : "GD-ROM",
	       iso_tc,(u32)gdi_ses.SessionCount,gdi_toc.tracks[iso_tc-1].FAD,cue_cd ? fad : 549300);
	return true;
}

bool iso_init(char* file)
{
	cue_cd=0;
	size_t len=strlen(file);
	if (len>4)
	{
		// Case-insensitive: the PS3 browser lists whatever the card holds,
		// and a ".GDI" would otherwise reach no reader at all.
		if (strcasecmp( &file[len-4],".gdi")==0)
		{
			return load_gdi(file);
		}
		if (strcasecmp(&file[len-4],".cue")==0)
			return load_cue(file);
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

