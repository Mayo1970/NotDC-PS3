// Bundle 14 PC check (WSL): random GL calls of NotDC's kinds through ps3gl onto a model of the RSX registers.
// "run": a hash of the RSX calls (run.sh: rows OFF = the old files). "cmp": rows ON hold the registers of rows OFF.
#include "ps3gl.h"
// Bundle 17: the table of the dc programs (the offset colour, the clamp and the weight), for check_dc().
#include "ps3gl_shader_dc.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

// ---- the RSX: registers, a hash of the calls, snapshots ---------------------------

enum
{
	R_BLEND_EN, R_BLEND_FUNC, R_BLEND_EQ = R_BLEND_FUNC + 4, R_ALPHA_EN = R_BLEND_EQ + 2, R_ALPHA_FUNC, R_ALPHA_REF,
	R_DEPTH_TEST, R_DEPTH_FUNC, R_DEPTH_WRITE, R_CULL_EN, R_CULL_FACE, R_FRONT_FACE, R_SCISSOR,
	R_VIEWPORT = R_SCISSOR + 4, R_VP_RANGE = R_VIEWPORT + 4, R_VP_SCALE = R_VP_RANGE + 2, R_VP_OFFSET = R_VP_SCALE + 4,
	R_VP_CLIP = R_VP_OFFSET + 4, R_COLOR_MASK = R_VP_CLIP + 3, R_POLY_EN, R_POLY, R_SHADE = R_POLY + 2, R_STENCIL_EN,
	R_STENCIL_FUNC, R_STENCIL_OP = R_STENCIL_FUNC + 3, R_STENCIL_MASK = R_STENCIL_OP + 3, R_CLEAR_COLOR, R_CLEAR_DS,
	// Per texture unit: 11 words of the texture, then control (4), filter (4), wrap (6).
	R_TEX, R_TEX_CTRL = 11, R_TEX_FILTER = 15, R_TEX_WRAP = 19, R_TEX_WORDS = 25, R_TEX_UNITS = 4,
	R_VPROG = R_TEX + R_TEX_UNITS * R_TEX_WORDS, R_FPROG, R_FPROG_LOC, R_CONST, R_CONSTS = 8,
	// Per vertex attribute: offset, stride, elements, type, location, frequency.
	R_ATTR = R_CONST + R_CONSTS, R_ATTR_WORDS = 6, R_ATTRS = 16,
	R_INVALIDATES = R_ATTR + R_ATTRS * R_ATTR_WORDS, R_COUNT
};
static uint32_t reg[R_COUNT];

static const char* reg_name(int r)
{
	static char buf[48];
	if (r >= R_ATTR && r < R_INVALIDATES)
		snprintf(buf, sizeof(buf), "attribute %d word %d", (r - R_ATTR) / R_ATTR_WORDS, (r - R_ATTR) % R_ATTR_WORDS);
	else if (r >= R_TEX && r < R_VPROG)
		snprintf(buf, sizeof(buf), "texture unit %d word %d", (r - R_TEX) / R_TEX_WORDS, (r - R_TEX) % R_TEX_WORDS);
	else
		snprintf(buf, sizeof(buf), r < R_TEX ? "state word %d" : r < R_ATTR ? "program word %d" : "texture cache clears", r);
	return buf;
}

static void die(const char* fmt, ...) __attribute__((format(printf, 1, 2), noreturn));
static void die(const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	printf("FAIL: ");
	vprintf(fmt, ap);
	printf("\n");
	va_end(ap);
	exit(1);
}

static uint32_t fbits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

static uint64_t g_call_hash;
static uint64_t g_calls;
static void note(uint32_t op, int n, ...)
{
	va_list ap;
	va_start(ap, n);
	g_call_hash = (g_call_hash ^ op) * 0x100000001B3ull;
	for (int k = 0; k < n; k++)
		g_call_hash = (g_call_hash ^ va_arg(ap, uint32_t)) * 0x100000001B3ull;
	va_end(ap);
	g_calls++;
}

enum { K_DRAW, K_DRAW_INLINE, K_DRAW_INDEX, K_CLEAR, K_FRAME };
typedef struct
{
	uint32_t kind, a[4];
	uint32_t reg[R_COUNT];
} Snap;
static Snap* g_base;       // the run at bits 0: every draw and clear, and the end of every frame
static uint32_t g_nbase, g_cap, g_nsnap;
static int g_compare;      // this run is held against g_base
// The compares are on for all of this run: a sampler word or a set of attribute binds that changes nothing is a fault.
static int g_minimal;
static const char* g_what = "";

static void snap(uint32_t kind, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
	Snap s;
	s.kind = kind;
	s.a[0] = a0; s.a[1] = a1; s.a[2] = a2; s.a[3] = a3;
	memcpy(s.reg, reg, sizeof(reg));
	const uint32_t i = g_nsnap++;
	if (!g_compare)
	{
		if (i >= g_cap)
		{
			g_cap = g_cap ? g_cap * 2 : 4096;
			g_base = (Snap*)realloc(g_base, (size_t)g_cap * sizeof(Snap));
			if (!g_base)
				die("out of memory");
		}
		g_base[i] = s;
		g_nbase = g_nsnap;
		return;
	}
	if (i >= g_nbase)
		die("%s: more draws than the run at 0 (%u)", g_what, g_nbase);
	const Snap* b = &g_base[i];
	if (b->kind != s.kind || memcmp(b->a, s.a, sizeof(s.a)))
		die("%s: draw %u is kind %u (%u %u %u %u), the run at 0 has kind %u (%u %u %u %u)", g_what, i, s.kind, s.a[0],
		    s.a[1], s.a[2], s.a[3], b->kind, b->a[0], b->a[1], b->a[2], b->a[3]);
	for (int r = 0; r < R_COUNT; r++)
		if (b->reg[r] != s.reg[r])
			die("%s: at draw %u (kind %u) the RSX holds %08X in %s, the run at 0 holds %08X", g_what, i, s.kind,
			    s.reg[r], reg_name(r), b->reg[r]);
}

