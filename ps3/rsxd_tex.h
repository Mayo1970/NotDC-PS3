#pragma once
// RSX Direct (G17, plan 9 stage E): the emitter's own textures, with ps3gl_textures.c's layouts, uploads and words.
// Only ps3_rsxd.cpp includes it (its state takes that file's TOC entry), and the PC checks. Needs rsx.h, gl.h, rsxd.h.
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define RT_MAX   4096       // PS3GL_MAX_TEXTURES: TEX CHECK's shadows sit at ps3gl's slots
#define RT_DIM   4096       // PS3GL_MAX_TEX_DIM
#define RT_REMAP 0x00AAE4u  // PS3GL_TEX_REMAP_IDENTITY

typedef struct
{
	uint8_t* data;          // RSX memory; NULL: no texels
	uint32_t offset;
	uint32_t name;          // TEX CHECK: the ps3gl name the shadow follows
	uint32_t fixed_pitch;   // rt_wrap: the memory's pitch while the size sampled changes
	// build_gcm_texture's words as tex_obj reads them: offset, format, remap, size0, size1.
	uint32_t g_off, g_fmt, g_remap, g_size0, g_size1;
	uint16_t w, h;
	uint8_t used, swz, alloc, levels;   // swizzled; levels the memory holds; levels written in order
	uint8_t nosw, mip, ext, dirty;      // the two hints; memory not the table's; texels the texture cache lacks
	uint8_t wrap_s, wrap_t, min, mag, aniso, pad[3];
} RtTex;

static struct
{
	RtTex t[RT_MAX];
	uint32_t hint;          // no free entry under it
	uint32_t swz_x[RT_DIM], swz_y[RT_DIM], swz_w, swz_h;
	uint8_t* stage;
	uint32_t stage_size;
	// The memory each vring segment's fence covers: given up while that segment was the frame's.
	void* pend[2][RT_MAX];
	uint32_t npend[2], seg;
	// Per stats window: allocations, frees, failed allocations, levels written, early frees of a full list, bytes.
	uint32_t n_alloc, n_free, n_fail, n_up, n_over;
	uint64_t n_bytes;
} RT;

static inline uint32_t rt_dim(uint32_t base, int level)
{
	const uint32_t d = base >> level;
	return d ? d : 1;
}

static inline int rt_nlevels(uint32_t w, uint32_t h)
{
	uint32_t m = w > h ? w : h;
	int n = 1;
	while (m > 1)
	{
		m >>= 1;
		n++;
	}
	return n;
}

// A linear pitch is a multiple of 64 bytes: an 8-texel row (32 bytes) stalls the RSX.
static inline uint32_t rt_pitch(uint32_t w)
{
	return (w * 4 + 63u) & ~63u;
}

// Swizzled levels pack tight; a linear chain keeps the base pitch on every level.
static inline uint32_t rt_level_bytes(uint32_t w, uint32_t h, int level, int swz)
{
	return swz ? rt_dim(w, level) * rt_dim(h, level) * 4 : rt_pitch(w) * rt_dim(h, level);
}

static uint32_t rt_level_off(uint32_t w, uint32_t h, int level, int swz)
{
	uint32_t o = 0;
	for (int i = 0; i < level; i++)
		o += rt_level_bytes(w, h, i, swz);
	return o;
}

static inline int rt_pow2(uint32_t v)
{
	return v && !(v & (v - 1));
}

// The RSX's swizzle: x and y bits interleave from the LSB, x first, while both last (swz_ensure_tables).
static void rt_tables(uint32_t w, uint32_t h)
{
	if ((w == RT.swz_w && h == RT.swz_h) || w > RT_DIM || h > RT_DIM)
		return;
	int lw = 0, lh = 0, nx = 0, ny = 0, shift = 0, xpos[32], ypos[32];
	while ((1u << lw) < w)
		lw++;
	while ((1u << lh) < h)
		lh++;
	while (nx < lw || ny < lh)
	{
		if (nx < lw)
			xpos[nx++] = shift++;
		if (ny < lh)
			ypos[ny++] = shift++;
	}
	for (uint32_t x = 0; x < w; x++)
	{
		uint32_t o = 0;
		for (int b = 0; b < lw; b++)
			if (x & (1u << b))
				o |= 1u << xpos[b];
		RT.swz_x[x] = o;
	}
	for (uint32_t y = 0; y < h; y++)
	{
		uint32_t o = 0;
		for (int b = 0; b < lh; b++)
			if (y & (1u << b))
				o |= 1u << ypos[b];
		RT.swz_y[y] = o;
	}
	RT.swz_w = w;
	RT.swz_h = h;
}

