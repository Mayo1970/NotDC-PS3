// SH4 op tests for the interpreter: Flycast tests/src/sh4_ops.h line by line (P46), then bundle 20's cases (mac
// with S=1, the FPSCR cause bits). The includer gives an Io (registers, memory, prepare, run); run_all plays them.
#pragma once
#include <limits>
#include <type_traits>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

struct Sh4tStats { uint32_t checks, fails, groups, group_fails[16]; };

// Io: SrType, r(n), gbr(), fr_hex(n), xf_hex(n), get_dr(n), set_dr(n, d), sr(), set_sr(v), mac(), set_mac(v), fpul(),
// set_pr(v), set_sz(v), prepare(op, op2, op3), run(n), write16/32(a, v) (any alignment), read8/16/32(a), mac_l_s1().
template<class Io>
class Sh4OpTest
{
	typedef uint8_t u8;
	typedef uint16_t u16;
	typedef uint32_t u32;
	typedef uint64_t u64;
	typedef int64_t s64;

	static const u32 REG_MAGIC = 0xbaadf00d;
	static const u32 START_PC = 0xAC000000;

	Io& io;
	Sh4tStats& st;
	const char* tag;
	const char* group;
	u32 checked_r, checked_fr, checked_xf;
	bool checked_gbr;

	static float inf() { return std::numeric_limits<float>::infinity(); }
	static float qnan() { return std::numeric_limits<float>::quiet_NaN(); }

	template<typename T> static double num(T v, std::true_type) { return (double)v; }
	template<typename T> static double num(T v, std::false_type) { return (double)(u64)v; }
	void fail(int line, const char* what, double got, double want, u64 got_bits, u64 want_bits)
	{
		if (st.fails++ < 8)
			printf("%s FAIL %s, sh4_optest.h:%d: %s = %.9g (%llX), want %.9g (%llX)\n", tag, group, line, what, got,
			       (unsigned long long)got_bits, want, (unsigned long long)want_bits);
	}
	static u64 bits(float f) { u32 b; memcpy(&b, &f, 4); return b; }
	static u64 bits(double d) { u64 b; memcpy(&b, &d, 8); return b; }
	template<typename T> static u64 bits(T v) { return (u64)v; }

	// gtest's ASSERT_EQ: a == b in the operands' common type.
	template<typename A, typename B> void eq(A a, B b, int line)
	{
		typedef typename std::common_type<A, B>::type C;
		st.checks++;
		if ((C)a == (C)b)
			return;
		const std::integral_constant<bool, std::is_floating_point<C>::value> fl;
		fail(line, "value", num((C)a, fl), num((C)b, fl), bits((C)a), bits((C)b));
	}
	void is_nan(float v, int line)
	{
		st.checks++;
		if (v != v)
			return;
		fail(line, "NaN check", v, qnan(), bits(v), bits(qnan()));
	}

	static u16 Rm(int r) { return r << 4; }
	static u16 Rn(int r) { return r << 8; }
	static u16 Imm8(int i) { return (u8)i; }
	static u16 Imm4(int i) { return i & 0xf; }

	void ClearRegs()
	{
		for (int i = 0; i < 16; i++)
		{
			io.r(i) = REG_MAGIC;
			io.xf_hex(i) = REG_MAGIC;
			io.fr_hex(i) = REG_MAGIC;
		}
		io.set_sr(0x700000F0);
		io.set_mac(0);
		io.gbr() = REG_MAGIC;
		checked_r = checked_fr = checked_xf = 0;
		checked_gbr = false;
	}
	void AssertState(int line)
	{
		for (int i = 0; i < 16; i++)
			if (!(checked_r >> i & 1))
				eq(io.r(i), REG_MAGIC, line);
		if (!checked_gbr)
			eq(io.gbr(), REG_MAGIC, line);
		for (int i = 0; i < 16; i++)
			if (!(checked_xf >> i & 1))
				eq(io.xf_hex(i), REG_MAGIC, line);
		for (int i = 0; i < 16; i++)
			if (!(checked_fr >> i & 1))
				eq(io.fr_hex(i), REG_MAGIC, line);
	}
	u32& r(u32 n) { checked_r |= 1u << n; return io.r(n); }
	u32& gbr() { checked_gbr = true; return io.gbr(); }
	u64 mac() { return io.mac(); }
	typename Io::SrType& sr() { return io.sr(); }
	float& fr(int n) { checked_fr |= 1u << n; return reinterpret_cast<float&>(io.fr_hex(n)); }
	double getDr(int n) { checked_fr |= 3u << (n * 2); return io.get_dr(n); }
	void setDr(int n, double d) { checked_fr |= 3u << (n * 2); io.set_dr(n, d); }
	u32& fpul() { return io.fpul(); }
	float& fpul_f() { return reinterpret_cast<float&>(io.fpul()); }
	void PrepareOp(u16 op, u16 op2 = 0, u16 op3 = 0) { io.prepare(op, op2, op3); }
	void RunOp(int numOp = 1) { io.run(numOp); }
	void write32(u32 a, u32 v) { io.write32(a, v); }
	void write16(u32 a, u32 v) { io.write16(a, v); }
	u32 read32(u32 a) { return io.read32(a); }
	u32 read16(u32 a) { return io.read16(a); }
	u32 read8(u32 a) { return io.read8(a); }

#define ASSERT_EQ(a, b) eq((a), (b), __LINE__)
#define ASSERT_NAN(a) is_nan((a), __LINE__)
#define AssertState() AssertState(__LINE__)

	void MovRmRnTest()
	{
		for (int src = 0; src < 16; src++)
		{
			for (int dst = 0; dst < 16; dst++)
			{
				ClearRegs();
				u32 v = (dst << 28) | src;
				r(src) = v;
				PrepareOp(0x6003 | Rm(src) | Rn(dst));	// mov Rm, Rn
				RunOp();
				ASSERT_EQ(r(dst), v);
				ASSERT_EQ(r(src), v);
				AssertState();
			}
		}
	}
	void MovImmRnTest()
	{
		ClearRegs();
		int v = 42;
		PrepareOp(0xe000 | Rn(1) | Imm8(v));	// mov #imm, Rn
		RunOp();
		ASSERT_EQ(r(1), (u32)v);
		AssertState();

		ClearRegs();
		v = -1;
		PrepareOp(0xe000 | Rn(12) | Imm8(v));	// mov #imm, Rn
		RunOp();
		ASSERT_EQ(r(12), 0xffffffff);
		AssertState();
	}
	void MovMiscTest()
	{
		ClearRegs();
		u32 disp = 0x10;
		PrepareOp(0xc700 | Imm8(disp));	// mova @(disp,PC),R0
		RunOp();
		ASSERT_EQ(r(0), START_PC + 4 + disp * 4);
		AssertState();

		ClearRegs();
		disp = 0x11;
		PrepareOp(0x9,						// nop
				0xc700 | Imm8(disp));		// mova @(disp,PC),R0
		RunOp(2);
		ASSERT_EQ(r(0), START_PC + 4 + disp * 4);	// uses PC & 0xfffffffc
		AssertState();

		ClearRegs();
		sr().T = 1;
		PrepareOp(0x0029 | Rn(1));			// movt Rn
		RunOp();
		ASSERT_EQ(r(1), 1u);
		AssertState();
		sr().T = 0;
		RunOp();
		ASSERT_EQ(r(1), 0u);

		ClearRegs();
		r(3) = 0x12345678;
		PrepareOp(0x6009 | Rn(4) | Rm(3));			// swap.w Rm, Rn
		RunOp();
		ASSERT_EQ(r(4), 0x56781234u);
		ASSERT_EQ(r(3), 0x12345678u);
		AssertState();

		ClearRegs();
		r(3) = 0xaabbccdd;
		PrepareOp(0x6008 | Rn(4) | Rm(3));			// swap.b Rm, Rn
		RunOp();
		ASSERT_EQ(r(4), 0xaabbddccu);
		ASSERT_EQ(r(3), 0xaabbccddu);
		AssertState();

		ClearRegs();
		r(3) = 0xaabbccdd;
		r(4) = 0xeeff0011;
		PrepareOp(0x200d | Rn(4) | Rm(3));			// xtrct Rm, Rn
		RunOp();
		ASSERT_EQ(r(4), 0xccddeeffu);
		ASSERT_EQ(r(3), 0xaabbccddu);
		AssertState();
	}

