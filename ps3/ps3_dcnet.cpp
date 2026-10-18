/*
	Copyright 2025 flyinghead

	This file is part of Flycast.

    Flycast is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    Flycast is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Flycast.  If not, see <https://www.gnu.org/licenses/>.

	Changed for NotDC (PS3 port) on 2026-10-18.
 */
// G11 network end: Flycast's DCNet (core/network/dcnet.cpp PPPSocket), one TCP link to dcnet.flyca.st:7654 that
// carries the modem's bytes both ways. A PPU thread owns the socket; two one-way rings join it to the SH4 thread.
#include "types.h"
#include "ps3_dcnet.h"
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/thread.h>
#include <net/net.h>
#include <net/netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "ps3_timebase.h"

enum { DCNET_PORT = 7654, PRIO = 999 };   // PRIO: above the SH4 (1001), as the GD-ROM worker; it mostly sleeps

static int dcnet_connect();
static bool dcnet_thread_start();
static u64 now_us();

// dcnet_check: begin ring
#define ACQ(v)      __atomic_load_n(&(v), __ATOMIC_ACQUIRE)
#define REL(v, x)   __atomic_store_n(&(v), (x), __ATOMIC_RELEASE)

enum { RX_SIZE = 16384, TX_SIZE = 4096, STATS_US = 5000000 };

// rx: DCNet to the modem, tx: the modem to DCNet; each ring has one writing and one reading thread.
static struct
{
	u8 rx[RX_SIZE];
	u8 tx[TX_SIZE];
	u32 rx_w, rx_r, tx_w, tx_r;
	u32 call;      // the last call the SH4 thread started
	u32 want;      // the call the link serves; 0 = hung up
	u32 tx_drop;
	bool up;
} s_dc;

// PSL1GHT's errno holds its own codes (net/net.h), not newlib's.
static bool would_block()
{
	return errno == NET_EWOULDBLOCK || errno == EAGAIN || errno == EWOULDBLOCK || net_errno == NET_EWOULDBLOCK;
}

// One call on a connected socket, until the SH4 thread hangs up or the link fails. No data stops the loop.
static void serve(int fd, u32 call)
{
	u32 sent = 0, got = 0, shown_sent = 0, shown_got = 0;
	u64 next = now_us() + STATS_US;
	while (ACQ(s_dc.want) == call)
	{
		bool busy = false;
		const u32 tr = s_dc.tx_r, tw = ACQ(s_dc.tx_w);
		if (tr != tw)
		{
			const u32 at = tr & (TX_SIZE - 1);
			u32 n = tw - tr;
			if (n > TX_SIZE - at)
				n = TX_SIZE - at;
			const int k = (int)send(fd, &s_dc.tx[at], n, 0);
			if (k > 0)
			{
				REL(s_dc.tx_r, tr + (u32)k);
				sent += (u32)k;
				busy = true;
			}
			else if (k < 0 && !would_block())
			{
				printf("[dcnet] call %u: send failed, errno %d\n", call, errno);
				break;
			}
		}
		const u32 rw = s_dc.rx_w, rr = ACQ(s_dc.rx_r);
		if (rw - rr < RX_SIZE)
		{
			const u32 at = rw & (RX_SIZE - 1);
			u32 n = RX_SIZE - (rw - rr);
			if (n > RX_SIZE - at)
				n = RX_SIZE - at;
			const int k = (int)recv(fd, &s_dc.rx[at], n, 0);
			if (k > 0)
			{
				REL(s_dc.rx_w, rw + (u32)k);
				got += (u32)k;
				busy = true;
			}
			else if (k == 0)
			{
				printf("[dcnet] call %u: DCNet closed the link\n", call);
				break;
			}
			else if (!would_block())
			{
				printf("[dcnet] call %u: recv failed, errno %d\n", call, errno);
				break;
			}
		}
		const u64 t = now_us();
		if (t >= next)
		{
			if (sent != shown_sent || got != shown_got)
				printf("[dcnet] call %u: %u bytes sent, %u received\n", call, sent, got);
			shown_sent = sent;
			shown_got = got;
			next = t + STATS_US;
		}
		if (!busy)
			usleep(1000);
	}
	printf("[dcnet] call %u ended: %u bytes sent, %u received, %u dropped\n", call, sent, got,
	       __atomic_load_n(&s_dc.tx_drop, __ATOMIC_RELAXED));
}

