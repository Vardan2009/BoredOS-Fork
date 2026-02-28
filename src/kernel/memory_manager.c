// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include "memory_manager.h"
#include <stdint.h>
#include "limine.h"
#include "platform.h"

// --- Internal State ---
// memory_pool is no longer a single pointer, as we now manage multiple regions.
// The block_list will contain all information about free and allocated regions.
static size_t memory_pool_size = 0;
static MemBlock block_list[MAX_ALLOCATIONS];
static int block_count = 0;
static size_t total_allocated = 0;
static size_t peak_allocated = 0;
static uint32_t allocation_counter = 0;
static bool initialized = false;

extern void serial_write(const char *str);
extern void serial_write_num(uint32_t n);

// --- Helper Functions ---

// Simple memset for internal use
void mem_memset(void *dest, int val, size_t len) {
    uint8_t *ptr = (uint8_t *)dest;
    while (len-- > 0) {
        *ptr++ = (uint8_t)val;
    }
}

void mem_memcpy(void *dest, const void *src, size_t len) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    while (len-- > 0) {
        *d++ = *s++;
    }
}

// Simple memmove
static void mem_memmove(void *dest, const void *src, size_t len) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    
    if (d < s) {
        while (len--) *d++ = *s++;
    } else {
        d += len;
        s += len;
        while (len--) *(--d) = *(--s);
    }
}

// Get current time in ticks (simple counter)
static uint32_t get_timestamp(void) {
    static uint32_t tick = 0;
    return tick++;
}

// Sorts the block list by address. This is crucial for efficient merging of free blocks.
static void sort_block_list() {
    for (int i = 0; i < block_count - 1; i++) {
        for (int j = i + 1; j < block_count; j++) {
            if ((uintptr_t)block_list[i].address > (uintptr_t)block_list[j].address) {
                MemBlock tmp = block_list[i];
                block_list[i] = block_list[j];
                block_list[j] = tmp;
            }
        }
    }
}

// Calculate fragmentation
static size_t calculate_fragmentation(void) {
    size_t total_free = memory_pool_size - total_allocated;
    if (total_free == 0) return 0;

    size_t largest_free = 0;
    for (int i = 0; i < block_count; i++) {
        if (!block_list[i].allocated && block_list[i].size > largest_free) {
            largest_free = block_list[i].size;
        }
    }
    
    if (total_allocated == 0) return 0;
    
    // Fragmentation = 1 - (Largest Free / Total Free)
    size_t frag_percent = 100 - ((largest_free * 100) / total_free);
    return frag_percent;
}

// --- Public API ---

void memory_manager_init_from_memmap(struct limine_memmap_response *memmap) {
    if (initialized || !memmap) return;
    
    // Clear metadata
    mem_memset(block_list, 0, sizeof(block_list));
    block_count = 0;
    total_allocated = 0;
    peak_allocated = 0;
    allocation_counter = 0;
    memory_pool_size = 0;

    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];

        if (entry->type == LIMINE_MEMMAP_USABLE) {
            uint64_t base = entry->base;
            uint64_t size = entry->length;

            // Avoid low memory below 1MB which is used for boot/kernel structures
            if (base < 0x100000) {
                if (base + size <= 0x100000) {
                    continue; // Skip this low memory block entirely
                }
                uint64_t diff = 0x100000 - base;
                base = 0x100000;
                size -= diff;
            }

            if (size < 4096) continue; // Ignore small fragments

            if (block_count >= MAX_ALLOCATIONS) {
                serial_write("[MEM] WARN: Exceeded MAX_ALLOCATIONS while parsing memmap.\n");
                break;
            }

            block_list[block_count].address = (void*)p2v(base);
            block_list[block_count].size = size;
            block_list[block_count].allocated = false;
            block_list[block_count].allocation_id = 0;
            block_count++;

            memory_pool_size += size;
        }
    }

    sort_block_list();
    initialized = true;
    serial_write("[MEM] Total usable memory: ");
    serial_write_num(memory_pool_size / 1024 / 1024);
    serial_write(" MB\n");
}

