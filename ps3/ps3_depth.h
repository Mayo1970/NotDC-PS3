#pragma once
// P18 depth mapping. FIXED: Z24 holds z = 1 - n/W in steps of 2^-24. FLOAT: RSX float depth (f32 bits >> 7)
// holds z = n/W, 16 mantissa bits at any W. misc/depth_check tests it against a model of the RSX.

// GL compare per DC DepthMode, as an offset from GL_NEVER (GL orders its compares as the DC does).
// The DC compares 1/W; FIXED depth falls as 1/W rises, so its tests flip.
static const unsigned char k_depth_cmp[2][8] = {
	{ 0, 4, 2, 6, 1, 5, 3, 7 },
	{ 0, 1, 2, 3, 4, 5, 6, 7 },
};

static inline unsigned depth_cmp(bool fz, unsigned mode)
{
	return k_depth_cmp[fz ? 1 : 0][mode & 7];
}

// Clip z = m10 * W + m14 for the input (x*W, y*W, W), clip w = W. FLOAT's depth range (-1, 1) sets the
// viewport z scale 1 and offset 0: GL's (z/w + 1) / 2 would round n/W to the old 2^-24 step.
static inline void depth_proj(bool fz, float n, float* m10, float* m14, float* range_near)
{
	*m10 = fz ? 0.0f : 1.0f;
	*m14 = fz ? n : -2.0f * n;
	*range_near = fz ? -1.0f : 0.0f;
}

// Depth at 1/W = 0. FLOAT's 0.0 is raw 0, which rglClear's fixed-point encoding also writes.
static inline float depth_clear(bool fz)
{
	return fz ? 0.0f : 1.0f;
}

// P116 BG Depth Bias RELATIVE, FLOAT only: the background's 1/W less 1e-6 (Flycast 443d5a2ba), or less 2^-15 of it when
// that is more, so it stays 2 FLOAT steps (16 mantissa bits) behind a polygon at its 1/W. Floor 1e-11 as before.
static inline float depth_bg_rel(float b)
{
	float bg = b - 1e-6f;
	const float r = b * (1.0f - 1.0f / 32768.0f);
	if (r < bg)
		bg = r;
	return bg < 1e-11f ? 1e-11f : bg;
}