	void LoadTest()
	{
		ClearRegs();
		r(14) = 0x8C001000;
		write32(r(14), 0xffccaa88u);
		PrepareOp(0x6002 | Rm(14) | Rn(11));	// mov.l @Rm,Rn
		RunOp();
		ASSERT_EQ(r(11), 0xffccaa88u);
		AssertState();

		ClearRegs();
		r(14) = 0x8C001000;
		PrepareOp(0x6001 | Rm(14) | Rn(11));	// mov.w @Rm,Rn
		RunOp();
		ASSERT_EQ(r(11), 0xffffaa88u);
		AssertState();

		ClearRegs();
		r(14) = 0x8C001000;
		PrepareOp(0x6000 | Rm(14) | Rn(11));	// mov.b @Rm,Rn
		RunOp();
		ASSERT_EQ(r(11), 0xffffff88u);
		AssertState();

		ClearRegs();
		r(8) = 0x8C001004;
		write32(r(8), 0x4433ff11);
		PrepareOp(0x6006 | Rm(8) | Rn(7));		// mov.l @Rm+,Rn
		RunOp();
		ASSERT_EQ(r(7), 0x4433ff11u);
		ASSERT_EQ(r(8), 0x8C001008);
		AssertState();

		ClearRegs();
		r(7) = 0x8C001004;
		write32(r(7), 0x4433ff11);
		PrepareOp(0x6006 | Rm(7) | Rn(7));		// mov.l @Rm+,Rn
		RunOp();
		ASSERT_EQ(r(7), 0x4433ff11u);
		AssertState();

		ClearRegs();
		r(8) = 0x8C001004;
		PrepareOp(0x6005 | Rm(8) | Rn(7));		// mov.w @Rm+,Rn
		RunOp();
		ASSERT_EQ(r(7), 0xffffff11u);
		ASSERT_EQ(r(8), 0x8C001006);
		AssertState();

		ClearRegs();
		r(8) = 0x8C001004;
		PrepareOp(0x6004 | Rm(8) | Rn(7));		// mov.b @Rm+,Rn
		RunOp();
		ASSERT_EQ(r(7), 0x11u);
		ASSERT_EQ(r(8), 0x8C001005);
		AssertState();

		ClearRegs();
		write32(0x8C001010, 0x50607080);
		r(8) = 0x8C001004;
		PrepareOp(0x5000 | Rm(8) | Rn(7) | Imm4(3));// mov.l @(disp, Rm), Rn
		RunOp();
		ASSERT_EQ(r(7), 0x50607080u);
		AssertState();

		ClearRegs();
		write32(0x8C001010, 0x50607080);
		r(8) = 0x8C001004;
		PrepareOp(0x8500 | Rm(8) | Imm4(6));		// mov.w @(disp, Rm), R0
		RunOp();
		ASSERT_EQ(r(0), 0x7080u);
		AssertState();

		ClearRegs();
		write32(0x8C001010, 0x50607080);
		r(8) = 0x8C001004;
		PrepareOp(0x8400 | Rm(8) | Imm4(12));		// mov.b @(disp, Rm), R0
		RunOp();
		ASSERT_EQ(r(0), 0xffffff80u);
		AssertState();
	}
	void LoadTest2()
	{
		ClearRegs();
		r(11) = 0x8C000800;
		r(0) = 0x00000800;
		write32(r(11) + r(0), 0x88aaccffu);
		PrepareOp(0x000e | Rm(11) | Rn(12));	// mov.l @(R0, Rm), Rn
		RunOp();
		ASSERT_EQ(r(12), 0x88aaccffu);
		AssertState();

		ClearRegs();
		r(11) = 0x8C000800;
		r(0) = 0x00000800;
		write32(r(11) + r(0), 0x88aaccffu);
		PrepareOp(0x000d | Rm(11) | Rn(12));	// mov.w @(R0, Rm), Rn
		RunOp();
		ASSERT_EQ(r(12), 0xffffccffu);
		AssertState();

		ClearRegs();
		r(11) = 0x8C000800;
		r(0) = 0x00000800;
		write32(r(11) + r(0), 0x88aaccffu);
		PrepareOp(0x000c | Rm(11) | Rn(12));	// mov.b @(R0, Rm), Rn
		RunOp();
		ASSERT_EQ(r(12), 0xffffffffu);
		AssertState();

		ClearRegs();
		gbr() = 0x8C000800;
		write32(gbr() + 0x10 * 4, 0x11223344u);
		PrepareOp(0xc600 | Imm8(0x10));		// mov.l @(disp, GBR), R0
		RunOp();
		ASSERT_EQ(r(0), 0x11223344u);
		AssertState();

		ClearRegs();
		gbr() = 0x8C000800;
		write32(gbr() + 0x18 * 2, 0x11223344u);
		PrepareOp(0xc500 | Imm8(0x18));		// mov.w @(disp, GBR), R0
		RunOp();
		ASSERT_EQ(r(0), 0x3344u);
		AssertState();

		ClearRegs();
		gbr() = 0x8C000800;
		write32(gbr() + 0x17, 0x112233c4u);
		PrepareOp(0xc400 | Imm8(0x17));		// mov.b @(disp, GBR), R0
		RunOp();
		ASSERT_EQ(r(0), 0xffffffc4u);
		AssertState();

		ClearRegs();
		u32 disp = 0x11;
		write32(START_PC + 4 + disp * 4, 0x01020304u);
		PrepareOp(0x9,							// nop
				0xd000 | Rn(6) | Imm8(disp));	// mov.l @(disp, PC), Rn
		RunOp(2);
		ASSERT_EQ(r(6), 0x01020304u);	// uses PC & 0xfffffffc
		AssertState();

		ClearRegs();
		disp = 0x12;
		write32(START_PC + 4 + disp * 2, 0x01020304u);
		PrepareOp(0x9000 | Rn(5) | Imm8(disp));	// mov.w @(disp, PC), Rn
		RunOp();
		ASSERT_EQ(r(5), 0x0304u);
		AssertState();
	}