void* kmalloc_aligned(size_t size, size_t alignment) {
    if (!initialized || size == 0) return NULL;
    
    uint64_t rflags;
    asm volatile("pushfq; pop %0; cli" : "=r"(rflags));

    if (alignment == 0) alignment = 8;
    size = (size + 7) & ~7ULL; // Ensure size is multiple of 8

    for (int i = 0; i < block_count; i++) {
        if (block_list[i].allocated) continue;

        uintptr_t block_start = (uintptr_t)block_list[i].address;
        size_t block_size = block_list[i].size;

        uintptr_t aligned_addr = block_start;
        if (aligned_addr % alignment != 0) {
            aligned_addr = (aligned_addr + alignment - 1) & ~(alignment - 1);
        }

        size_t padding = aligned_addr - block_start;

        if (block_size >= size + padding) {
            void* ptr = (void*)aligned_addr;
            size_t remaining_size = block_size - (size + padding);

            // The original free block becomes the trailing free part.
            block_list[i].address = (void*)(aligned_addr + size);
            block_list[i].size = remaining_size;
            if (remaining_size == 0) {
                for (int j = i; j < block_count - 1; j++) block_list[j] = block_list[j+1];
                block_count--;
            }

            // Create a new block for the allocation.
            if (block_count >= MAX_ALLOCATIONS) continue;
            block_list[block_count].address = ptr;
            block_list[block_count].size = size;
            block_list[block_count].allocated = true;
            block_list[block_count].allocation_id = ++allocation_counter;
            block_list[block_count].timestamp = get_timestamp();
            block_count++;

            // Create a new block for the leading padding if it exists.
            if (padding > 0) {
                if (block_count < MAX_ALLOCATIONS) {
                    block_list[block_count].address = (void*)block_start;
                    block_list[block_count].size = padding;
                    block_list[block_count].allocated = false;
                    block_count++;
                }
            }
            
            sort_block_list();

            total_allocated += size;
            if (total_allocated > peak_allocated) peak_allocated = total_allocated;

            mem_memset(ptr, 0, size);
            asm volatile("push %0; popfq" : : "r"(rflags));
            return ptr;
        }
    }

    asm volatile("push %0; popfq" : : "r"(rflags));
    return NULL;
}

void* kmalloc(size_t size) {
    return kmalloc_aligned(size, 8);
}

void kfree(void *ptr) {
    if (ptr == NULL || !initialized) return;
    
    uint64_t rflags;
    asm volatile("pushfq; pop %0; cli" : "=r"(rflags));

    int block_idx = -1;
    for (int i = 0; i < block_count; i++) {
        if (block_list[i].allocated && block_list[i].address == ptr) {
            block_idx = i;
            break;
        }
    }

    if (block_idx == -1) {
        asm volatile("push %0; popfq" : : "r"(rflags));
        return;
    }

    total_allocated -= block_list[block_idx].size;
    block_list[block_idx].allocated = false;

    // Merge with next block if it's free and physically adjacent
    if (block_idx + 1 < block_count && !block_list[block_idx + 1].allocated) {
        uintptr_t current_end = (uintptr_t)block_list[block_idx].address + block_list[block_idx].size;
        uintptr_t next_start = (uintptr_t)block_list[block_idx + 1].address;
        if (current_end == next_start) {
            block_list[block_idx].size += block_list[block_idx + 1].size;
            for (int i = block_idx + 1; i < block_count - 1; i++) block_list[i] = block_list[i + 1];
            block_count--;
        }
    }

    // Merge with previous block if it's free and physically adjacent
    if (block_idx > 0 && !block_list[block_idx - 1].allocated) {
        uintptr_t prev_end = (uintptr_t)block_list[block_idx - 1].address + block_list[block_idx - 1].size;
        uintptr_t current_start = (uintptr_t)block_list[block_idx].address;
        if (prev_end == current_start) {
            block_list[block_idx - 1].size += block_list[block_idx].size;
            for (int i = block_idx; i < block_count - 1; i++) block_list[i] = block_list[i + 1];
            block_count--;
            asm volatile("push %0; popfq" : : "r"(rflags));
            return;
        }
    }

    asm volatile("push %0; popfq" : : "r"(rflags));
}

void* krealloc(void *ptr, size_t new_size) {
    if (new_size == 0) {
        kfree(ptr);
        return NULL;
    }
    
    if (ptr == NULL) {
        return kmalloc(new_size);
    }
    
    for (int i = 0; i < block_count; i++) {
        if (block_list[i].allocated && block_list[i].address == ptr) {
            if (block_list[i].size >= new_size) {
                return ptr;
            }
            
            void *new_ptr = kmalloc(new_size);
            if (new_ptr == NULL) {
                return NULL;
            }
            
            mem_memmove(new_ptr, ptr, block_list[i].size);
            kfree(ptr);
            
            return new_ptr;
        }
    }
    
    return NULL;
}

MemStats memory_get_stats(void) {
    MemStats stats;
    
    stats.total_memory = memory_pool_size;
    stats.used_memory = total_allocated;
    stats.available_memory = memory_pool_size - total_allocated;
    stats.allocated_blocks = 0;
    stats.free_blocks = 0;
    stats.largest_free_block = 0;
    stats.smallest_free_block = memory_pool_size;
    stats.peak_memory_used = peak_allocated;
    
    // Count and analyze blocks
    for (int i = 0; i < block_count; i++) {
        if (block_list[i].allocated) {
            stats.allocated_blocks++;
        } else {
            stats.free_blocks++;
            if (block_list[i].size > stats.largest_free_block) {
                stats.largest_free_block = block_list[i].size;
            }
            if (block_list[i].size < stats.smallest_free_block) {
                stats.smallest_free_block = block_list[i].size;
            }
        }
    }
    
    if (stats.free_blocks == 0) {
        stats.smallest_free_block = 0;
    }
    
    stats.fragmentation_percent = calculate_fragmentation();
    
    return stats;
}