// The bytes R, G, B, A of a GL_RGBA pixel as the RSX's A8R8G8B8 word: one load and a rotate.
static inline uint32_t rt_argb(const uint8_t* p)
{
	uint32_t w;
	memcpy(&w, p, 4);
#if __BYTE_ORDER__ != __ORDER_BIG_ENDIAN__
	w = __builtin_bswap32(w);
#endif
	return (w >> 8) | (w << 24);
}

// A level's pixels into dst: rows pitch bytes apart, or (swz) in Morton order after rt_tables(w, h).
static void rt_convert(uint8_t* dst, uint32_t pitch, const uint8_t* src, uint32_t w, uint32_t h, int swz)
{
	for (uint32_t y = 0; y < h; y++, src += w * 4)
	{
		if (swz)
		{
			uint32_t* const d = (uint32_t*)dst;
			const uint32_t yo = RT.swz_y[y];
			for (uint32_t x = 0; x < w; x++)
				d[yo | RT.swz_x[x]] = rt_argb(src + x * 4);
		}
		else
		{
			uint32_t* const d = (uint32_t*)(dst + y * pitch);
			for (uint32_t x = 0; x < w; x++)
				d[x] = rt_argb(src + x * 4);
		}
	}
}

// Swizzled levels are built in cached main memory, then one memcpy to RSX memory: scattered stores to GDDR3 are slow.
static uint8_t* rt_stage(uint32_t size)
{
	if (size > RT.stage_size)
	{
		uint8_t* const p = (uint8_t*)realloc(RT.stage, size);
		if (!p)
			return 0;
		RT.stage = p;
		RT.stage_size = size;
	}
	return RT.stage;
}

// t's memory goes once the RSX is past the draws of this frame: at a later begin that waits for this segment.
static void rt_defer(RtTex* t)
{
	if (t->data && !t->ext)
	{
		uint32_t* const n = &RT.npend[RT.seg];
		if (*n == RT_MAX)
		{
			// As ps3gl's full list: it goes now.
			for (uint32_t i = 0; i < *n; i++)
				rsxFree(RT.pend[RT.seg][i]);
			RT.n_free += *n;
			*n = 0;
			RT.n_over++;
		}
		RT.pend[RT.seg][(*n)++] = t->data;
	}
	t->data = 0;
	t->ext = 0;
}

// A frame's begin waited for vring segment seg's fence: the memory given up while seg was last the frame's goes.
static void rt_frame(uint32_t seg)
{
	RT.seg = seg & 1;
	for (uint32_t i = 0; i < RT.npend[RT.seg]; i++)
		rsxFree(RT.pend[RT.seg][i]);
	RT.n_free += RT.npend[RT.seg];
	RT.npend[RT.seg] = 0;
}

// A new entry: ps3gl_texture_alloc's sampler (clamp, linear) and the words of its zeroed gcmTexture.
static void rt_init(RtTex* t, uint32_t name)
{
	memset(t, 0, sizeof(*t));
	t->used = 1;
	t->name = name;
	t->wrap_s = t->wrap_t = GCM_TEXTURE_CLAMP_TO_EDGE;
	t->min = t->mag = GCM_TEXTURE_LINEAR;
	t->aniso = GCM_TEXTURE_MAX_ANISO_1;
	t->g_fmt = 0x8009u;
}

// The lowest free entry under lim, or -1.
static int rt_new(uint32_t lim)
{
	for (uint32_t i = RT.hint; i < lim; i++)
		if (!RT.t[i].used)
		{
			RT.hint = i + 1;
			rt_init(&RT.t[i], 0);
			return (int)i;
		}
	return -1;
}

static void rt_delete(uint32_t i)
{
	RtTex* const t = &RT.t[i];
	rt_defer(t);
	t->used = 0;
	if (i < RT.hint)
		RT.hint = i;
}

// build_gcm_texture: A8R8G8B8, swizzled or linear, the levels written, 2D, RSX memory.
static void rt_build(RtTex* t)
{
	const uint32_t fmt = GCM_TEXTURE_FORMAT_A8R8G8B8 | (t->swz ? 0 : GCM_TEXTURE_FORMAT_LIN);
	const uint32_t mip = t->levels ? t->levels : 1;
	t->g_off = t->offset;
	t->g_fmt = (GCM_LOCATION_RSX + 1) | (GCM_TEXTURE_DIMS_2D << 4) | (fmt << 8) | (mip << 16) | (1u << 3) | 0x8000u;
	t->g_remap = RT_REMAP;
	t->g_size0 = ((uint32_t)t->w << 16) | t->h;
	t->g_size1 = (1u << 20) | (t->fixed_pitch ? t->fixed_pitch : rt_pitch(t->w));
}

