/* ps3gl_vertices.c -- GL-to-RSX: vertex accumulation and ring buffer. */

#include "ps3gl.h"
#include <stdio.h>
#include <stdlib.h>   // memalign/free for the host ring below
#include <malloc.h>

/* Forward declaration from ps3gl_matrices.c */

/* Ring buffer management */

void ps3gl_vring_init(void)
{
    static const int seg_labels[PS3GL_VRING_SEGMENTS] = {
        PS3GL_LABEL_VRING_SEG0, PS3GL_LABEL_VRING_SEG1
    };

    ps3gl.vring.seg_capacity = PS3GL_VRING_SIZE / PS3GL_VRING_SEGMENTS;
    ps3gl.vring.base = (uint8_t *)rsxMemalign(128, PS3GL_VRING_SIZE);
    if (!ps3gl.vring.base) {
        ps3gl_log("[ps3gl] FATAL: failed to allocate vertex ring buffer (%u bytes)\n",
               PS3GL_VRING_SIZE);
        return;
    }
    rsxAddressToOffset(ps3gl.vring.base, &ps3gl.vring.base_off);
    ps3gl.vring.head    = 0;
    ps3gl.vring.cur_seg = 0;

    /* One backend label per segment -- RSX only writes fence_val here once the pipeline
     * fully drains. begin_frame waits on it before reusing that segment's head. */
    for (int i = 0; i < PS3GL_VRING_SEGMENTS; i++) {
        ps3gl.vring.fence_label[i] = (volatile uint32_t *)gcmGetLabelAddress(seg_labels[i]);
        ps3gl.vring.fence_val[i]   = 0;
        if (ps3gl.vring.fence_label[i])
            *ps3gl.vring.fence_label[i] = 0;
    }

    ps3gl_log("[ps3gl] Vertex ring buffer: %u bytes (%d x %u) at %p (offset 0x%08x)\n",
           PS3GL_VRING_SIZE, PS3GL_VRING_SEGMENTS, ps3gl.vring.seg_capacity,
           ps3gl.vring.base, ps3gl.vring.base_off);
}

void ps3gl_hring_init(void)
{
    uint32_t io_offset = 0;
    s32 ret;

    ps3gl.hring.seg_capacity = PS3GL_HRING_SIZE / PS3GL_VRING_SEGMENTS;
    ps3gl.hring.head   = 0;
    ps3gl.hring.mapped = 0;

    /* 1 MB alignment: that is the IO-mapping granularity the RSX uses for main
     * memory, so anything less can fail to map or map neighbouring data. */
    ps3gl.hring.base = (uint8_t *)memalign(1024 * 1024, PS3GL_HRING_SIZE);
    if (!ps3gl.hring.base) {
        ps3gl_log("[ps3gl] host ring: memalign(%u) FAILED -- studio arrays stay in BSS\n",
                  PS3GL_HRING_SIZE);
        return;
    }

    ret = gcmMapMainMemory(ps3gl.hring.base, PS3GL_HRING_SIZE, &io_offset);
    if (ret != 0) {
        ps3gl_log("[ps3gl] host ring: gcmMapMainMemory ret=%d -- studio arrays stay in BSS\n",
                  (int)ret);
        free(ps3gl.hring.base);
        ps3gl.hring.base = NULL;
        return;
    }

    ps3gl.hring.mapped = 1;
    ps3gl_log("[ps3gl] host ring: %u bytes (%d x %u) at %p (io offset 0x%08x)\n",
              PS3GL_HRING_SIZE, PS3GL_VRING_SEGMENTS, ps3gl.hring.seg_capacity,
              ps3gl.hring.base, (unsigned)io_offset);
}

void ps3gl_hring_shutdown(void)
{
    if (ps3gl.hring.base) {
        free(ps3gl.hring.base);
        ps3gl.hring.base = NULL;
        ps3gl.hring.mapped = 0;
    }
}

