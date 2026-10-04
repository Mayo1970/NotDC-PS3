#!/bin/bash
# WSL: each deliberate bug in the A3 code must make diff_a3 fail (CAUGHT); MISSED means the test cannot see it.
S=$(cd "$(dirname "$0")" && pwd); cd "$S"
SRC=$S/../../plugs/caiqueAICA
M=$S/out/mut3
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
  $CXX -I"$M" -o "$M/diff" diff_a3.cpp out/base_model.o out/base_wrap.o "$M/m.o" "$M/w.o" || { echo "BUILDFAIL $3"; return; }
  if "$M/diff" ${EPI:-300} 11 > "$M/out.txt"; then echo "MISSED $3"; else echo "CAUGHT $3: $(head -c 110 "$M/out.txt")"; fi
}
run 'for (const int r0 = ch < 7 ? -1 : 0; i < i1; i++) {' 'for (const int r0 = 0; i < i1; i++) {' 'fast: slots 0..6 claim at their own sample'
run '            ca_prev = ca;
            if (__builtin_expect(' '            if (__builtin_expect(' 'fast: claim with the round-start CA as ca_old'
run '        if (c.ca_clr) adv_row[i1 - 1][3] = (int32_t)c.CA;
' '' 'fast: one-shot end claim from the row'
run 'if (en0 && ((coll_need >> ch) & 1) && i1 > i0) {' 'if (en0 && ((coll_need >> ch) & 1) && i1 > i0 && ch >= 7) {' 'fast: no claims for slots 0..6'
run '{ ca_prev = (uint32_t)adv_row[0][3]; i = 1; }' '{ i = 1; }' 'fast: sample 0 of slots 0..6 not taken as ca_old'
run '(uint32_t)!adpcm | ((uint32_t)!adp_nf & (uint32_t)!same)' '(uint32_t)!adpcm | (uint32_t)!adp_nf' 'fast: ADPCM claims without a word change'
run 'adp_nf = ((chr(ch, 0x18) >> 11) & 15) - 3u <= 4u;
        uint32_t lb' 'adp_nf = ((chr(ch, 0x18) >> 11) & 15) - 3u <= 3u;
        uint32_t lb' 'fast: ADPCM fetches at OCT 7'
run 'fs = f == 0 ? 0 : f == 1 ? 1 : 2;' 'fs = f == 0 ? 0 : f == 1 ? 2 : 2;' 'fast: PCM8 fetch address'
run '        busp[16 * i] += send_apply(row[2], ilv);
' '' 'fast: no bus send'
run '    const SendLv ilv = send_lv((r20 >> 4) & 0xF);
    int32_t *const busp' '    const SendLv ilv = send_lv(((r20 >> 4) & 0xF) ^ 1);
    int32_t *const busp' 'fast: bus send level off by one'
run '(side & pm) | (dir & ~pm), V16, (int32_t)CA};' '(side & pm) | (dir & ~pm), V16 >> 4, (int32_t)CA};' 'fast: lane 2 holds V16 >> 4'
run '        *(mix4 *)adv_row[i] = zero;
' '' 'fast: rows not cleared (stale V16 and CA)'
run '                if (!no_dsp) adv_bus[i][chr(ch, 0x20) & 0xF] += d;
' '' 'sample: no bus send'
run '                    if (cl && (ch >= 7 || i > 0)) { adv_cc[i - (ch < 7)][ch] = true; adv_cw[i - (ch < 7)][ch] = w; }' '                    if (cl && ch >= 7) { adv_cc[i][ch] = true; adv_cw[i][ch] = w; }' 'sample: no claims for slots 0..6'
run '                adv_cc[n - 1][k] = true;
' '' 'round: no preview claim'
run '    const uint32_t pm = dsp ? (uint32_t)coll_need & 0x7F : 0;' '    const uint32_t pm = 0;' 'round: no preview at all'
run 'for (int b = 0; b < 16; b++) adv_busw[b] = isel_cnt[b] ? -1 : 0;' 'for (int b = 0; b < 16; b++) adv_busw[b] = -1;' 'round: buses no slot points at overwritten'
run '        const mix4 v = (mix4)((umix4)bus[k] << 12) >> 12;' '        const mix4 v = bus[k];' 'dsp_sample: bus sum not wrapped at 20 bits'
run '        bus[k] = zero;
' '' 'dsp_sample: bus rows not cleared'
run '{ ((mix4 *)coll_claim)[k] = cc[k]; cc[k] = zero; }' '{ ((mix4 *)coll_claim)[k] = cc[k]; }' 'dsp_sample: claim rows not cleared'
run '*bank = (mix4 *)MIXS_bank[smp & 1];' '*bank = (mix4 *)MIXS_bank[(smp + 1) & 1];' 'dsp_sample: wrong bank parity'
run '        for (int k = 0; k < 8; k++) ((mix4 *)coll_word)[k] = cw[k];
' '' 'dsp_sample: words not copied'
run '    const uint32_t mdec = (adv_mdec0 - i) & 0xFFFF;' '    const uint32_t mdec = (adv_mdec0 - i - 1) & 0xFFFF;' 'dsp_sample: MDEC_CT one behind'
run '            coll_need = (prog[s].MWT | prog[s].MRD) ? coll_need | b : coll_need & ~b;' '            coll_need = prog[s].MWT ? coll_need | b : coll_need & ~b;' 'coll_need: even MRD left out'
run '            const uint64_t b = 1ull << (((s + 14) >> 1) & 63);' '            const uint64_t b = 1ull << (((s + 12) >> 1) & 63);' 'coll_need: wrong slot'
run '        if (v & 0xF00) volpan(EFREG[i]' '        if (v & 0x700) volpan(EFREG[i]' 'final_mix: EFSDL 8 skipped'
run '    coll_claim[ch] = false;
    const uint32_t i1' '    const uint32_t i1' 'fast: stale claim seen by slot_quiet (perf only, expect MISSED)'
echo done