// tex_obj's words of t; fields: with its own sampler (tex_from_fields), else none (ps3tex_bind_rsxd sets them).
static void rt_words(const RtTex* t, uint32_t obj, int fields, RsxdTex* o)
{
	o->offset = t->g_off;
	o->format = t->g_fmt;
	o->remap = t->g_remap;
	o->size0 = t->g_size0;
	o->size1 = t->g_size1;
	const uint32_t maxlod = (uint32_t)((t->levels ? t->levels : 1) - 1) << 8;
	o->ctrl = (1u << 31) | (maxlod << 7) | (fields ? (uint32_t)t->aniso << 4 : 0);
	o->filter = fields ? ((uint32_t)t->mag << 24) | ((uint32_t)t->min << 16) | (1u << 13) : 0;
	o->wrap = fields ? t->wrap_s | ((uint32_t)t->wrap_t << 8) | (3u << 16) : 0;
	o->obj = obj;
	o->valid = t->data != 0;
	o->pad[0] = o->pad[1] = 0;
}

// glTexParameteri (ps3gl's gl_to_gcm_wrap, _filter, _aniso).
static void rt_param(RtTex* t, uint32_t pname, int v)
{
	switch (pname)
	{
	case GL_TEXTURE_WRAP_S:
	case GL_TEXTURE_WRAP_T:
	{
		const uint8_t m = v == GL_CLAMP ? GCM_TEXTURE_CLAMP : v == GL_CLAMP_TO_EDGE ? GCM_TEXTURE_CLAMP_TO_EDGE
		                : v == GL_MIRRORED_REPEAT ? GCM_TEXTURE_MIRRORED_REPEAT : GCM_TEXTURE_REPEAT;
		if (pname == GL_TEXTURE_WRAP_S)
			t->wrap_s = m;
		else
			t->wrap_t = m;
		break;
	}
	case GL_TEXTURE_MIN_FILTER:
	case GL_TEXTURE_MAG_FILTER:
	{
		const uint8_t f = v == GL_NEAREST ? GCM_TEXTURE_NEAREST : v == GL_NEAREST_MIPMAP_NEAREST
		                ? GCM_TEXTURE_NEAREST_MIPMAP_NEAREST : v == GL_LINEAR_MIPMAP_NEAREST
		                ? GCM_TEXTURE_LINEAR_MIPMAP_NEAREST : v == GL_NEAREST_MIPMAP_LINEAR
		                ? GCM_TEXTURE_NEAREST_MIPMAP_LINEAR : v == GL_LINEAR_MIPMAP_LINEAR
		                ? GCM_TEXTURE_LINEAR_MIPMAP_LINEAR : GCM_TEXTURE_LINEAR;
		if (pname == GL_TEXTURE_MIN_FILTER)
			t->min = f;
		else
			t->mag = f;
		break;
	}
	case GL_TEXTURE_MAX_ANISOTROPY_EXT:
		t->aniso = v >= 16 ? GCM_TEXTURE_MAX_ANISO_16 : v >= 12 ? GCM_TEXTURE_MAX_ANISO_12 : v >= 10
		         ? GCM_TEXTURE_MAX_ANISO_10 : v >= 8 ? GCM_TEXTURE_MAX_ANISO_8 : v >= 6 ? GCM_TEXTURE_MAX_ANISO_6 : v >= 4
		         ? GCM_TEXTURE_MAX_ANISO_4 : v >= 2 ? GCM_TEXTURE_MAX_ANISO_2 : GCM_TEXTURE_MAX_ANISO_1;
		break;
	default:
		break;
	}
}

// Level 0's memory (tex_alloc_level0): new when the size changed. Its layout is fixed then: a mirrored wrap, the
// linear hint or a size not a power of two keep it linear (the RSX stalls mirroring swizzled texels); a chain swizzled.
static int rt_alloc0(RtTex* t, uint32_t w, uint32_t h)
{
	if (t->data && (t->w != (uint16_t)w || t->h != (uint16_t)h))
	{
		rt_defer(t);
		t->fixed_pitch = 0;
	}
	t->w = (uint16_t)w;
	t->h = (uint16_t)h;
	t->levels = 0;
	if (t->data)
		return 1;
	const int mirror = t->wrap_s == GCM_TEXTURE_MIRRORED_REPEAT || t->wrap_t == GCM_TEXTURE_MIRRORED_REPEAT;
	t->swz = (uint8_t)(!mirror && !t->nosw && rt_pow2(w) && rt_pow2(h) && w <= RT_DIM && h <= RT_DIM);
	t->alloc = (uint8_t)(t->mip && t->swz ? rt_nlevels(w, h) : 1);
	const uint32_t size = rt_level_off(w, h, t->alloc, t->swz);
	t->data = (uint8_t*)rsxMemalign(128, size);
	if (!t->data)
	{
		RT.n_fail++;
		return 0;
	}
	rsxAddressToOffset(t->data, &t->offset);
	RT.n_alloc++;
	RT.n_bytes += size;
	return 1;
}