void *ps3gl_hring_reserve(uint32_t bytes)
{
    if (!ps3gl.hring.mapped || bytes == 0)
        return NULL;

    if (ps3gl.hring.head + bytes > ps3gl.hring.seg_capacity) {
        /* Refuse to wrap, as the vring: that would stomp data the RSX has not fetched yet. The caller falls back
         * to BSS for this run, correct but slow, so it is counted and reported. */
        ps3gl.hring.frame_drops++;
        ps3gl.hring.total_drops++;
        return NULL;
    }

    return ps3gl.hring.base
         + (uint32_t)ps3gl.vring.cur_seg * ps3gl.hring.seg_capacity
         + ps3gl.hring.head;
}

void ps3gl_hring_commit(uint32_t bytes)
{
    if (!ps3gl.hring.mapped)
        return;

    ps3gl.hring.head = (ps3gl.hring.head + bytes + 127u) & ~127u;
    if (ps3gl.hring.head > ps3gl.hring.peak_head)
        ps3gl.hring.peak_head = ps3gl.hring.head;
}

void ps3gl_vring_shutdown(void)
{
    if (ps3gl.vring.base) {
        rsxFree(ps3gl.vring.base);
        ps3gl.vring.base = NULL;
    }
}

ps3gl_vertex_t *ps3gl_vring_alloc(int count, uint32_t *out_offset)
{
    uint32_t needed = (uint32_t)(count * PS3GL_VERTEX_SIZE);

    if (ps3gl.vring.head + needed > ps3gl.vring.seg_capacity) {
        /* Segment full: refuse to wrap onto data the GPU has not fetched (submission runs ahead). A drop is missing
         * geometry, a PS3GL_VRING_SIZE sizing bug: ps3gl_end_frame reports it per frame, not once per process. */
        ps3gl.vring.frame_drops++;
        ps3gl.vring.total_drops++;
        return NULL;
    }

    uint32_t seg_base = (uint32_t)ps3gl.vring.cur_seg * ps3gl.vring.seg_capacity;
    ps3gl_vertex_t *ptr = (ps3gl_vertex_t *)(ps3gl.vring.base + seg_base + ps3gl.vring.head);
    *out_offset = ps3gl.vring.base_off + seg_base + ps3gl.vring.head;

    /* Round up to 16-byte alignment -- every carve must self-align its own start or
     * attribute sub-offsets (TC0/TC1/COLOR) drift and RSX vertex fetch wedges the whole console. */
    ps3gl.vring.head = (ps3gl.vring.head + needed + 15u) & ~15u;

    if (ps3gl.vring.head > ps3gl.vring.peak_head)
        ps3gl.vring.peak_head = ps3gl.vring.head;

    return ptr;
}

void *ps3gl_vring_alloc_raw(uint32_t bytes, uint32_t *out_offset)
{
    if (bytes == 0 || !ps3gl.vring.base)
        return NULL;

    if (ps3gl.vring.head + bytes > ps3gl.vring.seg_capacity) {
        /* Same policy as ps3gl_vring_alloc: drop rather than wrap onto data
         * the GPU has not fetched yet, and count it so end_frame reports it. */
        ps3gl.vring.frame_drops++;
        ps3gl.vring.total_drops++;
        return NULL;
    }

    uint32_t seg_base = (uint32_t)ps3gl.vring.cur_seg * ps3gl.vring.seg_capacity;
    void *ptr = ps3gl.vring.base + seg_base + ps3gl.vring.head;
    *out_offset = ps3gl.vring.base_off + seg_base + ps3gl.vring.head;

    ps3gl.vring.head = (ps3gl.vring.head + bytes + 15u) & ~15u;

    if (ps3gl.vring.head > ps3gl.vring.peak_head)
        ps3gl.vring.peak_head = ps3gl.vring.head;

    return ptr;
}

/* GL primitive type -> GCM primitive type */

