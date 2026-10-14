#!/usr/bin/env python3
# Plan section 7, bundle 20 (WSL): plants one bug at a time in a copy of a system source (S20_OVL goes before the
# tree); run.sh must FAIL on each. A mutant that does not build is a bad mutant. Args: a text the names hold.
import os, shutil, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
OVL = '/tmp/s20mut'
REG = 'dc/mem/sh4_internal_reg.cpp'
SB = 'dc/mem/sb.cpp'
A0 = 'dc/mem/sh4_area0.cpp'
SCI = 'dc/sh4/sci.cpp'
ELF = 'plugs/ImgReader/elf_loader.cpp'
MEM = 'dc/mem/sh4_mem.cpp'
WIDTH = 'const u32 v = (rs.flags & REG_32BIT_READWRITE) ? *rs.data32 : (rs.flags & REG_16BIT_READWRITE) ? *rs.data16 : *rs.data8;'

MUTANTS = [
    (REG, 'reg read16 keeps 8 bits', 'size == 2 ? (v & 0xFFFF) : (v & 0xFF);', 'size == 2 ? (v & 0xFF) : (v & 0xFF);'),
    (REG, 'reg read through data32 always', WIDTH, 'const u32 v = *rs.data32;'),
    (REG, 'reg byte write to a 32-bit register lands high', ': (*rs.data32 & 0xFFFFFF00u) | (data & 0xFF);',
     ': (*rs.data32 & 0x00FFFFFFu) | (data << 24);'),
    (REG, 'reg byte write to a 16-bit register takes it whole',
     '*rs.data16 = size == 1 ? (u16)((*rs.data16 & 0xFF00) | (data & 0xFF)) : (u16)data;', '*rs.data16 = (u16)data;'),
    (REG, 'reg reads without the row', '\t\t\tif (g_reg_size_preset)\n\t\t\t\treturn reg_read_sized<size>(reg[offset]);\n', ''),
    (REG, 'area 7 stays in P0-P3', 'else if (base == 0xE0 || !g_mem_map_preset)', 'else if (base == 0xE0 || true)'),
    (SB, 'ADSTAR keeps the low 5 bits', 'sb_write_masked<&SB_ADSTAR, 0x1FFFFFE0u>', 'sb_write_masked<&SB_ADSTAR, 0x1FFFFFFFu>'),
    (SB, 'DDSTAR keeps the top 3 bits', 'sb_write_masked<&SB_DDSTAR, 0x1FFFFFE0u>', 'sb_write_masked<&SB_DDSTAR, 0xFFFFFFE0u>'),
    (SB, 'E1STAG not masked', '\t\tsb_set_masked(SB_E1STAG_addr, sb_write_masked<&SB_E1STAG, 0x1FFFFFE0u>);\n', ''),
    (SB, 'PDLEN keeps the low 5 bits', 'sb_write_masked<&SB_PDLEN, 0x00FFFFE0u>', 'sb_write_masked<&SB_PDLEN, 0x00FFFFFFu>'),
    (SB, 'PDLEN masked without its row', '\tif (g_block_write_preset)\n\t\tsb_set_masked(SB_PDLEN_addr',
     '\tif (true)\n\t\tsb_set_masked(SB_PDLEN_addr'),
    (SB, 'TFREM 8 without its row', 'SB_TFREM = g_holly_regs_preset ? 8 : 0;', 'SB_TFREM = 8;'),
    (SB, 'a masked register cannot be read', '\trs.flags = REG_32BIT_READWRITE | REG_READ_DATA;\n\trs.writeFunction = fn;',
     '\trs.flags = REG_32BIT_READWRITE;\n\trs.writeFunction = fn;'),
    (SB, 'SB byte write keeps the high byte', 'sz == 2 ? 0xFFFF0000u : 0xFFFFFF00u;', 'sz == 2 ? 0xFFFF0000u : 0x00FFFFFFu;'),
    (SB, 'SB halfword read keeps 8 bits', 'return sz == 4 ? v : sz == 2 ? (v & 0xFFFF) : (v & 0xFF);',
     'return sz == 4 ? v : sz == 2 ? (v & 0xFF) : (v & 0xFF);'),
    (A0, 'the mirror reads the BIOS', '\tif (no_rom && base <= 0x21)\n\t{\n\t\tarea0_mirror_note(addr, sz, false);',
     '\tif (false)\n\t{\n\t\tarea0_mirror_note(addr, sz, false);'),
    (A0, 'the mirror reads the flash', '\tif (no_rom && base <= 0x21)\n\t{\n\t\tarea0_mirror_note(addr, sz, false);',
     '\tif (no_rom && base <= 0x1F)\n\t{\n\t\tarea0_mirror_note(addr, sz, false);'),
    (A0, 'the mirror writes the flash', '\tif (no_rom && base <= 0x21)\n\t{\n\t\tarea0_mirror_note(addr, sz, true);',
     '\tif (false)\n\t{\n\t\tarea0_mirror_note(addr, sz, true);'),
    (A0, 'the mirror test without the row', 'T FASTCALL ReadMem_area0(u32 addr)\n{\n\tconst bool no_rom = g_mem_map_preset && (addr & 0x02000000);',
     'T FASTCALL ReadMem_area0(u32 addr)\n{\n\tconst bool no_rom = (addr & 0x02000000);'),
    (A0, 'the mirror test on bit 24', 'void FASTCALL WriteMem_area0(u32 addr, T data)\n{\n\tconst bool no_rom = g_mem_map_preset && (addr & 0x02000000);',
     'void FASTCALL WriteMem_area0(u32 addr, T data)\n{\n\tconst bool no_rom = g_mem_map_preset && (addr & 0x01000000);'),
    (A0, 'a third area 0 handler', '\tarea0_handler_0100_01FF = _vmem_register_handler_Template2(\n\t\tReadMem_area0, WriteMem_area0, 0x0100, 0x01FF);\n',
     '\tarea0_handler_0100_01FF = _vmem_register_handler_Template2(\n\t\tReadMem_area0, WriteMem_area0, 0x0100, 0x01FF);\n'
     '\t_vmem_register_handler_Template2(ReadMem_area0, WriteMem_area0, 0x0200, 0x02FF);\n'),
    (SCI, 'SCSSR1 keeps TEND', 'SCI_SCSSR1 = (u8)(data & 0xF9);', 'SCI_SCSSR1 = (u8)(data & 0xFD);'),
    (SCI, 'SCSPTR1 takes every bit', 'SCI_SCSPTR1 = (u8)(data & 0x8F);', 'SCI_SCSPTR1 = (u8)(data & 0xFF);'),
    (SCI, 'SCRDR1 writable', 'sci_reg(SCI_SCRDR1_addr,  &SCI_SCRDR1,  REG_READ_DATA, write_SCRDR1);',
     'sci_reg(SCI_SCRDR1_addr,  &SCI_SCRDR1,  REG_READ_DATA | REG_WRITE_DATA, 0);'),
    (SCI, 'SCSSR1 resets to 0x80', '\tSCI_SCSSR1  = 0x84;', '\tSCI_SCSSR1  = 0x80;'),
    (SCI, 'SCI registers without the row', '\tif (!g_onchip_regs_preset)\n\t\treturn;', '\tif (false)\n\t\treturn;'),
    (ELF, 'segments without the zero tail', '        elf_copy_bytes((vaddr & RAM_MASK) + filesz, NULL, memsz - filesz);\n', ''),
    (ELF, 'words not in the SH4 order', '*(u32*)&ram[dst + i] = elf_wordswap32(w);', '*(u32*)&ram[dst + i] = w;'),
    (ELF, 'head bytes in the host order', 'for (; i < size && ((dst + i) & 3); i++)\n        ram[(dst + i) ^ 3]',
     'for (; i < size && ((dst + i) & 3); i++)\n        ram[(dst + i)]'),
    (ELF, 'NOBITS sections loaded', ' || LE32(sh->sh_type) == SHT_NOBITS', ''),
    (ELF, 'sections outside the segments skipped', '        elf_copy_bytes(addr & RAM_MASK, buf + off, size);\n', ''),
    (ELF, 'AICA levels without the row', '    if (g_elf_loader_preset)\n    {\n        libAICA_WriteMem_aica_reg',
     '    if (true)\n    {\n        libAICA_WriteMem_aica_reg'),
    (ELF, 'SCIEB written as a byte', 'libAICA_WriteMem_aica_reg(0x0070289C, 0x48, 2);', 'libAICA_WriteMem_aica_reg(0x0070289C, 0x48, 1);'),
    (ELF, 'SCILV1 0x05', 'libAICA_WriteMem_aica_reg(0x007028AC, 0x50, 1);', 'libAICA_WriteMem_aica_reg(0x007028AC, 0x05, 1);'),
    (ELF, 'segments not loaded with the row', '    if (g_elf_loader_preset)\n        loaded = elf_load_segments(',
     '    if (false)\n        loaded = elf_load_segments('),
    (MEM, 'block tail without the halfword',
     '\t\tif (size - i >= 2)\n\t\t{\n\t\t\tWriteMem16_nommu(dst + i, block_src16(src, i));',
     '\t\tif (false)\n\t\t{\n\t\t\tWriteMem16_nommu(dst + i, block_src16(src, i));'),
    (MEM, 'block tail byte from the next lane', '\treturn ((const u8*)src)[i];', '\treturn ((const u8*)src)[i ^ 1];'),
    (MEM, 'DMA tail halfword from the target', 'WriteMem16_nommu(dst + i, ReadMem16_nommu(src + i));',
     'WriteMem16_nommu(dst + i, ReadMem16_nommu(dst + i));'),
    (MEM, 'block_ptr byte tail one byte on', 'WriteMem8(addr + i, block_src8(data, i));', 'WriteMem8(addr + i, block_src8(data, i + 1));'),
    (MEM, 'odd sizes without the row in the DMA', '\tif ((size & 3) && g_block_write_preset)\n\t{\n\t\tu32 i = 0;\n\t\tfor (; i + 4 <= size; i += 4)\n\t\t\tWriteMem32_nommu(dst + i, ReadMem32_nommu(src + i));',
     '\tif ((size & 3) && false)\n\t{\n\t\tu32 i = 0;\n\t\tfor (; i + 4 <= size; i += 4)\n\t\t\tWriteMem32_nommu(dst + i, ReadMem32_nommu(src + i));'),
]