	void StoreTest()
	{
		ClearRegs();
		r(14) = 0x8C001000;
		r(11) = 0xbeeff00d;
		PrepareOp(0x2002 | Rm(11) | Rn(14));	// mov.l Rm, @Rn
		RunOp();
		ASSERT_EQ(read32(0x8C001000), 0xbeeff00du);
		ASSERT_EQ(r(14), 0x8C001000u);
		ASSERT_EQ(r(11), 0xbeeff00du);
		AssertState();

		ClearRegs();
		r(14) = 0x8C001000;
		r(11) = 0xf00dbeef;
		write32(0x8C001000, 0xbaadbaad);
		PrepareOp(0x2001 | Rm(11) | Rn(14));	// mov.w Rm, @Rn
		RunOp();
		ASSERT_EQ(read32(0x8C001000), 0xbaadbeefu);
		ASSERT_EQ(r(14), 0x8C001000u);
		ASSERT_EQ(r(11), 0xf00dbeefu);
		AssertState();

		ClearRegs();
		r(14) = 0x8C001000;
		r(11) = 0xccccccf0;
		write32(0x8C001000, 0xbaadbaad);
		PrepareOp(0x2000 | Rm(11) | Rn(14));	// mov.b Rm, @Rn
		RunOp();
		ASSERT_EQ(read32(0x8C001000), 0xbaadbaf0u);
		ASSERT_EQ(r(14), 0x8C001000u);
		ASSERT_EQ(r(11), 0xccccccf0u);
		AssertState();

		ClearRegs();
		r(8) = 0x8C001004;
		r(7) = 0xfeedf00d;
		PrepareOp(0x2006 | Rm(7) | Rn(8));		// mov.l Rm, @-Rn
		RunOp();
		ASSERT_EQ(read32(0x8C001000), 0xfeedf00du);
		ASSERT_EQ(r(7), 0xfeedf00du);
		ASSERT_EQ(r(8), 0x8C001000u);
		AssertState();

		ClearRegs();
		r(7) = 0x8C001004;
		PrepareOp(0x2006 | Rm(7) | Rn(7));		// mov.l Rm, @-Rn
		RunOp();
		ASSERT_EQ(read32(0x8C001000), 0x8C001004); // value before decrement is stored
		ASSERT_EQ(r(7), 0x8C001000u);
		AssertState();

		ClearRegs();
		r(8) = 0x8C001002;
		r(7) = 0x1234cafe;
		PrepareOp(0x2005 | Rm(7) | Rn(8));		// mov.w Rm, @-Rn
		RunOp();
		ASSERT_EQ(read16(0x8C001000), 0xcafeu);
		ASSERT_EQ(r(7), 0x1234cafeu);
		ASSERT_EQ(r(8), 0x8C001000u);
		AssertState();

		ClearRegs();
		r(8) = 0x8C001001;
		r(7) = 0x12345642;
		PrepareOp(0x2004 | Rm(7) | Rn(8));		// mov.b Rm, @-Rn
		RunOp();
		ASSERT_EQ(read8(0x8C001000), 0x42u);
		ASSERT_EQ(r(7), 0x12345642u);
		ASSERT_EQ(r(8), 0x8C001000u);
		AssertState();

		ClearRegs();
		r(8) = 0x8C001004;
		r(7) = 0x50607080;
		PrepareOp(0x1000 | Rm(7) | Rn(8) | Imm4(3));// mov.l Rm, @(disp, Rn)
		RunOp();
		ASSERT_EQ(read32(0x8C001010), 0x50607080u);
		ASSERT_EQ(r(7), 0x50607080u);
		ASSERT_EQ(r(8), 0x8C001004u);
		AssertState();

		ClearRegs();
		r(8) = 0x8C001004;
		r(0) = 0x10203040;
		PrepareOp(0x8100 | Rm(8) | Imm4(3));		// mov.w R0, @(disp, Rn)
		RunOp();
		ASSERT_EQ(read16(0x8C00100A), 0x3040u);
		ASSERT_EQ(r(0), 0x10203040u);
		ASSERT_EQ(r(8), 0x8C001004u);
		AssertState();

		ClearRegs();
		r(8) = 0x8C001004;
		r(0) = 0x66666672;
		PrepareOp(0x8000 | Rm(8) | Imm4(3));		// mov.b R0, @(disp, Rn)
		RunOp();
		ASSERT_EQ(read8(0x8C001007), 0x72u);
		ASSERT_EQ(r(0), 0x66666672u);
		ASSERT_EQ(r(8), 0x8C001004u);
		AssertState();
	}
	void StoreTest2()
	{
		ClearRegs();
		r(11) = 0x8C000800;
		r(0) = 0x00000800;
		r(12) = 0x87654321;
		PrepareOp(0x0006 | Rm(12) | Rn(11));	// mov.l Rm, @(R0, Rn)
		RunOp();
		ASSERT_EQ(read32(0x8C001000), 0x87654321u);
		ASSERT_EQ(r(12), 0x87654321u);
		ASSERT_EQ(r(11), 0x8C000800u);
		ASSERT_EQ(r(0), 0x00000800u);
		AssertState();

		ClearRegs();
		r(11) = 0x8C000800;
		r(0) = 0x00000800;
		r(12) = 0x12345678;
		PrepareOp(0x0005 | Rm(12) | Rn(11));	// mov.w Rm, @(R0, Rn)
		RunOp();
		ASSERT_EQ(read32(0x8C001000), 0x87655678u);	// relies on value set in previous test
		ASSERT_EQ(r(12), 0x12345678u);
		ASSERT_EQ(r(11), 0x8C000800u);
		ASSERT_EQ(r(0), 0x00000800u);
		AssertState();

		ClearRegs();
		r(11) = 0x8C000800;
		r(0) = 0x00000800;
		r(12) = 0x99999999;
		PrepareOp(0x0004 | Rm(12) | Rn(11));	// mov.b Rm, @(R0, Rn)
		RunOp();
		ASSERT_EQ(read32(0x8C001000), 0x87655699u);	// relies on value set in 2 previous tests
		ASSERT_EQ(r(12), 0x99999999u);
		ASSERT_EQ(r(11), 0x8C000800u);
		ASSERT_EQ(r(0), 0x00000800u);
		AssertState();

		ClearRegs();
		gbr() = 0x8C000800;
		r(0) = 0xabcdef01;
		PrepareOp(0xc200 | Imm8(0x10));			// mov.l R0, @(disp, GBR)
		RunOp();
		ASSERT_EQ(read32(0x8C000840), 0xabcdef01u);
		ASSERT_EQ(gbr(), 0x8C000800u);
		ASSERT_EQ(r(0), 0xabcdef01u);
		AssertState();

		ClearRegs();
		gbr() = 0x8C000800;
		r(0) = 0x11117777;
		PrepareOp(0xc100 | Imm8(0x20));			// mov.w R0, @(disp, GBR)
		RunOp();
		ASSERT_EQ(read32(0x8C000840), 0xabcd7777u);	// relies on value set in previous test
		AssertState();

		ClearRegs();
		gbr() = 0x8C000800;
		r(0) = 0x22222266;
		PrepareOp(0xc000 | Imm8(0x40));			// mov.b R0, @(disp, GBR)
		RunOp();
		ASSERT_EQ(read32(0x8C000840), 0xabcd7766u);	// relies on value set in 2 previous tests
		AssertState();
	}

