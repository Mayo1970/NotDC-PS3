// modem_check: the ring section of ps3/ps3_dcnet.cpp (cut by run.sh) on Linux sockets and pthreads, with a TCP
// server in this process: order and loss both ways, back-pressure, hang up, a closed link, new calls, a slow dial.
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <atomic>
#include <vector>

typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int32_t s32;
#define NET_EWOULDBLOCK EWOULDBLOCK
#define net_errno errno

static char s_log[1 << 16];
static size_t s_log_len;
static pthread_mutex_t s_log_mtx = PTHREAD_MUTEX_INITIALIZER;
static int check_printf(const char* fmt, ...)
{
	char b[256];
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(b, sizeof(b), fmt, ap);
	va_end(ap);
	pthread_mutex_lock(&s_log_mtx);
	if (s_log_len + strlen(b) < sizeof(s_log))
	{
		memcpy(s_log + s_log_len, b, strlen(b) + 1);
		s_log_len += strlen(b);
	}
	pthread_mutex_unlock(&s_log_mtx);
	return n;
}
static bool log_has(const char* t)
{
	pthread_mutex_lock(&s_log_mtx);
	bool r = strstr(s_log, t) != 0;
	pthread_mutex_unlock(&s_log_mtx);
	return r;
}

// What ps3_dcnet.cpp has before the section.
enum { DCNET_PORT = 7654 };
static int dcnet_connect();
static bool dcnet_thread_start();
static u64 now_us();
#define printf check_printf
#include "dcnet_ring.inc"
#undef printf

static int g_fail;
#define CHECK(c, ...) do { if (!(c)) { if (g_fail++ < 12) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } } while (0)

static u64 now_us()
{
	timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (u64)t.tv_sec * 1000000 + t.tv_nsec / 1000;
}

static void* thread_main(void*)
{
	dcnet_thread(0);
	return 0;
}
static bool dcnet_thread_start()
{
	pthread_t t;
	if (pthread_create(&t, 0, thread_main, 0))
		return false;
	pthread_detach(t);
	return true;
}

// ---- the server end, and a dial that can stall or fail ----
static int s_listen, s_port;
static std::atomic<int> s_gate{1}, s_fail_dial{0}, s_dials{0};

static void small_buffers(int fd)
{
	int n = 4096;
	setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &n, sizeof(n));
	setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &n, sizeof(n));
}

static int dcnet_connect()
{
	s_dials++;
	while (!s_gate.load())
		usleep(1000);
	if (s_fail_dial.load())
	{
		check_printf("[dcnet] test: dial refused\n");
		return -1;
	}
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	small_buffers(fd);
	sockaddr_in sa;
	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_port = htons(s_port);
	sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (connect(fd, (sockaddr*)&sa, sizeof(sa)) < 0)
	{
		close(fd);
		return -1;
	}
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
	return fd;
}

static int accept_one(int ms)
{
	pollfd p = { s_listen, POLLIN, 0 };
	if (poll(&p, 1, ms) <= 0)
		return -1;
	int fd = accept(s_listen, 0, 0);
	if (fd >= 0)
		fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
	return fd;
}

// Bytes the server got before the time ran out.
static size_t srv_recv(int fd, u8* buf, size_t n, int ms)
{
	size_t got = 0;
	const u64 end = now_us() + (u64)ms * 1000;
	while (got < n && now_us() < end)
	{
		ssize_t k = recv(fd, buf + got, n - got, 0);
		if (k > 0)
			got += (size_t)k;
		else if (k == 0)
			break;
		else
			usleep(200);
	}
	return got;
}

static bool srv_eof(int fd, int ms)
{
	const u64 end = now_us() + (u64)ms * 1000;
	u8 b;
	while (now_us() < end)
	{
		ssize_t k = recv(fd, &b, 1, 0);
		if (k == 0)
			return true;
		if (k < 0 && errno != EAGAIN)
			return true;
		usleep(500);
	}
	return false;
}

static size_t sh4_read(u8* buf, size_t n, int ms)
{
	size_t got = 0;
	const u64 end = now_us() + (u64)ms * 1000;
	while (got < n && now_us() < end)
	{
		int b = dcnet_read();
		if (b >= 0)
			buf[got++] = (u8)b;
		else
			usleep(200);
	}
	return got;
}

