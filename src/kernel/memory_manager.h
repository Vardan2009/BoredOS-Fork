// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#ifndef MEMORY_MANAGER_H
#define MEMORY_MANAGER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "limine.h"

// Memory Manager Configuration
#define DEFAULT_POOL_SIZE (128 * 1024 * 1024)  // 128MB default
#define MAX_ALLOCATIONS 16384  // Increased for larger pools
#define MAX_FRAGMENTATION_SLOTS 2048

// Allocation block metadata
typedef struct {
    void *address;
    size_t size;
    bool allocated;
    uint32_t allocation_id;
    uint32_t timestamp;
} MemBlock;

// Memory statistics
typedef struct {
    size_t total_memory;
    size_t used_memory;
    size_t available_memory;
    size_t allocated_blocks;
    size_t free_blocks;
    size_t largest_free_block;
    size_t smallest_free_block;
    size_t fragmentation_percent;
    size_t peak_memory_used;
} MemStats;

// Public API
void memory_manager_init_from_memmap(struct limine_memmap_response *memmap);

// Allocation/Deallocation
void* kmalloc(size_t size);
void* kmalloc_aligned(size_t size, size_t alignment);
void kfree(void *ptr);
void* krealloc(void *ptr, size_t new_size);

// Statistics and Information
MemStats memory_get_stats(void);
void memory_print_stats(void);
void memory_print_detailed(void);

void memory_validate(void);
void memory_dump_blocks(void);

// Internal utilities
size_t memory_get_peak_usage(void);
void memory_reset_peak(void);
bool memory_is_valid_ptr(void *ptr);

void mem_memset(void *dest, int val, size_t len);
void mem_memcpy(void *dest, const void *src, size_t len);

#endif // MEMORY_MANAGER_H
