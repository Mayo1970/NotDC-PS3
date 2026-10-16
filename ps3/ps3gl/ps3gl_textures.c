/* ps3gl_textures.c -- GL-to-RSX: texture allocation, format conversion, binding. */

#include "ps3gl.h"
#include <stdio.h>
#include <stdlib.h>

/* Texture-upload counters, defined in ps3gl_main.c and reported per 5 s window in ps3gl_end_frame. */
extern uint32_t ps3gl_frame_tex_alloc_count;
extern uint32_t ps3gl_frame_subimage_count;
extern uint32_t ps3gl_frame_subimage_pixels;
extern uint32_t ps3gl_frame_defer_overflow_count;
extern uint32_t ps3gl_frame_mip_uploads;
extern uint32_t ps3gl_frame_mipped_binds;
extern uint32_t ps3gl_frame_tex_binds;

/* Debug only: tints mip levels >= 1 and forces a nonzero minlod, to prove the levels reach the sampler.
 * MUST be 0 in any real build. */
#ifndef PS3GL_MIP_DEBUG
#define PS3GL_MIP_DEBUG 0
#endif
#define PS3GL_MIP_DEBUG_MINLOD (2 << 8)

/* Read at upload time. Swizzle (ON) only changes the layout: a bilinear tap's two rows share a texture-cache
 * line. Mipmaps (OFF) change the image, so they stay opt-in. */
static int g_swizzle_enabled = 1;
static int g_mipmap_enabled  = 0;
static int g_texture_upload_probe_armed = 0;

void ps3gl_set_texture_options(int swizzle_enabled, int mipmap_enabled)
{
    g_swizzle_enabled = swizzle_enabled;
    g_mipmap_enabled  = mipmap_enabled;
}

void ps3gl_arm_texture_upload_probe(void)
{
    g_texture_upload_probe_armed = 1;
}

/* Swizzled levels are built in cached main memory, then copied to RSX memory with one sequential memcpy:
 * scattered CPU stores to GDDR3 are the worst case. Grown on demand, freed at shutdown. */
static uint8_t *g_swz_staging = NULL;
static uint32_t g_swz_staging_size = 0;

/* Swizzle (Morton) lookup tables and the size they were built for; layout rules at swz_ensure_tables(). */
#define PS3GL_MAX_TEX_DIM 4096

static uint32_t g_swz_x[PS3GL_MAX_TEX_DIM];
static uint32_t g_swz_y[PS3GL_MAX_TEX_DIM];
static uint32_t g_swz_tbl_w = 0;
static uint32_t g_swz_tbl_h = 0;

/* PS3GL_FAST_BIND: slot + 1 of the last texture whose name had these low bits, 0 = none. A hit must still hold the
 * name; a miss takes the scan, which writes the entry. A power of two (misc/glpath_check builds a small one). */
#ifndef PS3GL_NAME_SLOTS
#define PS3GL_NAME_SLOTS 4096
#endif
static uint16_t g_name_slot[PS3GL_NAME_SLOTS];

/* No pool entry below it is free. Kept on every path; PS3GL_FAST_GEN starts the scan for a free entry there, which
 * finds the entry the scan from 0 finds. */
static int g_free_hint = 0;

/* Init / Shutdown */

void ps3gl_textures_init(void)
{
    for (int i = 0; i < PS3GL_MAX_TEXTURES; i++) {
        ps3gl.textures[i].glname = -1;
        ps3gl.textures[i].data   = NULL;
    }
    ps3gl.tex_next_name = 1;
    memset(g_name_slot, 0, sizeof(g_name_slot));
    g_free_hint = 0;
}

void ps3gl_textures_shutdown(void)
{
    /* Caller (R_Free_Video) already rsxFinish()'d, so the GPU is idle --
     * safe to free pending and live textures directly here. */
    ps3gl_flush_deferred_texture_frees();

    for (int i = 0; i < PS3GL_MAX_TEXTURES; i++) {
        if (ps3gl.textures[i].data) {
            rsxFree(ps3gl.textures[i].data);
            ps3gl.textures[i].data = NULL;
        }
        ps3gl.textures[i].glname = -1;
    }
    g_free_hint = 0;

    /* Main-memory scratch: free it, and reset the table size so a later ps3gl_init rebuilds the tables. */
    if (g_swz_staging) {
        free(g_swz_staging);
        g_swz_staging = NULL;
        g_swz_staging_size = 0;
    }
    g_swz_tbl_w = 0;
    g_swz_tbl_h = 0;
}

/* The RSX can still run draws queued against a texture the CPU just unbound, so rsxFree() waits for
 * ps3gl_begin_frame's vring fence. */
void ps3gl_defer_texture_free(void *data)
{
    if (!data) return;
    if (ps3gl.pending_tex_free_count >= PS3GL_MAX_TEXTURES) {
        /* Pool full under pathological churn: flush now, without the fence wait (unsafe); counted in
         * ps3gl_end_frame's window log. */
        ps3gl_frame_defer_overflow_count++;
        ps3gl_flush_deferred_texture_frees();
    }
    ps3gl.pending_tex_free[ps3gl.pending_tex_free_count++] = data;
}

void ps3gl_flush_deferred_texture_frees(void)
{
    for (int i = 0; i < ps3gl.pending_tex_free_count; i++)
        rsxFree(ps3gl.pending_tex_free[i]);
    ps3gl.pending_tex_free_count = 0;
}

/* Lookup / Allocate */

ps3gl_texture_t *ps3gl_texture_find(GLuint name)
{
    uint16_t *e = &g_name_slot[name & (PS3GL_NAME_SLOTS - 1)];

    if ((ps3gl.fast & PS3GL_FAST_BIND) && *e && ps3gl.textures[*e - 1].glname == (int)name)
        return &ps3gl.textures[*e - 1];

    /* Simple linear scan -- 4096 slots is small enough */
    for (int i = 0; i < PS3GL_MAX_TEXTURES; i++) {
        if (ps3gl.textures[i].glname == (int)name) {
            *e = (uint16_t)(i + 1);
            return &ps3gl.textures[i];
        }
    }
    return NULL;
}

