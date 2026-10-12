/* ps3gl_shaders.c -- GL-to-RSX layer: shader program management. One shared vertex
 * program, one fragment program per texenv mode. If PS3GL_SHADERS_AVAILABLE==0 the shaders weren't compiled -- every draw is a silent no-op, nothing renders. */

#include "ps3gl.h"
#include "ps3gl_shader_data.h"
#include "ps3gl_shader_bump.h"
#include "ps3gl_shader_fog.h"
#include <stdio.h>

/* Forward declarations */

#if PS3GL_SHADERS_AVAILABLE

/* Shader loading helpers */

static void load_vp(ps3gl_shader_t *s, const void *data, uint32_t size)
{
    s->vp = (rsxVertexProgram *)data;
    rsxVertexProgramGetUCode(s->vp, &s->vp_ucode, &s->vp_ucode_size);
}

static void load_fp(ps3gl_shader_t *s, const void *data, uint32_t size)
{
    s->fp = (rsxFragmentProgram *)data;

    void *ucode;
    rsxFragmentProgramGetUCode(s->fp, &ucode, &s->fp_ucode_size);

    /* Fragment program ucode must reside in RSX-accessible memory */
    s->fp_ucode = rsxMemalign(64, s->fp_ucode_size);
    if (s->fp_ucode) {
        memcpy(s->fp_ucode, ucode, s->fp_ucode_size);
        rsxAddressToOffset(s->fp_ucode, &s->fp_offset);
    }
}

void ps3gl_shaders_init(void)
{
    ps3gl_log("[ps3gl] Loading compiled shaders...\n");

    /* All shader slots share the same vertex program */
    for (int i = 0; i < PS3GL_TENV_COUNT; i++) {
        memset(&ps3gl.shaders[i], 0, sizeof(ps3gl_shader_t));
        load_vp(&ps3gl.shaders[i], shader_vp_data, shader_vp_data_size);
        ps3gl.shaders[i].mvp_const = rsxVertexProgramGetConst(ps3gl.shaders[i].vp, "mvp");
    }

    /* Load per-mode fragment programs */
    load_fp(&ps3gl.shaders[PS3GL_TENV_DISABLED], shader_fp_coloronly_data, shader_fp_coloronly_data_size);
    load_fp(&ps3gl.shaders[PS3GL_TENV_MODULATE], shader_fp_modulate_data, shader_fp_modulate_data_size);
    load_fp(&ps3gl.shaders[PS3GL_TENV_REPLACE],  shader_fp_replace_data,  shader_fp_replace_data_size);
    load_fp(&ps3gl.shaders[PS3GL_TENV_DECAL],    shader_fp_decal_data,    shader_fp_decal_data_size);
    load_fp(&ps3gl.shaders[PS3GL_TENV_ADD],       shader_fp_add_data,      shader_fp_add_data_size);
    load_fp(&ps3gl.shaders[PS3GL_TENV_BLEND],     shader_fp_blend_data,    shader_fp_blend_data_size);
    load_fp(&ps3gl.shaders[PS3GL_TENV_MODULATE2], shader_fp_modulate2_data, shader_fp_modulate2_data_size);

    /* NotDC 3j: the bump programs have their own VP, which passes texcoord1 as 4 components. */
    for (int i = PS3GL_TENV_BUMP_REPLACE; i <= PS3GL_TENV_BUMP_DECAL; i++) {
        load_vp(&ps3gl.shaders[i], shader_vp_bump_data, shader_vp_bump_data_size);
        ps3gl.shaders[i].mvp_const = rsxVertexProgramGetConst(ps3gl.shaders[i].vp, "mvp");
    }
    load_fp(&ps3gl.shaders[PS3GL_TENV_BUMP_REPLACE],  shader_fp_bump_replace_data,  shader_fp_bump_replace_data_size);
    load_fp(&ps3gl.shaders[PS3GL_TENV_BUMP_MODULATE], shader_fp_bump_modulate_data, shader_fp_bump_modulate_data_size);
    load_fp(&ps3gl.shaders[PS3GL_TENV_BUMP_DECAL],    shader_fp_bump_decal_data,    shader_fp_bump_decal_data_size);

    /* NotDC 3a: the fog programs, in key order; their VP also gives texcoord0.z = position.z * fogp.x. */
    static const struct { const unsigned char *data; unsigned int size; } fog_fp[] = {
#define FOG_FP(n) { shader_fp_##n##_data, sizeof(shader_fp_##n##_data) }
        FOG_FP(fogt_coloronly), FOG_FP(fogt_modulate), FOG_FP(fogt_replace), FOG_FP(fogt_decal),
        FOG_FP(fogt_bump_replace), FOG_FP(fogt_bump_modulate), FOG_FP(fogt_bump_decal),
        FOG_FP(fogv_modulate), FOG_FP(fogv_replace), FOG_FP(fogv_decal),
        FOG_FP(fogb_coloronly), FOG_FP(fogb_modulate), FOG_FP(fogb_modulate_ta), FOG_FP(fogb_decal),
#undef FOG_FP
    };
    for (int i = PS3GL_TENV_FOGT_DISABLED; i < PS3GL_TENV_COUNT; i++) {
        load_vp(&ps3gl.shaders[i], shader_vp_fog_data, shader_vp_fog_data_size);
        ps3gl.shaders[i].mvp_const = rsxVertexProgramGetConst(ps3gl.shaders[i].vp, "mvp");
        ps3gl.shaders[i].fog_const = rsxVertexProgramGetConst(ps3gl.shaders[i].vp, "fogp");
        load_fp(&ps3gl.shaders[i], fog_fp[i - PS3GL_TENV_FOGT_DISABLED].data, fog_fp[i - PS3GL_TENV_FOGT_DISABLED].size);
    }

    ps3gl.active_shader = -1;
    ps3gl.active_vp = NULL;
    ps3gl.mvp_uploaded_gen = 0;
    ps3gl_log("[ps3gl] Shaders loaded: %d modes\n", PS3GL_TENV_COUNT);
}