	void ArithmeticTest()
	{
		ClearRegs();
		r(0) = 0x111111;
		r(1) = 0x222222;
		PrepareOp(0x300c | Rm(0) | Rn(1));	// add Rm, Rn
		RunOp();
		ASSERT_EQ(r(0), 0x111111u);
		ASSERT_EQ(r(1), 0x333333u);
		ASSERT_EQ(sr().T, 0u);
		AssertState();

		ClearRegs();
		r(2) = 0x11111;
		PrepareOp(0x7000 | Rn(2) | Imm8(-1));// add #imm, Rn
		RunOp();
		ASSERT_EQ(r(2), 0x11110u);
		AssertState();

		ClearRegs();
		r(2) = 0;
		r(3) = 0;
		PrepareOp(0x300e | Rn(2) | Rm(3));	// addc Rm, Rn
		RunOp();
		ASSERT_EQ(r(2), 0u);
		ASSERT_EQ(sr().T, 0u);
		AssertState();

		ClearRegs();
		sr().T = 1;
		r(2) = 0;
		r(3) = 0;
		RunOp();
		ASSERT_EQ(r(2), 1u);
		ASSERT_EQ(sr().T, 0u);
		AssertState();

		ClearRegs();
		sr().T = 0;
		r(2) = 0xffffffff;
		r(3) = 1;
		RunOp();
		ASSERT_EQ(r(2), 0u);
		ASSERT_EQ(sr().T, 1u);
		AssertState();

		ClearRegs();
		sr().T = 0;
		r(2) = 0xfffffffe;
		r(3) = 1;
		RunOp();
		ASSERT_EQ(r(2), 0xffffffffu);
		ASSERT_EQ(sr().T, 0u);
		AssertState();

		ClearRegs();
		sr().T = 1;
		r(2) = 0xfffffffe;
		r(3) = 1;
		RunOp();
		ASSERT_EQ(r(2), 0u);
		ASSERT_EQ(sr().T, 1u);
		AssertState();

		ClearRegs();
		r(2) = 0;
		r(3) = 0;
		PrepareOp(0x300f | Rn(2) | Rm(3));	// addv Rm, Rn
		RunOp();
		ASSERT_EQ(r(2), 0u);
		ASSERT_EQ(sr().T, 0u);
		AssertState();

		ClearRegs();
		r(2) = 1;
		r(3) = 0x7FFFFFFE;
		RunOp();
		ASSERT_EQ(r(2), 0x7FFFFFFFu);
		ASSERT_EQ(sr().T, 0u);
		AssertState();

		ClearRegs();
		r(2) = 2;
		r(3) = 0x7FFFFFFE;
		RunOp();
		ASSERT_EQ(r(2), 0x80000000u);
		ASSERT_EQ(sr().T, 1u);
		AssertState();

		ClearRegs();
		r(2) = -1;
		r(3) = 0x80000001;
		RunOp();
		ASSERT_EQ(r(2), 0x80000000u);
		ASSERT_EQ(sr().T, 0u);
		AssertState();

		ClearRegs();
		r(2) = -2;
		r(3) = 0x80000001;
		RunOp();
		ASSERT_EQ(r(2), 0x7FFFFFFFu);
		ASSERT_EQ(sr().T, 1u);
		AssertState();

		ClearRegs();
		r(0) = 0x111111;
		r(1) = 0x333333;
		PrepareOp(0x3008 | Rm(0) | Rn(1));	// sub Rm, Rn
		RunOp();
		ASSERT_EQ(r(1), 0x222222u);
		ASSERT_EQ(r(0), 0x111111u);
		ASSERT_EQ(sr().T, 0u);
		AssertState();

		ClearRegs();
		r(0) = 0x111111;
		r(1) = 0x333333;
		PrepareOp(0x300A | Rm(0) | Rn(1));	// subc Rm, Rn
		RunOp();
		ASSERT_EQ(r(1), 0x222222u);
		ASSERT_EQ(r(0), 0x111111u);
		ASSERT_EQ(sr().T, 0u);
		AssertState();

		ClearRegs();
		sr().T = 0;
		r(0) = 1;
		r(1) = 0;
		RunOp();
		ASSERT_EQ(r(1), 0xffffffffu);
		ASSERT_EQ(r(0), 1u);
		ASSERT_EQ(sr().T, 1u);
		AssertState();

		ClearRegs();
		sr().T = 1;
		r(0) = 0;
		r(1) = 0;
		RunOp();
		ASSERT_EQ(r(1), 0xffffffffu);
		ASSERT_EQ(r(0), 0u);
		ASSERT_EQ(sr().T, 1u);
		AssertState();

		ClearRegs();
		sr().T = 1;
		r(0) = 0;
		r(1) = 1;
		RunOp();
		ASSERT_EQ(r(1), 0u);
		ASSERT_EQ(r(0), 0u);
		ASSERT_EQ(sr().T, 0u);
		AssertState();

		ClearRegs();
		r(2) = 0;
		r(3) = 0;
		PrepareOp(0x300b | Rn(2) | Rm(3));	// subv Rm, Rn
		RunOp();
		ASSERT_EQ(r(2), 0u);
		ASSERT_EQ(sr().T, 0u);
		AssertState();

		ClearRegs();
		r(2) = 0x80000001;
		r(3) = 2;
		RunOp();
		ASSERT_EQ(r(2), 0x7fffffffu);
		ASSERT_EQ(sr().T, 1u);
		AssertState();

		ClearRegs();
		r(2) = 0x7FFFFFFE;
		r(3) = 0xFFFFFFFE;
		RunOp();
		ASSERT_EQ(r(2), 0x80000000u);
		ASSERT_EQ(sr().T, 1u);
		AssertState();

		ClearRegs();
		r(2) = 0x7FFFFFFE;
		r(3) = 0xFFFFFFFF;
		RunOp();
		ASSERT_EQ(r(2), 0x7FFFFFFFu);
		ASSERT_EQ(sr().T, 0u);
		AssertState();

		ClearRegs();
		r(13) = 0;
		PrepareOp(0x600b | Rn(15) | Rm(13));	// neg Rm, Rn
		RunOp();
		ASSERT_EQ(r(15), 0u);
		ASSERT_EQ(r(13), 0u);
		ASSERT_EQ(sr().T, 0u);
		AssertState();

		ClearRegs();
		r(13) = 1;
		RunOp();
		ASSERT_EQ(r(15), 0xffffffff);
		ASSERT_EQ(r(13), 1u);
		ASSERT_EQ(sr().T, 0u);
		AssertState();

		ClearRegs();
		r(13) = 0;
		PrepareOp(0x600a | Rn(15) | Rm(13));	// negc Rm, Rn
		RunOp();
		ASSERT_EQ(r(15), 0u);
		ASSERT_EQ(r(13), 0u);
		ASSERT_EQ(sr().T, 0u);
		AssertState();

		ClearRegs();
		sr().T = 1;
		r(13) = 0xffffffff;
		RunOp();
		ASSERT_EQ(r(15), 0u);
		ASSERT_EQ(r(13), 0xffffffffu);
		ASSERT_EQ(sr().T, 1u);
		AssertState();

		ClearRegs();
		sr().T = 0;
		r(13) = 0xffffffff;
		RunOp();
		ASSERT_EQ(r(15), 1u);
		ASSERT_EQ(r(13), 0xffffffffu);
		ASSERT_EQ(sr().T, 1u);
		AssertState();

		ClearRegs();
		r(13) = 1;
		PrepareOp(0x600a | Rn(13) | Rm(13));	// negc Rm, Rn
		RunOp();
		ASSERT_EQ(r(13), 0xffffffffu);
		ASSERT_EQ(sr().T, 1u);
		AssertState();

		ClearRegs();
		sr().T = 1;
		r(13) = 0;
		RunOp();
		ASSERT_EQ(r(13), 0xffffffffu);
		ASSERT_EQ(sr().T, 1u);
		AssertState();

		ClearRegs();
		r(13) = 1;
		PrepareOp(0x600f | Rn(12) | Rm(13));	// exts.w Rm, Rn
		RunOp();
		ASSERT_EQ(r(12), 1u);
		ASSERT_EQ(sr().T, 0u);
		AssertState();

		ClearRegs();
		r(13) = 0xffff;
		RunOp();
		ASSERT_EQ(r(12), -1u);
		ASSERT_EQ(sr().T, 0u);
		AssertState();

		ClearRegs();
		r(13) = 1;
		PrepareOp(0x600d | Rn(12) | Rm(13));	// extu.w Rm, Rn
		RunOp();
		ASSERT_EQ(r(12), 1u);
		ASSERT_EQ(sr().T, 0u);
		AssertState();

		ClearRegs();
		r(13) = 0xffff;
		RunOp();
		ASSERT_EQ(r(12), 0xffffu);
		ASSERT_EQ(sr().T, 0u);
		AssertState();

		ClearRegs();
		r(13) = 1;
		PrepareOp(0x600e | Rn(12) | Rm(13));	// exts.b Rm, Rn
		RunOp();
		ASSERT_EQ(r(12), 1u);
		ASSERT_EQ(sr().T, 0u);
		AssertState();

		ClearRegs();
		r(13) = 0x80;
		RunOp();
		ASSERT_EQ(r(12), 0xffffff80u);
		ASSERT_EQ(sr().T, 0u);
		AssertState();

		ClearRegs();
		r(13) = 1;
		PrepareOp(0x600c | Rn(12) | Rm(13));	// extu.b Rm, Rn
		RunOp();
		ASSERT_EQ(r(12), 1u);
		ASSERT_EQ(sr().T, 0u);
		AssertState();

		ClearRegs();
		r(13) = 0xff;
		RunOp();
		ASSERT_EQ(r(12), 0xffu);
		ASSERT_EQ(sr().T, 0u);
		AssertState();
	}
	void MulDivTest()
	{
		ClearRegs();
		r(2) = 0;
		r(3) = 0;
		PrepareOp(0x2007 | Rn(2) | Rm(3));	// div0s Rm, Rn
		RunOp();
		ASSERT_EQ(sr().Q, 0u);
		ASSERT_EQ(sr().M, 0u);
		ASSERT_EQ(sr().T, 0u);
		AssertState();

		ClearRegs();
		r(2) = 0x80000000;
		r(3) = 0;
		RunOp();
		ASSERT_EQ(sr().Q, 1u);
		ASSERT_EQ(sr().M, 0u);
		ASSERT_EQ(sr().T, 1u);
		AssertState();

		ClearRegs();
		r(2) = 0x80000000;
		r(3) = 0xffffffff;
		RunOp();
		ASSERT_EQ(sr().Q, 1u);
		ASSERT_EQ(sr().M, 1u);
		ASSERT_EQ(sr().T, 0u);
		AssertState();

		ClearRegs();
		sr().Q = sr().M = sr().T = 1;
		PrepareOp(0x0019);					// div0u
		RunOp();
		ASSERT_EQ(sr().Q, 0u);
		ASSERT_EQ(sr().M, 0u);
		ASSERT_EQ(sr().T, 0u);
		AssertState();

		// div1 with Rn == Rm
		ClearRegs();
		sr().T = sr().M = 1;
		r(7) = 0xb1e12951;
		PrepareOp(0x3774);					// div1 r7, r7
		RunOp();
		ASSERT_EQ(r(7), 0x15a37bf4);
		AssertState();

		ClearRegs();
		r(7) = 1000000000;
		r(8) = -1000000000;
		PrepareOp(0x300d | Rn(7) | Rm(8));	// dmuls.l Rm, Rn
		RunOp();
		ASSERT_EQ(mac(), (u64)((s64)1000000000 * -1000000000));
		AssertState();

		ClearRegs();
		r(7) = 0x12345678;
		r(8) = 0x10000000;
		PrepareOp(0x3005 | Rn(7) | Rm(8));	// dmulu.l Rm, Rn
		RunOp();
		ASSERT_EQ(mac(), 0x123456780000000ull);
		AssertState();

		ClearRegs();
		r(7) = 0x12345678;
		r(8) = 0x10000000;
		PrepareOp(0x0007 | Rn(7) | Rm(8));	// mul.l Rm, Rn
		RunOp();
		ASSERT_EQ(mac(), 0x80000000ull);
		AssertState();

		ClearRegs();
		r(7) = 0x5678;
		r(8) = 0x8000;
		PrepareOp(0x200e | Rn(7) | Rm(8));	// mulu.l Rm, Rn
		RunOp();
		ASSERT_EQ(mac(), 0x2B3C0000ull);
		AssertState();

		ClearRegs();
		r(7) = -5678;
		r(8) = -1000;
		PrepareOp(0x200f | Rn(7) | Rm(8));	// muls.l Rm, Rn
		RunOp();
		ASSERT_EQ(mac(), 5678000ull);
		AssertState();

		ClearRegs();
		r(7) = 0xAC001000;
		write32(r(7), 4);
		r(8) = 0xAC002000;
		write32(r(8), 3);
		PrepareOp(0x000f | Rn(7) | Rm(8));	// mac.l @Rm+, @Rn+
		RunOp();
		ASSERT_EQ(mac(), 12ull);
		ASSERT_EQ(r(7), 0xAC001004u);
		ASSERT_EQ(r(8), 0xAC002004u);

		write32(r(7), -5);
		write32(r(8), 7);
		RunOp();
		ASSERT_EQ(mac(), -23ull);
		ASSERT_EQ(r(7), 0xAC001008u);
		ASSERT_EQ(r(8), 0xAC002008u);
		AssertState();

		ClearRegs();
		r(7) = 0xAC001000;
		write32(r(7), (u16)-7);
		r(8) = 0xAC002000;
		write32(r(8), 3);
		PrepareOp(0x400f | Rn(7) | Rm(8));	// mac.w @Rm+, @Rn+
		RunOp();
		ASSERT_EQ(mac(), -21ull);
		ASSERT_EQ(r(7), 0xAC001002u);
		ASSERT_EQ(r(8), 0xAC002002u);

		write16(r(7), 5);
		write16(r(8), 7);
		RunOp();
		ASSERT_EQ(mac(), 14ull);
		ASSERT_EQ(r(7), 0xAC001004u);
		ASSERT_EQ(r(8), 0xAC002004u);
		AssertState();
	}
	void CmpTest()
	{
		ClearRegs();
		r(0) = 0;
		PrepareOp(0x8800 | Imm8(3));	// cmp/eq #imm, R0
		RunOp();
		ASSERT_EQ(sr().T, 0u);
		AssertState();

		ClearRegs();
		r(0) = 3;
		RunOp();
		ASSERT_EQ(sr().T, 1u);
		AssertState();

		ClearRegs();
		r(0) = -1;
		PrepareOp(0x8800 | Imm8(-1));	// cmp/eq #imm, R0
		RunOp();
		ASSERT_EQ(sr().T, 1u);
		AssertState();

		ClearRegs();
		r(2) = 0;
		r(3) = 0;
		PrepareOp(0x3000 | Rn(2) | Rm(3));	// cmp/eq Rm, Rn
		RunOp();
		ASSERT_EQ(sr().T, 1u);
		ASSERT_EQ(r(2), 0u);
		ASSERT_EQ(r(3), 0u);
		AssertState();

		ClearRegs();
		r(2) = 0;
		r(3) = 1;
		RunOp();
		ASSERT_EQ(sr().T, 0u);
		ASSERT_EQ(r(2), 0u);
		ASSERT_EQ(r(3), 1u);
		AssertState();

		ClearRegs();
		r(2) = 0;
		r(3) = 0;
		PrepareOp(0x3002 | Rn(2) | Rm(3));	// cmp/hs Rm, Rn
		RunOp();
		ASSERT_EQ(sr().T, 1u);
		ASSERT_EQ(r(2), 0u);
		ASSERT_EQ(r(3), 0u);
		AssertState();

		ClearRegs();
		r(2) = 0x80000000;
		r(3) = 1;
		RunOp();
		ASSERT_EQ(sr().T, 1u);
		ASSERT_EQ(r(2), 0x80000000u);
		ASSERT_EQ(r(3), 1u);
		AssertState();
	}

