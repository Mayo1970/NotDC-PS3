#pragma once
// P118 (row Pad Reconnect): a PS3 pad port that turns on starts a new generation; each poller (who: 0 game, 1 menu)
// gets it once as fresh and clears its own copy of that pad's data. Plain C types: misc/pad_check builds it on the PC.

#define PADCON_PORTS 7
#define PADCON_WHO   2

struct PadCon
{
	unsigned char on[PADCON_PORTS], gen[PADCON_PORTS], seen[PADCON_WHO][PADCON_PORTS];
	unsigned connects;
};

// status: n ports from ioPadGetInfo. Returns who's fresh ports; *turned_on gets the ports that turned on in this call.
static inline unsigned padcon_track(PadCon* c, const unsigned char* status, int n, int who, unsigned* turned_on)
{
	unsigned fresh = 0, on_now = 0;
	for (int i = 0; i < n && i < PADCON_PORTS; i++)
	{
		const unsigned char on = status[i] != 0;
		if (on && !c->on[i])
		{
			c->gen[i]++;
			c->connects++;
			on_now |= 1u << i;
		}
		c->on[i] = on;
		if (c->seen[who][i] != c->gen[i])
		{
			c->seen[who][i] = c->gen[i];
			fresh |= 1u << i;
		}
	}
	*turned_on = on_now;
	return fresh;
}
