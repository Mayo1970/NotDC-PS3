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

// level: 0 = one file per slot (the old names), 1 = slot A1 has one file per disc (Flycast), 2 = each slot has; no
// disc ID = the old name. port: "A1" .. "D2". page (G15, the 4X cards): 2 and up add "_p<page>", one more card each.
static inline void vmu_file_name(char* out, size_t len, const char* port, const char* id10, int level, int page)
{
	char id[11], pg[16] = "";
	const int n = vmu_game_id(id, id10);
	if (page > 1)
		snprintf(pg, sizeof(pg), "_p%d", page);
	if (n > 0 && (level == 2 || (level == 1 && !strcmp(port, "A1"))))
		snprintf(out, len, "%s_vmu_save_%s%s.bin", id, port, pg);
	else
		snprintf(out, len, "vmu_save_%s%s.bin", port, pg);
}