void memory_print_stats(void) {
    MemStats stats = memory_get_stats();
    
    // Use CLI write functions - declare as extern
    extern void cmd_write(const char *str);
    extern void cmd_write_int(int n);
    extern void cmd_putchar(char c);
    
    cmd_write("\n=== MEMORY STATISTICS ===\n");
    cmd_write("Total Memory:     ");
    cmd_write_int(stats.total_memory / 1024);
    cmd_write(" KB\n");
    
    cmd_write("Used Memory:      ");
    cmd_write_int(stats.used_memory / 1024);
    cmd_write(" KB\n");
    
    cmd_write("Available Memory: ");
    cmd_write_int(stats.available_memory / 1024);
    cmd_write(" KB\n");
    
    cmd_write("Allocated Blocks: ");
    cmd_write_int(stats.allocated_blocks);
    cmd_write("\n");
    
    cmd_write("Free Blocks:      ");
    cmd_write_int(stats.free_blocks);
    cmd_write("\n");
    
    cmd_write("Largest Free:     ");
    cmd_write_int(stats.largest_free_block / 1024);
    cmd_write(" KB\n");
    
    cmd_write("Peak Usage:       ");
    cmd_write_int(stats.peak_memory_used / 1024);
    cmd_write(" KB\n");
    
    cmd_write("Fragmentation:    ");
    cmd_write_int(stats.fragmentation_percent);
    cmd_write("%\n");
    
    cmd_write("Usage:            ");
    int usage_percent = (stats.used_memory * 100) / stats.total_memory;
    cmd_write_int(usage_percent);
    cmd_write("%\n");
    
    cmd_write("========================\n\n");
}

void memory_print_detailed(void) {
    extern void cmd_write(const char *str);
    extern void cmd_write_int(int n);
    extern void cmd_putchar(char c);
    
    cmd_write("\n=== DETAILED MEMORY BLOCKS ===\n");
    cmd_write("ID       Address   Size        Status\n");
    cmd_write("------   --------  --------    --------\n");
    
    for (int i = 0; i < block_count; i++) {
        if (block_list[i].size == 0) continue;
        
        // ID
        cmd_write_int(block_list[i].allocation_id);
        cmd_write("       ");
        
        // Address (simplified hex output)
        cmd_write("0x");
        cmd_write_int((uintptr_t)block_list[i].address / 1024);
        cmd_write("  ");
        
        // Size
        cmd_write_int(block_list[i].size / 1024);
        cmd_write("KB      ");
        
        // Status
        if (block_list[i].allocated) {
            cmd_write("ALLOC\n");
        } else {
            cmd_write("FREE\n");
        }
    }
    
    cmd_write("==============================\n\n");
}

void memory_validate(void) {
    extern void cmd_write(const char *str);
    extern void cmd_write_int(int n);
    
    int errors = 0;
    
    // Check for overlapping blocks
    for (int i = 0; i < block_count; i++) {
        for (int j = i + 1; j < block_count; j++) {
            void *i_start = block_list[i].address;
            void *i_end = (uint8_t *)i_start + block_list[i].size;
            void *j_start = block_list[j].address;
            void *j_end = (uint8_t *)j_start + block_list[j].size;
            
            if (i_start < j_end && i_end > j_start) {
                errors++;
                cmd_write("ERROR: Overlapping blocks detected!\n");
            }
        }
    }
    
    if (errors == 0) {
        cmd_write("Memory validation: OK\n");
    } else {
        cmd_write("Memory validation failed with ");
        cmd_write_int(errors);
        cmd_write(" errors\n");
    }
}

void memory_dump_blocks(void) {
    extern void cmd_write(const char *str);
    extern void cmd_write_int(int n);
    
    cmd_write("\nMemory block dump:\n");
    cmd_write("Total blocks: ");
    cmd_write_int(block_count);
    cmd_write("\n");
    
    memory_print_detailed();
}

size_t memory_get_peak_usage(void) {
    return peak_allocated;
}

void memory_reset_peak(void) {
    peak_allocated = total_allocated;
}

bool memory_is_valid_ptr(void *ptr) {
    if (ptr == NULL) return false;
    
    if (!initialized) {
        return false;
    }
    
    // Check if it's an allocated block
    for (int i = 0; i < block_count; i++) {
        if (block_list[i].allocated && block_list[i].address == ptr) {
            return true;
        }
    }
    
    return false;
}
