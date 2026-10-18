#!/bin/sh
# G11 stage 1 PC check, from Git Bash: MSYS_NO_PATHCONV=1 docker run --rm --security-opt seccomp=unconfined -v "$(pwd -W):/src"
# -w /src ps3dev-latest:latest sh misc/modem_check/run.sh [episodes seed]. Exit 1: a check failed, 2: no build.
cd "${MODEM_SRC:-$(dirname "$0")/../..}" || exit 2   # MODEM_SRC, MODEM_TMP: mutate.py's tree copy and work dir
T=${MODEM_TMP:-/tmp/modem_check}
mkdir -p "$T" || exit 2
r=0
# 1. Every register field at Flycast's bits, on both byte orders.
python3 misc/modem_check/layout.py "$T" || r=1
# 2. The modem in lockstep with Flycast's.
CXX="g++ -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all"
REF="-std=c++17 -w -DNDEBUG -Imisc/modem_check/fake_ref -Imisc/modem_check/ref"
OURS="-std=c++14 -Wall -Wno-unused-function -Wno-unused-variable -D_LINUX -D_X64 -I. -Iplugins -Imisc/tr4_check/fake"
$CXX $REF -c misc/modem_check/ref/modem.cpp -o "$T/ref_modem.o" || exit 2
$CXX $REF -c misc/modem_check/ref_glue.cpp -o "$T/ref_glue.o" || exit 2
$CXX $OURS -include misc/modem_check/log.h -c plugs/nullExtDev/nullExtDev.cpp -o "$T/ours.o" || exit 2
$CXX $OURS -c misc/modem_check/lockstep.cpp -o "$T/lockstep.o" || exit 2
$CXX -o "$T/lockstep" "$T/lockstep.o" "$T/ours.o" "$T/ref_modem.o" "$T/ref_glue.o" || exit 2
"$T/lockstep" "${1:-300}" "${2:-1}" || r=1
# 3. The DCNet link's rings and thread on Linux sockets, with ASan/UBSan, then TSan.
tr -d '\r' < ps3/ps3_dcnet.cpp | sed -n '/^\/\/ dcnet_check: begin ring$/,/^\/\/ dcnet_check: end ring$/p' > "$T/dcnet_ring.inc"
if [ ! -s "$T/dcnet_ring.inc" ]; then
	echo "FAIL: ps3/ps3_dcnet.cpp has no ring section"
	exit 2
fi
for san in address,undefined thread; do
	g++ -std=c++14 -O1 -g -fsanitize=$san -Wall -I"$T" -o "$T/dcnet_check" misc/modem_check/dcnet_check.cpp -lpthread || exit 2
	if ! setarch "$(uname -m)" -R "$T/dcnet_check"; then
		echo "FAIL: dcnet_check with $san (TSan needs the docker run of the header)"
		r=1
	fi
done
# 4. The lines that do not build on the PC.
has() {
	if [ "$(tr -d '\r' < "$1" | grep -cF -- "$3")" != "$2" ]; then
		echo "FAIL: $1 does not have $2 line(s): $3"
		r=1
	fi
}
pair() {
	n=$(tr -d '\r' < "$1" | awk -v a="$3" -v b="$4" 'p && index($0, b) { n++ } { p = index($0, a) } END { print n + 0 }')
	if [ "$n" != "$2" ]; then
		echo "FAIL: $1 does not have $2 time(s) the line / the next line: $3 / $4"
		r=1
	fi
}
has Makefile 1 '$(BUILD)/plugs/nullExtDev/nullExtDev.o: CXXFLAGS += -mminimal-toc'
has Makefile 1 '$(BUILD)/ps3/ps3_dcnet.o: CXXFLAGS += -mminimal-toc'
I=dc/sh4/sh4_interpreter.cpp
has $I 1 'libExtDevice_Update(s_timeslice * s_medium_period);'
pair $I 1 'sp_mark(SP_DMA);' 'libExtDevice_Update(s_timeslice * s_medium_period);'
has $I 1 'void FASTCALL libExtDevice_Update(u32 cycles);'
E=plugs/nullExtDev/nullExtDev.cpp
has $E 1 's_on = g_modem_preset != 0;'
has $E 1 's_cps = SH4_CLOCK_EFF;'
F=dc/flash_fix.cpp
pair $F 1 'if (g_modem_preset)' 'flash_isp();'
D=ps3/ps3_dcnet.cpp
has $D 1 'struct hostent* h = gethostbyname("dcnet.flyca.st");'
has $D 1 'sa.sin_len = sizeof(sa);'
has $D 1 'sa.sin_port = htons(DCNET_PORT);'
has $D 1 'enum { DCNET_PORT = 7654, PRIO = 999 };'
has $D 1 'if (setsockopt(fd, SOL_SOCKET, SO_NBIO, &one, sizeof(one)) < 0)'
has $D 1 'const s32 r = sysThreadCreate(&tid, dcnet_thread, NULL, PRIO, 0x20000, 0, (char*)"notdc_dcnet");'
P=ps3/ps3_presets.cpp
# Default DCNET since the HW success (Matt, 2026-10-18).
has $P 1 'int g_modem_preset         = 1;'
has $P 1 '{ "Modem",         &g_modem_preset },'
M=ps3/ps3_menu.cpp
has $M 1 'static const char* k_modem[]     = { "OFF", "DCNET", NULL };'
has $M 1 '{ "Modem",           &g_modem_preset,         OPT_CHOICE, k_modem,     0, 0,0,0, NULL,'
has $M 1 '{ "NETWORK",       s_net,   (int)(sizeof(s_net) / sizeof(s_net[0])) },'
# Set before boot (Init latches it): the row's own line only, not in k_live_opts.
has $M 1 '&g_modem_preset,'
[ $r = 0 ] && echo "PASS: 20 source line checks"
exit $r
