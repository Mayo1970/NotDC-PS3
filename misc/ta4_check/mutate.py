#!/usr/bin/env python3
# Bundle 4 (WSL, from the tree's root): each planted bug in the decode options must make run.sh fail.
# python3 misc/ta4_check/mutate.py [frames per phase (400)]. It takes a long time: run it detached, with a log.
import os, subprocess, sys

FRAMES = sys.argv[1] if len(sys.argv) > 1 else '400'
TMP = '/tmp/ta4mut'
os.makedirs(TMP, exist_ok=True)

REND, DEC, TA = 'ps3/ps3_rend.cpp', 'ps3/spu/spe_dec.c', 'plugs/drkPvr/ta.h'
ENV = {REND: 'REND_SOURCE', DEC: 'SPE_DEC_SOURCE', TA: 'TA_SOURCE'}

MUTANTS = [
    # the PPU replay
    (REND, 'P8: divide by 255', '(((((face >> 16) & 0xFF) * sat) >> 8) << 16)', '(((((face >> 16) & 0xFF) * sat) / 255) << 16)'),
    (REND, 'P8: alpha from the intensity', 'return (face & 0xFF000000u) | (((((face >> 16)', 'return (sat << 24) | (((((face >> 16)'),
    (REND, 'P8: blue not scaled', '(((face & 0xFF) * sat) >> 8);', '(face & 0xFF);'),
    (REND, 'P8: the offset face colour', 'return int_scale(s_face8, clamp_byte(i));', 'return int_scale(s_foffs8, clamp_byte(i));'),
    (REND, 'P8: no bytes at PolyParam1',
     's_face_g = pp->FaceColorG; s_face_b = pp->FaceColorB;\n\t\ts_face8 = float_argb(s_face_a, s_face_r, s_face_g, s_face_b);\n\t}\n\tstatic void AppendPolyParam2A',
     's_face_g = pp->FaceColorG; s_face_b = pp->FaceColorB;\n\t}\n\tstatic void AppendPolyParam2A'),
    (REND, 'P8: no bytes at PolyParam4B',
     's_face_g = pp->FaceColor0G; s_face_b = pp->FaceColor0B;\n\t\ts_face8 = float_argb(s_face_a, s_face_r, s_face_g, s_face_b);',
     's_face_g = pp->FaceColor0G; s_face_b = pp->FaceColor0B;'),
    (REND, 'P63: the bytes stay', 's_face8 = s_foffs8 = 0xFFFFFFFF;\n\t\t}', 's_foffs8 = 0xFFFFFFFF;\n\t\t}'),
    (REND, 'P63: the floats stay', '\t\t\ts_face_a = s_face_r = s_face_g = s_face_b = 1;\n', '\t\t\ts_face_a = 1;\n'),
    (REND, 'P63: reset without the option', 'if (s_dopt & TAOPT_FACE_RESET)', 'if (s_dopt & (TAOPT_FACE_RESET | TAOPT_INT_CLAMP))'),
    (REND, 'P27: the wrong option bit', '(list == ListType_Translucent_Modifier_Volume && (s_dopt & TAOPT_TR_VOL))', '(list == ListType_Translucent_Modifier_Volume && (s_dopt & TAOPT_VOL_8K))'),
    (REND, 'P27: no tag at the list end', 'p.list = (u8)(ListType == ListType_Translucent_Modifier_Volume);', 'p.list = 0;'),
    (REND, 'P27: no tag at the volume', 'p.list = (u8)(s_cur_list == ListType_Translucent_Modifier_Volume);', 'p.list = 0;'),
    (REND, 'P66: the old cap always', '? MAX_MODTRI : MAX_MODTRI_OLD;', '? MAX_MODTRI_OLD : MAX_MODTRI_OLD;'),
    (REND, 'P66: the new cap always', '? MAX_MODTRI : MAX_MODTRI_OLD;', '? MAX_MODTRI : MAX_MODTRI;'),
    (REND, 'P10: the rect for mode 3 only', '((p.pcw.full >> 16) & 3) >= 2 ? s_clip_rect : 0;', '((p.pcw.full >> 16) & 3) >= 3 ? s_clip_rect : 0;'),
    (REND, 'P10: 19 bits in the ISP', '(p.isp.full & 0xFFF00000u) | (clip & 0xFFFFFu);', '(p.isp.full & 0xFFF00000u) | (clip & 0x7FFFFu);'),
    (REND, 'P10: the PCW bits one up', '(p.pcw.full & ~0x300u) | ((clip >> 20) << 8);', '(p.pcw.full & ~0x300u) | ((clip >> 20) << 9);'),
    (REND, 'P10: ymin one bit up', 's_clip_rect = xmin | (xmax << 6) | (ymin << 12) | (ymax << 17);', 's_clip_rect = xmin | (xmax << 6) | (ymin << 13) | (ymax << 17);'),
    (REND, 'P10: no rect reset', '\t\t\ts_clip_rect = TA_CLIP_RESET;', '\t\t\t;'),
    (REND, 'P10: pass mark counts vertices', 's_dec->pass_strip[s_dec->npass] = s_dec->nstrip;', 's_dec->pass_strip[s_dec->npass] = s_dec->nvtx;'),
    (REND, 'P10: pass mark counts triangles', 's_dec->pass_mvol[s_dec->npass] = s_dec->nmvol;', 's_dec->pass_mvol[s_dec->npass] = s_dec->nmtri;'),
    # the SPU decoder
    (DEC, 'P8: opaque alpha', 'return (face8 & 0xFF000000u) |', 'return 0xFF000000u |'),
    (DEC, 'P8: blue shifted by 7', '(((face8 & 0xFF) * s) >> 8);', '(((face8 & 0xFF) * s) >> 7);'),
    (DEC, 'P8: no bytes at the header', '\tface_b = f[3];\n\tface8 = float_argb(f[0], f[1], f[2], f[3]);', '\tface_b = f[3];'),
    (DEC, 'P63: the bytes stay', '\t\tface8 = 0xFFFFFFFFu;\n', '\t\t;\n'),
    (DEC, 'P63: the floats stay', '\t\tface_a = face_r = face_g = face_b = F_ONE;\n', '\t\tface_a = F_ONE;\n'),
    (DEC, 'P27: always kept', 'return list == 1 || (list == 3 && (d->opt & TAOPT_TR_VOL));', 'return list == 1 || list == 3;'),
    (DEC, 'P27: no tag at the volume', 'p->list = (uint8_t)(dec_list == 3);', 'p->list = 0;'),
    (DEC, 'P27: no tag at the list end', 'p->list = (uint8_t)(list == 3);', 'p->list = 0;'),
    (DEC, 'P66: the old cap always', '? SPE_MAX_MODTRI : SPE_MODTRI_OLD;', '? SPE_MODTRI_OLD : SPE_MODTRI_OLD;'),
    (DEC, 'P10: the rect for mode 3 only', '((r[0] >> 16) & 3) >= 2 ? clip_rect : 0;', '((r[0] >> 16) & 3) >= 3 ? clip_rect : 0;'),
    (DEC, 'P10: the rect one word early', 'clip_rect = (r[4] & 63) | ((r[6] & 63) << 6) | ((r[5] & 31) << 12) | ((r[7] & 31) << 17);', 'clip_rect = (r[3] & 63) | ((r[5] & 63) << 6) | ((r[4] & 31) << 12) | ((r[6] & 31) << 17);'),
    (DEC, 'P10: ymax 6 bits', '((r[7] & 31) << 17)', '((r[7] & 63) << 17)'),
    (DEC, 'P10: no rect reset', '\t\tclip_rect = TA_CLIP_RESET;', '\t\tclip_rect = 0;'),
    (DEC, 'P10: the PCW keeps its bits', 'p->pcw = (r[0] & ~0x300u) | ((clip >> 20) << 8);', 'p->pcw = r[0] | ((clip >> 20) << 8);'),
    (DEC, 'the fence without the options', '\tres->opt = c->opt;', '\tres->opt = 0;'),
    (DEC, 'the RESET drops the options', '\tc->opt = opt;\n', '\tc->opt = 0;\n'),
    # the splitter
    (TA, 'P9: Object_List_Set takes 64 bytes', 'die("ParamType_Object_List_Set");\n\t\t\t\t\t\tdata+=SZ32;', 'die("ParamType_Object_List_Set");\n\t\t\t\t\t\tdata+=SZ64;'),
    (TA, 'P9: a reserved type takes 64 bytes', 'die("Unhadled parameter");\n\t\t\t\t\t\tdata+=SZ32;', 'die("Unhadled parameter");\n\t\t\t\t\t\tdata+=SZ64;'),
    (TA, 'P66: the typeless header takes 64 bytes', 'if (uid==0xFFFFFFFF && ta_bad_skip())\n\t\t\t\t\t\t\t{\n\t\t\t\t\t\t\t\tdata+=SZ32;', 'if (uid==0xFFFFFFFF && ta_bad_skip())\n\t\t\t\t\t\t\t{\n\t\t\t\t\t\t\t\tdata+=SZ64;'),
    (TA, 'P9: the skips are not counted', '\tg_ta_bad_records++;\n', ''),
]