	void StatusRegTest()
	{
		ClearRegs();
		sr().S = 1;
		PrepareOp(0x0048);	// clrs
		RunOp();
		ASSERT_EQ(sr().S, 0u);

		PrepareOp(0x0058);	// sets
		RunOp();
		ASSERT_EQ(sr().S, 1u);

		sr().T = 1;
		PrepareOp(0x0008);	// clrt
		RunOp();
		ASSERT_EQ(sr().T, 0u);

		PrepareOp(0x0018);	// sett
		RunOp();
		ASSERT_EQ(sr().T, 1u);
	}

	void FloatingPointTest()
	{
		io.set_pr(0);

		ClearRegs();
		PrepareOp(0xF08D);	// fldi0 fr0
		RunOp();
		ASSERT_EQ(fr(0), 0.f);
		AssertState();

		ClearRegs();
		PrepareOp(0xF39D);	// fldi1 fr3
		RunOp();
		ASSERT_EQ(fr(3), 1.f);
		AssertState();

		ClearRegs();
		fr(2) = 48.f;
		PrepareOp(0xF21D);	// flds fr2, fpul
		RunOp();
		ASSERT_EQ(fpul_f(), 48.f);
		AssertState();

		ClearRegs();
		fpul_f() = 844.f;
		PrepareOp(0xF60D);	// fsts fpul, fr6
		RunOp();
		ASSERT_EQ(fr(6), 844.f);
		AssertState();

		ClearRegs();
		fr(10) = -128.f;
		PrepareOp(0xFA5D);	// fabs fr10
		RunOp();
		ASSERT_EQ(fr(10), 128.f);
		AssertState();

		ClearRegs();
		fr(11) = 64.f;
		PrepareOp(0xFB4D);	// fneg fr11
		RunOp();
		ASSERT_EQ(fr(11), -64.f);
		AssertState();

		// FADD
		ClearRegs();
		fr(12) = 13.f;
		fr(13) = 12.f;
		PrepareOp(0xFCD0);	// fadd fr13, fr12
		RunOp();
		ASSERT_EQ(fr(12), 25.f);
		ASSERT_EQ(fr(13), 12.f);
		AssertState();
		// +inf + norm = +inf
		fr(12) = inf();
		fr(13) = -10.f;
		RunOp();
		ASSERT_EQ(fr(12), inf());
		// norm + -inf = -inf
		fr(12) = 2.f;
		fr(13) = -inf();
		RunOp();
		ASSERT_EQ(fr(12), -inf());
		// NaN + norm = NaN
		fr(12) = 2.f;
		fr(13) = qnan();
		RunOp();
		ASSERT_NAN(fr(12));

		// FSUB
		ClearRegs();
		fr(14) = 10.f;
		fr(15) = 11.f;
		PrepareOp(0xFEF1);	// fsub fr15, fr14
		RunOp();
		ASSERT_EQ(fr(14), -1.f);
		ASSERT_EQ(fr(15), 11.f);
		AssertState();
		// +inf - norm = +inf
		fr(14) = inf();
		fr(15) = 10.f;
		RunOp();
		ASSERT_EQ(fr(14), inf());
		// -inf - norm = -inf
		fr(14) = -inf();
		fr(15) = -1.f;
		RunOp();
		ASSERT_EQ(fr(14), -inf());
		// norm - NaN = NaN
		fr(14) = 2.f;
		fr(15) = qnan();
		RunOp();
		ASSERT_NAN(fr(14));

		// FMUL
		ClearRegs();
		fr(0) = 4.f;
		fr(2) = 8.f;
		PrepareOp(0xF022);	// fmul fr2, fr0
		RunOp();
		ASSERT_EQ(fr(0), 32.f);
		ASSERT_EQ(fr(2), 8.f);
		AssertState();
		// +inf * norm = +inf
		fr(0) = inf();
		fr(2) = 50.f;
		RunOp();
		ASSERT_EQ(fr(0), inf());
		// -inf * norm = -inf
		fr(0) = -inf();
		fr(2) = 100.f;
		RunOp();
		ASSERT_EQ(fr(0), -inf());
		// norm * NaN = NaN
		fr(0) = 2.f;
		fr(2) = qnan();
		RunOp();
		ASSERT_NAN(fr(0));

		// FDIV
		ClearRegs();
		fr(1) = 8.f;
		fr(3) = -2.f;
		PrepareOp(0xF133);	// fdiv fr3, fr1
		RunOp();
		ASSERT_EQ(fr(1), -4.f);
		ASSERT_EQ(fr(3), -2.f);
		AssertState();
		// +inf / -norm = -inf
		fr(1) = inf();
		fr(3) = -50.f;
		RunOp();
		ASSERT_EQ(fr(1), -inf());
		// -inf / norm = -inf
		fr(1) = -inf();
		fr(3) = 100.f;
		RunOp();
		ASSERT_EQ(fr(1), -inf());
		// norm / NaN = NaN
		fr(1) = 2.f;
		fr(3) = qnan();
		RunOp();
		ASSERT_NAN(fr(1));

		// FMAC
		ClearRegs();
		fr(0) = 3.f;
		fr(4) = 5.f;
		fr(5) = -7.f;
		PrepareOp(0xF45E);	// fmac fr0, fr5, fr4
		RunOp();
		ASSERT_EQ(fr(0), 3.f);
		ASSERT_EQ(fr(4), -16.f);
		ASSERT_EQ(fr(5), -7.f);
		AssertState();
		// +inf + norm * norm = +inf
		fr(4) = inf();
		RunOp();
		ASSERT_EQ(fr(4), inf());
		// norm + +inf * -inf = -inf
		fr(0) = inf();
		fr(4) = 1.f;
		fr(5) = -inf();
		RunOp();
		ASSERT_EQ(fr(4), -inf());
		// norm + norm * NaN = NaN
		fr(0) = 2.f;
		fr(4) = 1.f;
		fr(5) = qnan();
		RunOp();
		ASSERT_NAN(fr(4));

		// FSQRT
		ClearRegs();
		fr(11) = 64.f;
		PrepareOp(0xFB6D);	// fsqrt fr11
		RunOp();
		ASSERT_EQ(fr(11), 8.f);
		AssertState();
		// sqrt(+inf) = +inf
		fr(11) = inf();
		RunOp();
		ASSERT_EQ(fr(11), inf());
		// sqrt(NaN) = NaN
		fr(11) = qnan();
		RunOp();
		ASSERT_NAN(fr(11));
		// sqrt(0) = 0
		fr(11) = 0.f;
		RunOp();
		ASSERT_EQ(fr(11), 0.f);

		// FCMP/EQ
		ClearRegs();
		fr(4) = 45.f;
		fr(7) = 45.f;
		PrepareOp(0xF474);	// fcmp/eq fr7, fr4
		RunOp();
		ASSERT_EQ(sr().T, 1u);
		AssertState();
		fr(4) = 46.f;
		RunOp();
		ASSERT_EQ(sr().T, 0u);
		AssertState();
		// +inf == +inf
		fr(4) = inf();
		fr(7) = inf();
		RunOp();
		ASSERT_EQ(sr().T, 1u);
		// -inf == -inf
		fr(4) = -inf();
		fr(7) = -inf();
		RunOp();
		ASSERT_EQ(sr().T, 1u);
		// NaN != NaN
		fr(4) = qnan();
		fr(7) = qnan();
		RunOp();
		ASSERT_EQ(sr().T, 0u);

		// FCMP/GT
		ClearRegs();
		fr(8) = 23.f;
		fr(9) = 22.f;
		PrepareOp(0xF895);	// fcmp/gt fr9, fr8
		RunOp();
		ASSERT_EQ(sr().T, 1u);
		AssertState();
		fr(9) = 100.f;
		RunOp();
		ASSERT_EQ(sr().T, 0u);
		AssertState();
		// +inf > norm
		fr(8) = inf();
		fr(9) = 77.f;
		RunOp();
		ASSERT_EQ(sr().T, 1u);
		// -inf < +inf
		fr(8) = -inf();
		fr(9) = inf();
		RunOp();
		ASSERT_EQ(sr().T, 0u);
		// norm > -inf
		fr(8) = -27.f;
		fr(9) = -inf();
		RunOp();
		ASSERT_EQ(sr().T, 1u);
		// !(Nan > NaN)
		fr(8) = qnan();
		fr(9) = qnan();
		RunOp();
		ASSERT_EQ(sr().T, 0u);

		ClearRegs();
		fpul() = 222;
		PrepareOp(0xF62D);	// float fpul, fr6
		RunOp();
		ASSERT_EQ(fr(6), 222.f);
		AssertState();

		// FTRC
		ClearRegs();
		fr(1) = 100.f;
		PrepareOp(0xF13D);	// ftrc fr1, fpul
		RunOp();
		ASSERT_EQ(fpul(), 100u);
		AssertState();
		// 2147483520.f -> 2147483520
		fr(1) = 2147483520.f;
		RunOp();
		ASSERT_EQ(fpul(), 2147483520u);
		// >2147483520.f -> 0x7fffffff;
		fr(1) = 2147483648.f;
		RunOp();
		ASSERT_EQ(fpul(), 0x7fffffffu);
		// -2147483648 -> -2147483648
		fr(1) = -2147483648.f;
		RunOp();
		ASSERT_EQ(fpul(), (u32)0x80000000u);
		// <-2147483648 -> 0x80000000
		fr(1) = -2147483904.f;
		RunOp();
		ASSERT_EQ(fpul(), 0x80000000u);
		// +inf -> 0x7fffffff
		fr(1) = inf();
		RunOp();
		ASSERT_EQ(fpul(), 0x7fffffffu);
		// -inf -> 0x80000000
		fr(1) = -inf();
		RunOp();
		ASSERT_EQ(fpul(), 0x80000000u);
		// NaN -> 0x80000000
		fr(1) = -qnan();
		RunOp();
		ASSERT_EQ(fpul(), 0x80000000u);

		// FSRRA
		ClearRegs();
		fr(5) = 16.f;
		PrepareOp(0xF57D);	// fsrra fr5
		RunOp();
		ASSERT_EQ(fr(5), 0.25f);
		AssertState();
		// 1/sqrt(+inf) -> 0
		fr(5) = inf();
		RunOp();
		ASSERT_EQ(fr(5), 0);
		// 1/sqrt(NaN) -> NaN
		fr(5) = qnan();
		RunOp();
		ASSERT_NAN(fr(5));

		// FSCA
		ClearRegs();
		fpul() = 0x8000;	// pi
		PrepareOp(0xF6FD);	// fsca fpul, dr3
		RunOp();
		ASSERT_EQ(fr(6), 0.f);	// sin
		ASSERT_EQ(fr(7), -1.f);	// cos
		AssertState();
	}

