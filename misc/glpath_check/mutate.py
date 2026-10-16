#!/usr/bin/env python3
# Bundle 14 (WSL, from the tree's root): each planted bug in ps3gl's state path must make run.sh fail.
# python3 misc/glpath_check/mutate.py [seeds (8)] [frames (300)]. Run it detached, with a log.
import os, shutil, subprocess, sys

SEEDS = sys.argv[1] if len(sys.argv) > 1 else '8'
FRAMES = sys.argv[2] if len(sys.argv) > 2 else '300'
TMP = '/tmp/glpmut'
REF = '/tmp/glpath'
S, T, V, D, M, X = 'states', 'textures', 'vertices', 'draw', 'main', 'shaders'
# Files of ps3/ that only run.sh's source lines read.
P, N, R = 'ps3_presets.cpp', 'ps3_menu.cpp', 'ps3_rend.cpp'

MUTANTS = [
    # Bundle 17: the dc programs (ps3gl_shaders.c).
    ('dc: no program without fog as the second choice', X, '    if (k < 0)\n        k = dc_tab[ps3gl.dc_xt][offs][PS3GL_FOG_NONE][tenv];\n', ''),
    ('dc: the offset colour with no texture', X, 'const int offs = ps3gl.dc_offs && tenv != PS3GL_TENV_DISABLED;', 'const int offs = ps3gl.dc_offs;'),
    ('dc: a bump texture takes a dc program', X, 'if ((ps3gl.dc_offs || ps3gl.dc_xt) && !(tmu0 && ps3gl.tmu[0].bump)) {', 'if (ps3gl.dc_offs || ps3gl.dc_xt) {'),
    ('dc: the weight is not sent', X, 'const float fogp[4] = { ps3gl.dc_fog_scale, ps3gl.dc_xt_v[8], 0.0f, 0.0f };', 'const float fogp[4] = { ps3gl.dc_fog_scale, 1.0f, 0.0f, 0.0f };'),
    ('dc: a new clamp is not sent', X, '            memcmp(ps3gl.dc_xt_v, ps3gl.dc_xt_uploaded, sizeof(ps3gl.dc_xt_v)) != 0) {', '            0) {'),
    ('dc: cmax takes cmin', X, 'rsxSetVertexProgramParameter(ctx, s->vp, s->cmax_const, &ps3gl.dc_xt_v[4]);', 'rsxSetVertexProgramParameter(ctx, s->vp, s->cmax_const, &ps3gl.dc_xt_v[0]);'),
    ('dc: the table one program off', X, 'return k < 0 ? -1 : PS3GL_TENV_DC_FIRST + k;', 'return k < 0 ? -1 : PS3GL_TENV_DC_FIRST + (k ^ 1);'),
    ('dc: ps3gl_dc_xt keeps the old ends', X, '    memcpy(&ps3gl.dc_xt_v[0], cmin, 4 * sizeof(float));\n', ''),
    # Left out as equivalent: the table with the row OFF or not written (a hit is checked); a new frame keeping the
    # attribute record (frames take the two vring segments in turn: the last frame's offsets do not come back).
    ('compare path: a setter never marks', S, 'if (changed || !(ps3gl.fast & PS3GL_FAST_COMPARE))', 'if (!(ps3gl.fast & PS3GL_FAST_COMPARE))'),
    ('rows OFF: a setter marks only a change', S, 'if (changed || !(ps3gl.fast & PS3GL_FAST_COMPARE))', 'if (changed)'),
    ('blend enable', S, 'rs_mark(ps3gl.rs.blend_enable != val, PS3GL_DIRTY_BLEND);', 'rs_mark(0, PS3GL_DIRTY_BLEND);'),
    ('alpha test enable', S, 'rs_mark(ps3gl.rs.alpha_test_enable != val, PS3GL_DIRTY_ALPHA);', 'rs_mark(0, PS3GL_DIRTY_ALPHA);'),
    ('depth test enable', S, 'rs_mark(ps3gl.rs.depth_test_enable != val, PS3GL_DIRTY_DEPTH);', 'rs_mark(0, PS3GL_DIRTY_DEPTH);'),
    ('cull enable', S, 'rs_mark(ps3gl.rs.cull_enable != val, PS3GL_DIRTY_CULL);', 'rs_mark(0, PS3GL_DIRTY_CULL);'),
    ('scissor enable', S, 'rs_mark(ps3gl.rs.scissor_enable != val, PS3GL_DIRTY_SCISSOR);', 'rs_mark(0, PS3GL_DIRTY_SCISSOR);'),
    ('stencil enable', S, 'rs_mark(ps3gl.rs.stencil_enable != val, PS3GL_DIRTY_STENCIL);', 'rs_mark(0, PS3GL_DIRTY_STENCIL);'),
    ('polygon offset enable', S, 'rs_mark(ps3gl.rs.polyoffset_fill != val, PS3GL_DIRTY_POLYOFFSET);', 'rs_mark(0, PS3GL_DIRTY_POLYOFFSET);'),
    ('blend: the destination factor', S, 'rs_mark(ps3gl.rs.blend_src != src || ps3gl.rs.blend_dst != dst, PS3GL_DIRTY_BLEND);', 'rs_mark(ps3gl.rs.blend_src != src, PS3GL_DIRTY_BLEND);'),
    ('blend: the source factor', S, 'rs_mark(ps3gl.rs.blend_src != src || ps3gl.rs.blend_dst != dst, PS3GL_DIRTY_BLEND);', 'rs_mark(ps3gl.rs.blend_dst != dst, PS3GL_DIRTY_BLEND);'),
    ('alpha: the reference', S, 'rs_mark(ps3gl.rs.alpha_func != f || ps3gl.rs.alpha_ref != r, PS3GL_DIRTY_ALPHA);', 'rs_mark(ps3gl.rs.alpha_func != f, PS3GL_DIRTY_ALPHA);'),
    ('alpha: the function', S, 'rs_mark(ps3gl.rs.alpha_func != f || ps3gl.rs.alpha_ref != r, PS3GL_DIRTY_ALPHA);', 'rs_mark(ps3gl.rs.alpha_ref != r, PS3GL_DIRTY_ALPHA);'),
    ('depth function', S, 'rs_mark(ps3gl.rs.depth_func != f, PS3GL_DIRTY_DEPTH);', 'rs_mark(0, PS3GL_DIRTY_DEPTH);'),
    ('depth mask', S, 'rs_mark(ps3gl.rs.depth_mask != mask, PS3GL_DIRTY_DEPTH);', 'rs_mark(0, PS3GL_DIRTY_DEPTH);'),
    ('depth range: far', S, 'rs_mark(ps3gl.rs.depth_near != (float)n || ps3gl.rs.depth_far != (float)f, PS3GL_DIRTY_VIEWPORT);', 'rs_mark(ps3gl.rs.depth_near != (float)n, PS3GL_DIRTY_VIEWPORT);'),
    ('depth range: near', S, 'rs_mark(ps3gl.rs.depth_near != (float)n || ps3gl.rs.depth_far != (float)f, PS3GL_DIRTY_VIEWPORT);', 'rs_mark(ps3gl.rs.depth_far != (float)f, PS3GL_DIRTY_VIEWPORT);'),
    ('colour mask: alpha', S, 'ps3gl.rs.color_mask_b != b || ps3gl.rs.color_mask_a != a, PS3GL_DIRTY_COLORMASK);', 'ps3gl.rs.color_mask_b != b, PS3GL_DIRTY_COLORMASK);'),
    ('colour mask: red', S, 'rs_mark(ps3gl.rs.color_mask_r != r || ps3gl.rs.color_mask_g != g ||', 'rs_mark(ps3gl.rs.color_mask_g != g ||'),
    ('cull face', S, 'rs_mark(ps3gl.rs.cull_face != face, PS3GL_DIRTY_CULL);', 'rs_mark(0, PS3GL_DIRTY_CULL);'),
    ('front face', S, 'rs_mark(ps3gl.rs.front_face != front, PS3GL_DIRTY_CULL);', 'rs_mark(0, PS3GL_DIRTY_CULL);'),
    ('scissor: the height', S, 'ps3gl.rs.scissor_w != (uint16_t)w || ps3gl.rs.scissor_h != (uint16_t)h, PS3GL_DIRTY_SCISSOR);', 'ps3gl.rs.scissor_w != (uint16_t)w, PS3GL_DIRTY_SCISSOR);'),
    ('scissor: x', S, 'rs_mark(ps3gl.rs.scissor_x != (int16_t)x || ps3gl.rs.scissor_y != (int16_t)rsx_y ||', 'rs_mark(ps3gl.rs.scissor_y != (int16_t)rsx_y ||'),
    ('viewport: y', S, 'rs_mark(ps3gl.rs.vp_x != (int16_t)x || ps3gl.rs.vp_y != (int16_t)rsx_y ||', 'rs_mark(ps3gl.rs.vp_x != (int16_t)x ||'),
    ('viewport: the width', S, 'ps3gl.rs.vp_w != (uint16_t)w || ps3gl.rs.vp_h != (uint16_t)h, PS3GL_DIRTY_VIEWPORT);', 'ps3gl.rs.vp_h != (uint16_t)h, PS3GL_DIRTY_VIEWPORT);'),
    ('shade model', S, 'rs_mark(ps3gl.rs.shade_model != model, PS3GL_DIRTY_SHADE);', 'rs_mark(0, PS3GL_DIRTY_SHADE);'),
    ('polygon offset: the units', S, 'rs_mark(ps3gl.rs.polyoffset_factor != factor || ps3gl.rs.polyoffset_units != units, PS3GL_DIRTY_POLYOFFSET);', 'rs_mark(ps3gl.rs.polyoffset_factor != factor, PS3GL_DIRTY_POLYOFFSET);'),
    ('stencil function: the reference', S, 'rs_mark(ps3gl.rs.stencil_func != f || ps3gl.rs.stencil_ref != (uint32_t)ref || ps3gl.rs.stencil_mask != mask,', 'rs_mark(ps3gl.rs.stencil_func != f || ps3gl.rs.stencil_mask != mask,'),
    ('stencil function: the mask', S, 'rs_mark(ps3gl.rs.stencil_func != f || ps3gl.rs.stencil_ref != (uint32_t)ref || ps3gl.rs.stencil_mask != mask,', 'rs_mark(ps3gl.rs.stencil_func != f || ps3gl.rs.stencil_ref != (uint32_t)ref,'),
    ('stencil write mask', S, 'rs_mark(ps3gl.rs.stencil_writemask != mask, PS3GL_DIRTY_STENCIL);', 'rs_mark(0, PS3GL_DIRTY_STENCIL);'),
    ('stencil op: the pass op', S, 'rs_mark(ps3gl.rs.stencil_fail != f || ps3gl.rs.stencil_zfail != zf || ps3gl.rs.stencil_zpass != zp,', 'rs_mark(ps3gl.rs.stencil_fail != f || ps3gl.rs.stencil_zfail != zf,'),
    ('stencil op: the fail op', S, 'rs_mark(ps3gl.rs.stencil_fail != f || ps3gl.rs.stencil_zfail != zf || ps3gl.rs.stencil_zpass != zp,', 'rs_mark(ps3gl.rs.stencil_zfail != zf || ps3gl.rs.stencil_zpass != zp,'),
    ('bind table: a hit is not checked', T, '(ps3gl.fast & PS3GL_FAST_BIND) && *e && ps3gl.textures[*e - 1].glname == (int)name)', '(ps3gl.fast & PS3GL_FAST_BIND) && *e)'),
    ('texture parameter: a new value is not marked', T, 'if ((ps3gl.fast & PS3GL_FAST_COMPARE) && (!field || *field == value))', 'if (ps3gl.fast & PS3GL_FAST_COMPARE)'),
    ('texture parameter: the compare with the rows OFF', T, 'if ((ps3gl.fast & PS3GL_FAST_COMPARE) && (!field || *field == value))', 'if (!field || *field == value)'),
    ('texture parameter: a name with no field is read', T, 'if ((ps3gl.fast & PS3GL_FAST_COMPARE) && (!field || *field == value))', 'if ((ps3gl.fast & PS3GL_FAST_COMPARE) && *field == value)'),
    ('unit words: held with no record', T, 'const int held = (ps3gl.fast & PS3GL_FAST_COMPARE) && tmu->sent;', 'const int held = (ps3gl.fast & PS3GL_FAST_COMPARE) != 0;'),
    ('unit words: the control word is not sent again', T, 'if (!held || ctrl != tmu->sent_ctrl)', 'if (!held)'),
    ('unit words: the filter word is not sent again', T, 'if (!held || filter != tmu->sent_filter)', 'if (!held)'),
    ('unit words: the wrap word is not sent again', T, 'if (!held || wrap != tmu->sent_wrap)', 'if (!held)'),
    ('unit words: the control record stays', T, '            tmu->sent_ctrl   = ctrl;\n', ''),
    ('unit words: the filter record stays', T, '            tmu->sent_filter = filter;\n', ''),
    ('unit words: the wrap record stays', T, '            tmu->sent_wrap   = wrap;\n', ''),
    ('unit words: a unit turned off keeps its control record', T, '                tmu->sent_ctrl = ~0u;\n', ''),
    ('unit words: a unit turned off drops its whole record', T, '                tmu->sent_ctrl = ~0u;\n', '                tmu->sent = 0;\n'),
    ('unit words: the control word without the mip range', T, 'ctrl = ((uint32_t)minlod << 16) | ((uint32_t)maxlod << 4) | t->max_aniso;', 'ctrl = t->max_aniso;'),
    ('unit words: the control word without the anisotropy', T, 'ctrl = ((uint32_t)minlod << 16) | ((uint32_t)maxlod << 4) | t->max_aniso;', 'ctrl = ((uint32_t)minlod << 16) | ((uint32_t)maxlod << 4);'),
    ('unit words: the filter word without mag', T, 'const uint32_t filter = ((uint32_t)t->min_filter << 8) | t->mag_filter;', 'const uint32_t filter = (uint32_t)t->min_filter << 8;'),
    ('unit words: the filter word without min', T, 'const uint32_t filter = ((uint32_t)t->min_filter << 8) | t->mag_filter;', 'const uint32_t filter = t->mag_filter;'),
    ('unit words: the wrap word without t', T, 'const uint32_t wrap   = ((uint32_t)t->wrap_s << 8) | t->wrap_t;', 'const uint32_t wrap   = (uint32_t)t->wrap_s << 8;'),
    ('unit words: the wrap word without s', T, 'const uint32_t wrap   = ((uint32_t)t->wrap_s << 8) | t->wrap_t;', 'const uint32_t wrap   = t->wrap_t;'),
    ('attribute binds: another block', V, 'if (ps3gl.vattr_valid && ps3gl.vattr_off == vb_offset && ps3gl.vattr_stride == stride &&', 'if (ps3gl.vattr_valid && ps3gl.vattr_stride == stride &&'),
    ('attribute binds: another stride', V, 'if (ps3gl.vattr_valid && ps3gl.vattr_off == vb_offset && ps3gl.vattr_stride == stride &&', 'if (ps3gl.vattr_valid && ps3gl.vattr_off == vb_offset &&'),
    ('attribute binds: another colour shift', V, 'ps3gl.vattr_shift == color_shift && ps3gl.vattr_tc1_u8 == tc1_u8)', 'ps3gl.vattr_tc1_u8 == tc1_u8)'),
    ('attribute binds: another texcoord1 form', V, 'ps3gl.vattr_shift == color_shift && ps3gl.vattr_tc1_u8 == tc1_u8)', 'ps3gl.vattr_shift == color_shift)'),
    ('attribute binds: a record that is not known', V, 'if (ps3gl.vattr_valid && ps3gl.vattr_off == vb_offset && ps3gl.vattr_stride == stride &&', 'if (ps3gl.vattr_off == vb_offset && ps3gl.vattr_stride == stride &&'),
    ('attribute binds: the block is not recorded', V, '        ps3gl.vattr_off    = vb_offset;\n', ''),
    ('attribute binds: the shift is not recorded', V, '        ps3gl.vattr_shift  = color_shift;\n', ''),
    ('attribute binds: the compare with the rows OFF', V, '    if (ps3gl.fast & PS3GL_FAST_COMPARE) {\n        if (ps3gl.vattr_valid', '    if (1) {\n        if (ps3gl.vattr_valid'),
    ('glDrawElements keeps the attribute record', D, '    ps3gl.vattr_valid = 0;\n\n    /* Any buffer-backed array', '\n    /* Any buffer-backed array'),
    ('a new frame keeps the unit words record', M, '        ps3gl.tmu[i].dirty = 1;\n        ps3gl.tmu[i].sent = 0;\n    }\n}\n', '        ps3gl.tmu[i].dirty = 1;\n    }\n}\n'),
    ('a change of the rows keeps the unit words record', M, '    for (int i = 0; i < PS3GL_MAX_TMUS; i++)\n        ps3gl.tmu[i].sent = 0;\n}', '}'),
    ('a change of the rows keeps the attribute record', M, '    ps3gl.fast = bits;\n    ps3gl.vattr_valid = 0;\n', '    ps3gl.fast = bits;\n'),
    # Tex Recycle: new names. Left out as equivalent: the scan from the hint with the bit OFF (the hint is always right),
    # the reset at init or at shutdown (each one covers the other).
    ('new names: the hint skips a free entry', T, '            g_free_hint = i + 1;\n', '            g_free_hint = i + 2;\n'),
    ('new names: a delete keeps the hint', T, '            if (t - ps3gl.textures < g_free_hint)\n                g_free_hint = (int)(t - ps3gl.textures);\n', ''),
    ('new names: no entry at glGenTextures', T, '        if (ps3gl.fast & PS3GL_FAST_GEN)\n            ps3gl_texture_alloc(textures[i]);\n', ''),
    ('new names: an entry at glGenTextures with the bit OFF', T, '        if (ps3gl.fast & PS3GL_FAST_GEN)\n            ps3gl_texture_alloc(textures[i]);\n', '        ps3gl_texture_alloc(textures[i]);\n'),
    ('Tex Recycle ON by default', P, 'int g_tex_recycle_preset   = 0;', 'int g_tex_recycle_preset   = 1;'),
    ('Tex Recycle is not saved', P, '\t{ "TexRecycle",    &g_tex_recycle_preset },\n\t{ "GlCompare"', '\t{ "GlCompare"'),
    ('Tex Recycle is not live', N, '\t&g_tex_recycle_preset,\n\t&g_exit_save_preset,', '\t&g_exit_save_preset,'),
    ('the renderer leaves out new names', R, ' |\n\t                (g_tex_recycle_preset ? PS3GL_FAST_GEN : 0));', ');'),
    ('Tex Bind Table OFF by default', P, 'int g_tex_bind_table_preset = 1;', 'int g_tex_bind_table_preset = 0;'),
    ('GL State Compare OFF by default', P, 'int g_gl_compare_preset    = 1;', 'int g_gl_compare_preset    = 0;'),
    ('TR First Pass SKIP by default', P, 'int g_tr_first_pass_preset = 2;', 'int g_tr_first_pass_preset = 1;'),
    ('GL State Compare is not saved', P, '\t{ "GlCompare",     &g_gl_compare_preset },\n\t{ "TrFirstPass",   &g_tr_first_pass_preset },\n\t{ "MmuBits"',
     '\t{ "TrFirstPass",   &g_tr_first_pass_preset },\n\t{ "MmuBits"'),
    ('TR First Pass is not live', N, '\t&g_tex_bind_table_preset, &g_gl_compare_preset, &g_tr_first_pass_preset,\n',
     '\t&g_tex_bind_table_preset, &g_gl_compare_preset,\n'),
    ('the DRAW COST page is not in the menu', N, '\t{ "DRAW COST",     s_cost,  (int)(sizeof(s_cost) / sizeof(s_cost[0])) },\n', ''),
    ('no [costcfg] line at boot', N, '\tps3_costcfg_print("boot", true);\n', ''),
    ('the renderer does not set the state path', R,
     '\tps3gl_fast_path((g_tex_bind_table_preset ? PS3GL_FAST_BIND : 0) | (g_gl_compare_preset ? PS3GL_FAST_COMPARE : 0) |\n'
     '\t                (g_tex_recycle_preset ? PS3GL_FAST_GEN : 0));\n', ''),
    ('the two rows change places', R, '(g_tex_bind_table_preset ? PS3GL_FAST_BIND : 0) | (g_gl_compare_preset ? PS3GL_FAST_COMPARE : 0)',
     '(g_tex_bind_table_preset ? PS3GL_FAST_COMPARE : 0) | (g_gl_compare_preset ? PS3GL_FAST_BIND : 0)'),
    ('TR First Pass is not latched', R, '\ts_do.first_pass = (u8)g_tr_first_pass_preset;\n', ''),
]