static u32 s_rng = 0x1234567;
static u32 rnd() { s_rng ^= s_rng << 13; s_rng ^= s_rng >> 17; s_rng ^= s_rng << 5; return s_rng; }
static u32 drops() { return __atomic_load_n(&s_dc.tx_drop, __ATOMIC_RELAXED); }

// 1. Both ways in rounds: every byte, in order, none dropped.
static int test_both_ways()
{
	dcnet_start();
	int fd = accept_one(2000);
	CHECK(fd >= 0, "call 1: no connection");
	if (fd < 0)
		return -1;
	usleep(20000);
	u8 out[256], back[256];
	for (int round = 0; round < 300 && !g_fail; round++)
	{
		const size_t k = 1 + rnd() % 200, m = 1 + rnd() % 200;
		for (size_t i = 0; i < k; i++)
			dcnet_write(out[i] = (u8)rnd());
		CHECK(srv_recv(fd, back, k, 2000) == k && !memcmp(out, back, k), "round %d: the server did not get the %zu bytes",
		      round, k);
		for (size_t i = 0; i < m; i++)
			out[i] = (u8)rnd();
		CHECK(send(fd, out, m, MSG_NOSIGNAL) == (ssize_t)m, "round %d: server send", round);
		CHECK(sh4_read(back, m, 2000) == m && !memcmp(out, back, m), "round %d: the modem did not get the %zu bytes",
		      round, m);
	}
	CHECK(dcnet_read() == -1 && drops() == 0, "both ways: bytes left over or dropped");
	return fd;
}

// 2. The server sends far more than the ring holds while the modem reads slowly: the ring stops the link.
static void test_back_pressure(int fd)
{
	const size_t total = 300000;
	std::vector<u8> data(total), got;
	for (size_t i = 0; i < total; i++)
		data[i] = (u8)(i * 7 + (i >> 9));
	size_t sent = 0;
	const u64 end = now_us() + 20000000;
	while (got.size() < total && now_us() < end)
	{
		if (sent < total)
		{
			ssize_t k = send(fd, &data[sent], total - sent, MSG_NOSIGNAL);
			if (k > 0)
				sent += (size_t)k;
		}
		for (int i = 0; i < 700; i++)
		{
			int b = dcnet_read();
			if (b < 0)
				break;
			got.push_back((u8)b);
		}
		usleep(300);
	}
	CHECK(got.size() == total && !memcmp(&got[0], &data[0], total), "back-pressure: %zu of %zu bytes, or not in order",
	      got.size(), total);
}