// RSX memory: each block has its own offsets, in the order of the requests, so two runs give the same ones.
typedef struct
{
	uint8_t* p;
	uint32_t size, off;
} Block;
static Block g_blk[8192];
static int g_nblk;
static uint32_t g_next_off;

void* rsxMemalign(uint32_t alignment, uint32_t size)
{
	if (g_nblk >= 8192)
		die("too many RSX blocks");
	if (alignment < 16)
		alignment = 16;
	uint8_t* p = (uint8_t*)aligned_alloc(alignment, (size + alignment - 1) / alignment * alignment);
	if (!p)
		die("out of memory");
	const Block b = { p, size, g_next_off };
	g_blk[g_nblk++] = b;
	g_next_off += (size + 4095u) & ~4095u;
	return p;
}

void rsxFree(void* ptr)
{
	for (int k = 0; k < g_nblk; k++)
		if (g_blk[k].p == ptr)
		{
			free(ptr);
			g_blk[k] = g_blk[--g_nblk];
			return;
		}
	die("rsxFree of an unknown block");
}

int32_t rsxAddressToOffset(const void* ptr, uint32_t* offset)
{
	for (int k = 0; k < g_nblk; k++)
		if ((const uint8_t*)ptr >= g_blk[k].p && (const uint8_t*)ptr < g_blk[k].p + g_blk[k].size)
		{
			*offset = g_blk[k].off + (uint32_t)((const uint8_t*)ptr - g_blk[k].p);
			return 0;
		}
	return -1;
}

static uint32_t g_label[256];
uint32_t* gcmGetLabelAddress(uint8_t index) { return &g_label[index]; }
// The RSX writes the label when it reaches the command: at once here, so no fence wait.
void rsxSetWriteBackendLabel(gcmContextData* ctx, uint8_t index, uint32_t value) { (void)ctx; g_label[index] = value; }
void rsxFinish(gcmContextData* ctx, uint32_t ref) { (void)ctx; (void)ref; }
int32_t gcmMapMainMemory(const void* address, uint32_t size, uint32_t* offset)
{
	(void)address; (void)size;
	*offset = 0x0E000000;
	return 0;
}

