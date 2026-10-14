#!/bin/bash
# WSL: each deliberate bug in the advance() code must make diff_adv fail (CAUGHT); MISSED means the test cannot see it.
S=$(cd "$(dirname "$0")" && pwd); cd "$S"
SRC=$S/../../plugs/caiqueAICA
M=$S/out/mut
CXX="g++ -O2 -std=c++17 -w"
run() {
  rm -rf "$M"; mkdir -p "$M"; cp "$SRC"/*.h "$SRC"/*.cpp "$M"/
  python3 - "$M/aica_model.cpp" "$1" "$2" <<'PY' || { echo "BADPATTERN $3"; return; }
import sys
p, a, b = sys.argv[1:4]; s = open(p).read()
if s.count(a) != 1: sys.exit(1)
open(p, 'w').write(s.replace(a, b))
PY
  $CXX -DHAVE_ADVANCE -I"$M" -c "$M/aica_model.cpp" -o "$M/m.o" && $CXX -DHAVE_ADVANCE -DPFX=cur_ -I"$M" -c model_wrap.cpp -o "$M/w.o" &&
  $CXX -o "$M/diff" diff_adv.cpp out/base_model.o out/base_wrap.o "$M/m.o" "$M/w.o" || { echo "BUILDFAIL $3"; return; }
  if "$M/diff" ${EPI:-300} 11 > "$M/out.txt"; then echo "MISSED $3"; else echo "CAUGHT $3: $(head -c 110 "$M/out.txt")"; fi
}
run 'const bool clean = CA + ip < ev;' 'const bool clean = CA + ip <= ev;' 'fast walk bound <='
run '                const int32_t ls = -(int32_t)(LPSLNK & (ast == EG_ATTACK) & (LSA < ev));' '                const int32_t ls = 0;' 'fast walk: no LPSLNK bound'
run 'const int32_t d1 = mul32x16(B >> 8, qd) + ((mul16(qd, B & 0xFF) + 255) >> 8);' 'const int32_t d1 = mul32x16(B >> 8, qd) + ((mul16(qd, B & 0xFF) + 254) >> 8);' 'lpf32 damping ceil off by one'
run '    const int32_t nb = -band;' '    const int32_t nb = 1 - band;' 'lpf32 low rounding'
run '    k = (512 & m) | ((256 + (int32_t)((v >> 1) & 0xFF)) & ~m);' '    k = (511 & m) | ((256 + (int32_t)((v >> 1) & 0xFF)) & ~m);' 'lpf k unity 511'
run '                        fix = (x8 == 0) & (B == B0) & (L == L0);' '                        fix = (B == B0) & (L == L0);' 'fast: lpf_fix without x8 == 0'
run 'const uint32_t md = (x.mdec0 - (n - 1) - 2) & 0xFFFF;' 'const uint32_t md = (x.mdec0 - n - 2) & 0xFFFF;' 'round: eg_cnt of the wrong sample'
run '    if (c.keyed || c.keyed_off) c.keyed = c.keyed_off = false;   /* each lasts one sample */' '' 'sample: key flags never cleared'
run '        if (!kx0 && (render ? (quiet & rsilent & bit) != 0 : (quiet & bit) != 0)) break;' '        if (!kx0 && (quiet & bit) != 0) break;' 'run: render stops at quiet (tails dropped)'
run '            s1 = (int16_t)(rd8(a1) | rd8(a1 + 1) << 8);' '            s1 = (int16_t)(rd8(a0) | rd8(a0 + 1) << 8);' 'fast: PCM16 s1 from s0'
run '                            s0 = (int16_t)v;' '' 'fast: ADPCM loop reset keeps s0'
run '& -(int32_t)last;' ';' 'fast: ADPCM preview kept on a step that is not the last'
run 'const uint64_t todo = x.kx ? ~0ull : render ? ~(quiet & rsilent) : (fetching | ~quiet);' 'const uint64_t todo = x.kx ? ~0ull : render ? ~quiet : (fetching | ~quiet);' 'round: render todo misses tails'
run '            lfsr = lfsr_adv(lfsr_at(x, i), ch);   /* only noise reads it; slot_control steps it once more */' '            ;' 'sample: noise LFSR not set'
run 'mod = (w & ~1) >> (7 - plfos), m = m0 + ((m0 * mod) >> 10);' 'mod = (w & ~1) >> (7 - plfos), m = m0 + ((m0 * mod) >> 9);' 'fast: PLFO depth x2'
run '                        alfo = (c.lfo.alfo_w & 0xFEu) >> (7 - alfos);' '                        alfo = (c.lfo.alfo_w & 0xFFu) >> (7 - alfos);' 'fast: ALFO bit 0 kept'
run '    if (c0 & 0x100) { mixl >>= 2; mixr >>= 2; }
    outL = (int16_t)clampi(mixl, -32768, 32767);
    outR = (int16_t)clampi(mixr, -32768, 32767);
}