static void dcnet_thread(void*)
{
	u32 call = 0;
	for (;;)
	{
		const u32 want = ACQ(s_dc.want);
		if (!want || want == call)
		{
			usleep(10000);
			continue;
		}
		call = want;
		const int fd = dcnet_connect();
		if (fd < 0)
			continue;
		// Flycast drops the modem's bytes until the socket exists.
		REL(s_dc.tx_r, ACQ(s_dc.tx_w));
		serve(fd, call);
		close(fd);
	}
}

void dcnet_start()
{
	if (!s_dc.up)
	{
		if (!dcnet_thread_start())
			return;
		s_dc.up = true;
	}
	REL(s_dc.rx_r, ACQ(s_dc.rx_w));
	u32 call = s_dc.call + 1;
	if (!call)
		call = 1;
	s_dc.call = call;
	REL(s_dc.want, call);
	printf("[dcnet] call %u: dialing dcnet.flyca.st:%d\n", call, (int)DCNET_PORT);
}

void dcnet_stop()
{
	if (ACQ(s_dc.want))
		printf("[dcnet] call %u: hang up\n", s_dc.call);
	REL(s_dc.want, 0u);
}

void dcnet_write(u8 b)
{
	const u32 w = s_dc.tx_w;
	if (w - ACQ(s_dc.tx_r) >= TX_SIZE)
	{
		__atomic_fetch_add(&s_dc.tx_drop, 1u, __ATOMIC_RELAXED);
		return;
	}
	s_dc.tx[w & (TX_SIZE - 1)] = b;
	REL(s_dc.tx_w, w + 1);
}

int dcnet_read()
{
	const u32 r = s_dc.rx_r;
	if (r == ACQ(s_dc.rx_w))
		return -1;
	const int b = s_dc.rx[r & (RX_SIZE - 1)];
	REL(s_dc.rx_r, r + 1);
	return b;
}
// dcnet_check: end ring

static u64 now_us()
{
	return ticks_to_microsecs(gettime());
}

// A blocking connect, then non-blocking reads and writes; -1 when DCNet cannot be reached.
static int dcnet_connect()
{
	struct hostent* h = gethostbyname("dcnet.flyca.st");
	if (!h || h->h_length != 4 || !h->h_addr_list || !h->h_addr_list[0])
	{
		printf("[dcnet] dcnet.flyca.st has no address, h_errno %d\n", net_h_errno);
		return -1;
	}
	struct sockaddr_in sa;
	memset(&sa, 0, sizeof(sa));
	sa.sin_len = sizeof(sa);   // PSL1GHT: left at 0, the call fails
	sa.sin_family = AF_INET;
	sa.sin_port = htons(DCNET_PORT);
	memcpy(&sa.sin_addr, h->h_addr_list[0], 4);
	const u8* ip = (const u8*)&sa.sin_addr;
	const int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
	{
		printf("[dcnet] socket failed, errno %d\n", errno);
		return -1;
	}
	if (connect(fd, (struct sockaddr*)&sa, sizeof(sa)) < 0)
	{
		printf("[dcnet] connect to %u.%u.%u.%u:%d failed, errno %d\n", ip[0], ip[1], ip[2], ip[3], (int)DCNET_PORT, errno);
		close(fd);
		return -1;
	}
	int one = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_NBIO, &one, sizeof(one)) < 0)
	{
		printf("[dcnet] SO_NBIO failed, errno %d\n", errno);
		close(fd);
		return -1;
	}
	printf("[dcnet] connected to %u.%u.%u.%u:%d\n", ip[0], ip[1], ip[2], ip[3], (int)DCNET_PORT);
	return fd;
}

static bool dcnet_thread_start()
{
	sys_ppu_thread_t tid;
	const s32 r = sysThreadCreate(&tid, dcnet_thread, NULL, PRIO, 0x20000, 0, (char*)"notdc_dcnet");
	if (r != 0)
		printf("[dcnet] thread start failed: 0x%08X\n", (u32)r);
	return r == 0;
}