void rsxSetBlendEnable(gcmContextData* c, uint32_t e) { (void)c; reg[R_BLEND_EN] = e; note(1, 1, e); }
void rsxSetBlendFunc(gcmContextData* c, uint16_t sc, uint16_t dc, uint16_t sa, uint16_t da)
{
	(void)c;
	reg[R_BLEND_FUNC] = sc; reg[R_BLEND_FUNC + 1] = dc; reg[R_BLEND_FUNC + 2] = sa; reg[R_BLEND_FUNC + 3] = da;
	note(2, 4, (uint32_t)sc, (uint32_t)dc, (uint32_t)sa, (uint32_t)da);
}
void rsxSetBlendEquation(gcmContextData* c, uint16_t col, uint16_t a)
{
	(void)c;
	reg[R_BLEND_EQ] = col; reg[R_BLEND_EQ + 1] = a;
	note(3, 2, (uint32_t)col, (uint32_t)a);
}
void rsxSetAlphaTestEnable(gcmContextData* c, uint32_t e) { (void)c; reg[R_ALPHA_EN] = e; note(4, 1, e); }
void rsxSetAlphaFunc(gcmContextData* c, uint32_t f, uint32_t r)
{
	(void)c;
	reg[R_ALPHA_FUNC] = f; reg[R_ALPHA_REF] = r;
	note(5, 2, f, r);
}
void rsxSetDepthTestEnable(gcmContextData* c, uint32_t e) { (void)c; reg[R_DEPTH_TEST] = e; note(6, 1, e); }
void rsxSetDepthFunc(gcmContextData* c, uint32_t f) { (void)c; reg[R_DEPTH_FUNC] = f; note(7, 1, f); }
void rsxSetDepthWriteEnable(gcmContextData* c, uint32_t e) { (void)c; reg[R_DEPTH_WRITE] = e; note(8, 1, e); }
void rsxSetCullFaceEnable(gcmContextData* c, uint32_t e) { (void)c; reg[R_CULL_EN] = e; note(9, 1, e); }
void rsxSetCullFace(gcmContextData* c, uint32_t f) { (void)c; reg[R_CULL_FACE] = f; note(10, 1, f); }
void rsxSetFrontFace(gcmContextData* c, uint32_t f) { (void)c; reg[R_FRONT_FACE] = f; note(11, 1, f); }
void rsxSetScissor(gcmContextData* c, uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
	(void)c;
	reg[R_SCISSOR] = x; reg[R_SCISSOR + 1] = y; reg[R_SCISSOR + 2] = w; reg[R_SCISSOR + 3] = h;
	note(12, 4, (uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h);
}
void rsxSetViewport(gcmContextData* c, uint16_t x, uint16_t y, uint16_t w, uint16_t h, float min, float max,
                    const float scale[4], const float offset[4])
{
	(void)c;
	reg[R_VIEWPORT] = x; reg[R_VIEWPORT + 1] = y; reg[R_VIEWPORT + 2] = w; reg[R_VIEWPORT + 3] = h;
	reg[R_VP_RANGE] = fbits(min); reg[R_VP_RANGE + 1] = fbits(max);
	uint32_t h2 = 0;
	for (int k = 0; k < 4; k++)
	{
		reg[R_VP_SCALE + k] = fbits(scale[k]);
		reg[R_VP_OFFSET + k] = fbits(offset[k]);
		h2 = h2 * 31 + fbits(scale[k]) * 7 + fbits(offset[k]);
	}
	note(13, 7, (uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, fbits(min), fbits(max), h2);
}
void rsxSetViewportClip(gcmContextData* c, uint8_t sel, uint16_t w, uint16_t h)
{
	(void)c;
	reg[R_VP_CLIP] = sel; reg[R_VP_CLIP + 1] = w; reg[R_VP_CLIP + 2] = h;
	note(14, 3, (uint32_t)sel, (uint32_t)w, (uint32_t)h);
}
void rsxSetColorMask(gcmContextData* c, uint32_t m) { (void)c; reg[R_COLOR_MASK] = m; note(15, 1, m); }
void rsxSetPolygonOffsetFillEnable(gcmContextData* c, uint32_t e) { (void)c; reg[R_POLY_EN] = e; note(16, 1, e); }
void rsxSetPolygonOffset(gcmContextData* c, float f, float u)
{
	(void)c;
	reg[R_POLY] = fbits(f); reg[R_POLY + 1] = fbits(u);
	note(17, 2, fbits(f), fbits(u));
}
void rsxSetShadeModel(gcmContextData* c, uint32_t m) { (void)c; reg[R_SHADE] = m; note(18, 1, m); }
void rsxSetStencilTestEnable(gcmContextData* c, uint32_t e) { (void)c; reg[R_STENCIL_EN] = e; note(19, 1, e); }
void rsxSetStencilFunc(gcmContextData* c, uint32_t f, uint32_t r, uint32_t m)
{
	(void)c;
	reg[R_STENCIL_FUNC] = f; reg[R_STENCIL_FUNC + 1] = r; reg[R_STENCIL_FUNC + 2] = m;
	note(20, 3, f, r, m);
}
void rsxSetStencilOp(gcmContextData* c, uint32_t f, uint32_t zf, uint32_t zp)
{
	(void)c;
	reg[R_STENCIL_OP] = f; reg[R_STENCIL_OP + 1] = zf; reg[R_STENCIL_OP + 2] = zp;
	note(21, 3, f, zf, zp);
}
void rsxSetStencilMask(gcmContextData* c, uint32_t m) { (void)c; reg[R_STENCIL_MASK] = m; note(22, 1, m); }
void rsxSetClearColor(gcmContextData* c, uint32_t v) { (void)c; reg[R_CLEAR_COLOR] = v; note(23, 1, v); }
void rsxSetClearDepthStencil(gcmContextData* c, uint32_t v) { (void)c; reg[R_CLEAR_DS] = v; note(24, 1, v); }
void rsxClearSurface(gcmContextData* c, uint32_t m)
{
	(void)c;
	note(25, 1, m);
	snap(K_CLEAR, m, 0, 0, 0);
}

void rsxInvalidateTextureCache(gcmContextData* c, uint32_t type)
{
	(void)c;
	reg[R_INVALIDATES]++;
	note(26, 1, type);
}
static uint32_t* unit_reg(uint8_t index)
{
	if (index >= R_TEX_UNITS)
		die("texture unit %u", index);
	return &reg[R_TEX + index * R_TEX_WORDS];
}
void rsxLoadTexture(gcmContextData* c, uint8_t index, const gcmTexture* t)
{
	(void)c;
	uint32_t* r = unit_reg(index);
	const uint32_t w[11] = { t->format, t->mipmap, t->dimension, t->cubemap, t->remap, t->width, t->height, t->depth,
	                         t->location, t->pitch, t->offset };
	memcpy(r, w, sizeof(w));
	note(27, 12, (uint32_t)index, w[0], w[1], w[2], w[3], w[4], w[5], w[6], w[7], w[8], w[9], w[10]);
}
void rsxTextureControl(gcmContextData* c, uint8_t index, uint32_t enable, uint16_t minlod, uint16_t maxlod, uint8_t aniso)
{
	(void)c;
	uint32_t* r = unit_reg(index) + R_TEX_CTRL;
	// A unit that is off can be turned off again: that call is the old path's, not a compare.
	if (g_minimal && enable && r[0] == enable && r[1] == minlod && r[2] == maxlod && r[3] == aniso)
		die("%s: the control word of unit %u was sent again", g_what, index);
	r[0] = enable; r[1] = minlod; r[2] = maxlod; r[3] = aniso;
	note(28, 5, (uint32_t)index, enable, (uint32_t)minlod, (uint32_t)maxlod, (uint32_t)aniso);
}
void rsxTextureFilter(gcmContextData* c, uint8_t index, uint16_t bias, uint8_t min, uint8_t mag, uint8_t conv)
{
	(void)c;
	uint32_t* r = unit_reg(index) + R_TEX_FILTER;
	if (g_minimal && r[0] == bias && r[1] == min && r[2] == mag && r[3] == conv)
		die("%s: the filter word of unit %u was sent again", g_what, index);
	r[0] = bias; r[1] = min; r[2] = mag; r[3] = conv;
	note(29, 5, (uint32_t)index, (uint32_t)bias, (uint32_t)min, (uint32_t)mag, (uint32_t)conv);
}
void rsxTextureWrapMode(gcmContextData* c, uint8_t index, uint8_t s, uint8_t t, uint8_t r3, uint8_t remap, uint8_t zfunc,
                        uint8_t gamma)
{
	(void)c;
	uint32_t* r = unit_reg(index) + R_TEX_WRAP;
	if (g_minimal && r[0] == s && r[1] == t && r[2] == r3 && r[3] == remap && r[4] == zfunc && r[5] == gamma)
		die("%s: the wrap word of unit %u was sent again", g_what, index);
	r[0] = s; r[1] = t; r[2] = r3; r[3] = remap; r[4] = zfunc; r[5] = gamma;
	note(30, 7, (uint32_t)index, (uint32_t)s, (uint32_t)t, (uint32_t)r3, (uint32_t)remap, (uint32_t)zfunc, (uint32_t)gamma);
}

// Programs: a pointer has the number of its first use, so the hash is the same in two builds.
static const void* g_ptr[64];
static int g_nptr;
static uint32_t ptr_id(const void* p)
{
	for (int k = 0; k < g_nptr; k++)
		if (g_ptr[k] == p)
			return (uint32_t)k + 1;
	if (g_nptr >= 64)
		die("too many programs");
	g_ptr[g_nptr++] = p;
	return (uint32_t)g_nptr;
}
struct rsxProgramConst
{
	const rsxVertexProgram* vp;
	char name[8];
	int words;
};
static struct rsxProgramConst g_const[R_CONSTS];
static int g_nconst;

void rsxVertexProgramGetUCode(const rsxVertexProgram* vp, void** ucode, uint32_t* size) { *ucode = (void*)vp; *size = 8; }
void rsxFragmentProgramGetUCode(const rsxFragmentProgram* fp, void** ucode, uint32_t* size) { *ucode = (void*)fp; *size = 8; }
rsxProgramConst* rsxVertexProgramGetConst(const rsxVertexProgram* vp, const char* name)
{
	for (int k = 0; k < g_nconst; k++)
		if (g_const[k].vp == vp && !strcmp(g_const[k].name, name))
			return &g_const[k];
	// The fog parameter is in the fog program only; the matrix is in every one.
	if (g_nconst >= R_CONSTS)
		die("too many program constants");
	struct rsxProgramConst* c = &g_const[g_nconst++];
	c->vp = vp;
	snprintf(c->name, sizeof(c->name), "%s", name);
	c->words = !strcmp(name, "mvp") ? 16 : 4;
	return c;
}
void rsxLoadVertexProgram(gcmContextData* c, const rsxVertexProgram* vp, const void* ucode)
{
	(void)c; (void)ucode;
	reg[R_VPROG] = ptr_id(vp);
	note(31, 1, reg[R_VPROG]);
}
void rsxLoadFragmentProgramLocation(gcmContextData* c, const rsxFragmentProgram* fp, uint32_t offset, uint32_t location)
{
	(void)c; (void)fp;
	reg[R_FPROG] = offset; reg[R_FPROG_LOC] = location;
	note(32, 2, offset, location);
}
void rsxSetVertexProgramParameter(gcmContextData* c, const rsxVertexProgram* vp, const rsxProgramConst* param,
                                  const float* value)
{
	(void)c;
	if (param < g_const || param >= g_const + g_nconst || param->vp != vp)
		die("a program constant of another program");
	uint32_t h = 0x811C9DC5u;
	for (int k = 0; k < param->words; k++)
		h = (h ^ fbits(value[k])) * 0x01000193u;
	reg[R_CONST + (param - g_const)] = h;
	note(33, 2, (uint32_t)(param - g_const), h);
}

void rsxBindVertexArrayAttrib(gcmContextData* c, uint8_t attr, uint16_t frequency, uint32_t offset, uint8_t stride,
                              uint8_t elems, uint8_t dtype, uint8_t location)
{
	(void)c;
	if (attr >= R_ATTRS)
		die("vertex attribute %u", attr);
	uint32_t* r = &reg[R_ATTR + attr * R_ATTR_WORDS];
	const uint32_t w[R_ATTR_WORDS] = { offset, stride, elems, dtype, location, frequency };
	// A set of binds runs from the position to the colour.
	static int changed;
	if (attr == GCM_VERTEX_ATTRIB_POS)
		changed = 0;
	changed |= memcmp(r, w, sizeof(w)) != 0;
	if (g_minimal && attr == GCM_VERTEX_ATTRIB_COLOR0 && !changed)
		die("%s: the vertex attribute binds were sent again", g_what);
	memcpy(r, w, sizeof(w));
	note(34, 7, (uint32_t)attr, offset, (uint32_t)stride, (uint32_t)elems, (uint32_t)dtype, (uint32_t)location,
	     (uint32_t)frequency);
}
void rsxDrawVertexArray(gcmContextData* c, uint32_t type, uint32_t start, uint32_t count)
{
	(void)c;
	note(35, 3, type, start, count);
	snap(K_DRAW, type, start, count, 0);
}
void rsxDrawIndexArray(gcmContextData* c, uint8_t type, uint32_t offset, uint32_t count, uint8_t data_type, uint8_t location)
{
	(void)c;
	note(36, 5, (uint32_t)type, offset, count, (uint32_t)data_type, (uint32_t)location);
	snap(K_DRAW_INDEX, type, offset, count, data_type);
}
void rsxDrawInlineIndexArray16(gcmContextData* c, uint8_t type, uint32_t start, uint32_t count, const uint16_t* data)
{
	(void)c;
	uint32_t h = 0x811C9DC5u;
	for (uint32_t k = 0; k < count; k++)
		h = (h ^ data[k]) * 0x01000193u;
	note(37, 4, (uint32_t)type, start, count, h);
	snap(K_DRAW_INLINE, type, start, count, h);
}

// What the rest of the program gives ps3gl.
static uint32_t g_warnings;
void Con_Printf(const char* fmt, ...)
{
	char msg[512];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);
	if (strstr(msg, "WARNING") || strstr(msg, "FATAL") || strstr(msg, "FAILED"))
	{
		if (g_warnings++ < 5)
			printf("ps3gl: %s", msg);
	}
}
double Platform_DoubleTime(void) { return 0.0; }
void* PS3_RSX_GetColorBuffer(uint32_t* pitch, uint32_t* width, uint32_t* height)
{
	(void)pitch; (void)width; (void)height;
	return 0;
}

// ---- the GL calls -------------------------------------------------------------------

static uint64_t rs;
static uint32_t rnd(void)
{
	rs ^= rs << 13;
	rs ^= rs >> 7;
	rs ^= rs << 17;
	return (uint32_t)(rs >> 32);
}
static uint32_t rndn(uint32_t n) { return rnd() % n; }

#define TEXS 48
typedef struct
{
	GLuint name;
	GLint wrap_s, wrap_t, min, mag, aniso;
	int w, h, levels;
} Tex;
static Tex g_tex[TEXS];
static GLuint g_dead[8];
static int g_ndead;
static GLuint g_table;   // unit 1's own texture
static uint8_t g_pix[64 * 64 * 4];
static float g_cv[16 * 3], g_ct[16 * 2];
static uint8_t g_cc[16 * 4];
static int g_switch;   // the bits change in mid run

static const GLenum k_blend[] = { GL_ZERO, GL_ONE, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_COLOR, GL_SRC_COLOR };
static const GLenum k_cmp[] = { GL_NEVER, GL_LESS, GL_EQUAL, GL_LEQUAL, GL_GREATER, GL_NOTEQUAL, GL_GEQUAL, GL_ALWAYS };
static const GLenum k_sop[] = { GL_KEEP, GL_ZERO, GL_REPLACE, GL_INVERT };
static const GLint k_wrap[] = { GL_REPEAT, GL_CLAMP_TO_EDGE, GL_MIRRORED_REPEAT };
static const GLint k_min[] = { GL_NEAREST, GL_LINEAR, GL_LINEAR_MIPMAP_NEAREST, GL_LINEAR_MIPMAP_LINEAR };
static const GLint k_mag[] = { GL_NEAREST, GL_LINEAR };
static const GLint k_aniso[] = { 1, 16, 4 };
static const GLenum k_cap[] = { GL_BLEND, GL_ALPHA_TEST, GL_DEPTH_TEST, GL_CULL_FACE, GL_SCISSOR_TEST, GL_STENCIL_TEST,
                                GL_POLYGON_OFFSET_FILL };
#define PICK(a) ((a)[rndn(sizeof(a) / sizeof((a)[0]))])

// A setter with values from a small set, so a call often repeats what is set.
static void op_state(void)
{
	switch (rndn(19))
	{
	case 0: case 1: glEnable(PICK(k_cap)); break;
	case 2: case 3: glDisable(PICK(k_cap)); break;
	case 4: { const GLenum s = PICK(k_blend); glBlendFunc(s, PICK(k_blend)); break; }
	case 5: glAlphaFunc(rndn(2) ? GL_GREATER : GL_GEQUAL, (float)rndn(3) * 0.47f); break;
	case 6: glDepthFunc(PICK(k_cmp)); break;
	case 7: glDepthMask((GLboolean)rndn(2)); break;
	case 8: { const double n = rndn(3) * 0.25; glDepthRange(n, rndn(3) ? 1.0 : 0.5); break; }
	case 9: { const GLboolean r = (GLboolean)rndn(2), g = (GLboolean)rndn(2), b = (GLboolean)rndn(2);
	          glColorMask(r, g, b, (GLboolean)rndn(2)); break; }
	case 10: glCullFace(rndn(4) == 0 ? GL_FRONT_AND_BACK : rndn(2) ? GL_FRONT : GL_BACK); break;
	case 11: glFrontFace(rndn(2) ? GL_CW : GL_CCW); break;
	case 12: { const int x = (int)rndn(3) * 100, y = (int)rndn(3) * 50, w = 200 + (int)rndn(3) * 100;
	           glScissor(x, y, w, 100 + (int)rndn(3) * 100); break; }
	case 13: { const int x = (int)rndn(3) * 160 - 160, y = (int)rndn(3) * 40, w = 640 + (int)rndn(3) * 320;
	           glViewport(x, y, w, 480 + (int)rndn(3) * 120); break; }
	case 14: glShadeModel(rndn(2) ? GL_FLAT : GL_SMOOTH); break;
	case 15: { const float f = (float)rndn(3); glPolygonOffset(f, (float)rndn(3)); break; }
	case 16: { const GLenum f = PICK(k_cmp); const int ref = (int)rndn(2) * 0x80; glStencilFunc(f, ref, rndn(2) ? 0xFF : 3); break; }
	case 17: glStencilMask(rndn(3) == 0 ? 3 : 0xFF); break;
	default: { const GLenum f = PICK(k_sop), zf = PICK(k_sop); glStencilOp(f, zf, PICK(k_sop)); break; }
	}
}

static Tex* live_tex(void)
{
	Tex* t = &g_tex[rndn(TEXS)];
	return t->name ? t : 0;
}

static void tex_upload(Tex* t)
{
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, t->w, t->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, g_pix);
	for (int l = 1; l < t->levels; l++)
	{
		const int w = t->w >> l ? t->w >> l : 1, h = t->h >> l ? t->h >> l : 1;
		glTexImage2D(GL_TEXTURE_2D, l, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, g_pix);
	}
}

