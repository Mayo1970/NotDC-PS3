#pragma once
// Plan section 7, bundle 12 (P36): a RetroArch cheat file as Flycast's CheatManager reads it and applies it.
// No emulator memory here (the caller gives the RAM read and write): misc/screen_check runs it.
#include <stdint.h>
#include <string.h>
#include <strings.h>

// cheat_type, as RetroArch numbers it.
enum { CHEAT_OFF, CHEAT_SET, CHEAT_INC, CHEAT_DEC, CHEAT_IF_EQ, CHEAT_IF_NEQ, CHEAT_IF_GT, CHEAT_IF_LT, CHEAT_COPY };

struct Cheat
{
	uint32_t type, bits;             // bits = 1 << memory_search_size
	uint32_t address, value, dest;   // RAM offsets and the value
	uint32_t repeat, add_value, add_address;
	uint8_t mask;                    // address_bit_position: the bits a cheat under 8 bits sets
	bool enabled, has_address;
};

#define CHEAT_MAX 256
// Flycast has no limit; a count from a bad file must not hold the emulator in one vblank.
#define CHEAT_REPEAT_MAX 65536

typedef uint32_t (*CheatRead)(uint32_t addr, uint32_t bits);
typedef void (*CheatWrite)(uint32_t addr, uint32_t value, uint32_t bits);

// ps3_cheats.cpp: reads the disc image's cheat file at boot (the Cheats row); applies it, from SPG at each vblank.
void ps3_cheats_load(const char* disc);
void ps3_cheats_vblank(void);

// Flycast IniFile::getInt: hex with a 0x prefix, else decimal with a sign; no digit is 0, too large is the maximum.
static inline uint32_t cheat_int(const char* s)
{
	while (*s == ' ' || *s == '\t')
		s++;
	const bool hex = s[0] == '0' && (s[1] == 'x' || s[1] == 'X');
	bool neg = false;
	if (hex)
		s += 2;
	else if (*s == '-' || *s == '+')
		neg = *s++ == '-';
	uint64_t v = 0;
	for (;; s++)
	{
		uint32_t d;
		if (*s >= '0' && *s <= '9')
			d = (uint32_t)(*s - '0');
		else if (hex && ((*s | 0x20) >= 'a' && (*s | 0x20) <= 'f'))
			d = (uint32_t)((*s | 0x20) - 'a' + 10);
		else
			break;
		v = v * (hex ? 16 : 10) + d;
		if (v > 0xFFFFFFFFull)
			v = 0xFFFFFFFFull;
	}
	return neg ? (uint32_t)(0 - v) : (uint32_t)v;
}

// Flycast IniFile::getBool.
static inline bool cheat_bool(const char* s)
{
	return !strcasecmp(s, "yes") || !strcasecmp(s, "true") || !strcasecmp(s, "on") || !strcasecmp(s, "1");
}

// One line as Flycast IniFile::load takes it: 1 = "key = value" (one space and one pair of quotes around the value
// go), 2 = a section header, 0 = nothing.
static inline int cheat_line(const char* line, size_t len, char* key, size_t klen, char* val, size_t vlen)
{
	size_t p = 0;
	while (p < len && (line[p] == ' ' || line[p] == '\t'))
		p++;
	if (p == len || line[p] == ';')
		return 0;
	if (line[p] == '[')
		return 2;
	const char* eq = (const char*)memchr(line + p, '=', len - p);
	if (!eq)
		return 0;
	size_t ke = (size_t)(eq - line);
	while (ke > p && (line[ke - 1] == ' ' || line[ke - 1] == '\t'))
		ke--;
	if (ke == p || ke - p >= klen)
		return 0;
	memcpy(key, line + p, ke - p);
	key[ke - p] = 0;
	size_t vs = (size_t)(eq - line) + 1, ve = len;
	if (vs < ve && line[vs] == ' ')
		vs++;
	if (vs < ve && line[vs] == '"')
	{
		vs++;
		size_t q = ve;
		while (q > vs && line[q - 1] != '"')
			q--;
		if (q > vs)
			ve = q - 1;
	}
	const size_t n = ve - vs < vlen - 1 ? ve - vs : vlen - 1;
	memcpy(val, line + vs, n);
	val[n] = 0;
	return 1;
}