	void DoubleFloatingPointTest()
	{
		io.set_pr(1);
		io.set_sz(0);

		ClearRegs();
		setDr(5, -128.0);
		PrepareOp(0xFA5D);	// fabs dr5
		RunOp();
		ASSERT_EQ(getDr(5), 128.0);
		AssertState();

		ClearRegs();
		setDr(4, 64.0);
		PrepareOp(0xF84D);	// fneg fr8
		RunOp();
		ASSERT_EQ(getDr(4), -64.0);
		AssertState();

		ClearRegs();
		fr(9) = 5.f;
		PrepareOp(0xF94D);	// fneg fr9 (fneg doesn't depend on PR)
		RunOp();
		ASSERT_EQ(fr(9), -5.f);
		AssertState();

		ClearRegs();
		setDr(6, 13.0);
		setDr(7, 12.0);
		PrepareOp(0xFCE0);	// fadd dr7, dr6
		RunOp();
		ASSERT_EQ(getDr(6), 25.0);
		ASSERT_EQ(getDr(7), 12.0);
		AssertState();

		ClearRegs();
		setDr(0, 10.0);
		setDr(1, 11.0);
		PrepareOp(0xF021);	// fsub dr1, dr0
		RunOp();
		ASSERT_EQ(getDr(0), -1.0);
		ASSERT_EQ(getDr(1), 11.0);
		AssertState();

		ClearRegs();
		setDr(0, 4.0);
		setDr(2, 8.0);
		PrepareOp(0xF042);	// fmul dr2, dr0
		RunOp();
		ASSERT_EQ(getDr(0), 32.0);
		ASSERT_EQ(getDr(2), 8.0);
		AssertState();

		ClearRegs();
		setDr(1, 8.0);
		setDr(3, -2.0);
		PrepareOp(0xF263);	// fdiv dr3, dr1
		RunOp();
		ASSERT_EQ(getDr(1), -4.0);
		ASSERT_EQ(getDr(3), -2.0);
		AssertState();

		ClearRegs();
		setDr(1, 64.0);
		PrepareOp(0xF26D);	// fsqrt dr1
		RunOp();
		ASSERT_EQ(getDr(1), 8.0);
		AssertState();

		ClearRegs();
		setDr(4, 45.0);
		setDr(7, 45.0);
		PrepareOp(0xF8E4);	// fcmp/eq dr7, dr4
		RunOp();
		ASSERT_EQ(sr().T, 1u);
		AssertState();
		setDr(4, 46.0);
		RunOp();
		ASSERT_EQ(sr().T, 0u);
		AssertState();

		ClearRegs();
		setDr(4, 23.0);
		setDr(7, 22.0);
		PrepareOp(0xF8E5);	// fcmp/gt dr7, dr4
		RunOp();
		ASSERT_EQ(sr().T, 1u);
		AssertState();
		setDr(7, 100.0);
		RunOp();
		ASSERT_EQ(sr().T, 0u);
		AssertState();

		ClearRegs();
		fpul() = 123;
		PrepareOp(0xFC2D);	// float fpul, dr6
		RunOp();
		ASSERT_EQ(getDr(6), 123.0);
		AssertState();

		ClearRegs();
		setDr(1, 100.0);
		PrepareOp(0xF23D);	// ftrc dr1, fpul
		RunOp();
		ASSERT_EQ(fpul(), 100u);
		AssertState();

		ClearRegs();
		fpul_f() = 0.5f;
		PrepareOp(0xF8AD);	// fcnvsd fpul, dr4
		RunOp();
		ASSERT_EQ(getDr(4), 0.5);
		AssertState();

		ClearRegs();
		setDr(1, 0.25);
		PrepareOp(0xF2BD);	// fcnvds dr1, fpul
		RunOp();
		ASSERT_EQ(fpul_f(), 0.25f);
		AssertState();

		io.set_pr(0);
	}