src = {f: open(f, 'rb').read().decode('utf-8').replace('\r\n', '\n') for f in (REND, DEC, TA)}
killed = survived = 0
for k, (f, name, old, new) in enumerate(MUTANTS):
    if old not in src[f]:
        print('NOT FOUND: %s (%s)' % (name, f), flush=True)
        survived += 1
        continue
    path = '%s/m%d%s' % (TMP, k, os.path.splitext(f)[1])
    # The first match only: ta.h has a second splitter with the same lines.
    open(path, 'w').write(src[f].replace(old, new, 1))
    env = dict(os.environ, TA4_OUT=TMP + '/out')
    env[ENV[f]] = path
    r = subprocess.run(['sh', 'misc/ta4_check/run.sh', FRAMES], env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    out = r.stdout.decode('utf-8', 'replace')
    if r.returncode == 0:
        print('SURVIVED: %s (%s)' % (name, f), flush=True)
        survived += 1
    else:
        why = [l for l in out.split('\n') if 'FAIL' in l or 'error' in l]
        print('killed: %s (%s): %s' % (name, f, why[0][:110] if why else 'exit %d' % r.returncode), flush=True)
        killed += 1
print('%d of %d planted bugs fail the check' % (killed, len(MUTANTS)), flush=True)
sys.exit(1 if survived else 0)