static void tex_params(const Tex* t, int vary)
{
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, t->wrap_s);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, t->wrap_t);
	// A punch-through bind of a chain texture samples level 0 only.
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, vary ? GL_LINEAR : t->min);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, t->mag);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, t->aniso);
}

static void op_tex_new(void)
{
	Tex* t = &g_tex[rndn(TEXS)];
	if (t->name)
	{
		// The first 6 stay for the whole run, so later names meet them in the table (4096 names on).
		if (t < g_tex + 6)
			return;
		g_dead[g_ndead++ % 8] = t->name;
		glDeleteTextures(1, &t->name);
		t->name = 0;
		return;
	}
	glGenTextures(1, &t->name);
	t->wrap_s = PICK(k_wrap);
	t->wrap_t = PICK(k_wrap);
	t->min = PICK(k_min);
	t->mag = PICK(k_mag);
	t->aniso = PICK(k_aniso);
	t->w = 8 << rndn(3);
	t->h = 8 << rndn(3);
	t->levels = 1;
	glActiveTextureARB(GL_TEXTURE0_ARB);
	glBindTexture(GL_TEXTURE_2D, t->name);
	tex_params(t, 0);
	if (rndn(3) == 0 && t->wrap_s != GL_MIRRORED_REPEAT && t->wrap_t != GL_MIRRORED_REPEAT)
	{
		ps3gl_texture_hint_mipmaps();
		t->levels = 1 + (int)rndn(3);
	}
	else if (rndn(4) == 0)
		ps3gl_texture_hint_no_swizzle();
	tex_upload(t);
}