ps3gl_texture_t *ps3gl_texture_alloc(GLuint name)
{
    for (int i = (ps3gl.fast & PS3GL_FAST_GEN) ? g_free_hint : 0; i < PS3GL_MAX_TEXTURES; i++) {
        if (ps3gl.textures[i].glname == -1) {
            ps3gl_texture_t *t = &ps3gl.textures[i];
            g_free_hint = i + 1;
            memset(t, 0, sizeof(*t));
            g_name_slot[name & (PS3GL_NAME_SLOTS - 1)] = (uint16_t)(i + 1);
            t->glname     = (int)name;
            t->wrap_s     = GCM_TEXTURE_CLAMP_TO_EDGE;
            t->wrap_t     = GCM_TEXTURE_CLAMP_TO_EDGE;
            t->min_filter = GCM_TEXTURE_LINEAR;
            t->mag_filter = GCM_TEXTURE_LINEAR;
            t->dirty      = 1;
            return t;
        }
    }
    ps3gl_log("[ps3gl] WARNING: texture pool exhausted\n");
    return NULL;
}

/* GL functions */

void glGenTextures(GLsizei n, GLuint *textures)
{
    if (!textures || n <= 0) return;
    for (int i = 0; i < n; i++) {
        textures[i] = ps3gl.tex_next_name++;
        /* PS3GL_FAST_GEN: the entry now, so the first bind finds a new name in the table, not by a scan of the pool. */
        if (ps3gl.fast & PS3GL_FAST_GEN)
            ps3gl_texture_alloc(textures[i]);
    }
}

void glDeleteTextures(GLsizei n, const GLuint *textures)
{
    if (!textures) return;
    for (int i = 0; i < n; i++) {
        ps3gl_texture_t *t = ps3gl_texture_find(textures[i]);
        if (t) {
            /* Unbind from any TMU */
            for (int j = 0; j < PS3GL_MAX_TMUS; j++) {
                if (ps3gl.tmu[j].bound == t) {
                    ps3gl.tmu[j].bound = NULL;
                    ps3gl.tmu[j].dirty = 1;
                }
            }
            if (t->data) {
                ps3gl_defer_texture_free(t->data);
                t->data = NULL;
            }
            t->glname = -1;
            if (t - ps3gl.textures < g_free_hint)
                g_free_hint = (int)(t - ps3gl.textures);
        }
    }
}

void glBindTexture(GLenum target, GLuint texture)
{
    (void)target; /* only GL_TEXTURE_2D supported */
    if (texture == 0) {
        if (ps3gl.tmu[ps3gl.active_tmu].bound != NULL)
            ps3gl.tmu[ps3gl.active_tmu].dirty = 1;
        ps3gl.tmu[ps3gl.active_tmu].bound = NULL;
        return;
    }

    ps3gl_texture_t *t = ps3gl_texture_find(texture);
    if (!t) {
        t = ps3gl_texture_alloc(texture);
    }

    if (ps3gl.tmu[ps3gl.active_tmu].bound != t)
        ps3gl.tmu[ps3gl.active_tmu].dirty = 1;
    ps3gl.tmu[ps3gl.active_tmu].bound = t;
}

/* Pixel format helpers */

/* BGR/BGRA must stay: without them GL_BGR fell to 4 bytes/pixel and desynced every row (xash
 * gl_image.c uploads BGR). */
static int gl_format_bpp(GLenum format)
{
    switch (format) {
    case GL_ALPHA:
    case GL_LUMINANCE:
    case GL_RED:
        return 1;
    case GL_LUMINANCE_ALPHA:
        return 2;
    case GL_RGB:
    case GL_RGB8:
    case GL_BGR:
        return 3;
    case GL_RGBA:
    case GL_RGBA8:
    case GL_BGRA:
    default:
        return 4;
    }
}

/* misc/spepix_check compiles the code from here to "spepix_check: end" as the reference for the SPU decode. */
/* Do the source bytes arrive as B,G,R[,A] rather than R,G,B[,A]? */
static int gl_format_is_bgr(GLenum format)
{
    return format == GL_BGR || format == GL_BGRA;
}

/* One source pixel -> A8R8G8B8 as a uint32: big-endian, the word lands as bytes A,R,G,B, as the RSX wants.
 * Shared by every converter so the linear and swizzled paths cannot drift. */
