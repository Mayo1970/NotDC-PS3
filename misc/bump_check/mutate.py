#!/usr/bin/env python3
# Plan 3j (WSL): plants one bug at a time in a copy of ps3_rend.cpp; the decoder bugs must FAIL bump_check/run.sh, the
# draw bugs spevb_check/run.sh (its bump passes). Args: streams (4000) scenes (60), name parts with commas ("3b").
import os, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
SRC = 'ps3/ps3_rend.cpp'
TMP = '/tmp/bumpmut/ps3_rend.cpp'
RUN = {'dec': 'misc/bump_check/run.sh', 'draw': 'misc/spevb_check/run.sh'}

MUTANTS = [
    # the decoder's offset colours, per vertex type
    ('dec', 'type 3 offs from BaseCol', 'VTX_ONE(p, p->BaseCol); v->u = p->u; v->v = p->v;\n\t\tVTX_OFFS(v - s_dec->vtx, p->OffsCol);',
     'VTX_ONE(p, p->BaseCol); v->u = p->u; v->v = p->v;\n\t\tVTX_OFFS(v - s_dec->vtx, p->BaseCol);'),
    ('dec', 'type 4 without offs', 'v->u = uv16(p->u); v->v = uv16(p->v);\n\t\tVTX_OFFS(v - s_dec->vtx, p->OffsCol);',
     'v->u = uv16(p->u); v->v = uv16(p->v);'),
    ('dec', 'type 5B offs alpha from BaseA',
     'AppendPolyVertex5B(TA_Vertex5B* p)\n\t{\n\t\tVTX_B(float_argb(p->BaseA, p->BaseR, p->BaseG, p->BaseB), float_argb(p->OffsA, p->OffsR',
     'AppendPolyVertex5B(TA_Vertex5B* p)\n\t{\n\t\tVTX_B(float_argb(p->BaseA, p->BaseR, p->BaseG, p->BaseB), float_argb(p->BaseA, p->OffsR'),
    ('dec', 'type 6B offs R and G swapped',
     'AppendPolyVertex6B(TA_Vertex6B* p)\n\t{\n\t\tVTX_B(float_argb(p->BaseA, p->BaseR, p->BaseG, p->BaseB), float_argb(p->OffsA, p->OffsR, p->OffsG',
     'AppendPolyVertex6B(TA_Vertex6B* p)\n\t{\n\t\tVTX_B(float_argb(p->BaseA, p->BaseR, p->BaseG, p->BaseB), float_argb(p->OffsA, p->OffsG, p->OffsR'),
    ('dec', 'type 7 offs from BaseInt', 'v->u = p->u; v->v = p->v;\n\t\tVTX_OFFS(v - s_dec->vtx, intensity_offs(p->OffsInt));',
     'v->u = p->u; v->v = p->v;\n\t\tVTX_OFFS(v - s_dec->vtx, intensity_offs(p->BaseInt));'),
    ('dec', 'type 8 offs as a base intensity', 'v->u = uv16(p->u); v->v = uv16(p->v);\n\t\tVTX_OFFS(v - s_dec->vtx, intensity_offs(p->OffsInt));',
     'v->u = uv16(p->u); v->v = uv16(p->v);\n\t\tVTX_OFFS(v - s_dec->vtx, intensity_argb(p->OffsInt));'),
    ('dec', 'type 11A offs from BaseCol0', 'v->u = p->u0; v->v = p->v0;\n\t\tVTX_OFFS(v - s_dec->vtx, p->OffsCol0);',
     'v->u = p->u0; v->v = p->v0;\n\t\tVTX_OFFS(v - s_dec->vtx, p->BaseCol0);'),
    ('dec', 'type 12A without offs', 'v->u = uv16(p->u0); v->v = uv16(p->v0);\n\t\tVTX_OFFS(v - s_dec->vtx, p->OffsCol0);',
     'v->u = uv16(p->u0); v->v = uv16(p->v0);'),
    ('dec', 'type 13A offs from BaseInt0',
     'VTX_OFFS(v - s_dec->vtx, intensity_offs(p->OffsInt0));\n\t}\n\tstatic void AppendPolyVertex13B',
     'VTX_OFFS(v - s_dec->vtx, intensity_offs(p->BaseInt0));\n\t}\n\tstatic void AppendPolyVertex13B'),
    ('dec', 'type 14A offs one vertex on',
     'VTX_OFFS(v - s_dec->vtx, intensity_offs(p->OffsInt0));\n\t}\n\tstatic void AppendPolyVertex14B',
     'VTX_OFFS(v - s_dec->vtx + 1, intensity_offs(p->OffsInt0));\n\t}\n\tstatic void AppendPolyVertex14B'),
    # the face offset colour and the intensity rule
    ('dec', '2B face offset R from the face colour', 's_foffs_a = pp->FaceOffsetA; s_foffs_r = pp->FaceOffsetR;',
     's_foffs_a = pp->FaceOffsetA; s_foffs_r = pp->FaceColorR;'),
    ('dec', '2B keeps the old face offset G, B', '\t\ts_foffs_g = pp->FaceOffsetG; s_foffs_b = pp->FaceOffsetB;\n', ''),
    ('dec', 'intensity offs alpha from the face colour', 'return float_argb(s_foffs_a, s_foffs_r * i,',
     'return float_argb(s_face_a, s_foffs_r * i,'),
    ('dec', 'intensity offs G not scaled', 's_foffs_g * i, s_foffs_b * i);', 's_foffs_g, s_foffs_b * i);'),
    # sprites and the bump gate
    ('dec', 'sprite offs from BaseCol', 's_sprite_offs = spr->OffsCol;', 's_sprite_offs = spr->BaseCol;'),
    ('dec', 'sprite corners without offs', '\t\t\tVTX_OFFS(s_dec->nvtx, s_sprite_offs);\n', ''),
    ('dec', 'bump is PixelFmt 2', 'pp->tcw.NO_PAL.PixelFmt == 4;', 'pp->tcw.NO_PAL.PixelFmt == 2;'),
    ('dec', 'offs kept with Bump Maps off', 's_bump_param = s_bump_on && pp->pcw.Texture', 's_bump_param = pp->pcw.Texture'),
    ('dec', 'bump without the Texture bit', 's_bump_on && pp->pcw.Texture && pp->tcw', 's_bump_on && pp->tcw'),
    ('dec', 'offs for every polygon', 'do { if (s_bump_param) s_dec->offs[i] = (argb); } while (0)',
     'do { s_dec->offs[i] = (argb); } while (0)'),
    ('dec', 'VTX_B offs one vertex on', 'VTX_OFFS(s_dec->nvtx, offs);', 'VTX_OFFS(s_dec->nvtx + 1, offs);'),
    # the draw: u1 of each emitted vertex
    ('draw', 'u1 not in RGBA order', '((volatile u32*)o)[6] = (offs << 8) | (offs >> 24);', '((volatile u32*)o)[6] = offs;'),
    ('draw', 'v1 takes the offs', '\to[7] = 0.0f;\n\t((volatile u32*)o)[8]', '\t((volatile u32*)o)[7] = offs;\n\t((volatile u32*)o)[8]'),
    ('draw', 'run_emit takes the next vertex offs', 's_run.offs ? s_run.offs[v - s_rl.vtx] : 0;',
     's_run.offs ? s_run.offs[v - s_rl.vtx + 1] : 0;'),
    ('draw', 'run_emit offs always 0', 'const u32 offs = s_run.offs ? s_run.offs[v - s_rl.vtx] : 0;', 'const u32 offs = 0;'),
    ('draw', 'a run keeps the last run offs', '\t\ts_run.offs = offs;\n', ''),
    ('draw', 'bump_offs takes PixelFmt 3', 'p.tcw.NO_PAL.PixelFmt == 4 ? s_rl.offs : 0;', 'p.tcw.NO_PAL.PixelFmt == 3 ? s_rl.offs : 0;'),
    ('draw', 'bump_offs never', 'return s_bump_on && p.pcw.Texture', 'return false && s_bump_on && p.pcw.Texture'),
    ('draw', 'bump_offs ignores the toggle', 'return s_bump_on && p.pcw.Texture', 'return p.pcw.Texture'),
    ('draw', 'bump_offs ignores the Texture bit', 'return s_bump_on && p.pcw.Texture && p.tcw', 'return s_bump_on && p.tcw'),
    ('draw', 'glBegin path takes the strip first offs', 'offs ? offs[s.first + k] : 0', 'offs ? offs[s.first] : 0'),
    # bundle 17, 3b: an offset-colour run (the SPU build's flag is the row, so the two sides part)
    ('draw', '3b: offs for a bump texture', 'return p.pcw.Texture && p.pcw.Offset && p.tcw.NO_PAL.PixelFmt != 4;',
     'return p.pcw.Texture && p.pcw.Offset;'),
    ('draw', '3b: offs without the Offset bit', 'return p.pcw.Texture && p.pcw.Offset && p.tcw.NO_PAL.PixelFmt != 4;',
     'return p.pcw.Texture && p.tcw.NO_PAL.PixelFmt != 4;'),
    ('draw', '3b: offs with the row off', 'return s_do.offs && offc_param(p) ? s_rl.offs : 0;', 'return offc_param(p) ? s_rl.offs : 0;'),
    ('draw', '3b: an offset-colour run carries nothing', 'return offs ? offs : offc_offs(p);', 'return offs;'),
]