static void op_tex(void)
{
	const uint32_t r = rndn(100);
	Tex* t = live_tex();
	if (r < 62)
	{
		// The renderer's bind: the texture, then its 5 parameters again.
		if (!t)
			return;
		glActiveTextureARB(GL_TEXTURE0_ARB);
		glBindTexture(GL_TEXTURE_2D, t->name);
		tex_params(t, rndn(10) == 0);
	}
	else if (r < 70)
	{
		static const GLenum k_pname[] = { GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T, GL_TEXTURE_MIN_FILTER,
		                                  GL_TEXTURE_MAG_FILTER, GL_TEXTURE_MAX_ANISOTROPY_EXT, GL_GENERATE_MIPMAP, 0x1234 };
		const GLenum pname = PICK(k_pname);
		glTexParameteri(GL_TEXTURE_2D, pname, pname == GL_TEXTURE_MIN_FILTER ? PICK(k_min) : pname == GL_TEXTURE_MAG_FILTER
		                ? PICK(k_mag) : pname == GL_TEXTURE_MAX_ANISOTROPY_EXT ? PICK(k_aniso) : PICK(k_wrap));
	}
	else if (r < 80)
		op_tex_new();
	else if (r < 83)
	{
		// A name that was deleted: the bind makes a new, empty texture of it.
		if (g_ndead)
			glBindTexture(GL_TEXTURE_2D, g_dead[rndn(g_ndead < 8 ? g_ndead : 8)]);
	}
	else if (r < 86)
		glBindTexture(GL_TEXTURE_2D, 0);
	else if (r < 90)
	{
		if (!t)
			return;
		glBindTexture(GL_TEXTURE_2D, t->name);
		if (rndn(2))
			tex_upload(t);
		else
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, t->w, rndn(2) ? t->h : t->h / 2, GL_RGBA, GL_UNSIGNED_BYTE, g_pix);
	}
	else if (r < 94)
	{
		// Unit 1: the fog table, bound and written with the unit active, as fog_upload() does. A texture is on one
		// unit only: ps3gl has one dirty flag per texture, so one on two units is a case of its own on both paths.
		glActiveTextureARB(GL_TEXTURE1_ARB);
		switch (rndn(4))
		{
		case 0:
		{
			const int fresh = !g_table;
			if (fresh)
				glGenTextures(1, &g_table);
			glBindTexture(GL_TEXTURE_2D, g_table);
			if (fresh)
			{
				ps3gl_texture_hint_no_swizzle();
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			}
			if (fresh || rndn(3) == 0)
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 64, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, g_pix);
			break;
		}
		case 1: glEnable(GL_TEXTURE_2D); break;
		case 2: glDisable(GL_TEXTURE_2D); break;
		default: ps3gl_tex_aux((int)rndn(2)); break;
		}
		glActiveTextureARB(GL_TEXTURE0_ARB);
	}
	else if (r < 97)
	{
		if (rndn(2))
			glEnable(GL_TEXTURE_2D);
		else
			glDisable(GL_TEXTURE_2D);
	}
	else
	{
		static const GLint k_env[] = { GL_MODULATE, GL_REPLACE, GL_DECAL };
		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, PICK(k_env));
	}
}