# The reference binary, and proof that the tree passes.
r = subprocess.run(['sh', 'misc/glpath_check/run.sh', SEEDS, FRAMES], env=dict(os.environ, GLP_OUT=REF),
                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
if r.returncode != 0:
    print('the tree does not pass:\n' + r.stdout.decode('utf-8', 'replace')[-600:])
    sys.exit(1)

killed = survived = 0
for k, (name, f, old, new) in enumerate(MUTANTS):
    shutil.rmtree(TMP, ignore_errors=True)
    os.makedirs(TMP + '/src')
    path, dst = ('ps3/' + f, f) if f.endswith('.cpp') else ('ps3/ps3gl/ps3gl_%s.c' % f, 'ps3gl_%s.c' % f)
    src = open(path, 'rb').read().decode('utf-8').replace('\r\n', '\n')
    if src.count(old) != 1:
        print('NOT FOUND (%d): %s' % (src.count(old), name), flush=True)
        survived += 1
        continue
    open('%s/src/%s' % (TMP, dst), 'w').write(src.replace(old, new))
    env = dict(os.environ, GLP_SRC=TMP + '/src', GLP_OUT=TMP + '/out', GLP_REFBIN=REF + '/glpath_ref', QUICK='1')
    r = subprocess.run(['sh', 'misc/glpath_check/run.sh', SEEDS, FRAMES], env=env, stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT)
    out = r.stdout.decode('utf-8', 'replace')
    if r.returncode == 0:
        print('SURVIVED: %s' % name, flush=True)
        survived += 1
    else:
        why = [l for l in out.split('\n') if 'FAIL' in l or 'error' in l or 'ERROR' in l]
        print('killed: %s: %s' % (name, why[0][:130] if why else 'exit %d' % r.returncode), flush=True)
        killed += 1
print('%d of %d planted bugs fail the check' % (killed, killed + survived), flush=True)
sys.exit(1 if survived else 0)