def run(kind, args, src):
    open(TMP, 'w', encoding='utf-8').write(src)
    return subprocess.run(['sh', os.path.join(ROOT, RUN[kind]), args[kind]], capture_output=True, text=True,
                          env=dict(os.environ, REND_SOURCE=TMP))

def main():
    args = {'dec': sys.argv[1] if len(sys.argv) > 1 else '4000', 'draw': sys.argv[2] if len(sys.argv) > 2 else '60'}
    src = open(os.path.join(ROOT, SRC), encoding='utf-8').read().replace('\r\n', '\n')
    os.makedirs(os.path.dirname(TMP), exist_ok=True)
    # The unchanged copy must pass both, or every mutant would count as caught.
    for kind in RUN:
        if run(kind, args, src).returncode != 0:
            print(f'BASELINE FAILS: {RUN[kind]} fails on the unchanged copy')
            sys.exit(2)
    caught = ran = 0
    only = sys.argv[3].split(',') if len(sys.argv) > 3 else ['']
    for kind, name, find, repl in MUTANTS:
        if not any(o in name for o in only):
            continue
        ran += 1
        if src.count(find) != 1:
            print(f'BAD MUTANT (pattern found {src.count(find)} times): {name}')
            sys.exit(2)
        r = run(kind, args, src.replace(find, repl))
        out = [l for l in (r.stdout + r.stderr).splitlines() if l.startswith('FAIL') or 'runtime error' in l
               or 'error' in l]
        ok = r.returncode != 0
        caught += ok
        print(f"{'caught' if ok else 'MISSED'}: {kind} {name}: {(out[0] if out else '')[:110]}")
    print(f'{caught} of {ran} mutants fail')
    sys.exit(0 if caught == ran else 1)

main()
