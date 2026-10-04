// 5.13 A2 high voices: base step_control() against advance(n, false) on x86, ns per sample (read the ratio: x86 is not
// the PPU). Usage: bench_ctl [voices] [chunk] [monitor reads/s]
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <random>
#include <string>
#include <vector>

extern "C" {
void* base_create(int); void base_write(void*, uint32_t, uint32_t); uint32_t base_read(void*, uint32_t);
void base_stepc(void*); uint32_t base_snap_hash(void*);
void* cur_create(int); void cur_write(void*, uint32_t, uint32_t); uint32_t cur_read(void*, uint32_t);
void cur_advance(void*, uint32_t, int, const int32_t*, int16_t*); uint32_t cur_snap_hash(void*);
}

struct Ev { uint32_t at, off, val; };

static void make_events(int voices, uint32_t total, std::vector<Ev>& ev) {
    std::mt19937 g(7);
    auto r = [&](uint32_t n) { return (uint32_t)(g() % n); };
    auto w = [&](uint32_t at, uint32_t off, uint32_t v) { ev.push_back({at, off, v}); };
    for (int ch = 0; ch < voices; ch++) {
        const uint32_t b = 0x80 * ch, fmt = r(10) < 6 ? 2 : r(4) ? 0 : 1;
        w(0, b + 0x00, fmt << 7 | 1u << 9);
        w(0, b + 0x04, r(0x8000) * 2);
        w(0, b + 0x08, r(500));
        w(0, b + 0x0C, 2500 + r(12000));
        w(0, b + 0x10, 31 | (8 + r(10)) << 6 | r(6) << 11);
        w(0, b + 0x14, (10 + r(10)) | (4 + r(16)) << 5 | 15 << 10);
        w(0, b + 0x18, (r(4) == 0 ? 0xEu : r(2) ? 0xFu : 0u) << 11 | r(0x400));
        w(0, b + 0x1C, r(5) == 0 ? (0x0200 | r(8) << 10 | 2u << 5) : 0);
        for (uint32_t o = 0x2C; o <= 0x3C; o += 4) w(0, b + o, 0x400 + r(0x1C00));
        w(0, b + 0x40, r(20) << 8 | r(20));
        w(0, b + 0x44, r(20) << 8 | r(20));
        w(0, b + 0x00, fmt << 7 | 1u << 9 | 0x4000);
    }
    w(0, 0, 0x4000 | 0x8000 | (2u << 7) | (1u << 9));
    for (uint32_t t = 1; t < total; t++)
        if (r(10000) < 1100) {   // ~4900 writes/s: pitch and volume
            const uint32_t b = 0x80 * r((uint32_t)voices);
            if (r(2)) w(t, b + 0x18, (r(2) ? 0xFu : 0u) << 11 | r(0x400));
            else w(t, b + 0x28, r(64) << 8 | r(32));
        }
}

int main(int argc, char** argv) {
    const int voices = argc > 1 ? atoi(argv[1]) : 28;
    const uint32_t chunk = argc > 2 ? (uint32_t)atoi(argv[2]) : 0;   // 0: catch up at every write and every 64 samples
    const uint32_t reads = argc > 3 ? (uint32_t)atoi(argv[3]) : 0;    // monitor reads per second, each a catch-up
    const uint32_t total = 441000;
    std::vector<Ev> ev;
    make_events(voices, total, ev);
    double ns[2];
    uint32_t hash[2];
    for (int m = 0; m < 2; m++) {
        void* h = m ? cur_create(0) : base_create(0);
        uint32_t now = 0, done = 0;
        size_t k = 0;
        auto t0 = std::chrono::steady_clock::now();
        auto catch_up = [&](uint32_t to) {
            if (m) { if (to > done) cur_advance(h, to - done, 0, 0, 0); }
            else for (uint32_t i = done; i < to; i++) base_stepc(h);
            done = to;
        };
        for (now = 0; now < total; now++) {
            for (; k < ev.size() && ev[k].at == now; k++) {
                catch_up(now);
                if (m) cur_write(h, ev[k].off, ev[k].val); else base_write(h, ev[k].off, ev[k].val);
            }
            if (chunk ? ((now + 1) % chunk == 0) : (((now + 1) & 63) == 0)) catch_up(now + 1);
            if (reads && (uint64_t)(now + 1) * reads / 44100 != (uint64_t)now * reads / 44100) catch_up(now + 1);
        }
        catch_up(total);
        ns[m] = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() / total;
        hash[m] = m ? cur_snap_hash(h) : base_snap_hash(h);
    }
    printf("reads/s %u voices %d chunk %s: base %.0f ns/sample, advance %.0f ns/sample (%.1fx) | %.1f / %.1f ns per voice-sample | state %s\n",
           reads, voices, chunk ? std::to_string(chunk).c_str() : "writes+64", ns[0], ns[1], ns[0] / ns[1], ns[0] / voices,
           ns[1] / voices, hash[0] == hash[1] ? "same" : "DIFFERENT");
    return hash[0] != hash[1];
}