// Bundle 17: at a draw under ps3gl_dc_offs() or ps3gl_dc_xt() the RSX holds the program that compile_dc.sh's table
// names for the state (or the one with no fog), its vertex program, and the constants of that vertex program.
static uint64_t g_dc_checks, g_dc_xt_checks;

static uint32_t const_hash(const float* v)
{
	uint32_t h = 0x811C9DC5u;
	for (int k = 0; k < 4; k++)
		h = (h ^ fbits(v[k])) * 0x01000193u;
	return h;
}

static void check_dc(void)
{
	const ps3gl_tmu_t* t0 = &ps3gl.tmu[0];
	const int tmu0 = t0->enabled && t0->bound && t0->bound->data;
	if (!(ps3gl.dc_offs || ps3gl.dc_xt) || (tmu0 && t0->bump))
		return;
	const int tenv = tmu0 ? t0->texenv : PS3GL_TENV_DISABLED, offs = ps3gl.dc_offs && tmu0;
	int key = -1;
	for (int pass = 0; pass < 2 && key < 0; pass++)
		for (int i = 0; i < PS3GL_DC_PROGRAMS && key < 0; i++)
			if (shader_dc_fp[i].xt == ps3gl.dc_xt && shader_dc_fp[i].offs == offs && shader_dc_fp[i].env == tenv &&
			    shader_dc_fp[i].fog == (pass ? PS3GL_FOG_NONE : ps3gl.dc_fog))
				key = i;
	if (key < 0)
		return;
	const ps3gl_shader_t* s = &ps3gl.shaders[PS3GL_TENV_DC_FIRST + key];
	if (reg[R_FPROG] != s->fp_offset || reg[R_VPROG] != ptr_id(s->vp))
		die("a draw with dc program %d: the RSX holds fragment program %X (want %X), vertex program %X (want %X)", key,
		    reg[R_FPROG], s->fp_offset, reg[R_VPROG], ptr_id(s->vp));
	g_dc_checks++;
	if (!shader_dc_fp[key].xt && !shader_dc_fp[key].fog)
		return;
	const float fogp[4] = { ps3gl.dc_fog_scale, shader_dc_fp[key].xt ? ps3gl.dc_xt_v[8] : 0.0f, 0.0f, 0.0f };
	if (reg[R_CONST + (s->fog_const - g_const)] != const_hash(fogp))
		die("a draw with dc program %d: fogp is not (%g, %g)", key, (double)fogp[0], (double)fogp[1]);
	if (!shader_dc_fp[key].xt)
		return;
	if (reg[R_CONST + (s->cmin_const - g_const)] != const_hash(&ps3gl.dc_xt_v[0]) ||
	    reg[R_CONST + (s->cmax_const - g_const)] != const_hash(&ps3gl.dc_xt_v[4]))
		die("a draw with dc program %d: the clamp's ends are not the ones set", key);
	g_dc_xt_checks++;
}