uint32_t ps3gl_gl_to_gcm_prim(GLenum mode)
{
    switch (mode) {
    case GL_POINTS:         return GCM_TYPE_POINTS;
    case GL_LINES:          return GCM_TYPE_LINES;
    case GL_LINE_LOOP:      return GCM_TYPE_LINE_LOOP;
    case GL_LINE_STRIP:     return GCM_TYPE_LINE_STRIP;
    case GL_TRIANGLES:      return GCM_TYPE_TRIANGLES;
    case GL_TRIANGLE_STRIP: return GCM_TYPE_TRIANGLE_STRIP;
    case GL_TRIANGLE_FAN:   return GCM_TYPE_TRIANGLE_FAN;
    case GL_QUADS:          return GCM_TYPE_QUADS;
    case GL_QUAD_STRIP:     return GCM_TYPE_QUAD_STRIP;
    case GL_POLYGON:        return GCM_TYPE_TRIANGLE_FAN;
    default:                return GCM_TYPE_TRIANGLES;
    }
}

/* Submit accumulated vertices to RSX */

/* Defined in ps3gl_main.c: the real per-frame draw-call counter. flush_immediate is the path every glBegin/glEnd
 * triangle takes; ps3gl_frame_draw_count below only counts glDrawElements, which xash never calls. */
extern uint32_t ps3gl_frame_flush_count;
extern uint32_t ps3gl_frame_flush_vertex_count;
/* GL_POLYGON draws merged into a batch per frame (ps3gl_main.c window): the only way to tell "batching works"
 * from "the state-clean gate never passes". */
extern uint32_t ps3gl_frame_batched_poly_count;

/* submit_ranges: one vring copy and attribute bind, then nranges draws on that binding (it is persistent register
 * state). apply_state 0 for a batch flush: its state is in the FIFO already; applying now drew batches a state late. */

/* Binds the four ps3gl_vertex_t attributes of a vring block, vertices stride bytes apart.
 * NotDC P25: the colour attribute starts color_shift vertices later (flat shading from the RSX's first vertex). */
static void bind_vertex_attribs(gcmContextData *ctx, uint32_t vb_offset, uint32_t stride, uint32_t color_shift)
{
    const int tc1_u8 = ps3gl.active_shader >= PS3GL_TENV_BUMP_REPLACE;

    /* PS3GL_FAST_COMPARE: the binds are register state, so a draw from the same block with the same stride,
     * colour shift and texcoord1 form keeps them. */
    if (ps3gl.fast & PS3GL_FAST_COMPARE) {
        if (ps3gl.vattr_valid && ps3gl.vattr_off == vb_offset && ps3gl.vattr_stride == stride &&
            ps3gl.vattr_shift == color_shift && ps3gl.vattr_tc1_u8 == tc1_u8)
            return;
        ps3gl.vattr_valid  = 1;
        ps3gl.vattr_off    = vb_offset;
        ps3gl.vattr_stride = stride;
        ps3gl.vattr_shift  = color_shift;
        ps3gl.vattr_tc1_u8 = tc1_u8;
    }

    rsxBindVertexArrayAttrib(ctx, GCM_VERTEX_ATTRIB_POS, 0, vb_offset + PS3GL_VATTR_POS_OFF,
                             stride, 4, GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_RSX);
    rsxBindVertexArrayAttrib(ctx, GCM_VERTEX_ATTRIB_TEX0, 0, vb_offset + PS3GL_VATTR_TC0_OFF,
                             stride, 2, GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_RSX);
    /* NotDC 3j: a bump program reads u1's 4 bytes (the offset colour) as normalised RGBA, as COLOR0 is read. */
    if (tc1_u8)
        rsxBindVertexArrayAttrib(ctx, GCM_VERTEX_ATTRIB_TEX1, 0, vb_offset + PS3GL_VATTR_TC1_OFF,
                                 stride, 4, GCM_VERTEX_DATA_TYPE_U8, GCM_LOCATION_RSX);
    else
        rsxBindVertexArrayAttrib(ctx, GCM_VERTEX_ATTRIB_TEX1, 0, vb_offset + PS3GL_VATTR_TC1_OFF,
                                 stride, 2, GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_RSX);
    rsxBindVertexArrayAttrib(ctx, GCM_VERTEX_ATTRIB_COLOR0, 0, vb_offset + PS3GL_VATTR_COLOR_OFF + color_shift * stride,
                             stride, 4, GCM_VERTEX_DATA_TYPE_U8, GCM_LOCATION_RSX);
}

