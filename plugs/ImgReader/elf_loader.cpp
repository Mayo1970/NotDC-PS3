// SH4 ELF loader, based on the nullDC 360 port's Elf.cpp (ZeZu, 2004/2005), for a big-endian host:
// the little-endian SH4 sections go straight into mem_b with the word swap below.

#include "plugs/ImgReader/elf_loader.h"
#include "dc/sh4/sh4_registers.h"   // Sh4cntx, next_pc, r[], sr macros
#include "dc/mem/sh4_mem.h"          // mem_b, RAM_MASK
#include "plugs/ImgReader/Elf.h"     // Elf32_Ehdr, Elf32_Phdr, Elf32_Shdr, constants
#include "plugins/plugin_manager.h"  // libAICA_WriteMem_aica_reg
#include "ps3/ps3_presets.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// _vmem_readt<u16> on a big-endian host reads DC address A from offset A^2, so file bytes [a,b,c,d] must sit in
// mem_b as [d,c,b,a]: a bswap32 of the BE word (checked against a hardware mem_b dump).
static inline u32 elf_wordswap32(u32 v) { return __builtin_bswap32(v); }


// File bytes -> mem_b at DC RAM offset dst_phys, bswap32 per word for _vmem_readt<u16> (addr ^= 2). Direct
// writes, no _vmem calls, to avoid side effects at load time.
static void elf_copy_to_ram(u32 dst_phys, const u8* src, u32 size)
{
    u32* dst32 = (u32*)(mem_b.data + dst_phys);
    const u32* src32 = (const u32*)src;
    u32 words = size >> 2;

    for (u32 w = 0; w < words; w++)
        dst32[w] = elf_wordswap32(src32[w]);

    // Handle trailing 1-3 bytes (pad to 32 bits, bswap, write)
    u32 rem = size & 3;
    if (rem) {
        u32 last = 0;
        memcpy(&last, src + (size & ~3u), rem);
        dst32[words] = elf_wordswap32(last);
    }
}

// ELF Loader SEGMENTS: byte-exact at any start, SH4 byte A at mem_b A ^ 3, words in the middle; src NULL writes zeros.
static void elf_copy_bytes(u32 dst, const u8* src, u32 size)
{
    u8* ram = mem_b.data;
    u32 i = 0;
    for (; i < size && ((dst + i) & 3); i++)
        ram[(dst + i) ^ 3] = src ? src[i] : 0;
    for (; i + 4 <= size; i += 4)
    {
        u32 w = 0;
        if (src)
            memcpy(&w, src + i, 4);
        *(u32*)&ram[dst + i] = elf_wordswap32(w);
    }
    for (; i < size; i++)
        ram[(dst + i) ^ 3] = src ? src[i] : 0;
}

static bool elf_in_ram(u32 addr, u32 size)
{
    return (addr & 0x0F000000) == 0x0C000000 && (addr & RAM_MASK) + size <= mem_b.size;
}

