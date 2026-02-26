#include "elf.h"
#include "fat32.h"
#include "memory_manager.h"
#include "paging.h"
#include "platform.h"

extern void serial_print(const char *s);
extern void serial_write(const char *str);
static void print_hex(uint64_t n) {
    char buf[17];
    char* x = "0123456789ABCDEF";
    buf[16] = 0;
    for (int i=15; i>=0; i--) { buf[i] = x[n & 0xF]; n >>= 4; }
    serial_write(buf);
}

uint64_t elf_load(const char *path, uint64_t user_pml4) {
    FAT32_FileHandle *file = fat32_open(path, "r");
    if (!file || !file->valid) {
        serial_write("[ELF] Error: Failed to open file ");
        serial_write(path);
        serial_write("\n");
        return 0;
    }

    // Read the ELF Header
    Elf64_Ehdr ehdr;
    if (fat32_read(file, &ehdr, sizeof(Elf64_Ehdr)) != sizeof(Elf64_Ehdr)) {
        serial_write("[ELF] Error: Could not read ELF Header\n");
        fat32_close(file);
        return 0;
    }

    // Validate Magic Number & Properties
    if (ehdr.e_ident[0] != ELFMAG0 || ehdr.e_ident[1] != ELFMAG1 || 
        ehdr.e_ident[2] != ELFMAG2 || ehdr.e_ident[3] != ELFMAG3) {
        serial_write("[ELF] Error: Invalid ELF Magic Number\n");
        fat32_close(file);
        return 0;
    }
    if (ehdr.e_ident[4] != ELFCLASS64) {
        serial_write("[ELF] Error: Not a 64-bit ELF\n");
        fat32_close(file);
        return 0;
    }
    if (ehdr.e_ident[5] != ELFDATA2LSB) {
        serial_write("[ELF] Error: Not Little Endian\n");
        fat32_close(file);
        return 0;
    }
    if (ehdr.e_type != ET_EXEC && ehdr.e_type != ET_DYN) {
        serial_write("[ELF] Error: Not an Executable\n");
        fat32_close(file);
        return 0;
    }
    if (ehdr.e_machine != EM_X86_64) {
        serial_write("[ELF] Error: Not x86_64 Architecture\n");
        fat32_close(file);
        return 0;
    }

    // Iterate Over Program Headers
    for (int i = 0; i < ehdr.e_phnum; i++) {
        fat32_seek(file, ehdr.e_phoff + (i * ehdr.e_phentsize), 0);
        Elf64_Phdr phdr;
        if (fat32_read(file, &phdr, sizeof(Elf64_Phdr)) != sizeof(Elf64_Phdr)) {
            serial_write("[ELF] Error: Failed to read Program Header\n");
            continue;
        }

        // Only load segments with type PT_LOAD
        if (phdr.p_type == PT_LOAD) {
            uint64_t p_vaddr = phdr.p_vaddr;
            uint64_t p_memsz = phdr.p_memsz;
            uint64_t p_filesz = phdr.p_filesz;
            uint64_t p_offset = phdr.p_offset;
            
            if (p_memsz == 0) continue;

            // Calculate page-aligned boundaries
            uint64_t align_offset = p_vaddr & 0xFFF;
            uint64_t start_page = p_vaddr & ~0xFFF;
            uint64_t num_pages = (p_memsz + align_offset + 0xFFF) / 4096;

            // Allocate and Map Pages
            for (uint64_t p = 0; p < num_pages; p++) {
                uint64_t vaddr = start_page + (p * 4096);
                void* phys = kmalloc_aligned(4096, 4096);
                if (!phys) {
                    serial_write("[ELF] Error: Out of memory mapping PT_LOAD\n");
                    fat32_close(file);
                    return 0;
                }
                
                // Map page to user space (Present, RW, User)
                paging_map_page(user_pml4, vaddr, v2p((uint64_t)phys), 0x07);

                // Zero out the entire page (handles BSS and padding)
                uint8_t* dest = (uint8_t*)phys;
                for (int j=0; j<4096; j++) dest[j] = 0;

                // Copy data from file if available for this page
                uint64_t page_vaddr_start = vaddr;
                uint64_t page_vaddr_end = vaddr + 4096;

                // What part of the segment (p_vaddr to p_vaddr + p_filesz) overlaps this page?
                uint64_t overlap_vaddr_start = p_vaddr;
                if (page_vaddr_start > overlap_vaddr_start) overlap_vaddr_start = page_vaddr_start;
                
                uint64_t overlap_vaddr_end = p_vaddr + p_filesz;
                if (page_vaddr_end < overlap_vaddr_end) overlap_vaddr_end = page_vaddr_end;

                if (overlap_vaddr_start < overlap_vaddr_end) {
                    uint64_t copy_size = overlap_vaddr_end - overlap_vaddr_start;
                    uint64_t dest_offset = overlap_vaddr_start - page_vaddr_start;
                    uint64_t file_offset = p_offset + (overlap_vaddr_start - p_vaddr);

                    fat32_seek(file, file_offset, 0);
                    fat32_read(file, dest + dest_offset, (uint32_t)copy_size);
                }
            }
        }
    }

    fat32_close(file);
    return ehdr.e_entry;
}
