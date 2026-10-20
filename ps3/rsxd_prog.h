#pragma once
// Plan 9 stage H: the programs of the emitter's keys, from the prebuilt headers in ps3/ps3gl (data; never regenerate
// them). Included by ps3_rsxd.cpp only.
#include "ps3gl_shader_data.h"
#include "ps3gl_shader_bump.h"
#include "ps3gl_shader_fog.h"
#include "ps3gl_shader_dc.h"

// The keys past the texenvs (RXE_*): dual texture, bump, the fog programs, then the dc programs.
enum
{
	RK_MODULATE2 = 6, RK_BUMP_REPLACE, RK_BUMP_MODULATE, RK_BUMP_DECAL, RK_FOGT_DISABLED, RK_FOGT_BUMP_REPLACE = 14,
	RK_FOGT_BUMP_MODULATE, RK_FOGT_BUMP_DECAL, RK_FOGV_MODULATE, RK_FOGB_DISABLED = 20, RK_FOGB_MODULATE,
	RK_FOGB_MODULATE_TA, RK_FOGB_DECAL, RK_DC_FIRST, RK_DC_PROGRAMS = 39
};
static_assert(RK_DC_FIRST + RK_DC_PROGRAMS == RSXD_KEYS, "the key count");
static_assert(sizeof(shader_dc_fp) / sizeof(shader_dc_fp[0]) == RK_DC_PROGRAMS, "the dc programs");

struct RkProg
{
	rsxVertexProgram* vp;
	void* vp_ucode;
	rsxProgramConst *mvp, *fogp, *cmin, *cmax;
	rsxFragmentProgram* fp;
	u32 fp_off;  // the ucode in the emitter's RSX memory
};

static struct
{
	RkProg k[RSXD_KEYS];
	signed char dc[2][2][5][4];  // the dc program of [clamp][offset colour][fog mode][texenv 0-3]; -1 = none
	u8* fpm;
	u32 fpm_bytes;
} RK;

static void rk_vp(RkProg& p, const unsigned char* data)
{
	u32 size;
	p.vp = (rsxVertexProgram*)data;
	rsxVertexProgramGetUCode(p.vp, &p.vp_ucode, &size);
	p.mvp = rsxVertexProgramGetConst(p.vp, "mvp");
	p.fogp = p.cmin = p.cmax = 0;
}

// The fog program of a texenv under a fog mode, -1 if there is none.
static int rk_fog_key(u32 mode, u32 env, u32 bump)
{
	switch (mode)
	{
	case RXF_TABLE:
		if (!bump)
			return env <= RXE_DECAL ? RK_FOGT_DISABLED + (int)env : -1;
		return env == RXE_REPLACE ? RK_FOGT_BUMP_REPLACE : env == RXE_MODULATE ? RK_FOGT_BUMP_MODULATE :
		       env == RXE_DECAL ? RK_FOGT_BUMP_DECAL : -1;
	case RXF_VERTEX:
		return !bump && env >= RXE_MODULATE && env <= RXE_DECAL ? RK_FOGV_MODULATE + (int)(env - RXE_MODULATE) : -1;
	case RXF_BASE:
		return bump ? -1 : env == RXE_DISABLED ? RK_FOGB_DISABLED : env == RXE_MODULATE ? RK_FOGB_MODULATE :
		       env == RXE_DECAL ? RK_FOGB_DECAL : -1;
	case RXF_BASE_TA:
		return !bump && env == RXE_MODULATE ? RK_FOGB_MODULATE_TA : -1;
	default:
		return -1;
	}
}

// The key of a state: tmu0, a texture on unit 0 with texenv env; tmu1, one on unit 1 that is no aux table.
static int rk_key(u32 tmu0, u32 env, u32 bump, u32 fog, u32 offs, u32 xt, u32 tmu1)
{
	const u32 e = tmu0 ? env : RXE_DISABLED;
	if ((offs || xt) && !(tmu0 && bump) && e <= RXE_DECAL)
	{
		const u32 o = offs && e != RXE_DISABLED;
		int k = RK.dc[xt][o][fog][e];
		if (k < 0)
			k = RK.dc[xt][o][RXF_NONE][e];
		if (k >= 0)
			return RK_DC_FIRST + k;
	}
	if (fog)
	{
		const int k = rk_fog_key(fog, e, tmu0 && bump);
		if (k >= 0)
			return k;
	}
	if (tmu0 && tmu1)
		return RK_MODULATE2;
	if (tmu0 && bump)
	{
		if (env == RXE_REPLACE)
			return RK_BUMP_REPLACE;
		if (env == RXE_MODULATE)
			return RK_BUMP_MODULATE;
		if (env == RXE_DECAL)
			return RK_BUMP_DECAL;
	}
	return tmu0 ? (int)env : RXE_DISABLED;
}