// ELF Loader SEGMENTS (P33, Flycast dev 643374c91): the PT_LOAD segments with their zero-filled tails, then each ALLOC
// section no segment holds (objcopy --add-section). OLD loads PROGBITS sections only: no .init_array, no tails.
static int elf_load_segments(const u8* buf, u32 flen, const Elf32_Phdr* ph0, u16 phnum, u16 phsize, const u8* sh0,
                             u16 shnum, u16 shsize)
{
    #define LE32(x) __builtin_bswap32((u32)(x))
    int loaded = 0;
    for (u16 i = 0; i < phnum; i++)
    {
        const Elf32_Phdr* ph = (const Elf32_Phdr*)((const u8*)ph0 + (u32)phsize * i);
        const u32 vaddr = LE32(ph->p_vaddr), off = LE32(ph->p_offset);
        const u32 filesz = LE32(ph->p_filesz), memsz = LE32(ph->p_memsz);
        if (LE32(ph->p_type) != PT_LOAD || memsz == 0)
            continue;
        if (memsz < filesz || off + filesz > flen || !elf_in_ram(vaddr, memsz))
        {
            printf("[ELF] Seg %u: 0x%08X file=0x%X mem=0x%X not loadable, skipped\n", i, vaddr, filesz, memsz);
            continue;
        }
        printf("[ELF] Seg %u: 0x%08X  file=0x%X  mem=0x%X\n", i, vaddr, filesz, memsz);
        elf_copy_bytes(vaddr & RAM_MASK, buf + off, filesz);
        elf_copy_bytes((vaddr & RAM_MASK) + filesz, NULL, memsz - filesz);
        loaded++;
    }
    for (u16 i = 0; i < shnum; i++)
    {
        const Elf32_Shdr* sh = (const Elf32_Shdr*)(sh0 + (u32)shsize * i);
        const u32 addr = LE32(sh->sh_addr), size = LE32(sh->sh_size), off = LE32(sh->sh_offset);
        if (!(LE32(sh->sh_flags) & SHF_ALLOC) || LE32(sh->sh_type) == SHT_NOBITS || addr == 0 || size == 0)
            continue;
        bool held = false;
        for (u16 j = 0; j < phnum && !held; j++)
        {
            const Elf32_Phdr* ph = (const Elf32_Phdr*)((const u8*)ph0 + (u32)phsize * j);
            const u32 start = LE32(ph->p_vaddr);
            held = LE32(ph->p_type) == PT_LOAD && addr >= start && (u64)addr + size <= (u64)start + LE32(ph->p_filesz);
        }
        if (held)
            continue;
        if (off + size > flen || !elf_in_ram(addr, size))
        {
            printf("[ELF] Section %u: 0x%08X (%u bytes) not loadable, skipped\n", i, addr, size);
            continue;
        }
        printf("[ELF] Section %u: 0x%08X <- file+0x%X  (%u bytes), outside the segments\n", i, addr, off, size);
        elf_copy_bytes(addr & RAM_MASK, buf + off, size);
        loaded++;
    }
    #undef LE32
    return loaded;
}
// ---- elf_load: parse the ELF, copy the PT_LOAD / SHT_PROGBITS sections into mem_b ----
bool elf_load(const char* path, u32* out_entry)
{
    printf("[ELF] Loading %s\n", path);

    FILE* f = fopen(path, "rb");
    if (!f) { printf("[ELF] Cannot open file\n"); return false; }

    // Get file size
    fseek(f, 0, SEEK_END);
    u32 flen = (u32)ftell(f);
    fseek(f, 0, SEEK_SET);

    if (flen < sizeof(Elf32_Ehdr) || flen > 0x800000) // 8 MB max
    {
        printf("[ELF] File size %u out of range\n", flen);
        fclose(f); return false;
    }

    // Read entire file into a heap buffer (same approach as original Elf.cpp)
    u8* buf = (u8*)malloc(flen);
    if (!buf) { printf("[ELF] Out of memory\n"); fclose(f); return false; }

    if (fread(buf, 1, flen, f) != flen)
    {
        printf("[ELF] Read error\n");
        free(buf); fclose(f); return false;
    }
    fclose(f);

    Elf32_Ehdr* eh = (Elf32_Ehdr*)buf;

    // --- Validate header (byte-by-byte magic, no endian issues) ---
    if (eh->e_ident[0] != 0x7F || eh->e_ident[1] != 'E' ||
        eh->e_ident[2] != 'L'  || eh->e_ident[3] != 'F')
    {
        printf("[ELF] Bad magic\n"); free(buf); return false;
    }
    if (eh->e_ident[EI_CLASS] != ELFCLASS32)
    {
        printf("[ELF] Not 32-bit ELF\n"); free(buf); return false;
    }
    if (eh->e_ident[EI_DATA] != ELFDATA2LSB)
    {
        printf("[ELF] Not little-endian ELF\n"); free(buf); return false;
    }

    // ELF fields are little-endian; swap them for the big-endian host
    #define LE16(x) (u16)(((u16)(x) >> 8) | ((u16)(x) << 8))
    #define LE32(x) (u32)(((u32)(x) >> 24) | (((u32)(x) >> 8) & 0xFF00u) | \
                          (((u32)(x) << 8) & 0xFF0000u) | ((u32)(x) << 24))

    u16 e_machine  = LE16(eh->e_machine);
    u16 e_type     = LE16(eh->e_type);
    u32 e_entry    = LE32(eh->e_entry);
    u32 e_phoff    = LE32(eh->e_phoff);
    u16 e_phnum    = LE16(eh->e_phnum);
    u16 e_phentsize= LE16(eh->e_phentsize);
    u32 e_shoff    = LE32(eh->e_shoff);
    u16 e_shnum    = LE16(eh->e_shnum);
    u16 e_shentsize= LE16(eh->e_shentsize);

    if (e_machine != EM_SH)
    {
        printf("[ELF] Wrong arch: %u (expected %u for SH4)\n", e_machine, EM_SH);
        free(buf); return false;
    }
    if (e_type != ET_EXEC)
    {
        printf("[ELF] Not an executable (type=%u)\n", e_type);
        free(buf); return false;
    }

    printf("[ELF] Entry: 0x%08X  Segments: %u  Sections: %u\n",
           e_entry, e_phnum, e_shnum);

    // Sanity check offsets
    if (flen < e_shoff + (u32)e_shentsize * e_shnum ||
        flen < e_phoff + (u32)e_phentsize * e_phnum)
    {
        printf("[ELF] Header offsets exceed file size\n");
        free(buf); return false;
    }

    int loaded = 0;

    if (g_elf_loader_preset)
        loaded = elf_load_segments(buf, flen, (const Elf32_Phdr*)(buf + e_phoff), e_phnum, e_phentsize, buf + e_shoff,
                                   e_shoff ? e_shnum : 0, e_shentsize);
    // Pass 1: section headers (SHT_PROGBITS + SHF_ALLOC), as the original Elf.cpp: each allocatable section
    // lands at its exact sh_addr.
    else if (e_shnum > 0 && e_shoff > 0)
    {
        for (u16 i = 0; i < e_shnum; i++)
        {
            Elf32_Shdr* sh = (Elf32_Shdr*)(buf + e_shoff + (u32)e_shentsize * i);

            u32 sh_type   = LE32(sh->sh_type);
            u32 sh_flags  = LE32(sh->sh_flags);
            u32 sh_addr   = LE32(sh->sh_addr);
            u32 sh_offset = LE32(sh->sh_offset);
            u32 sh_size   = LE32(sh->sh_size);

            if (sh_type != SHT_PROGBITS) continue;
            if (!(sh_flags & SHF_ALLOC))  continue;
            if (sh_size == 0)              continue;

            // Must land in DC RAM (physical 0x0C000000)
            if ((sh_addr & 0x0F000000) != 0x0C000000)
            {
                printf("[ELF] Section %u: addr 0x%08X not in DC RAM, skipping\n",
                       i, sh_addr);
                continue;
            }

            u32 dst = sh_addr & RAM_MASK;
            if (dst + sh_size > mem_b.size)
            {
                printf("[ELF] Section %u: overflows RAM\n", i);
                continue;
            }
            if (sh_offset + sh_size > flen)
            {
                printf("[ELF] Section %u: overflows file\n", i);
                continue;
            }

            printf("[ELF] Section %u: 0x%08X <- file+0x%X  (%u bytes)\n",
                   i, sh_addr, sh_offset, sh_size);

            elf_copy_to_ram(dst & RAM_MASK, buf + sh_offset, sh_size);

            loaded++;
        }
    }

    // Pass 2: program headers (PT_LOAD) when no section was loaded (stripped ELFs may have no section table)
    if (!g_elf_loader_preset && loaded == 0 && e_phnum > 0)
    {
        printf("[ELF] No sections loaded, trying program headers\n");
        for (u16 i = 0; i < e_phnum; i++)
        {
            Elf32_Phdr* ph = (Elf32_Phdr*)(buf + e_phoff + (u32)e_phentsize * i);

            u32 p_type   = LE32(ph->p_type);
            u32 p_offset = LE32(ph->p_offset);
            u32 p_vaddr  = LE32(ph->p_vaddr);
            u32 p_filesz = LE32(ph->p_filesz);
            u32 p_memsz  = LE32(ph->p_memsz);

            if (p_type != PT_LOAD) continue;
            if (p_filesz == 0 && p_memsz == 0) continue;
            if ((p_vaddr & 0x0F000000) != 0x0C000000)
            {
                printf("[ELF] Seg %u: vaddr 0x%08X not in DC RAM, skipping\n",
                       i, p_vaddr);
                continue;
            }

            u32 dst = p_vaddr & RAM_MASK;
            if (dst + p_memsz > mem_b.size) { printf("[ELF] Seg %u overflows RAM\n", i); continue; }

            printf("[ELF] Seg %u: 0x%08X  file=0x%X  mem=0x%X\n",
                   i, p_vaddr, p_filesz, p_memsz);

            elf_copy_to_ram(dst & RAM_MASK, buf + p_offset, p_filesz);
            // Zero BSS — no swap needed, all zeros
            if (p_memsz > p_filesz)
                memset(mem_b.data + (dst & RAM_MASK) + p_filesz, 0, p_memsz - p_filesz);

            loaded++;
        }
    }

    #undef LE16
    #undef LE32

    free(buf);

    if (loaded == 0)
    {
        printf("[ELF] Nothing was loaded\n");
        return false;
    }

    *out_entry = e_entry;
    printf("[ELF] Load complete (%d section(s))  entry=0x%08X\n", loaded, e_entry);

    // Diagnostic: dump first 16 bytes at entry point as stored in mem_b
    // This lets us verify the transform by comparing with what the dynarec sees
    {
        u32 entry_phys = e_entry & RAM_MASK;
        u8* p = mem_b.data + entry_phys;
        printf("[ELF] mem_b bytes at entry: %02X %02X %02X %02X  %02X %02X %02X %02X  %02X %02X %02X %02X  %02X %02X %02X %02X\n",
               p[0],p[1],p[2],p[3], p[4],p[5],p[6],p[7],
               p[8],p[9],p[10],p[11], p[12],p[13],p[14],p[15]);
        // Also show what _vmem_ReadMem16 would return for the first 4 opcodes
        // addr^=2 means: u16 at addr A reads from A^2
        printf("[ELF] Opcodes via addr^2: [0]=%04X [2]=%04X [4]=%04X [6]=%04X\n",
               *(u16*)(p+2), *(u16*)(p+0), *(u16*)(p+6), *(u16*)(p+4));
    }

    return true;
}

// ---- elf_apply_entry: poke the entry point into the SH4 context after the CPU reset ----
void elf_apply_entry(u32 entry)
{
    next_pc = entry;
    r[15]   = 0x8CFFF000u;  // stack pointer: top of DC RAM
    sr.SetFull(0x00000000u); // user mode, interrupts enabled

    printf("[ELF] SH4 context patched: PC=0x%08X  SP=0x%08X\n", next_pc, r[15]);

    // ELF Loader SEGMENTS (P68, Flycast 333b21fac): the AICA interrupt masks and levels the BIOS would leave, through
    // the plugin, so nullAICA and SPE ARM7 both get them.
    if (g_elf_loader_preset)
    {
        libAICA_WriteMem_aica_reg(0x0070289C, 0x48, 2);   // SCIEB
        libAICA_WriteMem_aica_reg(0x007028A8, 0x18, 1);   // SCILV0
        libAICA_WriteMem_aica_reg(0x007028AC, 0x50, 1);   // SCILV1
        libAICA_WriteMem_aica_reg(0x007028B0, 0x08, 1);   // SCILV2
        printf("[ELF] AICA SCIEB=0x48 SCILV0=0x18 SCILV1=0x50 SCILV2=0x08\n");
    }
}
