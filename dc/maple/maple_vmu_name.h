#pragma once
// Plan section 7, bundle 16 (P37): the VMU file of a slot, with Flycast's names (hostfs::getVmuPath), so its files
// can be copied in. Plain C types: misc/maple_check builds it on the PC.
#include <stdio.h>
#include <string.h>

// id10: the product number of the disc (IP.BIN 0x40, 10 bytes), or an empty text for no disc. Its trailing blanks
// go; a character that a file name cannot hold becomes '_'. Returns the length of the ID that is left.
static inline int vmu_game_id(char out[11], const char* id10)
{
	int n = 0;
	for (; id10 && n < 10 && id10[n]; n++)
		out[n] = id10[n];
	while (n > 0 && (out[n - 1] == ' ' || out[n - 1] == '\t' || out[n - 1] == '\r' || out[n - 1] == '\n'))
		n--;
	out[n] = 0;
	for (int i = 0; i < n; i++)
		if (strchr(" /\\:*?|<>\"", out[i]) || (unsigned char)out[i] < 0x20 || (unsigned char)out[i] > 0x7E)
			out[i] = '_';
	return n;
}

// level: 0 = one file per slot (the old names), 1 = slot A1 has one file per disc (Flycast), 2 = each slot has.
// port: "A1" .. "D2". With no disc ID the name is the old one at each level.
static inline void vmu_file_name(char* out, size_t len, const char* port, const char* id10, int level)
{
	char id[11];
	const int n = vmu_game_id(id, id10);
	if (n > 0 && (level == 2 || (level == 1 && !strcmp(port, "A1"))))
		snprintf(out, len, "%s_vmu_save_%s.bin", id, port);
	else
		snprintf(out, len, "vmu_save_%s.bin", port);
}
