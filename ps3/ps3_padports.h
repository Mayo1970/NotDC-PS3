#pragma once
// G15 (GOALS.md item 4): which PS3 pad drives each Dreamcast port. Plain C types: misc/pad_check builds it on the PC.

#define PADPORTS_DC 4

// map[port]: the PS3 pad of a DC port, -1 = none. status[i] != 0: PS3 pad i is connected (padInfo.status).
// A port gives its pad up when the pad left, or when the port is not below cap (the Players row).
static inline int padports_lost(const int* map, const unsigned char* status, int npads, unsigned cap, unsigned port)
{
	return map[port] >= 0 && (port >= cap || map[port] >= npads || !status[map[port]]);
}

static inline unsigned padports_mask(const int* map)
{
	unsigned ports = 0;
	for (unsigned port = 0; port < PADPORTS_DC; port++)
		if (map[port] >= 0)
			ports |= 1u << port;
	return ports;
}

// A connected pad with no port takes the lowest free port below cap; the others keep theirs, so nobody moves.
static inline unsigned padports_fill(int* map, const unsigned char* status, int npads, unsigned cap)
{
	for (int pad = 0; pad < npads; pad++)
	{
		if (!status[pad])
			continue;
		unsigned port = 0;
		while (port < PADPORTS_DC && map[port] != pad)
			port++;
		if (port < PADPORTS_DC)
			continue;
		for (port = 0; port < cap && port < PADPORTS_DC; port++)
			if (map[port] < 0)
			{
				map[port] = pad;
				break;
			}
	}
	return padports_mask(map);
}