// Flycast CheatManager::loadCheatFile over text (it ends in a 0). Returns the cheats kept in out: those with a type,
// an address and a destination inside the RAM, by their number. Numbers of CHEAT_MAX and up are not read.
static inline uint32_t cheats_parse(const char* text, uint32_t ram_size, Cheat* out)
{
	static Cheat raw[CHEAT_MAX];
	for (uint32_t i = 0; i < CHEAT_MAX; i++)
	{
		memset(&raw[i], 0, sizeof(raw[i]));
		raw[i].bits = 1;
		raw[i].repeat = 1;
	}
	uint32_t count = 0;
	bool section = false;
	while (*text)
	{
		const char* nl = strchr(text, '\n');
		size_t len = nl ? (size_t)(nl - text) : strlen(text);
		const char* line = text;
		text += len + (nl ? 1 : 0);
		if (len && line[len - 1] == '\r')
			len--;
		char key[64], val[64];
		const int kind = cheat_line(line, len, key, sizeof(key), val, sizeof(val));
		// Flycast reads the keys of the file's top only; a section's keys are another section's.
		if (kind == 2)
			section = true;
		if (kind != 1 || section)
			continue;
		if (!strcmp(key, "cheats"))
		{
			count = cheat_int(val);
			continue;
		}
		// "cheat<number>_<field>", the number as printf writes it.
		if (strncmp(key, "cheat", 5) || key[5] < '0' || key[5] > '9' || (key[5] == '0' && key[6] != '_'))
			continue;
		uint32_t i = 0;
		const char* f = key + 5;
		for (; *f >= '0' && *f <= '9' && i < CHEAT_MAX; f++)
			i = i * 10 + (uint32_t)(*f - '0');
		if (*f != '_' || i >= CHEAT_MAX)
			continue;
		f++;
		Cheat* c = &raw[i];
		if (!strcmp(f, "address"))
		{
			c->address = cheat_int(val);
			c->has_address = true;
		}
		else if (!strcmp(f, "cheat_type"))
			c->type = cheat_int(val);
		else if (!strcmp(f, "memory_search_size"))
			c->bits = 1u << (cheat_int(val) < 5 ? cheat_int(val) : 5);
		else if (!strcmp(f, "value"))
			c->value = cheat_int(val);
		else if (!strcmp(f, "repeat_count"))
			c->repeat = cheat_int(val) < CHEAT_REPEAT_MAX ? cheat_int(val) : CHEAT_REPEAT_MAX;
		else if (!strcmp(f, "repeat_add_to_value"))
			c->add_value = cheat_int(val);
		else if (!strcmp(f, "repeat_add_to_address"))
			c->add_address = cheat_int(val);
		else if (!strcmp(f, "enable"))
			c->enabled = cheat_bool(val);
		else if (!strcmp(f, "dest_address"))
			c->dest = cheat_int(val);
		else if (!strcmp(f, "address_bit_position"))
			c->mask = (uint8_t)cheat_int(val);
	}
	uint32_t n = 0;
	for (uint32_t i = 0; i < CHEAT_MAX && (count == 0 || i < count); i++)
	{
		const Cheat& c = raw[i];
		// With no "cheats" count the list ends at the first number with no address.
		if (count == 0 && (!c.has_address || c.address == 0xFFFFFFFFu))
			break;
		if (!c.has_address || c.address >= ram_size || c.dest >= ram_size || c.type == CHEAT_OFF)
			continue;
		out[n++] = c;
	}
	return n;
}

// Flycast CheatManager::apply, once per vblank. Returns the RAM writes it made.
static inline uint32_t cheats_apply(const Cheat* list, uint32_t n, CheatRead rd, CheatWrite wr)
{
	uint32_t writes = 0;
	bool skip = false;
	for (uint32_t k = 0; k < n; k++)
	{
		const Cheat& c = list[k];
		// A test that failed skips the next cheat, enabled or not.
		if (skip)
		{
			skip = false;
			continue;
		}
		if (!c.enabled)
			continue;
		bool set = false;
		uint32_t v = 0;
		switch (c.type)
		{
		case CHEAT_SET:    set = true; v = c.value; break;
		case CHEAT_INC:    set = true; v = rd(c.address, c.bits) + c.value; break;
		case CHEAT_DEC:    set = true; v = rd(c.address, c.bits) - c.value; break;
		case CHEAT_IF_EQ:  skip = rd(c.address, c.bits) != c.value; break;
		case CHEAT_IF_NEQ: skip = rd(c.address, c.bits) == c.value; break;
		case CHEAT_IF_GT:  skip = rd(c.address, c.bits) <= c.value; break;
		case CHEAT_IF_LT:  skip = rd(c.address, c.bits) >= c.value; break;
		case CHEAT_COPY:
			for (uint32_t i = 0; i < c.repeat; i++, writes++)
				wr(c.dest + i, rd(c.address + i, c.bits), c.bits);
			break;
		default:
			break;
		}
		if (!set)
			continue;
		uint32_t a = c.address;
		for (uint32_t r = 0; r < c.repeat; r++)
		{
			const uint32_t cur = rd(a, c.bits);
			// Under 8 bits: the byte's bits outside the mask stay as they are.
			if (c.bits < 8)
				v = (v & ~0xFFu) | (v & c.mask) | (cur & 0xFFu & ~(uint32_t)c.mask);
			if (cur != v)
			{
				wr(a, v, c.bits);
				writes++;
			}
			a += c.add_address * c.bits / 8;
			v += c.add_value;
		}
	}
	return writes;
}
