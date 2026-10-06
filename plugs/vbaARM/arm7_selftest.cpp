// ARM7DI conformance self-test: the vendored arm7di-tests-dreamcast binaries on vbaARM (port of the nullDC rust
// edition harness). Each starts at 0, ends at the word 0xDEADBEEF and passes when r1's low 30 bits are 0.

#include "arm7.h"
#include "arm_mem.h"
#include "arm7_selftest_data.h"

#include "ps3_timebase.h"
#define printf(...) printf(__VA_ARGS__)

// Core internals we drive directly.
extern u8* arm_aica_ram;

// Register file indices (match arm7.cpp).
#define RN_R1   1
#define RN_R15  15
#define RN_CPSR 16

static const u32 END_MARKER = 0xDEADBEEFu;
static const u32 MAX_INSNS  = 100000;   // safety limit, matches Rust MAX_CYCLES

// Scratch ARAM is the core's real sound RAM (ARAM_MASK behaves as in real operation), not a 2 MB static array:
// so the tests run before any game code (from arm_Init() under the ARM7 JIT) and the RAM is zeroed after them.
static u8* s_test_aram = 0;

// Decode r1 error flags into a short human-readable string (matches Rust).
static void decode_r1_errors(u32 r1, char* out, int out_sz)
{
    out[0] = 0;
    int used = 0;
    #define APPEND(s) do { \
        int _l = 0; const char* _p = (s); while (_p[_l]) _l++; \
        if (used + _l + 1 < out_sz) { for (int _i=0;_i<_l;_i++) out[used+_i]=_p[_i]; \
            used += _l; out[used]=0; } } while(0)

    if (r1 & 0x10) APPEND("BAD_Rd ");
    if (r1 & 0x20) APPEND("BAD_Rn ");
    if (r1 & 0xFF00) APPEND("OTHER ");
    u32 remaining = r1 & ~0x30u & 0xFF;
    if (remaining) APPEND("FLAGS ");
    #undef APPEND
}

// Run a single embedded test binary. Returns true on pass.
static bool run_one(const Arm7SelfTest& t)
{
    // Point the core at our scratch ARAM and clear it.
    arm_aica_ram = s_test_aram;
    memset(s_test_aram, 0, ARAM_SIZE);

    // The binaries are little-endian: byte loads apply the core's BE byte-swap, so arm_ReadMem32() reads
    // them back symmetrically, as the Rust harness's flat-LE memory does.
    for (u32 i = 0; i < t.size; i++)
        arm_WriteMem8(i, t.data[i]);

    // Reset + enable the core, then place the PC at the test entry (offset 0).
    // arm_SetEnabled(true) calls arm_Reset() internally on the off->on edge.
    arm_SetEnabled(false);
    arm_SetEnabled(true);

    // As arm_Reset(): the fetch does armNextPC = reg[15]; reg[15] += 4, so code at A sees reg[15] == A + 8.
    // Start at 0 with reg[15] = 4, not 8: 8 put every PC-relative LDR one word off.
    arm_SetNextPC(0);
    arm_SetReg(RN_R15, 4);

    u32 insns = 0;
    while (true)
    {
        u32 next_pc   = arm_GetNextPC();
        u32 next_insn = arm_ReadMem32(next_pc & ARAM_MASK);

        if (next_insn == END_MARKER)
            break;

        if (insns >= MAX_INSNS)
        {
            printf("[ARM7TEST]   %-8s TIMEOUT after %u insns (no END_MARKER)\n",
                   t.name, insns);
            return false;
        }

        arm_Run(1);   // execute exactly one instruction (>=6 ticks -> 1 step)
        insns++;
    }

    u32 r1 = arm_GetReg(RN_R1);
    u32 error_bits = r1 & 0x3FFFFFFFu;   // only low 30 bits are error flags

    if (error_bits == 0)
        return true;

    char errs[64];
    decode_r1_errors(r1, errs, sizeof(errs));
    printf("[ARM7TEST]   %-8s FAIL r1=0x%08X (%s)\n", t.name, r1, errs);
    return false;
}

int arm_RunSelfTests()
{
    printf("\n==================== ARM7DI SELF-TEST ====================\n");
    printf("[ARM7TEST] Running %d ARM7DI conformance tests...\n", arm7_selftest_count);

    // arm_Init() builds the cpuBitsSet[] table that LDM/STM use for the base writeback; without it the
    // LDM/STM tests fail with BAD_Rn.
    arm_Init();

    // The sound RAM doubles as the test scratch (see s_test_aram), so it has to
    // exist already: arm_init_mem() runs before arm_Init() in armInit().
    u8* saved_aram = arm_aica_ram;
    if (!saved_aram)
    {
        printf("[ARM7TEST] no sound RAM yet (call after arm_init_mem) - not run\n");
        printf("==========================================================\n\n");
        return -1;
    }
    s_test_aram = saved_aram;

    int failed = 0;
    for (int i = 0; i < arm7_selftest_count; i++)
    {
        if (!run_one(arm7_selftests[i]))
            failed++;
    }

    memset(saved_aram, 0, ARAM_SIZE);
    arm_aica_ram = saved_aram;
    arm_SetEnabled(false);   // leave the core disabled; emulator re-enables later

    if (failed == 0)
        printf("[ARM7TEST] PASS: all %d tests passed.\n", arm7_selftest_count);
    else
        printf("[ARM7TEST] FAIL: %d / %d tests failed.\n", failed, arm7_selftest_count);
    printf("==========================================================\n\n");

    return failed;
}
