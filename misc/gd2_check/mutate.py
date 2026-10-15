#!/usr/bin/env python3
# Plan section 7, bundle 2 (WSL): plants one bug at a time in a copy of gdromv3.cpp; run.sh must FAIL on each.
# Args: seeds and ops for run.sh (default 6 and 4000), then a text the names hold (only those mutants run).
import os, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
SRC = 'dc/gdrom/gdromv3.cpp'
TMP = '/tmp/gd2mut/gdromv3.cpp'

MUTANTS = [
    # P21
    ('step 1 keeps CD-DA on', '\t\t\t\tcdda_set(CDDA_NOINFO);\n\t\t\t\tgda_cdda_next(false,0);\n\t\t\t\tgd_trans_next=-1;',
     '\t\t\t\tgda_cdda_next(false,0);\n\t\t\t\tgd_trans_next=-1;'),
    ('step 1 leaves PLAY in the status', '\t\t\t\telse if (SecNumber.Status==GD_PLAY)\n\t\t\t\t\tSecNumber.Status=GD_PAUSE;\n', ''),
    ('seek takes 40 ms', 'cdda_transition(GD_PAUSE,20,GD_SEEK);', 'cdda_transition(GD_PAUSE,40,GD_SEEK);'),
    ('seek shows BUSY', 'cdda_transition(GD_PAUSE,20,GD_SEEK);', 'cdda_transition(GD_PAUSE,20,GD_BUSY);'),
    ('play starts after 50 ms', 'cdda_transition(GD_PLAY,5,SecNumber.Status);', 'cdda_transition(GD_PLAY,50,SecNumber.Status);'),
    ('play starts at once', 'cdda_transition(GD_PLAY,5,SecNumber.Status);',
     '{ cdda_set(CDDA_PLAYING); SecNumber.Status=GD_PLAY; }'),
    ('resume of a play that is not paused', 'play=cdda.status==CDDA_PAUSED;', 'play=true;'),
    ('stop ends in PAUSE', 'cdda_transition(GD_STANDBY,5,GD_BUSY);', 'cdda_transition(GD_PAUSE,5,GD_BUSY);'),
    ('PIO read: PLAY for 20 ms', 'cdda_transition(GD_PAUSE,67,GD_PLAY);   // P21: PLAY for 67 ms after a data read',
     'cdda_transition(GD_PAUSE,20,GD_PLAY);'),
    ('DMA read: no PLAY time',
     '\t\t\tif (get_gd_status_preset()>=2)\n\t\t\t\tcdda_transition(GD_PAUSE,67,GD_PLAY);\n\t\t}\n\t}\n\tif (real && moved)',
     '\t\t}\n\t}\n\tif (real && moved)'),
    ('status change does not set DSC', '\t\tGDStatus.DSC=1;\n\t\tif (gd_trans_next==GD_PAUSE', '\t\tif (gd_trans_next==GD_PAUSE'),
    ('PAUSE does not pause the play',
     'if (gd_trans_next==GD_PAUSE && cdda.status==CDDA_PLAYING)\n\t\t\tcdda_set(CDDA_PAUSED);\n\t\telse if', 'if'),
    ('PLAY does not start the play', '\t\telse if (gd_trans_next==GD_PLAY)\n\t\t\tcdda_set(CDDA_PLAYING);\n', ''),
    ('DSC forced with the timing on',
     'printf_rm("GDROM: Read From AltStatus (v=%X)\\n",GDStatus.full);\n\t\treturn GDStatus.full | (get_gd_status_preset()>=2 ? 0 : (1<<4));',
     'return GDStatus.full | (1<<4);'),
    ('a disc event keeps the pending change', '\tgd_trans_next=-1;   // P21: a pending status change does not outlive the disc state\n', ''),
    ('a read keeps the pending change',
     '\t\t\t\tgd_trans_next=-1;\n\t\t\t\tif (get_gd_status_preset()>=2)\n\t\t\t\t\tSecNumber.Status=GD_PLAY;',
     '\t\t\t\tif (get_gd_status_preset()>=2)\n\t\t\t\t\tSecNumber.Status=GD_PLAY;'),
    ('paused reads as no audio state', 'case CDDA_PAUSED:     return 0x12;', 'case CDDA_PAUSED:     return 0x15;'),
    ('pause with the timing on keeps the play', '\t\t\t\t\tSecNumber.Status=GD_PAUSE;\n\t\t\t\t\tcdda_set(CDDA_PAUSED);\n', '\t\t\t\t\tSecNumber.Status=GD_PAUSE;\n'),
    # P47
    ('FAD bytes shifted', 'stat[5]=(u8)(fad>>16);', 'stat[5]=(u8)(fad>>8);'),
    ('a paused play gives the data position', '(cdda.status==CDDA_PLAYING || cdda.status==CDDA_PAUSED)', '(cdda.status==CDDA_PLAYING)'),
    ('ADR in the high nibble', 'return (u8)((ctrl<<4)|adr);', 'return (u8)((adr<<4)|ctrl);'),
    ('GET_SCD changes at level 1', 'if (get_gd_position_preset()>=2)', 'if (get_gd_position_preset()>=1)'),
    ('the next sector, not the last read', ': read_params.start_sector-1;', ': read_params.start_sector;'),
    ('REQ_STAT index 0', '\t\t\t\tstat[4]=1;\n', '\t\t\t\tstat[4]=0;\n'),
    ('GET_SCD track not BCD', 'tno=(u8)(track<100 ? ((track/10)<<4)|(track%10) : track);', 'tno=(u8)(track+0x10);'),
    # P62
    ('format 1 ignores the asked length', 'sz=min((u32)14,alloc_len);', 'sz=14;'),
    ('format 1: FAD is the track time', 'subc_info[11]=(u8)(fad>>16);', 'subc_info[11]=(u8)(elapsed>>16);'),
    ('format 1: track time one byte late', 'subc_info[9]=(u8)elapsed;\n\t\t\t\tsubc_info[10]=0;', 'subc_info[10]=(u8)elapsed;'),
    ('format 0 not cut at 100', '\t\t\t\tif (get_gd_scd_q_preset() && sz>sizeof(subc_info))\n\t\t\t\t\tsz=sizeof(subc_info);\n', ''),
    # P61
    ('end test is ==', 'rules ? cdda.CurrAddr.FAD>=cdda.EndAddr.FAD : cdda.CurrAddr.FAD==cdda.EndAddr.FAD',
     'cdda.CurrAddr.FAD==cdda.EndAddr.FAD'),
    ('end test is >= with the rules off', 'rules ? cdda.CurrAddr.FAD>=cdda.EndAddr.FAD : cdda.CurrAddr.FAD==cdda.EndAddr.FAD',
     'cdda.CurrAddr.FAD>=cdda.EndAddr.FAD'),
    ('end 0 stays 0', 'if (rules && cdda.EndAddr.FAD==0)', 'if (false)'),
    ('a resume past the end plays', '\t\t\t\t\tif (play && cdda.CurrAddr.FAD>cdda.EndAddr.FAD)\n\t\t\t\t\t\tnext=GD_STANDBY;\n', ''),
    ('a resume takes a repeat count', 'if (!rules || param_type!=7)\n\t\t\t\tcdda.repeats=', 'cdda.repeats='),
    ('the end is STANDBY', 'SecNumber.Status=rules ? GD_PAUSE : GD_STANDBY;', 'SecNumber.Status=GD_STANDBY;'),
    ('a disc event keeps CD-DA',
     '\tif (get_cdda_rules_preset())\n\t{\n\t\tcdda_set(CDDA_NOINFO);\n\t\tgda_cdda_next(false,0);\n\t}\n', ''),
    # P22 commands
    ('unknown ATA: no interrupt',
     'Error.Sense=5;\t// illegal request\n\t\t\tGDStatus.BSY=0;\n\t\t\tGDStatus.CHECK=1;\n\t\t\tasic_RaiseInterrupt(holly_GDROM_CMD);',
     'Error.Sense=5;\t// illegal request\n\t\t\tGDStatus.BSY=0;\n\t\t\tGDStatus.CHECK=1;'),
    ('unknown ATA: no sense', 'Error.Sense=5;\t// illegal request\n', ''),
    ('EXEC_DIAG: no interrupt',
     'sns_key=0;\n\t\t\tGDStatus.BSY=0;\n\t\t\tGDStatus.CHECK=1;\n\t\t\tasic_RaiseInterrupt(holly_GDROM_CMD);',
     'sns_key=0;\n\t\t\tGDStatus.BSY=0;\n\t\t\tGDStatus.CHECK=1;'),
    ('slave status', '\t\tif ((DriveSel&0x10) && get_gd_cmd_fix_preset())\n\t\t\treturn 0;\n', ''),
    ('slave commands run', 'if (sz!=1 || (DriveSel&0x10))', 'if (sz!=1)'),
    ('16-bit command write runs', 'if (sz!=1 || (DriveSel&0x10))', 'if (DriveSel&0x10)'),
    ('CD_READ2 length bytes swapped', 'sector_count=(readcmd.b[6]<<8) | readcmd.b[7];', 'sector_count=(readcmd.b[7]<<8) | readcmd.b[6];'),
    ('GET_TOC cut 2 bytes late', 'toc_len=sizeof(toc_gd);', 'toc_len=sizeof(toc_gd)+2;'),
    ('SET_MODE cut at 10 bytes from any offset', 'Count=Offset<10 ? min(Count,10-Offset) : 0;', 'Count=min(Count,(u32)10);'),
    ('IDENTIFY sends 0x40 bytes', 'gd_spi_pio_end(&reply_a1[0], 0x50);', 'gd_spi_pio_end(&reply_a1[0], 0x40);'),
    ('drive select keeps 4 bits', '(0xa0 | (data&0x1f))', '(0xa0 | (data&0x0f))'),
    # P48
    ('SET_MODE at twice the offset', 'get_gd_set_mode_preset() ? set_mode_offset : set_mode_offset*2', 'set_mode_offset*2'),
    ('SET_MODE at the offset with the fix off', 'get_gd_set_mode_preset() ? set_mode_offset : set_mode_offset*2', 'set_mode_offset'),
    # P22 SB_GDLEN 0, P9
    ('a long DMA reaches the verify', '\t\t\t\tif (fix && read_params.remaining_sectors==0)\n\t\t\t\t\tbreak;\n', ''),
    ('length 0 is 16 MB', '? 0x02000000 : SB_GDLEN', '? 0x01000000 : SB_GDLEN'),
    ('the counter does not wrap', '\tif (fix)\n\t\tSB_GDLEND&=0x01ffffe0;\n', ''),
    ('the read does not end inside a long DMA',
     '\t\t\tif (!fix && !gd_swapped)\n\t\t\t{\n\t\t\t\tverify(!(SB_GDST&1));\n\t\t\t}\n', '\t\t\tverify(!(SB_GDST&1));\n'),
    ('an idle long DMA runs on', 'if ((fix || gd_swapped) && read_buff.cache_size==read_buff.cache_index && read_params.remaining_sectors==0)\n\t\treturn;',
     ''),
    # GD-ROM Speed
    ('chunks of 20 KB', '#define GD_DMA_CHUNK 10240', '#define GD_DMA_CHUNK 20480'),
    ('3.6 MB/s', 'SH4_CLOCK_EFF*GD_DMA_CHUNK/1800000', 'SH4_CLOCK_EFF*GD_DMA_CHUNK/3600000'),
    ('small chunks at 50 MB/s', 'SH4_CLOCK_EFF*left/25000000', 'SH4_CLOCK_EFF*left/50000000'),
    ('no wait at the start', 'if (ticks>=GD_DMA_NOW)', 'if (false)'),
    ('a short transfer waits too', 'if (ticks>=GD_DMA_NOW)', 'if (ticks>0)'),
    ('the wait of an old transfer stays', '\t\tgd_dma_wait=0;\n\t\tif (get_gd_speed_preset())', '\t\tif (get_gd_speed_preset())'),
    ('REAL chunks do not wait', '\tif (real && gd_dma_wait>0)\n\t\treturn;\n', ''),
    ('FAST paced too', 'len=min(len,(u32)(real ? GD_DMA_CHUNK : 32000));', 'len=min(len,(u32)GD_DMA_CHUNK);'),
    # GD-ROM Timing
    ('SEEK: no wait set', '\t\t\t\tgd_seek_wait=(s64)(SH4_CLOCK_EFF/1000)*seek_ms;', '\t\t\t\tgd_seek_wait=0;'),
    ('SEEK: the update does not wait', '\t// GD-ROM Timing SEEK: the head is still on its way.\n\tif (gd_seek_wait>0)\n\t\treturn;\n', ''),
    ('SEEK: the wait does not count down', '\tif (gd_seek_wait>0)\n\t\tgd_seek_wait-=cycles;\n', ''),
    ('SEEK: 100 ms at both levels', 'const u32 seek_ms=timing==2 ? 100 : 330;', 'const u32 seek_ms=100;'),
    ('SEEK: a read that goes on seeks too', ' && start_sector!=gd_head_fad)', ')'),
    ('SEEK: the head stays', '\t\t\tgd_head_fad=start_sector+sector_count;\n', ''),
    ('SEEK: the head at the read start', 'gd_head_fad=start_sector+sector_count;', 'gd_head_fad=start_sector;'),
    ('SEEK: a reset keeps the head', '\tgd_head_fad=0xFFFFFFFF;\n\t//Reset the drive', '\t//Reset the drive'),
    ('SEEK at HOST', 'if (timing>=2 && Features.CDRead.DMA==1', 'if (timing>=0 && Features.CDRead.DMA==1'),
    ('SEEK at FIXED', 'if (timing>=2 && Features.CDRead.DMA==1', 'if (timing>=1 && Features.CDRead.DMA==1'),
    # CD-DA Miss
    ('no read-ahead call at CD_PLAY', '\t\t\t\tif (next==GD_PLAY && get_cdda_miss_preset())\n\t\t\t\t\tgda_cdda_seek(cdda.CurrAddr.FAD);\n', ''),
    ('read-ahead call with the miss option off', 'if (next==GD_PLAY && get_cdda_miss_preset())', 'if (next==GD_PLAY)'),
    ('read-ahead from the start of the play', 'gda_cdda_seek(cdda.CurrAddr.FAD);', 'gda_cdda_seek(cdda.StartAddr.FAD);'),
    # Bundle 19: CD-DA Miss + LOOP AHEAD
    ('LOOP AHEAD: the loop is never named',
     '\t\tgda_cdda_loop(get_cdda_miss_preset()>=3 && cdda.playing && cdda.repeats ? cdda.StartAddr.FAD : GDA_NO_LOOP,\n'
     '\t\t              cdda.EndAddr.FAD);\n', ''),
    ('LOOP AHEAD: named at + SILENCE', 'get_cdda_miss_preset()>=3 && cdda.playing', 'get_cdda_miss_preset()>=2 && cdda.playing'),
    ('LOOP AHEAD: named with no repeat', 'cdda.playing && cdda.repeats ?', 'cdda.playing ?'),
    ('LOOP AHEAD: the end is the start', 'GDA_NO_LOOP,\n\t\t              cdda.EndAddr.FAD);',
     'GDA_NO_LOOP,\n\t\t              cdda.StartAddr.FAD);'),
    # Bundle 19: P82, the lead-out stop
    ('P82: no stop at the lead-out', 'if (get_cdda_rules_preset()>=2 && cdda.CurrAddr.FAD>=libGDR_GetLeadOut())',
     'if (false)'),
    ('P82: the stop at level 1', 'get_cdda_rules_preset()>=2 && cdda.CurrAddr.FAD>=libGDR_GetLeadOut()',
     'get_cdda_rules_preset()>=1 && cdda.CurrAddr.FAD>=libGDR_GetLeadOut()'),
    ('P82: the stop one sector late', 'cdda.CurrAddr.FAD>=libGDR_GetLeadOut()', 'cdda.CurrAddr.FAD>libGDR_GetLeadOut()'),
    ('P82: the position moves on', '\t\t\tcdda.CurrAddr.FAD--;\n', ''),
    ('P82: CD-DA goes on', '\t\t\tcdda_set(CDDA_TERMINATED);\n\t\t\tSecNumber.Status=GD_PAUSE;\n\t\t\tcdda_held=0;',
     '\t\t\tSecNumber.Status=GD_PAUSE;\n\t\t\tcdda_held=0;'),
    ('P82: the status stays PLAY', '\t\t\tSecNumber.Status=GD_PAUSE;\n\t\t\tcdda_held=0;', '\t\t\tcdda_held=0;'),
    # P34
    ('swap: sense key 3', '\tsns_key=2;\n\tsns_asc=4;', '\tsns_key=3;\n\tsns_asc=4;'),
    ('swap: no unit attention', '\t\tsns_key=6;\n', '\t\tsns_key=0;\n'),
    # bundle 20, P68
    ('soft reset: values without the row', '\t\t\tif (get_holly_regs_preset())\n\t\t\t{\n\t\t\t\tGDStatus.full = 0;',
     '\t\t\tif (true)\n\t\t\t{\n\t\t\t\tGDStatus.full = 0;'),
    ('soft reset: byte count high 0xEC', '\t\t\t\tByteCount.hi = 0xEB;', '\t\t\t\tByteCount.hi = 0xEC;'),
    ('soft reset: no interrupt reason', '\t\t\t\tIntReason.full = 1;\n', ''),
    ('swap: ASC 0x28 with no disc', '? 0x29 : 0x28', '? 0x28 : 0x28'),
    ('swap: busy for half a second', 'gd_swap_cycles=(s64)SH4_CLOCK_EFF;', 'gd_swap_cycles=(s64)SH4_CLOCK_EFF/2;'),
    ('swap: status not BUSY', '\tgd_swap_cycles=(s64)SH4_CLOCK_EFF;\n\tSecNumber.Status=GD_BUSY;', '\tgd_swap_cycles=(s64)SH4_CLOCK_EFF;'),
    ('REQ_ERROR keeps the sense', '\t\t\tsns_key=sns_asc=sns_ascq=0;\n\t\t\tGDStatus.CHECK=0;\n', ''),
    ('REQ_ERROR without the ASC', '\t\t\tresp[8]=(u8)sns_asc;\n', ''),
    ('no CHECK with a sense key', 'GDStatus.CHECK=(sns_key==0 || sns_key==0xB) ? 0 : 1;', 'GDStatus.CHECK=0;'),
    ('swap keeps CD-DA', '\tmemset(&cdda,0,sizeof(cdda));\n\tcdda_held=0;', '\tcdda_held=0;'),
    ('swap leaves the DMA to the verify', 'if ((fix || gd_swapped) && read_buff.cache_size==', 'if (fix && read_buff.cache_size=='),
    ('swap keeps the read buffer', '\tread_buff.cache_index=read_buff.cache_size=0;\n\tpio_buff.index=pio_buff.size=0;', '\tpio_buff.index=pio_buff.size=0;'),
    ('NOP keeps DRQ', '\t\tif (get_disc_swap_preset())\n\t\t\tGDStatus.DRQ=0;\n', ''),
    ('NOP clears DRQ with the option off', '\t\tif (get_disc_swap_preset())\n\t\t\tGDStatus.DRQ=0;\n', '\t\tGDStatus.DRQ=0;\n'),
    # the old path
    ('old play ends at once', 'cdda_set(next==GD_PLAY ? CDDA_PLAYING : CDDA_TERMINATED);', 'cdda_set(CDDA_TERMINATED);'),
    ('old seek keeps the play', '\t\t\t\tif (cdda.status==CDDA_PLAYING)\n\t\t\t\t\tcdda_set(CDDA_PAUSED);\n\t\t\t}', '\t\t\t}'),
    ('old seek status', '\t\t\tif (!timing)\n\t\t\t{\n\t\t\t\tSecNumber.Status=GD_PAUSE;', '\t\t\tif (!timing)\n\t\t\t{'),
    ('old stop status', '\t\t\t\t\tSecNumber.Status=GD_STANDBY;\n\t\t\t\t\tcdda_set(CDDA_NOINFO);', '\t\t\t\t\tcdda_set(CDDA_NOINFO);'),
    ('old audio status', 'if (SecNumber.Status==GD_PAUSE)\n\t\treturn 0x15;', 'if (SecNumber.Status==GD_PAUSE)\n\t\treturn 0x12;'),
    ('old GET_SCD track', 'u8 tno=0x01;', 'u8 tno=0x02;'),
    ('old DMA chunk', ': 32000));', ': 16000));'),
    ('old DMA end with nothing moved', '(moved || !fix)', '(moved)'),
    ('old IDENTIFY', 'gd_spi_pio_end(&reply_a1[packet_cmd.data_8[2] & ~1], packet_cmd.data_8[4]);',
     'gd_spi_pio_end(&reply_a1[packet_cmd.data_8[2]], packet_cmd.data_8[4]);'),
    ('old drive select', ': data;\n\t\tbreak;', ': (data&0x7f);\n\t\tbreak;'),
    ('old REQ_ERROR sense', 'resp[2]= SecNumber.Status==GD_BUSY ? 2:0;//sense', 'resp[2]=0;//sense'),
]