void AicaModel::advance(' '    if (c0 & 0x100) { mixl >>= 1; mixr >>= 2; }
    outL = (int16_t)clampi(mixl, -32768, 32767);
    outR = (int16_t)clampi(mixr, -32768, 32767);
}

void AicaModel::advance(' 'final_mix: DAC18B shift'
run '        if (LPSLNK && c.AEG.state == EG_ATTACK && CA >= c.LSA) { set_aeg_state(ch, EG_DECAY1); c.env_skip = 0; }' '        if (LPSLNK && c.AEG.state == EG_ATTACK && CA >= c.LSA) set_aeg_state(ch, EG_DECAY1);' 'stream_step: LPSLNK keeps the mask'
run '                        if (LPSLNK && ast == EG_ATTACK && ca >= LSA) { ast = c.AEG.state = EG_DECAY1; es = c.env_skip = 0; }' '                        if (LPSLNK && ast == EG_ATTACK && ca >= LSA) { ast = c.AEG.state = EG_DECAY1; }' 'fast: LPSLNK keeps the mask'
run '                const uint32_t ticks = 1 + ((n - 1 - d0) >> irq.pre[t]);' '                const uint32_t ticks = ((n - 1 - d0) >> irq.pre[t]);' 'timers: one tick short'
run '                if (irq.cnt[t] + ticks >= 256) set |= (uint16_t)(0x40 << t);' '                if (irq.cnt[t] + ticks > 256) set |= (uint16_t)(0x40 << t);' 'timers: wrap at 256 missed'
run '        else { c.lfo.counter = sv; st++; }' '        else { c.lfo.counter = sv - 1; st++; }' 'lfo_tick: reload one short'
run '    if (__builtin_expect((t - c.lfo_t == 1) & (lc - 1 < sv), 1)) {' '    if (__builtin_expect((t - c.lfo_t == 1) & (lc <= sv), 1)) {' 'lfo_tick: counter 0 taken inline'
run '        const uint32_t j0 = i + ((x.mdec0 + eg_par - i) & 1);' '        const uint32_t j0 = i + ((x.mdec0 + eg_par - i + 1) & 1);' 'ctl closed form: wrong clock parity'
run '        const uint32_t j = j0 + 2 * ((1u - ec0) & c.env_skip);' '        const uint32_t j = j0 + 2 * ((0u - ec0) & c.env_skip);' 'ctl closed form: clock phase off by one'
run '            if (c.CA + (tot >> 14) >= ev) return slot_fast<false>(ch, i, x, 0, rendered);' '            if (c.CA + (tot >> 14) > ev) return slot_fast<false>(ch, i, x, 0, rendered);' 'ctl closed form: event bound off by one'
run '            const uint32_t tot = c.step + (last - i + 1) * c.update_rate;' '            const uint32_t tot = c.step + (last - i) * c.update_rate;' 'ctl closed form: one sample short'
run '    rv = prev + ((rv ^ neg) - neg);' '    rv = prev + (rv ^ neg);' 'adpcm: one-s complement negate'
run '    for (uint32_t i = 0; i < n; i++) l = lfsr_adv64(l);' '    for (uint32_t i = 1; i < n; i++) l = lfsr_adv64(l);' 'round: LFSR one sample short'
run '                if (R) c.FEG.vo = (uint16_t)vo;   /* slot_quiet() reads it */' '' 'fast: vo not latched before slot_quiet (perf only, expect MISSED)'
run '                                es = c.env_skip = 0;
                                out = true;' '                                out = true;' 'fast: one-shot keeps the mask (perf only, expect MISSED)'
echo done
