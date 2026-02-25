#ifndef ELF_H
#define ELF_H

#include <stdint.h>

typedef uint16_t Elf64_Half;
typedef uint32_t Elf64_Word;
typedef int32_t  Elf64_Sword;
typedef uint64_t Elf64_Xword;
typedef int64_t  Elf64_Sxword;
typedef uint64_t Elf64_Addr;
typedef uint64_t Elf64_Off;

#define EI_NIDENT 16

typedef struct {
    unsigned char e_ident[EI_NIDENT]; /* Magic number and other info */
    Elf64_Half    e_type;             /* Object file type */
    Elf64_Half    e_machine;          /* Architecture */
    Elf64_Word    e_version;          /* Object file version */
    Elf64_Addr    e_entry;            /* Entry point virtual address */
    Elf64_Off     e_phoff;            /* Program header table file offset */
    Elf64_Off     e_shoff;            /* Section header table file offset */
    Elf64_Word    e_flags;            /* Processor-specific flags */
    Elf64_Half    e_ehsize;           /* ELF header size in bytes */
    Elf64_Half    e_phentsize;        /* Program header table entry size */
    Elf64_Half    e_phnum;            /* Program header table entry count */
    Elf64_Half    e_shentsize;        /* Section header table entry size */
    Elf64_Half    e_shnum;            /* Section header table entry count */
    Elf64_Half    e_shstrndx;         /* Section header string table index */
} Elf64_Ehdr;

typedef struct {
    Elf64_Word    p_type;             /* Segment type */
    Elf64_Word    p_flags;            /* Segment flags */
    Elf64_Off     p_offset;           /* Segment file offset */
    Elf64_Addr    p_vaddr;            /* Segment virtual address */
    Elf64_Addr    p_paddr;            /* Segment physical address */
    Elf64_Xword   p_filesz;           /* Segment size in file */
    Elf64_Xword   p_memsz;            /* Segment size in memory */
    Elf64_Xword   p_align;            /* Segment alignment */
} Elf64_Phdr;

/* e_ident constants */
#define ELFMAG0 0x7f
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'
#define ELFCLASS64 2
#define ELFDATA2LSB 1
#define EV_CURRENT 1

/* e_type constants */
#define ET_EXEC 2
#define ET_DYN  3

/* e_machine constants */
#define EM_X86_64 62

/* p_type constants */
#define PT_LOAD 1

/* p_flags constants */
#define PF_X 1
#define PF_W 2
#define PF_R 4

#include <stdbool.h>

// Loads the ELF executable at 'path' using fat32 into the pagemap given by user_pml4.
// Returns entry point address on success, or 0 on failure.
uint64_t elf_load(const char *path, uint64_t user_pml4);

#endif