/* Draws [first, first+count) of a caller-filled vring block under the current GL state. The
 * vertices are in ps3gl_vertex_t form, stride bytes apart; nothing is copied here. */
void ps3gl_draw_vb_shift(GLenum mode, uint32_t vb_offset, uint32_t stride, int first, int count, uint32_t color_shift)
{
    if (count <= 0) return;
    ps3gl_flush_polygon_batch();
    ps3gl_apply_states();
    ps3gl_apply_matrices();
    ps3gl_apply_textures();
    ps3gl_apply_shader();

    gcmContextData *ctx = ps3gl_get_ctx();
    if (!ctx) return;
    ps3gl_frame_flush_count++;
    ps3gl_frame_flush_vertex_count += (uint32_t)count;
    bind_vertex_attribs(ctx, vb_offset, stride, color_shift);
    rsxDrawVertexArray(ctx, ps3gl_gl_to_gcm_prim(mode), first, count);
}

void ps3gl_draw_vb_stride(GLenum mode, uint32_t vb_offset, uint32_t stride, int first, int count)
{
    ps3gl_draw_vb_shift(mode, vb_offset, stride, first, count, 0);
}

void ps3gl_draw_vb(GLenum mode, uint32_t vb_offset, int first, int count)
{
    ps3gl_draw_vb_stride(mode, vb_offset, PS3GL_VERTEX_SIZE, first, count);
}

static void submit_ranges(int total_verts, const int *first, const int *cnt,
                           int nranges, uint32_t gcm_prim, int apply_state)
{
    if (total_verts <= 0 || nranges <= 0) return;

    if (apply_state) {
        ps3gl_apply_states();
        ps3gl_apply_matrices();
        ps3gl_apply_textures();
        ps3gl_apply_shader();
    }

    /* Copy vertices into ring buffer */
    uint32_t vb_offset;
    ps3gl_vertex_t *dst = ps3gl_vring_alloc(total_verts, &vb_offset);
    if (!dst) return;
    memcpy(dst, ps3gl.imm.buf, (size_t)total_verts * PS3GL_VERTEX_SIZE);

    gcmContextData *ctx = ps3gl_get_ctx();
    if (!ctx) return;

    bind_vertex_attribs(ctx, vb_offset, PS3GL_VERTEX_SIZE, 0);

    /* Draw -- one call per range, all against the single binding above */
    for (int i = 0; i < nranges; i++)
        rsxDrawVertexArray(ctx, gcm_prim, first[i], cnt[i]);
}

static void flush_immediate(void)
{
    int n = ps3gl.imm.count;
    if (n == 0) return;
    ps3gl_frame_flush_count++;
    ps3gl_frame_flush_vertex_count += (uint32_t)n;

    int first0 = 0;
    submit_ranges(n, &first0, &n, 1, ps3gl_gl_to_gcm_prim(ps3gl.imm.prim), 1);
}

/* Force-submits a pending GL_POLYGON batch. Must run before anything else writes the GCM FIFO or ends the frame:
 * glBegin (non-GL_POLYGON), glClear, glDrawElements and ps3gl_end_frame call it. */
void ps3gl_flush_polygon_batch(void)
{
    if (!ps3gl.imm.batch_active) return;

    ps3gl_frame_flush_count++;
    ps3gl_frame_flush_vertex_count += (uint32_t)ps3gl.imm.count;
    submit_ranges(ps3gl.imm.count, ps3gl.imm.batch_first, ps3gl.imm.batch_count,
                  ps3gl.imm.batch_nranges, GCM_TYPE_TRIANGLE_FAN, /*apply_state=*/0);

    ps3gl.imm.batch_active  = 0;
    ps3gl.imm.batch_nranges = 0;
    ps3gl.imm.count         = 0;
}

/* May the polygon that just ended join the pending batch? Mirrors the exact conditions of the ps3gl_apply_* calls
 * (apply_shader has no dirty flag); a drift merges polygons across a real state change, so keep it in lockstep. */