void ps3gl_shaders_shutdown(void)
{
    for (int i = 0; i < PS3GL_TENV_COUNT; i++) {
        if (ps3gl.shaders[i].fp_ucode) {
            rsxFree(ps3gl.shaders[i].fp_ucode);
            ps3gl.shaders[i].fp_ucode = NULL;
        }
    }
}

/* NotDC 3a: the fog program of a texenv key under a ps3gl_dc_fog() mode, -1 if there is none. */
static int fog_key(int mode, int tenv, int bump)
{
    switch (mode) {
    case PS3GL_FOG_TABLE:
        if (!bump)
            return tenv <= PS3GL_TENV_DECAL ? PS3GL_TENV_FOGT_DISABLED + tenv : -1;
        return tenv == PS3GL_TENV_REPLACE ? PS3GL_TENV_FOGT_BUMP_REPLACE :
               tenv == PS3GL_TENV_MODULATE ? PS3GL_TENV_FOGT_BUMP_MODULATE :
               tenv == PS3GL_TENV_DECAL ? PS3GL_TENV_FOGT_BUMP_DECAL : -1;
    case PS3GL_FOG_VERTEX:
        return !bump && tenv >= PS3GL_TENV_MODULATE && tenv <= PS3GL_TENV_DECAL
               ? PS3GL_TENV_FOGV_MODULATE + tenv - PS3GL_TENV_MODULATE : -1;
    case PS3GL_FOG_BASE:
        return bump ? -1 : tenv == PS3GL_TENV_DISABLED ? PS3GL_TENV_FOGB_DISABLED :
               tenv == PS3GL_TENV_MODULATE ? PS3GL_TENV_FOGB_MODULATE :
               tenv == PS3GL_TENV_DECAL ? PS3GL_TENV_FOGB_DECAL : -1;
    case PS3GL_FOG_BASE_TA:
        return !bump && tenv == PS3GL_TENV_MODULATE ? PS3GL_TENV_FOGB_MODULATE_TA : -1;
    default:
        return -1;
    }
}

int ps3gl_shader_key(void)
{
    int tmu0 = ps3gl.tmu[0].enabled && ps3gl.tmu[0].bound && ps3gl.tmu[0].bound->data;
    int tmu1 = ps3gl.tmu[1].enabled && !ps3gl.tmu[1].aux && ps3gl.tmu[1].bound && ps3gl.tmu[1].bound->data;

    if (ps3gl.dc_fog) {
        int key = fog_key(ps3gl.dc_fog, tmu0 ? ps3gl.tmu[0].texenv : PS3GL_TENV_DISABLED,
                          tmu0 && ps3gl.tmu[0].bump);
        if (key >= 0)
            return key;
    }

    /* Both TMUs active: use dual-texture modulate (diffuse * lightmap). */
    if (tmu0 && tmu1)
        return PS3GL_TENV_MODULATE2;

    if (tmu0 && ps3gl.tmu[0].bump) {
        switch (ps3gl.tmu[0].texenv) {
        case PS3GL_TENV_REPLACE:  return PS3GL_TENV_BUMP_REPLACE;
        case PS3GL_TENV_MODULATE: return PS3GL_TENV_BUMP_MODULATE;
        case PS3GL_TENV_DECAL:    return PS3GL_TENV_BUMP_DECAL;
        default:                  break;
        }
    }

    if (tmu0)
        return ps3gl.tmu[0].texenv;

    return PS3GL_TENV_DISABLED;
}

