#!/usr/bin/env python3
# Bundle 3 (WSL): plants one bug at a time in a copy of ps3_tex.cpp (T) or ps3gl_textures.c (G); run.sh must FAIL on
# each. Args: trace seeds (3), trace epochs (8), model seeds (3), model epochs (30), parallel runs (6), name filter.
import os, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
SRC = {'T': 'ps3/ps3_tex.cpp', 'G': 'ps3/ps3gl/ps3gl_textures.c'}
ENV = {'T': 'TEX3_TEX', 'G': 'TEX3_GL'}
TMP = '/tmp/tex3mut'

# Left out as equivalent: a chain on a mirrored texture (ps3_tex.cpp and ps3gl each refuse it), the lower levels'
# black/alpha-0 counts (log only), build_gcm_texture() in the direct level end (glTexImage2D levels always follow).
MUTANTS = [
    # P25 VQ Formats: describe()
    ('ON: no VQ on palette textures', 'T', 'd->vq = tcw.NO_PAL.VQ_Comp && (s_vqfmt > 0 || !pal);', 'd->vq = tcw.NO_PAL.VQ_Comp && !pal;'),
    ('OFF: VQ on palette textures', 'T', 'd->vq = tcw.NO_PAL.VQ_Comp && (s_vqfmt > 0 || !pal);', 'd->vq = tcw.NO_PAL.VQ_Comp;'),
    ('ON: scan-order VQ stays twiddled', 'T', '(d->vq && (s_vqfmt <= 0 || tcw.NO_PAL.MipMapped))', 'd->vq'),
    ('OFF: scan-order VQ', 'T', '(d->vq && (s_vqfmt <= 0 || tcw.NO_PAL.MipMapped))', '(d->vq && tcw.NO_PAL.MipMapped)'),
    ('ON: mipmapped VQ in scan order', 'T', '(d->vq && (s_vqfmt <= 0 || tcw.NO_PAL.MipMapped))', '(d->vq && s_vqfmt <= 0)'),
    ('scan-order VQ hash span by the width', 'T', 'd->src_bytes = d->twiddled ? d->w * d->h / 4 : d->pitch * d->h / 4;',
     'd->src_bytes = d->w * d->h / 16;'),
    # P25: decode()
    ('palette VQ: 4-bit index bytes as 8-bit', 'T', 'u32 a = d.pal4 ? t >> 1 : t;', 'u32 a = t;'),
    ('palette VQ: nibble order', 'T', '(b >> ((t & 1) * 4)) & 15 : b));', '(b >> ((~t & 1) * 4)) & 15 : b));'),
    ('palette VQ: entry of 4 bytes', 'T', 'vram8(d.addr + (a >> 3)) * 8 + (a & 7)', 'vram8(d.addr + (a >> 3)) * 8 + (a & 3)'),
    ('palette VQ: index map a block off', 'T', 'vram8(d.addr + (a >> 3)) * 8 + (a & 7)', 'vram8(d.addr + (a >> 3) + 1) * 8 + (a & 7)'),
    ('scan-order VQ: rows a width apart', 'T', 'u32 row = d.addr + y * (d.pitch / 4);', 'u32 row = d.addr + y * (w / 4);'),
    ('scan-order VQ: texel order', 'T', 'o[k] = cvt16(d.fmt, vram16(e + k * 2));', 'o[k] = cvt16(d.fmt, vram16(e + (3 - k) * 2));'),
    ('scan-order VQ: second YUV pair', 'T', 'yuv_pair(vram16(e + 4), vram16(e + 6), o + 2);', 'yuv_pair(vram16(e), vram16(e + 2), o + 2);'),
    ('scan-order VQ: tail not cleared', 'T', 'o[k] = cvt16(d.fmt, vram16(e + k * 2));\n\t\t\t}\n\t\t\tfor (u32 x = cols; x < w; x++)',
     'o[k] = cvt16(d.fmt, vram16(e + k * 2));\n\t\t\t}\n\t\t\tfor (u32 x = w; x < w; x++)'),
    # P25: the SPUs and the P42 decoder do not know the two forms
    ('SPU hash on the VQ forms', 'T', 'const bool spu_ok = !vq_odd(d);', 'const bool spu_ok = true;'),
    ('SPU decode on the VQ forms', 'T', 'd.w >= 8 && d.h >= 8 && !take11 && !vq_odd(d);', 'd.w >= 8 && d.h >= 8 && !take11;'),
    ('scan-order VQ is not a VQ form', 'T', 'return d.vq && (d.pal4 || d.pal8 || !d.twiddled);', 'return d.vq && (d.pal4 || d.pal8);'),
    # P45: the levels
    ('VQ levels from the top level', 'T', 'm->addr = d.vq_cb + 2048 + vq_mip_bytes[i];', 'm->addr = d.addr;'),
    ('palette VQ levels by the palette rule', 'T', '\tif (d.vq)\n\t{\n\t\tm->addr = d.vq_cb', '\tif (d.vq && !d.pal4 && !d.pal8)\n\t{\n\t\tm->addr = d.vq_cb'),
    ('palette levels at texel offsets', 'T', 'mip_texels[i] * bits / 8;', 'mip_texels[i];'),
    ('16-bit levels at byte offsets', 'T', 'm->addr = d.base + mip_texels[i] * 2;', 'm->addr = d.base + mip_texels[i];'),
    ('VQ 1x1 level is texel (0,0)', 'T', 's_out[0] = s_out[3];', 's_out[0] = s_out[0];'),
    ('YUV VQ 1x1 from the 1x1 block', 'T', 'm->addr = d.vq_cb + 2048 + vq_mip_bytes[1];', 'm->addr = d.vq_cb + 2048 + vq_mip_bytes[0];'),
    ('YUV 1x1 level as YUV', 'T', '\t\t\tm->fmt = 1;', '\t\t\tm->fmt = 3;'),
    ('VQ 1x1 uploads its 2x2 block', 'T', 'const GLsizei w = take11 ? 1 : (GLsizei)d.w,', 'const GLsizei w = (GLsizei)d.w,'),
    ('levels one size up', 'T', 'level_desc(d, d.lvl - level, &m, &take11);', 'level_desc(d, d.lvl - level + 1, &m, &take11);'),
    ('the smallest level left out', 'T', 'level < levels && level <= d.lvl; level++)', 'level < levels && level < d.lvl; level++)'),
    ('IgnoreTexA lost on the levels', 'T', 'upload(m, ignore_alpha, level, take11);', 'upload(m, false, level, take11);'),
    ('bump levels go to level 0', 'T', 'upload(b, false, level, take11);', 'upload(b, false, 0, take11);'),
    ('small levels to the SPU', 'T', 'const bool fast = d.w >= 8 && d.h >= 8 && !take11', 'const bool fast = !take11'),
    ('swizzled YUV to the SPU', 'T', 'return !(swizzled && d.fmt == 3 && d.twiddled && !d.vq);', 'return true;'),
    ('SPU level at level 0', 'T', 'level ? ps3gl_tex_direct_level(level) : ps3gl_tex_direct_level0(d.w, d.h, &pitch, &swz);\n\tif (!dst',
     'ps3gl_tex_direct_level0(d.w, d.h, &pitch, &swz);\n\tif (!dst'),
    ('SPU level not counted', 'T', '\t\tps3gl_tex_direct_level_end(level);\n\t\treturn true;', '\t\treturn true;'),
    ('P42 decoder on a lower level', 'T', 'else if (d.fmt == 3 && fast && level == 0 && upload_yuv_block(d))', 'else if (d.fmt == 3 && fast && upload_yuv_block(d))'),
    # P45: which textures, and when
    ('OFF: chains', 'T', 'const bool chain = s_mips > 0 && d.mip && !mirror;', 'const bool chain = d.mip && !mirror;'),
    ('a chain on any twiddled texture', 'T', 'const bool chain = s_mips > 0 && d.mip && !mirror;', 'const bool chain = s_mips > 0 && d.twiddled && !mirror;'),
    ('a YUV chain keeps the linear hint', 'T', '\t\t\tps3gl_texture_hint_mipmaps();\n\t\telse if (!spare && d.fmt == 3)',
     '\t\t\tps3gl_texture_hint_mipmaps();\n\t\tif (!spare && d.fmt == 3)'),
    ('chain not decoded on a change', 'T', '\t\t\tif (hit->chain)\n\t\t\t\tupload_chain(d, tsp.IgnoreTexA);\n', ''),
    ('new texture without its levels', 'T', '\t\tif (chain)\n\t\t\tupload_chain(d, tsp.IgnoreTexA);\n', ''),
    ('no cache clear on a switch', 'T', 'if (mips != s_mips || vqfmt != s_vqfmt)\n\t{\n\t\tps3tex_reset();', 'if (mips != s_mips || vqfmt != s_vqfmt)\n\t{'),
    # P45, P52: the sampler
    ('punch-through takes the mip filter', 'T', 'hit->chain && !punch_through', 'hit->chain'),
    ('mip filter never set', 'T', 'set_tex_params(tsp, mirror_u, mirror_v, hit->chain && !punch_through);', 'set_tex_params(tsp, mirror_u, mirror_v, false);'),
    ('mip filter on a nearest texture', 'T', 'if (chain && !nearest)', 'if (chain)'),
    ('anisotropic keeps the bilinear mip filter', 'T', 's_aniso > 0 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR_MIPMAP_NEAREST;', 'GL_LINEAR_MIPMAP_NEAREST;'),
    ('trilinear without anisotropic', 'T', 's_aniso > 0 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR_MIPMAP_NEAREST;', 'GL_LINEAR_MIPMAP_LINEAR;'),
    ('Texture Filter NEAREST ignored', 'T', 'const bool nearest = s_filter == 1 || (s_filter == 0 && !tsp.FilterMode);',
     'const bool nearest = s_filter == 0 && !tsp.FilterMode;'),
    ('Texture Filter LINEAR ignored', 'T', 'const bool nearest = s_filter == 1 || (s_filter == 0 && !tsp.FilterMode);',
     'const bool nearest = s_filter == 1 || !tsp.FilterMode;'),
    ('OFF: filter forced', 'T', 'const bool nearest = s_filter == 1 || (s_filter == 0 && !tsp.FilterMode);', 'const bool nearest = false;'),
    ('magnification stays linear', 'T', 'GL_TEXTURE_MAG_FILTER, nearest ? GL_NEAREST : GL_LINEAR);', 'GL_TEXTURE_MAG_FILTER, GL_LINEAR);'),
    ('anisotropic on a nearest texture', 'T', 'nearest ? 1 : k_aniso[s_aniso]);', 'k_aniso[s_aniso]);'),
    ('anisotropic 16x as 8x', 'T', 'k_aniso[5] = { 1, 2, 4, 8, 16 };', 'k_aniso[5] = { 1, 2, 4, 8, 8 };'),
    ('anisotropic 2x as 4x', 'T', 'k_aniso[5] = { 1, 2, 4, 8, 16 };', 'k_aniso[5] = { 1, 4, 4, 8, 16 };'),
    ('anisotropic preset not latched', 'T', '\t\ts_aniso = aniso;\n', ''),
    # Tex Recycle (Garou). Left out as equivalent or memory only: a spare given the hints again, a chain texture kept as
    # a spare (no new texture can take it), the age-out and the byte limit.
    ('recycle: a name taken in the next frame', 'T', '#define SPARE_WAIT 2', '#define SPARE_WAIT 1'),
    ('recycle: any layout', 'T', ' && s_spare[i].kind == kind)', ')'),
    ('recycle: a taken spare stays in the list', 'T', '\t\t\tspare_remove(i, false);\n', ''),
    ('recycle: the entry keeps the name', 'T', '\te.name = 0;\n\ts_live--;\n\ts_spared++;', '\ts_live--;\n\ts_spared++;'),
    ('recycle: OFF keeps spares', 'T', 'if (s_recycle > 0 && victim->kind < 4)', 'if (victim->kind < 4)'),
    ('recycle: the layout without the mirror', 'T', 'const u8 kind = (u8)(mirror | ((d.fmt == 3) << 1) | (chain << 2));',
     'const u8 kind = (u8)(((d.fmt == 3) << 1) | (chain << 2));'),
    ('recycle: the layout without YUV', 'T', 'const u8 kind = (u8)(mirror | ((d.fmt == 3) << 1) | (chain << 2));',
     'const u8 kind = (u8)(mirror | (chain << 2));'),
    # ps3gl
    ('hint ignored', 'G', '(t->mip_hint && t->swizzled)', '(0)'),
    ('hint on every texture', 'G', 'if (t) t->mip_hint = 1;', 'if (t) t->mip_hint = 1;\n    g_mipmap_enabled = 1;'),
    ('chain of one level less', 'G', '? mip_num_levels((uint32_t)width, (uint32_t)height)\n', '? mip_num_levels((uint32_t)width, (uint32_t)height) - 1\n'),
    ('chain levels reads 1', 'G', 'return t->alloc_levels ? t->alloc_levels : 1;', 'return 1;'),
    ('direct level by the linear offsets', 'G', 'return t->data + tex_level_offset(t->width, t->height, level, 1);',
     'return t->data + tex_level_offset(t->width, t->height, level, 0);'),
    ('direct level one level off', 'G', 'return t->data + tex_level_offset(t->width, t->height, level, 1);',
     'return t->data + tex_level_offset(t->width, t->height, level + 1, 1);'),
    ('direct level end does not count', 'G', 'if (level >= 1 && level == t->num_levels && t->num_levels < 15) {\n        t->num_levels = (uint8_t)(level + 1);',
     'if (level >= 1 && level == t->num_levels && t->num_levels < 15) {\n        t->num_levels = (uint8_t)level;'),
    ('direct level end counts no upload', 'G', '        t->num_levels = (uint8_t)(level + 1);\n        ps3gl_frame_mip_uploads++;\n    }\n    build_gcm_texture(t);\n    t->dirty = 1;\n    t->content_dirty = 1;\n}\n\n/* 5.12 #4 CHECK',
     '        t->num_levels = (uint8_t)(level + 1);\n    }\n    build_gcm_texture(t);\n    t->dirty = 1;\n    t->content_dirty = 1;\n}\n\n/* 5.12 #4 CHECK'),
    ('anisotropic not sent', 'G', 'rsxTextureControl(ctx, i, GCM_TRUE, minlod, maxlod, t->max_aniso);',
     'rsxTextureControl(ctx, i, GCM_TRUE, minlod, maxlod, GCM_TEXTURE_MAX_ANISO_1);'),
    ('anisotropic parameter dropped', 'G', 'case GL_TEXTURE_MAX_ANISOTROPY_EXT: field = &t->max_aniso; value = gl_to_gcm_aniso(param); break;',
     'case GL_TEXTURE_MAX_ANISOTROPY_EXT: break;'),
    ('16 taps as 12', 'G', 'if (param >= 16) return GCM_TEXTURE_MAX_ANISO_16;', 'if (param >= 16) return GCM_TEXTURE_MAX_ANISO_12;'),
    ('8 taps as 6', 'G', 'if (param >= 8)  return GCM_TEXTURE_MAX_ANISO_8;', 'if (param >= 8)  return GCM_TEXTURE_MAX_ANISO_6;'),
    ('4 taps as 2', 'G', 'if (param >= 4)  return GCM_TEXTURE_MAX_ANISO_4;', 'if (param >= 5)  return GCM_TEXTURE_MAX_ANISO_4;'),
    ('2 taps as 1', 'G', 'if (param >= 2)  return GCM_TEXTURE_MAX_ANISO_2;', 'if (param >= 3)  return GCM_TEXTURE_MAX_ANISO_2;'),
]


