#!/usr/bin/env python3
# Plan section 7, bundle 20 (P46): the opcode table of dc/sh4/sh4_opcode_list.cpp as {handler, mask, rez} rows, and
# shil.cpp's mac.l / mac.w saturation, for interp_check. Args: source root, output folder.
import re, sys
root, out = sys.argv[1], sys.argv[2]

def read(p):
    return open('%s/%s' % (root, p), 'rb').read().decode('utf-8').replace('\r\n', '\n')

src = read('dc/sh4/sh4_opcode_list.cpp')
body = src[src.index('sh4_opcodelistentry opcodes[]='):]
body = body[:body.index('//end of list')]
rows = re.findall(r'\{\s*[^,{}]*,\s*(\w+)\s*,\s*(Mask_\w+)\s*,\s*(0x[0-9A-Fa-f]+)\s*,', body)
if len(rows) < 200:
    sys.exit('FAIL gen: %d opcode rows' % len(rows))
masks = dict(re.findall(r'#define (Mask_\w+) (0x[0-9A-Fa-f]+)', src))
names = sorted(set(r[0] for r in rows))
with open('%s/ops_table.inc' % out, 'w') as f:
    for n in names:
        f.write('extern sh4op(%s);\n' % n)
    f.write('static const OpRow k_ops[] = {\n')
    for h, m, z in rows:
        f.write('\t{ %s, %s, %s, "%s" },\n' % (h, masks[m], z, h))
    f.write('};\n')

shil = read('dc/sh4/rec_v2/shil.cpp')
a = shil.index('extern "C" void sh4_mac_l(u32 a, u32 b, u32 oldl, u32 oldh, u32 srs)\n')
b = shil.index('// C call-out census', a)
open('%s/mac_ref.inc' % out, 'w').write(shil[a:b])
print('gen: %d opcode rows, %d handlers' % (len(rows), len(names)))
