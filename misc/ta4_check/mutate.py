#!/usr/bin/env python3
# Bundles 4, 17 and 23 (WSL, from the tree's root): each planted bug in the decoders must make run.sh fail. Run it detached.
# python3 misc/ta4_check/mutate.py [frames per phase (400)] [name parts with commas: "P80,3b" keeps bundle 17's bugs]
import os, subprocess, sys

FRAMES = sys.argv[1] if len(sys.argv) > 1 else '400'
ONLY = sys.argv[2].split(',') if len(sys.argv) > 2 else ['']
TMP = '/tmp/ta4mut'
os.makedirs(TMP, exist_ok=True)

REND, DEC, TA, SPR = 'ps3/ps3_rend.cpp', 'ps3/spu/spe_dec.c', 'plugs/drkPvr/ta.h', 'ps3/spu/spe_sprite.h'
ENV = {REND: 'REND_SOURCE', DEC: 'SPE_DEC_SOURCE', TA: 'TA_SOURCE', SPR: 'SPRITE_SOURCE'}

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
    # bundle 17, the PPU replay
    (REND, 'P80: the option bit of the offset colours', 'if (s_dopt & TAOPT_SPR_PLANE)', 'if (s_dopt & TAOPT_OFFS)'),
    (REND, 'P80: u from v', 'memcpy(&s_spr[3].u, &o[1], 4);', 'memcpy(&s_spr[3].u, &o[2], 4);'),
    (REND, 'P80: the plane keeps the old 1/W', '\t\t\t\tmemcpy(&z3, &o[0], 4);\n', ''),
    (REND, 'P80: B and C swapped in x', 'spr_bits(s_spr[0].x), spr_bits(s_spr[1].x), spr_bits(s_spr[2].x), spr_bits(sv->x3)',
     'spr_bits(s_spr[0].x), spr_bits(s_spr[2].x), spr_bits(s_spr[1].x), spr_bits(sv->x3)'),
    (REND, 'P80: not counted', '\t\t\t\ts_stat_spr_plane++;\n', ''),
    (REND, '3b: no offset colours with the option', 'if (s_dopt & TAOPT_OFFS)\n\t\t\ts_bump_param = true;',
     'if (s_dopt & TAOPT_SPR_PLANE)\n\t\t\ts_bump_param = true;'),
    (REND, '3b: an untextured vertex keeps an old offset colour', '#define VTX_NO_OFFS() VTX_OFFS(v - s_dec->vtx, 0)',
     '#define VTX_NO_OFFS() do { } while (0)'),
    # bundle 17, the SPU decoder
    (DEC, 'P80: the option bit of the offset colours', 'if (d->opt & TAOPT_SPR_PLANE)', 'if (d->opt & TAOPT_OFFS)'),
    (DEC, 'P80: v from u', '\t\t\tv[3] = o[2];', '\t\t\tv[3] = o[1];'),
    (DEC, 'P80: the plane keeps the old 1/W', '\t\t\tz3 = o[0];\n', ''),
    (DEC, 'P80: not counted', '\t\t\techo.spr_plane++;\n', ''),
    (DEC, '3b: types 3 and 4 take the base colour', '(t == 3 || t == 4) ? r[7]', '(t == 3 || t == 4) ? r[6]'),
    (DEC, '3b: types 7 and 8 take the raw word', '(t == 7 || t == 8) ? intensity_offs(r[7]) : 0', '(t == 7 || t == 8) ? r[7] : 0'),
    (DEC, '3b: types 11 and 12 left out', 'if ((d->opt & TAOPT_OFFS) && t >= 11)', 'if ((d->opt & TAOPT_OFFS) && t >= 13)'),
    (DEC, '3b: types 13 and 14 take the raw word', 'put_offs(d->nvtx, t <= 12 ? r[7] : intensity_offs(r[7]));', 'put_offs(d->nvtx, r[7]);'),
    (DEC, '3b: float offsets from the base colour', 'put_offs(d->nvtx, float_argb(b[4], b[5], b[6], b[7]));',
     'put_offs(d->nvtx, float_argb(b[0], b[1], b[2], b[3]));'),
    (DEC, '3b: the sprite takes its base colour', 'put_offs(d->nvtx, sprite_offs);', 'put_offs(d->nvtx, sprite_col);'),
    (DEC, '3b: no face offset at the header', '\t\t\tif (ppid == 2)\n\t\t\t\tset_foffs(r + 12);\n', ''),
    (DEC, '3b: no face offset in a split header', '\t\tif (tc_type == 2)\n\t\t\tset_foffs(r + 4);\n', ''),
    (DEC, '3b: a face offset from header type 4', '\t\tif (tc_type == 2)\n\t\t\tset_foffs(r + 4);',
     '\t\tif (tc_type == 2 || tc_type == 4)\n\t\t\tset_foffs(r + 4);'),
    (DEC, '3b: the face offset keeps its old bytes', '\tfoffs_b = f[3];\n\tfoffs8 = float_argb(f[0], f[1], f[2], f[3]);', '\tfoffs_b = f[3];'),
    (DEC, '3b: P63 leaves the face offset', '\t\tfoffs_a = foffs_r = foffs_g = foffs_b = F_ONE;\n\t\tfoffs8 = 0xFFFFFFFFu;\n', ''),
    (DEC, '3b: the offset colours never leave LS', 'n[S_OFFS] = (c->opt & TAOPT_OFFS) ? c->nvtx : 0;', 'n[S_OFFS] = 0;'),
    (DEC, '3b: intensity offsets with the face alpha', 'return (clamp_byte(foffs_a) << 24) | (mul_byte(foffs_r, i) << 16)',
     'return (clamp_byte(face_a) << 24) | (mul_byte(foffs_r, i) << 16)'),
    # bundle 17, the plane math of both decoders: only the model can see these
    (SPR, 'P80: the determinant with the other sign', 'const double det = spr_mul(abx, acy) - spr_mul(aby, acx);',
     'const double det = spr_mul(aby, acx) - spr_mul(abx, acy);'),
    (SPR, 'P80: k1 and k2 swapped', 'const double r = a + spr_mul(k1, spr_d(w[k][1]) - a) + spr_mul(k2, spr_d(w[k][2]) - a);',
     'const double r = a + spr_mul(k2, spr_d(w[k][1]) - a) + spr_mul(k1, spr_d(w[k][2]) - a);'),
    (SPR, 'P80: every sprite keeps the old corner', '\tif (det == 0.0)\n\t\treturn 0;', '\treturn 0;'),
    (SPR, 'P80: a parallelogram takes the plane', '\tif (apx == acx - abx && apy == acy - aby)\n\t\treturn 0;\n', ''),
    (SPR, 'P80: values that are not plain pass', 'return !b || ((b >> 23) & 0xFF) - 87u <= 80u;', 'return 1;'),
    (SPR, 'P80: the bit-by-bit divide does not round', '\tif ((q & 1) && (ma || (m & 1)))\n\t\tm++;\n', ''),
    (SPR, 'P80: the bit-by-bit divide one bit short', 'for (int i = 0; i < 54; i++)', 'for (int i = 0; i < 53; i++)'),
    (SPR, 'P80: u from the z row', 'const uint32_t* const w[3] = { z, u, v };', 'const uint32_t* const w[3] = { z, z, v };'),
    (SPR, 'P80: D from the y of C', 'const double apx = spr_d(x[3]) - ax, apy = spr_d(y[3]) - ay;',
     'const double apx = spr_d(x[3]) - ax, apy = spr_d(y[2]) - ay;'),
    # bundle 23, P91 Near Plane Cap: the 1/W cap of the near plane in both decoders
    (REND, 'P91: the WIDE cap always', 'z.i < ((s_dopt & TAOPT_INVW_WIDE) ? INVW_CAP_WIDE : INVW_CAP_OLD)', 'z.i < INVW_CAP_WIDE'),
    (REND, 'P91: the old cap always', 'z.i < ((s_dopt & TAOPT_INVW_WIDE) ? INVW_CAP_WIDE : INVW_CAP_OLD)', 'z.i < INVW_CAP_OLD'),
    (REND, 'P91: the cap itself passes', 'm.i < z.i && z.i < ((s_dopt', 'm.i < z.i && z.i <= ((s_dopt'),
    (REND, 'P91: the option bit of the offset colours', '((s_dopt & TAOPT_INVW_WIDE) ?', '((s_dopt & TAOPT_OFFS) ?'),
    (DEC, 'P91: the WIDE cap always', 'const int32_t cap = (d->opt & TAOPT_INVW_WIDE) ? INVW_CAP_WIDE : INVW_CAP_OLD;',
     'const int32_t cap = INVW_CAP_WIDE;'),
    (DEC, 'P91: the old cap always', 'const int32_t cap = (d->opt & TAOPT_INVW_WIDE) ? INVW_CAP_WIDE : INVW_CAP_OLD;',
     'const int32_t cap = INVW_CAP_OLD;'),
    (DEC, 'P91: the cap itself passes', '(int32_t)z < cap)', '(int32_t)z <= cap)'),
    (DEC, 'P91: the option bit of the sprite plane', '(d->opt & TAOPT_INVW_WIDE) ?', '(d->opt & TAOPT_SPR_PLANE) ?'),
]

src = {f: open(f, 'rb').read().decode('utf-8').replace('\r\n', '\n') for f in (REND, DEC, TA, SPR)}
killed = survived = 0
for k, (f, name, old, new) in enumerate(MUTANTS):
    if not any(o in name for o in ONLY):
        continue
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
print('%d of %d planted bugs fail the check' % (killed, killed + survived), flush=True)
sys.exit(1 if survived else 0)