def run_one(k, which, text, args, refbin):
    d = os.path.join(TMP, str(k))
    os.makedirs(d, exist_ok=True)
    env = dict(os.environ, TEX3_OUT=os.path.join(d, 'out'), QUICK='1')
    if which:
        f = os.path.join(d, os.path.basename(SRC[which]))
        open(f, 'w', encoding='utf-8').write(text)
        env[ENV[which]] = f
    if refbin:
        env['TEX3_REFBIN'] = refbin
    try:
        r = subprocess.run(['sh', os.path.join(ROOT, 'misc/tex3_check/run.sh')] + args, capture_output=True, text=True,
                           env=env, timeout=1500)
    except subprocess.TimeoutExpired:
        subprocess.run(['pkill', '-f', os.path.join(d, 'out', 'tex3_new')])
        return True, 'hang (25 min)'
    out = [l for l in (r.stdout + r.stderr).splitlines() if 'FAIL' in l or 'ERROR' in l or 'error' in l]
    return r.returncode != 0, (out[0] if out else '')[:150]


def main():
    args = [sys.argv[i] if len(sys.argv) > i else v for i, v in ((1, '3'), (2, '8'), (3, '3'), (4, '30'))]
    jobs = int(sys.argv[5]) if len(sys.argv) > 5 else 6
    only = sys.argv[6] if len(sys.argv) > 6 else ''
    src = {w: open(os.path.join(ROOT, p), encoding='utf-8').read().replace('\r\n', '\n') for w, p in SRC.items()}
    for name, which, find, repl in MUTANTS:
        if src[which].count(find) != 1:
            print(f'BAD MUTANT (pattern found {src[which].count(find)} times): {name}')
            sys.exit(2)
    # The unchanged files must pass, or every mutant would count as caught. Its reference binary serves the mutants.
    bad, line = run_one('base', None, None, args, None)
    if bad:
        print('BASELINE FAILS: run.sh fails on the unchanged files: ' + line)
        sys.exit(2)
    refbin = os.path.join(TMP, 'base', 'out', 'tex3_ref')
    todo = [m for m in MUTANTS if only in m[0]]
    with ThreadPoolExecutor(jobs) as ex:
        res = list(ex.map(lambda km: run_one(km[0], km[1][1], src[km[1][1]].replace(km[1][2], km[1][3]), args, refbin),
                          enumerate(todo)))
    caught = 0
    for (name, which, _, _), (ok, line) in zip(todo, res):
        caught += ok
        print(f"{'caught' if ok else 'MISSED'}: {SRC[which].split('/')[-1]}: {name}: {line}")
    print(f'{caught} of {len(todo)} mutants fail run.sh')
    sys.exit(0 if caught == len(todo) else 1)


main()