static void op_program(void)
{
	switch (rndn(9))
	{
	case 0: case 1: ps3gl_tex_bump((int)rndn(2)); break;
	case 2: case 3: ps3gl_dc_fog((int)rndn(5)); break;
	case 4: ps3gl_dc_fog_scale((float)(1 + rndn(3))); break;
	case 5: ps3gl_dc_offs((int)rndn(2)); break;
	case 6: case 7:
	{
		const float lo[4] = { (float)rndn(3) * 0.25f, 0.0f, (float)rndn(2) * 0.5f, 0.0f };
		const float hi[4] = { 1.0f, (float)(1 + rndn(3)) * 0.25f, 1.0f, (float)(1 + rndn(2)) * 0.5f };
		ps3gl_dc_xt((int)rndn(3) != 0, lo, hi, (float)(1 + rndn(4)) * 0.25f);
		break;
	}
	default:
		glMatrixMode(rndn(2) ? GL_PROJECTION : GL_MODELVIEW);
		if (rndn(2))
			glLoadIdentity();
		else
			glTranslatef((float)rndn(3), 0.0f, 0.0f);
		break;
	}
}

// The frame's two vertex blocks: 36-byte vertices, and the SPU build's 48-byte slots.
static uint32_t g_vb_off[2];
static int g_vb_have[2];

static void op_draw(void)
{
	const uint32_t r = rndn(100);
	if (r < 55)
	{
		const int b = rndn(4) == 0;
		if (!g_vb_have[b])
		{
			const void* p = b ? ps3gl_vring_alloc_raw(300 * 48 + 128, &g_vb_off[b]) : (void*)ps3gl_vring_alloc(300, &g_vb_off[b]);
			if (!p)
				die("no vertex ring block");
			g_vb_have[b] = 1;
		}
		const int first = (int)rndn(200), count = 3 + (int)rndn(40);
		ps3gl_draw_vb_shift(rndn(2) ? GL_TRIANGLE_STRIP : GL_TRIANGLES, g_vb_off[b], b && rndn(4) ? 48 : 36, first, count,
		                    rndn(7) == 0 ? 2 : 0);
		check_dc();
	}
	else if (r < 85)
	{
		// The renderer's strips and triangles, the UI's quads. No GL_POLYGON: NotDC has none, and its batch takes in
		// more polygons with the compares (a state that stays is no longer marked), which is another set of draws.
		const uint32_t prim = rndn(3);
		glBegin(prim == 0 ? GL_QUADS : prim == 1 ? GL_TRIANGLE_STRIP : GL_TRIANGLES);
		const int nv = prim == 0 ? 4 : 3 + (int)rndn(4);
		for (int v = 0; v < nv; v++)
		{
			glColor4ub((GLubyte)rnd(), 0, 0, 255);
			glTexCoord2f((float)v, 0.0f);
			glVertex3f((float)v, 0.0f, 1.0f);
		}
		glEnd();
		check_dc();
	}
	else if (r < 92)
	{
		static const uint16_t idx[6] = { 0, 1, 2, 2, 1, 3 };
		glVertexPointer(3, GL_FLOAT, 0, g_cv);
		glTexCoordPointer(2, GL_FLOAT, 0, g_ct);
		glColorPointer(4, GL_UNSIGNED_BYTE, 0, g_cc);
		glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, idx);
		check_dc();
	}
	else if (r < 97)
	{
		if (rndn(2))
			ps3gl_apply_states();
		glClear((rndn(2) ? GL_COLOR_BUFFER_BIT : 0) | (rndn(2) ? GL_DEPTH_BUFFER_BIT : 0) | GL_STENCIL_BUFFER_BIT);
	}
	else
		ps3gl_apply_states();
}