static int batch_state_is_clean(void)
{
    if (ps3gl.dirty != 0) return 0;
    if (ps3gl.mv.dirty || ps3gl.proj.dirty) return 0;

    for (int i = 0; i < PS3GL_MAX_TMUS; i++) {
        ps3gl_tmu_t *tmu = &ps3gl.tmu[i];
        ps3gl_texture_t *t = tmu->bound;
        if (!tmu->enabled || !t || !t->data) {
            if (tmu->dirty) return 0;
        } else {
            if (tmu->dirty || t->dirty) return 0;
        }
    }

    int key = ps3gl_shader_key();
    if (key < 0 || key >= PS3GL_TENV_COUNT) key = PS3GL_TENV_DISABLED;
    if (key != ps3gl.active_shader) return 0;
    if (ps3gl.shaders[key].vp != ps3gl.active_vp) return 0;
    if (ps3gl_get_mvp_generation() != ps3gl.mvp_uploaded_gen) return 0;

    return 1;
}

/* GL immediate mode functions */

void glBegin(GLenum mode)
{
    /* A pending batch holds only polygons whose state is in the FIFO already, so any other primitive, or low batch
     * headroom, flushes it now: the imm.count reset below would drop it silently. */
    if (ps3gl.imm.batch_active &&
        (mode != GL_POLYGON ||
         ps3gl.imm.batch_nranges >= PS3GL_MAX_BATCH_RANGES ||
         ps3gl.imm.count > PS3GL_MAX_VERTS - PS3GL_BATCH_VERT_MARGIN))
        ps3gl_flush_polygon_batch();

    if (!ps3gl.imm.batch_active)
        ps3gl.imm.count = 0;
    ps3gl.imm.vert_start = ps3gl.imm.count;
    ps3gl.imm.prim = mode;
}

void glEnd(void)
{
    int n = ps3gl.imm.count - ps3gl.imm.vert_start;

    if (ps3gl.imm.prim != GL_POLYGON) {
        /* Any other primitive draws at once: this glBegin already flushed a pending batch. */
        flush_immediate();
        ps3gl.imm.count = 0;
        return;
    }
    if (n <= 0) return;

    if (batch_state_is_clean()) {
        ps3gl.imm.batch_first[ps3gl.imm.batch_nranges] = ps3gl.imm.vert_start;
        ps3gl.imm.batch_count[ps3gl.imm.batch_nranges] = n;
        ps3gl.imm.batch_nranges++;
        ps3gl.imm.batch_active = 1;
        ps3gl_frame_batched_poly_count++;
        /* Deliberately does NOT reset imm.count -- these vertices stay in
         * buf[] so the next GL_POLYGON glBegin can keep appending after them. */
        return;
    }

    /* The state changed: flush the batch under its own state (count cut to its vertices), then draw this polygon
     * alone, which applies the new state. Nothing stays pending. */
    ps3gl.imm.count = ps3gl.imm.vert_start;
    ps3gl_flush_polygon_batch();
    memmove(ps3gl.imm.buf, &ps3gl.imm.buf[ps3gl.imm.vert_start],
            (size_t)n * PS3GL_VERTEX_SIZE);
    ps3gl.imm.count = n;
    flush_immediate();
    ps3gl.imm.count = 0;
}

/* u1 and v1 go as bits: ps3gl_tex1_bits() puts a byte pattern in u1, not a float. */
void glVertex2f(GLfloat x, GLfloat y)
{
    if (ps3gl.imm.count >= PS3GL_MAX_VERTS) return;
    ps3gl_vertex_t *v = &ps3gl.imm.buf[ps3gl.imm.count++];
    v->x  = x;  v->y  = y;  v->z = 0.0f; v->w = 1.0f;
    v->u0 = ps3gl.imm.u0; v->v0 = ps3gl.imm.v0;
    memcpy(&v->u1, &ps3gl.imm.u1, 4); memcpy(&v->v1, &ps3gl.imm.v1, 4);
    v->color = ps3gl.imm.color;
}