def main():
    seeds = sys.argv[1] if len(sys.argv) > 1 else '6'
    ops = sys.argv[2] if len(sys.argv) > 2 else '4000'
    os.makedirs('/tmp/gd2mut', exist_ok=True)
    orig = open(os.path.join(ROOT, SRC), encoding='utf-8').read()
    todo = [m for m in MUTANTS if len(sys.argv) < 4 or sys.argv[3] in m[0]]
    caught = 0
    for name, find, repl in todo:
        if orig.count(find) != 1:
            print(f'BAD MUTANT (pattern found {orig.count(find)} times): {name}')
            sys.exit(2)
        open(TMP, 'w', encoding='utf-8').write(orig.replace(find, repl))
        r = subprocess.run(['sh', os.path.join(ROOT, 'misc/gd2_check/run.sh'), seeds, ops], capture_output=True,
                           text=True, env=dict(os.environ, GD_SRC=TMP, QUICK='1'))
        out = [l for l in (r.stdout + r.stderr).splitlines() if l.startswith(('FAIL', 'ERROR')) or 'error:' in l]
        ok = r.returncode != 0
        caught += ok
        print(f"{'caught' if ok else 'MISSED'}: {name}: {(out[0] if out else '')[:100]}", flush=True)
    print(f'{caught} of {len(todo)} mutants fail run.sh')
    sys.exit(0 if caught == len(todo) else 1)

main()