	// Bundle 20 (P32): mac.w and mac.l with S=1 saturate as the SH-4 manual, with their post-increments.
	void MacSaturationTest()
	{
		ClearRegs();
		sr().S = 1;
		io.set_mac(0x1234567800000064ull);
		r(7) = 0xAC001000;
		write16(r(7), (u16)-7);
		r(8) = 0xAC002000;
		write16(r(8), 3);
		PrepareOp(0x400f | Rn(7) | Rm(8));	// mac.w @Rm+, @Rn+
		RunOp();
		ASSERT_EQ(mac(), 0x123456780000004Full);	// MACH stays with S=1
		ASSERT_EQ(r(7), 0xAC001002u);
		ASSERT_EQ(r(8), 0xAC002002u);

		io.set_mac(0x7FFFFFF0ull);
		write16(r(7), 0x7FFF);
		write16(r(8), 0x7FFF);
		RunOp();
		ASSERT_EQ(mac(), 0x7FFFFFFFull);
		ASSERT_EQ(r(7), 0xAC001004u);
		ASSERT_EQ(r(8), 0xAC002004u);

		io.set_mac(0x80000010ull);
		write16(r(7), 0x8000);
		write16(r(8), 0x7FFF);
		RunOp();
		ASSERT_EQ(mac(), 0x80000000ull);
		AssertState();

		// mac.l with S=1: the OLD interpreter hangs in a verify; io.mac_l_s1() says if this one can run it.
		if (!io.mac_l_s1())
		{
			st.checks++;
			fail(__LINE__, "mac.l S=1 not run (Interp Fixes OLD hangs there)", 0, 0, 0, 0);
			return;
		}
		ClearRegs();
		sr().S = 1;
		r(7) = 0xAC001000;
		write32(r(7), 4);
		r(8) = 0xAC002000;
		write32(r(8), 3);
		PrepareOp(0x000f | Rn(7) | Rm(8));	// mac.l @Rm+, @Rn+
		RunOp();
		ASSERT_EQ(mac(), 12ull);
		ASSERT_EQ(r(7), 0xAC001004u);
		ASSERT_EQ(r(8), 0xAC002004u);

		write32(r(7), -5);
		write32(r(8), 7);
		RunOp();
		ASSERT_EQ(mac(), -23ull);

		io.set_mac(0);
		write32(r(7), 0x7FFFFFFF);
		write32(r(8), 0x7FFFFFFF);
		RunOp();
		ASSERT_EQ(mac(), 0x00007FFFFFFFFFFFull);	// 48-bit saturation

		io.set_mac(0);
		write32(r(7), 0x7FFFFFFF);
		write32(r(8), 0x80000001);
		RunOp();
		ASSERT_EQ(mac(), 0xFFFF800000000000ull);
		ASSERT_EQ(r(7), 0xAC001010u);
		ASSERT_EQ(r(8), 0xAC002010u);
		AssertState();
	}

