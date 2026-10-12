#pragma once
// P13, Flycast maple_sega_purupuru: a SetCondition word as the motor's power (0..1), its slope per ms (the power at
// rem ms before the end is slope x power x rem, when the slope is above 0) and its length. Plain C types: misc/pad_check.
#include <stdlib.h>

struct PuruVib
{
	float power, inclination;
	unsigned duration_ms;
};

static inline PuruVib puru_decode(unsigned vibset, unsigned ast_ms)
{
	const int pow_pos = (vibset >> 8) & 7, pow_neg = (vibset >> 12) & 7, freq = (vibset >> 16) & 0xFF;
	const int pow_max = pow_pos > pow_neg ? pow_pos : pow_neg;
	int inc = (vibset >> 24) & 0xFF;
	if (vibset & 0x8000)          // INH: the level converges
		inc = -inc;
	else if (!(vibset & 0x0800))  // no EXH: the level is constant
		inc = 0;
	const bool cnt = (vibset & 1) != 0;

	PuruVib v;
	v.power = (float)((pow_pos + pow_neg) / 7.0);
	if (v.power > 1.0f)
		v.power = 1.0f;
	v.duration_ms = ast_ms;
	if (freq > 0 && (!cnt || inc))
	{
		const int ms = 1000 * (inc ? abs(inc) * pow_max : 1) / freq;
		if (ms < (int)ast_ms)
			v.duration_ms = (unsigned)ms;
	}
	v.inclination = (inc == 0 || v.power == 0.0f) ? 0.0f : (float)(freq / (1000.0 * inc * pow_max));
	return v;
}