def run(ovl):
    env = dict(os.environ)
    env.pop('S20_OVL', None)
    if ovl:
        env['S20_OVL'] = ovl
    try:
        r = subprocess.run(['sh', os.path.join(ROOT, 'misc/sys20_check/run.sh')], capture_output=True, text=True,
                           env=env, timeout=600)
    except subprocess.TimeoutExpired:
        return 1, 'TIMEOUT'
    out = [l for l in (r.stdout + r.stderr).splitlines() if 'FAIL' in l or 'error' in l]
    return r.returncode, out[0] if out else ''

def main():
    todo = [m for m in MUTANTS if len(sys.argv) < 2 or sys.argv[1] in m[1]]
    rc, line = run(None)
    if rc != 0:
        print(f'BASELINE FAILS: {line}')
        sys.exit(2)
    caught = 0
    for path, name, find, repl in todo:
        src = open(os.path.join(ROOT, path), encoding='utf-8', newline='').read().replace('\r\n', '\n')
        if src.count(find) != 1:
            print(f'BAD MUTANT (pattern found {src.count(find)} times): {name}')
            sys.exit(2)
        shutil.rmtree(OVL, ignore_errors=True)
        os.makedirs(os.path.dirname(os.path.join(OVL, path)), exist_ok=True)
        open(os.path.join(OVL, path), 'w', encoding='utf-8', newline='').write(src.replace(find, repl))
        rc, line = run(OVL)
        if rc == 2:
            print(f'BAD MUTANT (does not build): {name}: {line[:100]}')
            sys.exit(2)
        caught += rc != 0
        print(f"{'caught' if rc else 'MISSED'}: {name}: {line[:100]}", flush=True)
    print(f'{caught} of {len(todo)} mutants fail run.sh')
    sys.exit(0 if caught == len(todo) else 1)

main()