static inline uint32_t fetch_pixel(const uint8_t *srow, int col, int src_bpp,
                                   GLenum src_format, int bgr)
{
    uint8_t r, g, b, a;

    switch (src_bpp) {
    case 1:
        if (src_format == GL_ALPHA) {
            r = g = b = 255;
            a = srow[col];
        } else {
            /* Luminance */
            r = g = b = srow[col];
            a = 255;
        }
        break;
    case 2:
        r = g = b = srow[col * 2];
        a = srow[col * 2 + 1];
        break;
    case 3:
        r = srow[col * 3 + (bgr ? 2 : 0)];
        g = srow[col * 3 + 1];
        b = srow[col * 3 + (bgr ? 0 : 2)];
        a = 255;
        break;
    case 4:
    default:
        r = srow[col * 4 + (bgr ? 2 : 0)];
        g = srow[col * 4 + 1];
        b = srow[col * 4 + (bgr ? 0 : 2)];
        a = srow[col * 4 + 3];
        break;
    }

    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

/* A level's pixels (src_bpp 1-4) to ARGB8888. Linear: rows dst_stride bytes apart (padded). Swizzled: Morton
 * order into the main-memory staging buffer, never VRAM; call swz_ensure_tables(width, height) first. */
static void convert_pixels(uint8_t *dst, uint32_t dst_stride, const uint8_t *src,
                           int width, int height, int src_bpp,
                           GLenum src_format, int swizzled)
{
    const int bgr = gl_format_is_bgr(src_format);
    /* Straight RGBA gets its own loop: the same packed word, without the per-pixel format switch. */
    const int fast = (src_bpp == 4 && !bgr);

    for (int row = 0; row < height; row++) {
        const uint8_t *srow = src + (size_t)row * width * src_bpp;

        if (swizzled) {
            uint32_t *d = (uint32_t *)dst;
            const uint32_t yo = g_swz_y[row];

            if (fast) {
                for (int col = 0; col < width; col++) {
                    d[yo | g_swz_x[col]] =
                        ((uint32_t)srow[col * 4 + 3] << 24) |
                        ((uint32_t)srow[col * 4 + 0] << 16) |
                        ((uint32_t)srow[col * 4 + 1] << 8)  |
                         (uint32_t)srow[col * 4 + 2];
                }
            } else {
                for (int col = 0; col < width; col++)
                    d[yo | g_swz_x[col]] = fetch_pixel(srow, col, src_bpp, src_format, bgr);
            }
        } else {
            uint32_t *drow = (uint32_t *)(dst + (uint32_t)row * dst_stride);

            if (fast) {
                for (int col = 0; col < width; col++) {
                    drow[col] =
                        ((uint32_t)srow[col * 4 + 3] << 24) |
                        ((uint32_t)srow[col * 4 + 0] << 16) |
                        ((uint32_t)srow[col * 4 + 1] << 8)  |
                         (uint32_t)srow[col * 4 + 2];
                }
            } else {
                for (int col = 0; col < width; col++)
                    drow[col] = fetch_pixel(srow, col, src_bpp, src_format, bgr);
            }
        }
    }
}

/* Mip chains. LINEAR: every level keeps the base pitch, only the rows shrink (do not pack it tight).
 * SWIZZLED: no pitch; each level is Morton-ordered and packed tight (1.33x the base, not 2x). */

static uint32_t mip_level_dim(uint32_t base, int level)
{
    uint32_t d = base >> level;
    return d ? d : 1;
}

static int mip_num_levels(uint32_t w0, uint32_t h0)
{
    uint32_t maxdim = (w0 > h0) ? w0 : h0;
    int levels = 1;
    while (maxdim > 1) { maxdim >>= 1; levels++; }
    return levels;
}

/* Levels to allocate. With mipmapping off it is 1: num_levels stays 1, maxlod 0, the mip paths inert. */
static int tex_alloc_levels(uint32_t w0, uint32_t h0)
{
    return g_mipmap_enabled ? mip_num_levels(w0, h0) : 1;
}

/* A linear pitch must be a multiple of 64 bytes: an 8-texel DC texture (32 bytes) stalls the RSX.
 * The swizzled path ignores pitch. */
static uint32_t tex_linear_pitch(uint32_t w0)
{
    return ((w0 * 4) + 63u) & ~63u;
}

static uint32_t tex_level_bytes(uint32_t w0, uint32_t h0, int level, int swizzled)
{
    if (swizzled)
        return mip_level_dim(w0, level) * mip_level_dim(h0, level) * 4;
    return tex_linear_pitch(w0) * mip_level_dim(h0, level); /* base pitch, shrinking rows */
}

static uint32_t tex_level_offset(uint32_t w0, uint32_t h0, int level, int swizzled)
{
    uint32_t off = 0;
    for (int i = 0; i < level; i++)
        off += tex_level_bytes(w0, h0, i, swizzled);
    return off;
}

static uint32_t tex_total_size(uint32_t w0, uint32_t h0, int levels, int swizzled)
{
    return tex_level_offset(w0, h0, levels, swizzled);
}

/* RSX swizzle: x and y bits interleave from the LSB, x first, while both last, then the larger size's bits
 * (4x4: y1 x1 y0 x0, 8x2: x2 x1 y0 x0). Two tables, one OR per texel, rebuilt when the size changes. */

static int is_pow2(uint32_t v)
{
    return v != 0 && (v & (v - 1)) == 0;
}

static int log2u(uint32_t v)
{
    int l = 0;
    while ((1u << l) < v) l++;
    return l;
}

static void swz_ensure_tables(uint32_t w, uint32_t h)
{
    if (w == g_swz_tbl_w && h == g_swz_tbl_h) return;
    if (w > PS3GL_MAX_TEX_DIM || h > PS3GL_MAX_TEX_DIM) return;

    const int lw = log2u(w), lh = log2u(h);
    int xpos[32], ypos[32];
    int nx = 0, ny = 0, shift = 0;

    while (nx < lw || ny < lh) {
        if (nx < lw) xpos[nx++] = shift++;
        if (ny < lh) ypos[ny++] = shift++;
    }

    for (uint32_t x = 0; x < w; x++) {
        uint32_t o = 0;
        for (int b = 0; b < lw; b++)
            if (x & (1u << b)) o |= 1u << xpos[b];
        g_swz_x[x] = o;
    }
    for (uint32_t y = 0; y < h; y++) {
        uint32_t o = 0;
        for (int b = 0; b < lh; b++)
            if (y & (1u << b)) o |= 1u << ypos[b];
        g_swz_y[y] = o;
    }

    g_swz_tbl_w = w;
    g_swz_tbl_h = h;
}
/* spepix_check: end */

static uint8_t *swz_staging_get(uint32_t size)
{
    if (size > g_swz_staging_size) {
        uint8_t *p = (uint8_t *)realloc(g_swz_staging, size);
        if (!p) return NULL;
        g_swz_staging      = p;
        g_swz_staging_size = size;
    }
    return g_swz_staging;
}

/* Build gcmTexture descriptor */
static void build_gcm_texture(ps3gl_texture_t *t)
{
    memset(&t->gcm_tex, 0, sizeof(t->gcm_tex));
    /* Swizzled is the absence of GCM_TEXTURE_FORMAT_LIN (the SZ variant is 0). */
    t->gcm_tex.format    = GCM_TEXTURE_FORMAT_A8R8G8B8 |
                           (t->swizzled ? 0 : GCM_TEXTURE_FORMAT_LIN);
    t->gcm_tex.mipmap    = t->num_levels ? t->num_levels : 1;
    t->gcm_tex.dimension = GCM_TEXTURE_DIMS_2D;
    t->gcm_tex.cubemap   = GCM_FALSE;
    t->gcm_tex.remap     = PS3GL_TEX_REMAP_IDENTITY;
    t->gcm_tex.width     = t->width;
    t->gcm_tex.height    = t->height;
    t->gcm_tex.depth     = 1;
    t->gcm_tex.location  = GCM_LOCATION_RSX;
    /* Swizzled textures ignore pitch, so the linear value stays. On scrambled swizzled texels, try 0 here
     * before touching swz_ensure_tables. */
    t->gcm_tex.pitch     = t->fixed_pitch ? t->fixed_pitch : tex_linear_pitch(t->width);
    t->gcm_tex.offset    = t->offset;
}

#if PS3GL_MIP_DEBUG
/* Flat tint per level >= 1, in either layout: ordered colour bands prove level selection reaches the
 * sampler (layout faults show as scrambled texels in the normal build). */
static void ps3gl_mip_debug_tint(uint8_t *level_data, uint32_t level_bytes, int level)
{
    static const uint32_t colors[] = {
        0xFFFF0000, 0xFF00FF00, 0xFF0000FF, 0xFFFFFF00,
        0xFFFF00FF, 0xFF00FFFF, 0xFFFFFFFF,
    };
    if (level < 1) return;
    uint32_t color = colors[(level - 1) % (sizeof(colors) / sizeof(colors[0]))];
    uint32_t *d = (uint32_t *)level_data;
    for (uint32_t i = 0; i < level_bytes / 4; i++)
        d[i] = color;
}
#endif

/* Level 0 (re)allocation for glTexImage2D and ps3gl_tex_direct_begin(); 0 if rsxMemalign failed. */
static int tex_alloc_level0(ps3gl_texture_t *t, GLsizei width, GLsizei height)
{
    /* Free old data if base dimensions changed */
    if (t->data && (t->width != (uint16_t)width || t->height != (uint16_t)height)) {
        ps3gl_defer_texture_free(t->data);
        t->data = NULL;
        t->fixed_pitch = 0;
    }

    t->width      = (uint16_t)width;
    t->height     = (uint16_t)height;
    t->bpp        = 4; /* always store as ARGB8888 */
    t->num_levels = 0; /* the growth logic in glTexImage2D rebuilds it as levels land (it sets maxlod) */

    /* The layout is fixed at allocation: the buffer size depends on it (linear chain ~2x, swizzled ~1.33x).
     * Non-power-of-two stays linear: RSX swizzle needs power-of-two sizes. */
    if (!t->data) {
        /* Mirrored wrap forces linear (the RSX stalls mirroring a swizzled texture): set the wrap modes
         * before the first upload. */
        const int wants_mirror = (t->wrap_s == GCM_TEXTURE_MIRRORED_REPEAT ||
                                  t->wrap_t == GCM_TEXTURE_MIRRORED_REPEAT);

        t->swizzled = (uint8_t)(g_swizzle_enabled && !wants_mirror && !t->no_swizzle_hint &&
                                is_pow2((uint32_t)width) && is_pow2((uint32_t)height) &&
                                width <= PS3GL_MAX_TEX_DIM && height <= PS3GL_MAX_TEX_DIM);

        /* A hinted chain only swizzled: a linear chain (the base pitch on every level) never ran on HW. */
        t->alloc_levels = (uint8_t)((t->mip_hint && t->swizzled)
                                        ? mip_num_levels((uint32_t)width, (uint32_t)height)
                                        : tex_alloc_levels((uint32_t)width, (uint32_t)height));

        uint32_t size = tex_total_size((uint32_t)width, (uint32_t)height,
                                       t->alloc_levels, t->swizzled);
        t->data = (uint8_t *)rsxMemalign(128, size);
        if (!t->data) {
            ps3gl_log("[ps3gl] WARNING: failed to allocate %u bytes for texture %d\n",
                   size, t->glname);
            return 0;
        }
        rsxAddressToOffset(t->data, &t->offset);
        ps3gl_frame_tex_alloc_count++;
    }
    return 1;
}

void glTexImage2D(GLenum target, GLint level, GLint internalformat,
                      GLsizei width, GLsizei height, GLint border,
                      GLenum format, GLenum type, const void *pixels)
{
    (void)border; (void)type; (void)internalformat;

    ps3gl_texture_t *t;

    if (level < 0) return;

    t = ps3gl.tmu[ps3gl.active_tmu].bound;
    if (!t) return;

    if (level == 0 && !tex_alloc_level0(t, width, height))
        return;

    if (!t->data) { /* level>0 arrived before level 0 -- ignore */
        return;
    }
    if (level >= (int)(t->alloc_levels ? t->alloc_levels : 1)) {
        /* Beyond what this buffer was sized for (with mipmaps off, every level > 0): dropped. Uses the
         * texture's own level count; the mipmap option can change after allocation. */
        return;
    }
    if (width != (GLsizei)mip_level_dim(t->width, level) ||
        height != (GLsizei)mip_level_dim(t->height, level)) {
        /* Wrong level size: refuse rather than write past this level's block. */
        return;
    }

    const uint32_t pitch      = tex_linear_pitch(t->width);
    const uint32_t lvl_off    = tex_level_offset(t->width, t->height, level, t->swizzled);
    const uint32_t lvl_bytes  = tex_level_bytes(t->width, t->height, level, t->swizzled);
    uint8_t       *lvl_data   = t->data + lvl_off;

    if (t->swizzled) {
        /* Never scatter-write texels straight into GDDR3: convert into cached
         * main memory, then one sequential memcpy to VRAM. */
        uint8_t *stage = swz_staging_get(lvl_bytes);
        if (!stage) {
            ps3gl_log("[ps3gl] WARNING: swizzle staging alloc failed (%u bytes), texture %d\n",
                      lvl_bytes, t->glname);
            return;
        }

        swz_ensure_tables((uint32_t)width, (uint32_t)height);

        if (pixels) {
            convert_pixels(stage, 0, (const uint8_t *)pixels,
                           width, height, gl_format_bpp(format), format, 1);
        } else {
            memset(stage, 0, lvl_bytes);
        }
#if PS3GL_MIP_DEBUG
        ps3gl_mip_debug_tint(stage, lvl_bytes, level);
#endif
        memcpy(lvl_data, stage, lvl_bytes);
    } else {
        if (pixels) {
            convert_pixels(lvl_data, pitch, (const uint8_t *)pixels,
                           width, height, gl_format_bpp(format), format, 0);
        } else {
            memset(lvl_data, 0, lvl_bytes);
        }
#if PS3GL_MIP_DEBUG
        ps3gl_mip_debug_tint(lvl_data, lvl_bytes, level);
#endif
    }

    /* Levels count only in order: a level past a gap stays written but uncounted, so the sampler never sees
     * uninitialized rows. */
    if (level == 0) {
        t->num_levels = 1;
    } else if (level == t->num_levels && t->num_levels < 15) {
        t->num_levels = (uint8_t)(level + 1);
        ps3gl_frame_mip_uploads++;
    }

    build_gcm_texture(t);
    t->dirty = 1;
    t->content_dirty = 1;
}

/* P42: level 0 of the bound texture as linear A8R8G8B8 rows for the caller to write, allocated as
 * glTexImage2D does. NULL if swizzled or out of memory; else ps3gl_tex_direct_end() after the rows. */
uint32_t *ps3gl_tex_direct_begin(GLsizei width, GLsizei height, uint32_t *pitch)
{
    ps3gl_texture_t *t = ps3gl.tmu[ps3gl.active_tmu].bound;
    if (!t || !tex_alloc_level0(t, width, height) || t->swizzled)
        return NULL;
    *pitch = tex_linear_pitch(t->width);
    return (uint32_t *)t->data;
}

void ps3gl_tex_direct_end(void)
{
    ps3gl_texture_t *t = ps3gl.tmu[ps3gl.active_tmu].bound;
    if (!t || !t->data)
        return;
    t->num_levels = 1;
    build_gcm_texture(t);
    t->dirty = 1;
    t->content_dirty = 1;
}

/* 5.12 #4: level 0 of the bound texture for an SPU to write, allocated as glTexImage2D does, in its own layout
 * (*swizzled, else rows *pitch bytes apart). NULL if out of memory; else ps3gl_tex_direct_end() after the write. */
uint8_t *ps3gl_tex_direct_level0(GLsizei width, GLsizei height, uint32_t *pitch, int *swizzled)
{
    ps3gl_texture_t *t = ps3gl.tmu[ps3gl.active_tmu].bound;
    if (!t || !tex_alloc_level0(t, width, height))
        return NULL;
    *pitch = tex_linear_pitch(t->width);
    *swizzled = t->swizzled;
    return t->data;
}

/* P45: the mip levels the bound texture's memory holds: 1 without a chain, 0 with no memory. */
int ps3gl_tex_chain_levels(void)
{
    ps3gl_texture_t *t = ps3gl.tmu[ps3gl.active_tmu].bound;
    if (!t || !t->data)
        return 0;
    return t->alloc_levels ? t->alloc_levels : 1;
}

/* P45: level `level` (1 or more) of the bound swizzled texture for the caller to write, after level 0. NULL if
 * the texture holds no such level; else ps3gl_tex_direct_level_end(level) after the write. */
uint8_t *ps3gl_tex_direct_level(int level)
{
    ps3gl_texture_t *t = ps3gl.tmu[ps3gl.active_tmu].bound;
    if (!t || !t->data || !t->swizzled || level < 1 || level >= (int)t->alloc_levels)
        return NULL;
    return t->data + tex_level_offset(t->width, t->height, level, 1);
}

/* Counts the level as glTexImage2D does: only in order, so the sampler never reaches an unwritten level. */
void ps3gl_tex_direct_level_end(int level)
{
    ps3gl_texture_t *t = ps3gl.tmu[ps3gl.active_tmu].bound;
    if (!t || !t->data)
        return;
    if (level >= 1 && level == t->num_levels && t->num_levels < 15) {
        t->num_levels = (uint8_t)(level + 1);
        ps3gl_frame_mip_uploads++;
    }
    build_gcm_texture(t);
    t->dirty = 1;
    t->content_dirty = 1;
}

/* 5.12 #4 CHECK: RGBA words as decode() writes them, converted into main memory as glTexImage2D would store them in
 * the bound texture's layout. */
void ps3gl_tex_convert_check(void *dst, GLsizei width, GLsizei height, const void *rgba)
{
    ps3gl_texture_t *t = ps3gl.tmu[ps3gl.active_tmu].bound;
    if (!t)
        return;
    if (t->swizzled)
        swz_ensure_tables((uint32_t)width, (uint32_t)height);
    convert_pixels((uint8_t *)dst, tex_linear_pitch((uint32_t)width), (const uint8_t *)rgba, width, height, 4,
                   GL_RGBA, t->swizzled);
}

/* 5.12 #5: the bound texture as linear A8R8G8B8 of max_w x max_h, allocated once; each call sets the size the RSX
 * samples (w x h) without moving its memory. Returns level 0 and its pitch; NULL if out of memory. */
uint8_t *ps3gl_tex_fixed(GLsizei max_w, GLsizei max_h, GLsizei w, GLsizei h, uint32_t *pitch)
{
    ps3gl_texture_t *t = ps3gl.tmu[ps3gl.active_tmu].bound;
    if (!t)
        return NULL;
    if (!t->data || !t->fixed_pitch) {
        t->no_swizzle_hint = 1;
        if (!tex_alloc_level0(t, max_w, max_h))
            return NULL;
        t->fixed_pitch = tex_linear_pitch((uint32_t)max_w);
    }
    t->width = (uint16_t)w;
    t->height = (uint16_t)h;
    t->num_levels = 1;
    build_gcm_texture(t);
    t->dirty = 1;
    t->content_dirty = 1;
    *pitch = t->fixed_pitch;
    return t->data;
}

void glTexSubImage2D(GLenum target, GLint level, GLint xoff, GLint yoff,
                          GLsizei w, GLsizei h, GLenum format, GLenum type,
                          const void *pixels)
{
    const int upload_probe = g_texture_upload_probe_armed;
    g_texture_upload_probe_armed = 0;

    (void)target; (void)type;
    if (level < 0 || !pixels) {
        if (upload_probe)
            ps3gl_log("[ps3gl] dlight upload probe: skipped (level=%d pixels=%p)\n",
                      level, pixels);
        return;
    }

    ps3gl_texture_t *t = ps3gl.tmu[ps3gl.active_tmu].bound;
    if (!t || !t->data || level >= (int)t->num_levels) {
        if (upload_probe)
            ps3gl_log("[ps3gl] dlight upload probe: skipped (texture unavailable, level=%d)\n",
                      level);
        return;
    }

    /* Through the level helpers, level 0 gets the same addresses as the old level-0-only code in both
     * layouts, so the HW-validated packed-store path below is unchanged. */
    const uint32_t pitch = tex_linear_pitch(t->width);
    const int lw = (int)mip_level_dim(t->width, level);
    const int lh = (int)mip_level_dim(t->height, level);
    uint8_t *level_data = t->data + tex_level_offset(t->width, t->height, level, t->swizzled);

    int src_bpp = gl_format_bpp(format);
    /* Same BGR handling as convert_pixels -- lightmap updates come through here. */
    const int bgr = gl_format_is_bgr(format);
    const uint8_t *src = (const uint8_t *)pixels;

    ps3gl_frame_subimage_count++;
    ps3gl_frame_subimage_pixels += (uint32_t)(w > 0 && h > 0 ? w * h : 0);

    /* Clip once, not per pixel: every store below is an uncached CPU write to RSX memory. */
    int row0 = (yoff < 0) ? -yoff : 0;
    int row1 = (yoff + h > lh) ? (lh - yoff) : h;
    int col0 = (xoff < 0) ? -xoff : 0;
    int col1 = (xoff + w > lw) ? (lw - xoff) : w;
    const int full_rect = (xoff == 0 && yoff == 0 && w == lw && h == lh);

    if (t->swizzled && full_rect) {
        /* A whole-level refresh is a glTexImage2D's volume, so it takes the staging route, not scattered
         * stores into RSX memory. */
        uint32_t lvl_bytes = tex_level_bytes(t->width, t->height, level, 1);
        uint8_t *stage = swz_staging_get(lvl_bytes);
        if (!stage) return;

        swz_ensure_tables((uint32_t)lw, (uint32_t)lh);
        convert_pixels(stage, 0, src, lw, lh, src_bpp, format, 1);
        memcpy(level_data, stage, lvl_bytes);
    } else if (t->swizzled) {
        /* A partial rect into a swizzled level goes straight to RSX memory: its volume is tiny (a few
         * thousand pixels per frame), so staging a full level would cost more. */
        swz_ensure_tables((uint32_t)lw, (uint32_t)lh);
        uint32_t *d = (uint32_t *)level_data;

        for (int row = row0; row < row1; row++) {
            const uint8_t *srow = src + (size_t)row * w * src_bpp;
            const uint32_t yo = g_swz_y[yoff + row];
            for (int col = col0; col < col1; col++)
                d[yo | g_swz_x[xoff + col]] =
                    fetch_pixel(srow, col, src_bpp, format, bgr);
        }
    } else if (src_bpp == 4 && !bgr) {
        /* Straight RGBA: one packed word store per pixel. Byte stores defeat write-combining on RSX memory;
         * an aligned word store does not. */
        for (int row = row0; row < row1; row++) {
            int dy = yoff + row;
            uint32_t *drow = (uint32_t *)(level_data + (uint32_t)dy * pitch);
            const uint8_t *srow = src + (size_t)row * w * 4;
            for (int col = col0; col < col1; col++) {
                int dx = xoff + col;
                uint8_t r = srow[col * 4 + 0];
                uint8_t g = srow[col * 4 + 1];
                uint8_t b = srow[col * 4 + 2];
                uint8_t a = srow[col * 4 + 3];
                drow[dx] = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
            }
        }
    } else {
        for (int row = row0; row < row1; row++) {
            int dy = yoff + row;
            uint32_t *drow = (uint32_t *)(level_data + (uint32_t)dy * pitch);
            const uint8_t *srow = src + (size_t)row * w * src_bpp;
            for (int col = col0; col < col1; col++)
                drow[xoff + col] = fetch_pixel(srow, col, src_bpp, format, bgr);
        }
    }

    if (upload_probe) {
        uint32_t mismatches = 0;
        uint32_t compared = 0;
        int first_row = -1, first_col = -1;
        uint8_t first_src[4] = { 0, 0, 0, 0 };
        uint8_t first_dst[4] = { 0, 0, 0, 0 };

        /* Compares bytes, not words: the RSX expects A,R,G,B in memory, so this checks the byte order too. */
        __asm__ volatile("sync" ::: "memory");
        if (t->swizzled)
            swz_ensure_tables((uint32_t)lw, (uint32_t)lh);

        for (int row = row0; row < row1; row++) {
            const uint8_t *srow = src + (size_t)row * w * src_bpp;
            for (int col = col0; col < col1; col++) {
                const uint32_t packed = fetch_pixel(srow, col, src_bpp, format, bgr);
                const uint8_t expected[4] = {
                    (uint8_t)(packed >> 24), (uint8_t)(packed >> 16),
                    (uint8_t)(packed >> 8), (uint8_t)packed
                };
                const uint8_t *actual;

                if (t->swizzled) {
                    const uint32_t texel = g_swz_y[yoff + row] | g_swz_x[xoff + col];
                    actual = level_data + texel * 4;
                } else {
                    actual = level_data + (uint32_t)(yoff + row) * pitch +
                             (uint32_t)(xoff + col) * 4;
                }

                compared++;
                if (actual[0] != expected[0] || actual[1] != expected[1] ||
                    actual[2] != expected[2] || actual[3] != expected[3]) {
                    mismatches++;
                    if (first_row < 0) {
                        first_row = row;
                        first_col = col;
                        first_src[0] = expected[0]; first_src[1] = expected[1];
                        first_src[2] = expected[2]; first_src[3] = expected[3];
                        first_dst[0] = actual[0]; first_dst[1] = actual[1];
                        first_dst[2] = actual[2]; first_dst[3] = actual[3];
                    }
                }
            }
        }

        ps3gl_log("[ps3gl] dlight upload probe: %dx%d at %d,%d texture=%d "
                  "swizzled=%d pitch=%u byte mismatches=%u/%u\n",
                  w, h, xoff, yoff, t->glname, (int)t->swizzled,
                  (unsigned)t->gcm_tex.pitch, (unsigned)mismatches,
                  (unsigned)compared);
        if (mismatches) {
            ps3gl_log("[ps3gl] dlight upload probe: first mismatch row=%d col=%d "
                      "expected ARGB=%02x,%02x,%02x,%02x got=%02x,%02x,%02x,%02x\n",
                      first_row, first_col,
                      first_src[0], first_src[1], first_src[2], first_src[3],
                      first_dst[0], first_dst[1], first_dst[2], first_dst[3]);
        }
    }

    /* A full-rect refresh grows num_levels as glTexImage2D does, so a re-uploaded chain keeps its count.
     * Partial rects never touch num_levels. */
    if (full_rect) {
        if (level == 0) {
            t->num_levels = 1;
        } else if (level == t->num_levels && t->num_levels < 15) {
            t->num_levels = (uint8_t)(level + 1);
            ps3gl_frame_mip_uploads++;
        }
    }

    build_gcm_texture(t);
    t->dirty = 1;
    t->content_dirty = 1;
}

/* Texture parameters */

static uint8_t gl_to_gcm_wrap(GLint param)
{
    switch (param) {
    case GL_REPEAT:        return GCM_TEXTURE_REPEAT;
    case GL_CLAMP:         return GCM_TEXTURE_CLAMP;
    case GL_CLAMP_TO_EDGE: return GCM_TEXTURE_CLAMP_TO_EDGE;
    case GL_MIRRORED_REPEAT: return GCM_TEXTURE_MIRRORED_REPEAT;
    default:               return GCM_TEXTURE_REPEAT;
    }
}

/* 1:1 mapping. An earlier probe clamped *_MIPMAP_LINEAR to *_MIPMAP_NEAREST; reverted, as it changed
 * the image. */
static uint8_t gl_to_gcm_filter(GLint param)
{
    switch (param) {
    case GL_NEAREST:                return GCM_TEXTURE_NEAREST;
    case GL_LINEAR:                 return GCM_TEXTURE_LINEAR;
    case GL_NEAREST_MIPMAP_NEAREST: return GCM_TEXTURE_NEAREST_MIPMAP_NEAREST;
    case GL_LINEAR_MIPMAP_NEAREST:  return GCM_TEXTURE_LINEAR_MIPMAP_NEAREST;
    case GL_NEAREST_MIPMAP_LINEAR:  return GCM_TEXTURE_NEAREST_MIPMAP_LINEAR;
    case GL_LINEAR_MIPMAP_LINEAR:   return GCM_TEXTURE_LINEAR_MIPMAP_LINEAR;
    default:                        return GCM_TEXTURE_LINEAR;
    }
}

/* The RSX has 1, 2, 4, 6, 8, 10, 12 and 16 taps; a GL value takes the largest one that does not pass it. */
static uint8_t gl_to_gcm_aniso(GLint param)
{
    if (param >= 16) return GCM_TEXTURE_MAX_ANISO_16;
    if (param >= 12) return GCM_TEXTURE_MAX_ANISO_12;
    if (param >= 10) return GCM_TEXTURE_MAX_ANISO_10;
    if (param >= 8)  return GCM_TEXTURE_MAX_ANISO_8;
    if (param >= 6)  return GCM_TEXTURE_MAX_ANISO_6;
    if (param >= 4)  return GCM_TEXTURE_MAX_ANISO_4;
    if (param >= 2)  return GCM_TEXTURE_MAX_ANISO_2;
    return GCM_TEXTURE_MAX_ANISO_1;
}

static void tex_param(GLenum pname, GLint param)
{
    ps3gl_texture_t *t = ps3gl.tmu[ps3gl.active_tmu].bound;
    uint8_t *field = NULL, value = 0;
    if (!t) return;

    switch (pname) {
    case GL_TEXTURE_WRAP_S:     field = &t->wrap_s;     value = gl_to_gcm_wrap(param); break;
    case GL_TEXTURE_WRAP_T:     field = &t->wrap_t;     value = gl_to_gcm_wrap(param); break;
    case GL_TEXTURE_MIN_FILTER: field = &t->min_filter; value = gl_to_gcm_filter(param); break;
    case GL_TEXTURE_MAG_FILTER: field = &t->mag_filter; value = gl_to_gcm_filter(param); break;
    case GL_TEXTURE_MAX_ANISOTROPY_EXT: field = &t->max_aniso; value = gl_to_gcm_aniso(param); break;
    case GL_GENERATE_MIPMAP:
        break;
    default: break;
    }
    /* PS3GL_FAST_COMPARE: a parameter that keeps its value does not send the sampler state again. */
    if ((ps3gl.fast & PS3GL_FAST_COMPARE) && (!field || *field == value))
        return;
    if (field)
        *field = value;
    t->dirty = 1;
}

void glTexParameterf(GLenum target, GLenum pname, GLfloat param)
{
    (void)target;
    tex_param(pname, (GLint)param);
}

void glTexParameteri(GLenum target, GLenum pname, GLint param)
{
    (void)target;
    tex_param(pname, param);
}

void ps3gl_texture_hint_no_swizzle(void)
{
    ps3gl_texture_t *t = ps3gl.tmu[ps3gl.active_tmu].bound;
    if (t) t->no_swizzle_hint = 1;
}

void ps3gl_texture_hint_mipmaps(void)
{
    ps3gl_texture_t *t = ps3gl.tmu[ps3gl.active_tmu].bound;
    if (t) t->mip_hint = 1;
}

/* TexEnv */

void glTexEnvf(GLenum target, GLenum pname, GLfloat param)
{
    (void)target;
    if (pname != GL_TEXTURE_ENV_MODE) return;

    int mode;
    GLenum p = (GLenum)(int)param;
    switch (p) {
    case GL_MODULATE:       mode = PS3GL_TENV_MODULATE; break;
    case GL_REPLACE:        mode = PS3GL_TENV_REPLACE; break;
    case GL_DECAL:          mode = PS3GL_TENV_DECAL; break;
    case GL_ADD:            mode = PS3GL_TENV_ADD; break;
    default:                mode = PS3GL_TENV_MODULATE; break;
    }
    if (ps3gl.tmu[ps3gl.active_tmu].texenv != mode)
        ps3gl.tmu[ps3gl.active_tmu].dirty = 1;
    ps3gl.tmu[ps3gl.active_tmu].texenv = mode;
}

void glTexEnvi(GLenum target, GLenum pname, GLint param)
{
    glTexEnvf(target, pname, (GLfloat)param);
}

/* Multitexture */

void glActiveTextureARB(GLenum texture)
{
    int tmu = (int)(texture - GL_TEXTURE0_ARB);
    if (tmu >= 0 && tmu < PS3GL_MAX_TMUS)
        ps3gl.active_tmu = tmu;
}

void glClientActiveTextureARB(GLenum texture)
{
    int tmu = (int)(texture - GL_TEXTURE0_ARB);
    if (tmu >= 0 && tmu < PS3GL_MAX_TMUS)
        ps3gl.client_active_tmu = tmu;
}

void glPixelStorei(GLenum pname, GLint param)
{
    (void)pname; (void)param;
}

void glCopyTexSubImage2D(GLenum target, GLint level, GLint xoff,
                              GLint yoff, GLint x, GLint y,
                              GLsizei w, GLsizei h)
{
    (void)target; (void)level; (void)xoff; (void)yoff;
    (void)x; (void)y; (void)w; (void)h;
}

/* Apply textures to RSX before draw */

void ps3gl_apply_textures(void)
{
    gcmContextData *ctx = ps3gl_get_ctx();
    if (!ctx) return;

    /* Uploads write RSX memory directly: sync the PPE stores, then invalidate the texture cache once before a
     * draw samples them. content_dirty, not dirty: sampler-only changes keep the cached texels. */
    int invalidate_texture_cache = 0;
    for (int i = 0; i < PS3GL_MAX_TMUS; i++) {
        ps3gl_tmu_t *tmu = &ps3gl.tmu[i];
        ps3gl_texture_t *t = tmu->bound;
        if (tmu->enabled && t && t->data && t->content_dirty) {
            invalidate_texture_cache = 1;
            break;
        }
    }
    if (invalidate_texture_cache) {
        __asm__ volatile("sync" ::: "memory");
        rsxInvalidateTextureCache(ctx, GCM_INVALIDATE_TEXTURE);
    }

    for (int i = 0; i < PS3GL_MAX_TMUS; i++) {
        ps3gl_tmu_t *tmu = &ps3gl.tmu[i];
        ps3gl_texture_t *t = tmu->bound;

        if (!tmu->enabled || !t || !t->data) {
            if (tmu->dirty) {
                rsxTextureControl(ctx, i, GCM_FALSE, 0, 0, 0);
                tmu->dirty = 0;
                /* PS3GL_FAST_COMPARE: no texture has this control word; the filter and wrap words stay. */
                tmu->sent_ctrl = ~0u;
            }
            continue;
        }

        /* Skip re-bind if nothing changed on this TMU */
        if (!tmu->dirty && !t->dirty) continue;

        /* PS3GL_FAST_COMPARE: the control, filter and wrap words are the unit's own registers, so each goes out
         * only when it differs from the one the unit holds. */
        const int held = (ps3gl.fast & PS3GL_FAST_COMPARE) && tmu->sent;
        const uint32_t filter = ((uint32_t)t->min_filter << 8) | t->mag_filter;
        const uint32_t wrap   = ((uint32_t)t->wrap_s << 8) | t->wrap_t;
        uint32_t ctrl;

        rsxLoadTexture(ctx, i, &t->gcm_tex);
        /* maxlod is 4.8 fixed point, NOT an integer level count -- get this wrong and the
         * texture binds but renders nothing. maxlod = (num_levels-1)<<8 so the sampler can reach every uploaded mip. */
        {
            uint16_t maxlod = (uint16_t)(((t->num_levels ? t->num_levels : 1) - 1) << 8);
            uint16_t minlod = 0;
#if PS3GL_MIP_DEBUG
            /* Debug only: forces a mip level at any distance, so the tinted levels show up close. */
            if (t->num_levels > 1)
                minlod = PS3GL_MIP_DEBUG_MINLOD;
#endif
            ctrl = ((uint32_t)minlod << 16) | ((uint32_t)maxlod << 4) | t->max_aniso;
            if (!held || ctrl != tmu->sent_ctrl)
                rsxTextureControl(ctx, i, GCM_TRUE, minlod, maxlod, t->max_aniso);
            ps3gl_frame_tex_binds++;
            if (t->num_levels > 1)
                ps3gl_frame_mipped_binds++;
        }
        if (!held || filter != tmu->sent_filter)
            rsxTextureFilter(ctx, i, 0, t->min_filter, t->mag_filter,
                             GCM_TEXTURE_CONVOLUTION_QUINCUNX);
        if (!held || wrap != tmu->sent_wrap)
            rsxTextureWrapMode(ctx, i, t->wrap_s, t->wrap_t,
                               GCM_TEXTURE_CLAMP_TO_EDGE,
                               GCM_TEXTURE_UNSIGNED_REMAP_NORMAL,
                               GCM_TEXTURE_ZFUNC_NEVER, 0);
        if (ps3gl.fast & PS3GL_FAST_COMPARE) {
            tmu->sent        = 1;
            tmu->sent_ctrl   = ctrl;
            tmu->sent_filter = filter;
            tmu->sent_wrap   = wrap;
        }

        tmu->dirty = 0;
        t->dirty = 0;
        t->content_dirty = 0;
    }
}