// Each key's VP and FP, the FP ucode copied into RSX memory and read back. False: no memory, or a
// copy the RSX would not read as written.
static bool rk_load()
{
	static const unsigned char* const k_base_fp[7] = {
		shader_fp_coloronly_data, shader_fp_modulate_data, shader_fp_replace_data, shader_fp_decal_data,
		shader_fp_add_data, shader_fp_blend_data, shader_fp_modulate2_data,
	};
	static const unsigned char* const k_bump_fp[3] = {
		shader_fp_bump_replace_data, shader_fp_bump_modulate_data, shader_fp_bump_decal_data,
	};
	static const unsigned char* const k_fog_fp[RK_DC_FIRST - RK_FOGT_DISABLED] = {
		shader_fp_fogt_coloronly_data, shader_fp_fogt_modulate_data, shader_fp_fogt_replace_data,
		shader_fp_fogt_decal_data, shader_fp_fogt_bump_replace_data, shader_fp_fogt_bump_modulate_data,
		shader_fp_fogt_bump_decal_data, shader_fp_fogv_modulate_data, shader_fp_fogv_replace_data,
		shader_fp_fogv_decal_data, shader_fp_fogb_coloronly_data, shader_fp_fogb_modulate_data,
		shader_fp_fogb_modulate_ta_data, shader_fp_fogb_decal_data,
	};
	const unsigned char* fp[RSXD_KEYS];
	memset(RK.dc, -1, sizeof(RK.dc));
	for (u32 k = 0; k < RSXD_KEYS; k++)
	{
		RkProg& p = RK.k[k];
		memset(&p, 0, sizeof(p));
		if (k < RK_BUMP_REPLACE)
		{
			rk_vp(p, shader_vp_data);
			fp[k] = k_base_fp[k];
		}
		else if (k < RK_FOGT_DISABLED)
		{
			rk_vp(p, shader_vp_bump_data);
			fp[k] = k_bump_fp[k - RK_BUMP_REPLACE];
		}
		else if (k < RK_DC_FIRST)
		{
			rk_vp(p, shader_vp_fog_data);
			p.fogp = rsxVertexProgramGetConst(p.vp, "fogp");
			fp[k] = k_fog_fp[k - RK_FOGT_DISABLED];
		}
		else
		{
			// The offset colour alone draws with the bump VP (no fog) or the fog VP; the clamp and the weight need the dc VP.
			const u32 i = k - RK_DC_FIRST;
			const bool xt = shader_dc_fp[i].xt != 0, fog = shader_dc_fp[i].fog != 0;
			rk_vp(p, xt ? shader_vp_dc_data : fog ? shader_vp_fog_data : shader_vp_bump_data);
			if (xt || fog)
				p.fogp = rsxVertexProgramGetConst(p.vp, "fogp");
			if (xt)
			{
				p.cmin = rsxVertexProgramGetConst(p.vp, "cmin");
				p.cmax = rsxVertexProgramGetConst(p.vp, "cmax");
			}
			fp[k] = shader_dc_fp[i].data;
			RK.dc[shader_dc_fp[i].xt][shader_dc_fp[i].offs][shader_dc_fp[i].fog][shader_dc_fp[i].env] = (signed char)i;
		}
		p.fp = (rsxFragmentProgram*)fp[k];
	}
	u32 total = 0;
	for (u32 k = 0; k < RSXD_KEYS; k++)
	{
		void* uc;
		u32 size;
		rsxFragmentProgramGetUCode(RK.k[k].fp, &uc, &size);
		total += (size + 63u) & ~63u;
	}
	if (!RK.fpm)
	{
		RK.fpm = (u8*)rsxMemalign(64, total);
		RK.fpm_bytes = total;
	}
	if (!RK.fpm || RK.fpm_bytes < total)
		return false;
	u32 at = 0;
	for (u32 k = 0; k < RSXD_KEYS; k++)
	{
		void* uc;
		u32 size;
		rsxFragmentProgramGetUCode(RK.k[k].fp, &uc, &size);
		if (size & 3)
			return false;
		u8* const d = RK.fpm + at;
		at += (size + 63u) & ~63u;
		memcpy(d, uc, size);
		const volatile u32* const v = (const volatile u32*)d;
		for (u32 i = 0; i < size / 4; i++)
		{
			u32 x;
			memcpy(&x, (const u8*)uc + i * 4, 4);
			if (v[i] != x)
				return false;
		}
		rsxAddressToOffset(d, &RK.k[k].fp_off);
	}
	return true;
}