void glVertex3f(GLfloat x, GLfloat y, GLfloat z)
{
    if (ps3gl.imm.count >= PS3GL_MAX_VERTS) return;
    ps3gl_vertex_t *v = &ps3gl.imm.buf[ps3gl.imm.count++];
    v->x  = x;  v->y  = y;  v->z = z; v->w = 1.0f;
    v->u0 = ps3gl.imm.u0; v->v0 = ps3gl.imm.v0;
    memcpy(&v->u1, &ps3gl.imm.u1, 4); memcpy(&v->v1, &ps3gl.imm.v1, 4);
    v->color = ps3gl.imm.color;
}

void glVertex3fv(const GLfloat *vv)
{
    glVertex3f(vv[0], vv[1], vv[2]);
}

void glTexCoord2f(GLfloat s, GLfloat t)
{
    ps3gl.imm.u0 = s;
    ps3gl.imm.v0 = t;
}

void glTexCoord2fv(const GLfloat *v)
{
    ps3gl.imm.u0 = v[0];
    ps3gl.imm.v0 = v[1];
}

void glMultiTexCoord2fARB(GLenum target, GLfloat s, GLfloat t)
{
    int tmu = (int)(target - GL_TEXTURE0_ARB);
    if (tmu == 0) {
        ps3gl.imm.u0 = s;
        ps3gl.imm.v0 = t;
    } else if (tmu == 1) {
        ps3gl.imm.u1 = s;
        ps3gl.imm.v1 = t;
    }
}

/* Frame draw count tracking */
static int ps3gl_frame_draw_count = 0;

void glFinish(void) { ps3gl_frame_draw_count = 0; }
void ps3gl_inc_draw_count(void) { ps3gl_frame_draw_count++; }
void glFlush(void) {}
void glDrawBuffer(GLenum mode) { (void)mode; }

/* Defined by the video layer; declared here, not in a shared header, as ps3gl_report_flip_wait is (ps3gl.h):
 * that file cannot be included across the ref_gl/ps3gl module boundary. */
extern void *PS3_RSX_GetColorBuffer(uint32_t *pitch, uint32_t *width, uint32_t *height);

/* RGBA bytes from the A8R8G8B8 colour surface (bytes A,R,G,B per word on this big-endian target), flipped to GL's
 * bottom-up rows; its xash caller (the saveshot capture) flips back with IMAGE_FLIP_Y. */
void glReadPixels(GLint x, GLint y, GLsizei w, GLsizei h,
                      GLenum format, GLenum type, void *pixels)
{
    uint32_t pitch, sw, sh;
    const uint8_t *base;
    uint8_t *dst = (uint8_t *)pixels;

    if (format != GL_RGBA || type != GL_UNSIGNED_BYTE)
        return;

    base = (const uint8_t *)PS3_RSX_GetColorBuffer(&pitch, &sw, &sh);
    if (!base)
        return;

    for (GLint row = 0; row < h; row++) {
        GLint sy = (GLint)sh - 1 - (y + row);
        uint8_t *drow = dst + (size_t)row * w * 4;

        if (sy < 0 || sy >= (GLint)sh) {
            memset(drow, 0, (size_t)w * 4);
            continue;
        }

        const uint32_t *srow = (const uint32_t *)(base + (size_t)sy * pitch);

        for (GLint col = 0; col < w; col++) {
            GLint sx = x + col;

            if (sx < 0 || sx >= (GLint)sw) {
                drow[col * 4 + 0] = drow[col * 4 + 1] = drow[col * 4 + 2] = drow[col * 4 + 3] = 0;
                continue;
            }

            uint32_t argb = srow[sx];
            drow[col * 4 + 0] = (uint8_t)(argb >> 16); /* R */
            drow[col * 4 + 1] = (uint8_t)(argb >> 8);  /* G */
            drow[col * 4 + 2] = (uint8_t)argb;         /* B */
            drow[col * 4 + 3] = (uint8_t)(argb >> 24); /* A */
        }
    }
}