// Levels count only in order: the sampler never reaches a level not written.
static void rt_level_done(RtTex* t, int level)
{
	if (level == 0)
		t->levels = 1;
	else if (level == t->levels && t->levels < 15)
		t->levels = (uint8_t)(level + 1);
	rt_build(t);
	t->dirty = 1;
	RT.n_up++;
}

// glTexImage2D(GL_RGBA, GL_UNSIGNED_BYTE): level 0 makes the memory; a level it lacks, or of a wrong size, drops.
static void rt_image(RtTex* t, int level, uint32_t w, uint32_t h, const void* px)
{
	if (level < 0 || (level == 0 && !rt_alloc0(t, w, h)) || !t->data || level >= (t->alloc ? t->alloc : 1) ||
	    w != rt_dim(t->w, level) || h != rt_dim(t->h, level))
		return;
	const uint32_t bytes = rt_level_bytes(t->w, t->h, level, t->swz);
	uint8_t* const d = t->data + rt_level_off(t->w, t->h, level, t->swz);
	if (t->swz)
	{
		uint8_t* const stage = rt_stage(bytes);
		if (!stage)
		{
			RT.n_fail++;
			return;
		}
		rt_tables(w, h);
		if (px)
			rt_convert(stage, 0, (const uint8_t*)px, w, h, 1);
		else
			memset(stage, 0, bytes);
		memcpy(d, stage, bytes);
	}
	else if (px)
		rt_convert(d, rt_pitch(t->w), (const uint8_t*)px, w, h, 0);
	else
		memset(d, 0, bytes);
	rt_level_done(t, level);
}

// P42: level 0 as linear rows for the caller to write; NULL if swizzled or no memory. rt_direct_end after the rows.
static uint32_t* rt_direct_begin(RtTex* t, uint32_t w, uint32_t h, uint32_t* pitch)
{
	if (!rt_alloc0(t, w, h) || t->swz)
		return 0;
	*pitch = rt_pitch(t->w);
	return (uint32_t*)t->data;
}

static void rt_direct_end(RtTex* t)
{
	if (t->data)
		rt_level_done(t, 0);
}

// 5.12 #4: level 0 in its own layout for an SPU to write.
static uint8_t* rt_direct_level0(RtTex* t, uint32_t w, uint32_t h, uint32_t* pitch, int* swz)
{
	if (!rt_alloc0(t, w, h))
		return 0;
	*pitch = rt_pitch(t->w);
	*swz = t->swz;
	return t->data;
}

// P45: the levels the memory holds (1 without a chain), 0 with none.
static int rt_chain_levels(const RtTex* t)
{
	return t->data ? (t->alloc ? t->alloc : 1) : 0;
}

// P45: level `level` (1 or more) of a swizzled chain; rt_direct_level_end after the write.
static uint8_t* rt_direct_level(RtTex* t, int level)
{
	if (!t->data || !t->swz || level < 1 || level >= t->alloc)
		return 0;
	return t->data + rt_level_off(t->w, t->h, level, 1);
}

static void rt_direct_level_end(RtTex* t, int level)
{
	if (t->data && level >= 1)
		rt_level_done(t, level);
	else if (t->data)
	{
		rt_build(t);
		t->dirty = 1;
	}
}

// SPE Tex Decode CHECK: RGBA words converted into main memory in t's layout.
static void rt_convert_check(const RtTex* t, void* dst, uint32_t w, uint32_t h, const void* rgba)
{
	if (t->swz)
		rt_tables(w, h);
	rt_convert((uint8_t*)dst, rt_pitch(w), (const uint8_t*)rgba, w, h, t->swz);
}

// Linear memory the table does not own (Native Size, the SPU's 2D buffers): w x h of it, rows pitch bytes apart.
static void rt_wrap(RtTex* t, uint8_t* mem, uint32_t pitch, uint32_t w, uint32_t h)
{
	if (t->data != mem)
	{
		rt_defer(t);
		t->data = mem;
		rsxAddressToOffset(mem, &t->offset);
		t->ext = 1;
		t->swz = 0;
		t->alloc = 1;
	}
	t->fixed_pitch = pitch;
	t->w = (uint16_t)w;
	t->h = (uint16_t)h;
	t->levels = 1;
	rt_build(t);
	t->dirty = 1;
}