int main()
{
	signal(SIGPIPE, SIG_IGN);
	s_listen = socket(AF_INET, SOCK_STREAM, 0);
	int one = 1;
	setsockopt(s_listen, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	small_buffers(s_listen);
	sockaddr_in sa;
	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	socklen_t sl = sizeof(sa);
	if (bind(s_listen, (sockaddr*)&sa, sizeof(sa)) || listen(s_listen, 128) || getsockname(s_listen, (sockaddr*)&sa, &sl))
	{
		printf("FAIL: no test server\n");
		return 2;
	}
	s_port = ntohs(sa.sin_port);

	// Before any call: nothing to read, a hang up is harmless, and no thread runs.
	CHECK(dcnet_read() == -1, "a byte before any call");
	dcnet_stop();
	CHECK(!s_dc.up && s_dials == 0, "a hang up with no call dials");

	int fd = test_both_ways();
	if (fd >= 0)
		test_back_pressure(fd);

	// 3. Hang up: the server sees the link close, the log names the call.
	dcnet_stop();
	CHECK(fd >= 0 && srv_eof(fd, 2000), "hang up: the link stays open");
	usleep(50000);
	CHECK(log_has("[dcnet] call 1: hang up\n") && log_has("[dcnet] call 1 ended: "), "hang up: the log lines:\n%s", s_log);
	CHECK(accept_one(200) < 0, "hang up: a new dial");
	close(fd);

	// 4. A new call drops the old call's bytes and dials again.
	dcnet_start();
	int fd2 = accept_one(2000);
	CHECK(fd2 >= 0, "call 2: no connection");
	u8 junk[100];
	memset(junk, 0xA5, sizeof(junk));
	CHECK(send(fd2, junk, sizeof(junk), MSG_NOSIGNAL) == (ssize_t)sizeof(junk), "call 2: server send");
	const u64 end = now_us() + 2000000;
	while (ACQ(s_dc.rx_w) - s_dc.rx_r < sizeof(junk) && now_us() < end)
		usleep(500);
	CHECK(ACQ(s_dc.rx_w) - s_dc.rx_r == sizeof(junk), "call 2: the bytes never reached the ring");
	dcnet_start();
	CHECK(dcnet_read() == -1, "call 3: the last call's bytes are still there");
	CHECK(srv_eof(fd2, 2000), "call 3: the old link stays open");
	int fd3 = accept_one(2000);
	CHECK(fd3 >= 0, "call 3: no connection");
	close(fd2);

	// 5. DCNet closes the link: the bytes before it arrive, then nothing, and no new dial.
	usleep(20000);
	u8 last[50], back[50];
	for (int i = 0; i < 50; i++)
		last[i] = (u8)(200 + i);
	CHECK(send(fd3, last, 50, MSG_NOSIGNAL) == 50, "call 3: server send");
	close(fd3);
	CHECK(sh4_read(back, 50, 2000) == 50 && !memcmp(back, last, 50), "closed link: the last bytes");
	usleep(100000);
	CHECK(dcnet_read() == -1 && log_has("[dcnet] call 3: DCNet closed the link\n"), "closed link: the log:\n%s", s_log);
	CHECK(accept_one(300) < 0, "closed link: a new dial without a new call");

	// 6. A slow dial: the bytes from before the link are dropped, those after it pass; a full ring counts drops.
	s_gate = 0;
	const int dials = s_dials;
	dcnet_start();
	const u64 end2 = now_us() + 2000000;
	while (s_dials == dials && now_us() < end2)
		usleep(500);
	const u32 d0 = drops();
	for (int i = 0; i < 5000; i++)
		dcnet_write((u8)i);
	CHECK(drops() - d0 == 5000 - 4096, "slow dial: %u bytes dropped, not 904", drops() - d0);
	s_gate = 1;
	int fd4 = accept_one(2000);
	CHECK(fd4 >= 0, "call 4: no connection");
	usleep(100000);
	u8 none[16];
	CHECK(srv_recv(fd4, none, sizeof(none), 200) == 0, "slow dial: bytes from before the link reached DCNet");
	for (int i = 0; i < 10; i++)
		dcnet_write((u8)(0x30 + i));
	u8 ten[10];
	CHECK(srv_recv(fd4, ten, 10, 2000) == 10 && ten[0] == 0x30 && ten[9] == 0x39, "slow dial: the bytes after the link");

	// 7. A dial that fails: nothing until the next call.
	s_fail_dial = 1;
	dcnet_start();
	CHECK(srv_eof(fd4, 2000), "call 5: the old link stays open");
	close(fd4);
	usleep(100000);
	CHECK(accept_one(200) < 0 && log_has("[dcnet] test: dial refused\n"), "call 5: the failed dial");
	s_fail_dial = 0;
	CHECK(accept_one(300) < 0, "call 5: a second dial without a new call");
	dcnet_start();
	int fd6 = accept_one(2000);
	CHECK(fd6 >= 0, "call 6: no connection after a failed one");

	// 8. Calls and hang ups in a burst; the last call works. The links are taken as they come: the newest is the last.
	int last_fd = -1;
	for (int i = 0; i <= 60; i++)
	{
		dcnet_start();
		if (i < 60 && (rnd() & 1))
			dcnet_stop();
		usleep(i < 60 ? rnd() % 3000 : 0);
		for (int fdx; (fdx = accept_one(i < 60 ? 0 : 800)) >= 0;)
		{
			if (last_fd >= 0)
				close(last_fd);
			last_fd = fdx;
		}
	}
	close(fd6);
	CHECK(last_fd >= 0, "burst: no link at the end");
	usleep(50000);
	for (int i = 0; i < 20; i++)
		dcnet_write((u8)i);
	u8 twenty[20];
	CHECK(last_fd >= 0 && srv_recv(last_fd, twenty, 20, 2000) == 20 && twenty[19] == 19, "burst: the last call carries no data");
	const char* text = "x";
	CHECK(last_fd >= 0 && send(last_fd, text, 1, MSG_NOSIGNAL) == 1 && sh4_read(twenty, 1, 2000) == 1 && twenty[0] == 'x',
	      "burst: the last call brings no data");

	if (g_fail)
	{
		printf("FAIL: %d check(s); log:\n%s", g_fail, s_log);
		return 1;
	}
	printf("PASS: dcnet: %u calls, both ways in order, back-pressure, hang up, closed link, slow and failed dials\n",
	       s_dc.call);
	return 0;
}