void ps3gl_tex_bump(int on)
{
    ps3gl.tmu[ps3gl.active_tmu].bump = on != 0;
}

void ps3gl_dc_fog(int mode)
{
    ps3gl.dc_fog = mode;
}

void ps3gl_dc_fog_scale(float scale)
{
    ps3gl.dc_fog_scale = scale;
}

void ps3gl_tex_aux(int on)
{
    ps3gl.tmu[ps3gl.active_tmu].aux = on != 0;
}

void ps3gl_apply_shader(void)
{
    int key = ps3gl_shader_key();
    if (key < 0 || key >= PS3GL_TENV_COUNT) key = PS3GL_TENV_DISABLED;

    ps3gl_shader_t *s = &ps3gl.shaders[key];
    gcmContextData *ctx = ps3gl_get_ctx();
    if (!ctx) return;

    /* All shader slots share one VP (see ps3gl_shaders_init) -- reload it only
     * when it's not already bound, not on every FP/texenv key change. */
    int vp_reloaded = 0;
    if (s->vp != ps3gl.active_vp) {
        rsxLoadVertexProgram(ctx, s->vp, s->vp_ucode);
        ps3gl.active_vp = s->vp;
        vp_reloaded = 1;
    }

    if (key != ps3gl.active_shader) {
        if (s->fp_ucode) {
            rsxLoadFragmentProgramLocation(ctx, s->fp, s->fp_offset,
                                           GCM_LOCATION_RSX);
        }

        ps3gl.active_shader = key;
    }

    /* The MVP is independent of the FP: re-patch it when it changed (patching RSX microcode isn't free),
     * or on a VP switch (the NotDC bump VP), which may keep it elsewhere. */
    uint32_t mvp_gen = ps3gl_get_mvp_generation();
    if (s->mvp_const && (vp_reloaded || mvp_gen != ps3gl.mvp_uploaded_gen)) {
        const float *mvp = ps3gl_get_mvp();
        rsxSetVertexProgramParameter(ctx, s->vp, s->mvp_const, mvp);
        ps3gl.mvp_uploaded_gen = mvp_gen;
    }

    /* The world-space clip plane, only on a binary built with it (else NULL: ps3gl_draw.c clips in software), and only
     * when it changed: this runs every flush, and a portal/mirror pass moves it a few times per frame at most. */
    if (s->clip_plane_const &&
        (vp_reloaded ||
         memcmp(ps3gl.clip_plane_uploaded, ps3gl.clip_plane, sizeof(ps3gl.clip_plane)) != 0)) {
        rsxSetVertexProgramParameter(ctx, s->vp, s->clip_plane_const,
                                     ps3gl.clip_plane);
        memcpy(ps3gl.clip_plane_uploaded, ps3gl.clip_plane, sizeof(ps3gl.clip_plane));
    }

    /* NotDC 3a: the fog VP's scale, when it changed or on a VP reload. */
    if (s->fog_const && (vp_reloaded || ps3gl.dc_fog_scale != ps3gl.dc_fog_scale_uploaded)) {
        const float fogp[4] = { ps3gl.dc_fog_scale, 0.0f, 0.0f, 0.0f };
        rsxSetVertexProgramParameter(ctx, s->vp, s->fog_const, fogp);
        ps3gl.dc_fog_scale_uploaded = ps3gl.dc_fog_scale;
    }
}

#else /* PS3GL_SHADERS_AVAILABLE == 0 */

/* Stub implementations when shaders are not compiled */

void ps3gl_shaders_init(void)
{
    ps3gl_log("[ps3gl] WARNING: No compiled shaders available.\n");
    ps3gl_log("[ps3gl] Build ps3toolchain, then run: cd 3rdparty/ps3gl/shaders && ./compile_shaders.sh\n");
    ps3gl.active_shader = -1;
}

void ps3gl_shaders_shutdown(void) {}

int ps3gl_shader_key(void) { return PS3GL_TENV_DISABLED; }

void ps3gl_tex_bump(int on) { (void)on; }

void ps3gl_dc_fog(int mode) { (void)mode; }

void ps3gl_dc_fog_scale(float scale) { (void)scale; }

void ps3gl_tex_aux(int on) { (void)on; }

void ps3gl_apply_shader(void)
{
    /* No shaders to apply -- draws will produce no visible output */
}

#endif /* PS3GL_SHADERS_AVAILABLE */