// Before each frame: what the RSX held in the last one must not count in this one.
static void scramble(uint32_t frame)
{
	const uint32_t n = reg[R_INVALIDATES];
	for (int r = 0; r < R_COUNT; r++)
		reg[r] = 0xBAD00000u | (frame & 0xFFFFF);
	reg[R_INVALIDATES] = n;
}

static gcmContextData g_ctx;

// One run from a fresh ps3gl. bits: the state path, or -1 = a new value now and then.
static void run(uint32_t seed, uint32_t frames, int bits)
{
	rs = 0x9E3779B97F4A7C15ull ^ ((uint64_t)seed * 0xD1B54A32D192ED03ull);
	for (int k = 0; k < 8; k++)
		rnd();
	for (size_t k = 0; k < sizeof(g_pix); k++)
		g_pix[k] = (uint8_t)rnd();
	memset(g_tex, 0, sizeof(g_tex));
	g_ndead = 0;
	g_table = 0;
	while (g_nblk)
		rsxFree(g_blk[0].p);
	g_next_off = 0x01000000;
	memset(reg, 0, sizeof(reg));
	memset(g_label, 0, sizeof(g_label));
	g_call_hash = 0xCBF29CE484222325ull;
	g_calls = 0;
	g_nsnap = 0;
	g_switch = bits < 0;
	g_minimal = bits > 0 && (bits & PS3GL_FAST_COMPARE);
	if (!ps3gl_init(&g_ctx, 1280, 720))
		die("ps3gl_init");
#ifndef GLP_REF
	ps3gl_fast_path(bits < 0 ? 0 : bits);
#endif
	for (uint32_t f = 0; f < frames; f++)
	{
		scramble(f);
		ps3gl_begin_frame();
		g_vb_have[0] = g_vb_have[1] = 0;
		const uint32_t ops = 30 + rndn(90);
		for (uint32_t k = 0; k < ops; k++)
		{
			const uint32_t r = rndn(100);
			if (r < 22)
				op_state();
			else if (r < 52)
				op_tex();
			else if (r < 58)
				op_program();
			else if (r < 98)
				op_draw();
			else
			{
				// Both rows are live: the bits can change at any call.
				const int to = (int)rndn(4);
#ifndef GLP_REF
				if (g_switch)
					ps3gl_fast_path(to);
#else
				(void)to;
#endif
			}
		}
		ps3gl_end_frame();
		snap(K_FRAME, f, 0, 0, 0);
	}
	ps3gl_shutdown();
	if (g_warnings)
		die("ps3gl printed %u warnings", g_warnings);
}

int main(int argc, char** argv)
{
	if (argc < 4)
		die("usage: run SEED FRAMES BITS | cmp SEED FRAMES");
	const uint32_t seed = (uint32_t)atoi(argv[2]), frames = (uint32_t)atoi(argv[3]);
	if (!strcmp(argv[1], "run"))
	{
		run(seed, frames, argc > 4 ? atoi(argv[4]) : 0);
		printf("run %u: %u draws and clears, %llu RSX calls, hash %016llX\n", seed, g_nsnap, (unsigned long long)g_calls,
		       (unsigned long long)g_call_hash);
		return 0;
	}
	run(seed, frames, 0);
	const uint64_t calls0 = g_calls, hash0 = g_call_hash;
	uint64_t calls[5] = { calls0, 0, 0, 0, 0 };
	static const char* const k_what[5] = { "", "bits 1 (the bind table)", "bits 2 (the compares)", "bits 3 (both)",
	                                       "the bits changed in mid run" };
	g_compare = 1;
	for (int m = 1; m < 5; m++)
	{
		g_what = k_what[m];
		run(seed, frames, m < 4 ? m : -1);
		if (g_nsnap != g_nbase)
			die("%s: %u draws, the run at 0 has %u", g_what, g_nsnap, g_nbase);
		calls[m] = g_calls;
		// The bind table changes no call. The compares only leave calls out.
		if (m == 1 && (g_calls != calls0 || g_call_hash != hash0))
			die("the bind table changes the RSX calls (%llu, %llu at 0)", (unsigned long long)g_calls,
			    (unsigned long long)calls0);
		if (g_calls > calls0)
			die("%s: %llu RSX calls, more than the %llu at 0", g_what, (unsigned long long)g_calls,
			    (unsigned long long)calls0);
	}
	if (!g_dc_checks || !g_dc_xt_checks)
		die("no draw with a dc program was checked");
	printf("cmp %u: %u draws and clears; RSX calls: %llu at 0, %llu with the compares (%.1f%%), %llu with changes; "
	       "%llu draws with a dc program checked (%llu with the clamp and the weight)\n", seed,
	       g_nbase, (unsigned long long)calls0, (unsigned long long)calls[3], 100.0 * (double)calls[3] / (double)calls0,
	       (unsigned long long)calls[4], (unsigned long long)g_dc_checks, (unsigned long long)g_dc_xt_checks);
	return 0;
}