	// Bundle 20 (P64): cause bits written to FPSCR do not outlive the next FPU op, so an fcmp reads them as 0.
	void FpscrCauseTest()
	{
		ClearRegs();
		r(1) = 0x0007F001;
		fr(0) = 2.f;
		fr(1) = 1.f;
		PrepareOp(0x416A,	// lds r1, FPSCR
				0xF015,		// fcmp/gt fr1, fr0
				0x026A);	// sts FPSCR, r2
		RunOp(3);
		ASSERT_EQ(sr().T, 1u);
		ASSERT_EQ(r(2), 0x00040001u);
		AssertState();

		ClearRegs();
		r(3) = 0xAC001000;
		write32(r(3), 0x0007F001);
		PrepareOp(0x4366,	// lds.l @r3+, FPSCR
				0xF015,		// fcmp/gt fr1, fr0
				0x026A);	// sts FPSCR, r2
		RunOp(3);
		ASSERT_EQ(r(2), 0x00040001u);
		ASSERT_EQ(r(3), 0xAC001004u);
		AssertState();
	}

	// Bundle 20: what Flycast's groups leave out. mac.w / mac.l with one register (the second value at Rn + size),
	// addv with mixed signs, and system register stores to @-Rn (the value at Rn - 4, Rn moved once).
	void Bundle20OpTest()
	{
		ClearRegs();
		r(5) = 0xAC001000;
		write16(r(5), (u16)-3);
		write16(r(5) + 2, 5);
		PrepareOp(0x400f | Rn(5) | Rm(5));	// mac.w @r5+, @r5+
		RunOp();
		ASSERT_EQ(mac(), (u64)-15);
		ASSERT_EQ(r(5), 0xAC001004u);
		AssertState();

		ClearRegs();
		r(5) = 0xAC001000;
		write32(r(5), 6);
		write32(r(5) + 4, (u32)-7);
		PrepareOp(0x000f | Rn(5) | Rm(5));	// mac.l @r5+, @r5+
		RunOp();
		ASSERT_EQ(mac(), (u64)-42);
		ASSERT_EQ(r(5), 0xAC001008u);
		AssertState();

		ClearRegs();
		r(1) = 0xFFFFFFFF;
		r(2) = 1;
		PrepareOp(0x300f | Rn(1) | Rm(2));	// addv r2, r1: -1 + 1 has no overflow
		RunOp();
		ASSERT_EQ(r(1), 0u);
		ASSERT_EQ(sr().T, 0u);
		AssertState();

		ClearRegs();
		io.set_mac(0x1111111122222222ull);
		r(6) = 0xAC001010;
		PrepareOp(0x4612);	// sts.l MACL, @-r6
		RunOp();
		ASSERT_EQ(r(6), 0xAC00100Cu);
		ASSERT_EQ(read32(0xAC00100C), 0x22222222u);
		r(1) = 0x00040001;
		PrepareOp(0x416A, 0x4662);	// lds r1, FPSCR; sts.l FPSCR, @-r6
		RunOp(2);
		ASSERT_EQ(r(6), 0xAC001008u);
		ASSERT_EQ(read32(0xAC001008), 0x00040001u);
		PrepareOp(0x4603);	// stc.l SR, @-r6
		RunOp();
		ASSERT_EQ(r(6), 0xAC001004u);
		ASSERT_EQ(read32(0xAC001004), 0x700000F0u);
		AssertState();
	}

#undef ASSERT_EQ
#undef ASSERT_NAN
#undef AssertState

	template<typename F> void group_run(const char* name, F f)
	{
		group = name;
		const u32 before = st.fails;
		(this->*f)();
		if (st.groups < 16)
			st.group_fails[st.groups] = st.fails - before;
		st.groups++;
		if (st.fails != before)
			printf("%s %s: %u failed\n", tag, name, st.fails - before);
	}

public:
	Sh4OpTest(Io& io_, Sh4tStats& st_, const char* tag_) : io(io_), st(st_), tag(tag_), group("")
	{
		checked_r = checked_fr = checked_xf = 0;
		checked_gbr = false;
	}

	// Flycast's 13 groups in Sh4InterpreterTest.cpp's order, then bundle 20's three.
	void run_all()
	{
		typedef void (Sh4OpTest::*Fn)();
		static const struct { const char* name; Fn fn; } k_groups[] = {
			{ "MovRmRnTest", &Sh4OpTest::MovRmRnTest }, { "MovImmRnTest", &Sh4OpTest::MovImmRnTest },
			{ "MovMiscTest", &Sh4OpTest::MovMiscTest }, { "LoadTest", &Sh4OpTest::LoadTest },
			{ "LoadTest2", &Sh4OpTest::LoadTest2 }, { "StoreTest", &Sh4OpTest::StoreTest },
			{ "StoreTest2", &Sh4OpTest::StoreTest2 }, { "ArithmeticTest", &Sh4OpTest::ArithmeticTest },
			{ "MulDivTest", &Sh4OpTest::MulDivTest }, { "CmpTest", &Sh4OpTest::CmpTest },
			{ "StatusRegTest", &Sh4OpTest::StatusRegTest }, { "FloatingPointTest", &Sh4OpTest::FloatingPointTest },
			{ "DoubleFloatingPointTest", &Sh4OpTest::DoubleFloatingPointTest },
			{ "MacSaturationTest", &Sh4OpTest::MacSaturationTest }, { "FpscrCauseTest", &Sh4OpTest::FpscrCauseTest },
			{ "Bundle20OpTest", &Sh4OpTest::Bundle20OpTest },
		};
		for (u32 i = 0; i < sizeof(k_groups) / sizeof(k_groups[0]); i++)
			group_run(k_groups[i].name, k_groups[i].fn);
	}
};
