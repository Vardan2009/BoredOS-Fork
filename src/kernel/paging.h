#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>
#include <stdbool.h>

#define PAGE_SIZE 4096

// Page Table Entry Flags
#define PT_PRESENT      (1ull << 0)
#define PT_RW           (1ull << 1)
#define PT_USER         (1ull << 2)
#define PT_WRITE_THROUGH (1ull << 3)
#define PT_CACHE_DISABLE (1ull << 4)
#define PT_ACCESSED     (1ull << 5)
#define PT_DIRTY        (1ull << 6)
#define PT_HUGE         (1ull << 7)
#define PT_GLOBAL       (1ull << 8)
#define PT_NX           (1ull << 63)

#define PT_ADDR_MASK    0x000FFFFFFFFFF000ull

typedef struct {
    uint64_t entries[512];
} __attribute__((aligned(PAGE_SIZE))) page_table_t;

// Get the current PML4 physical address
uint64_t paging_get_pml4_phys(void);

// Map a physical address to a virtual address
void paging_map_page(uint64_t pml4_phys, uint64_t virtual_addr, uint64_t physical_addr, uint64_t flags);

// Create a new, isolated PML4 for a user process (returns physical address)
uint64_t paging_create_user_pml4_phys(void);

// Switch the active page table (takes physical address)
void paging_switch_directory(uint64_t pml4_phys);

// Initialize paging system (if needed beyond Limine's setup)
void paging_init(void);

#endif // PAGING_H
