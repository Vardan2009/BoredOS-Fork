// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include "memory_manager.h"
#include <stdint.h>
#include "limine.h"
#include "platform.h"
#include "spinlock.h"

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
static spinlock_t mm_lock = SPINLOCK_INIT;

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
    bool swapped;
    for (int i = 0; i < block_count - 1; i++) {
        swapped = false;
        for (int j = 0; j < block_count - i - 1; j++) {
            if ((uintptr_t)block_list[j].address > (uintptr_t)block_list[j + 1].address) {
                MemBlock tmp = block_list[j];
                block_list[j] = block_list[j + 1];
                block_list[j + 1] = tmp;
                swapped = true;
            }
        }
        if (!swapped) break;
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

// Internal helper to insert a block at a specific index
static void insert_block_at(int idx, void* addr, size_t size, bool allocated, uint32_t id) {
    if (block_count >= MAX_ALLOCATIONS) return;
    for (int j = block_count; j > idx; j--) {
        block_list[j] = block_list[j - 1];
    }
    block_list[idx].address = addr;
    block_list[idx].size = size;
    block_list[idx].allocated = allocated;
    block_list[idx].allocation_id = id;
    block_list[idx].timestamp = (allocated) ? get_timestamp() : 0;
    block_count++;
}

// Internal helper to remove a block at a specific index
static void remove_block_at(int idx) {
    if (idx < 0 || idx >= block_count) return;
    for (int j = idx; j < block_count - 1; j++) {
        block_list[j] = block_list[j + 1];
    }
    block_count--;
}

void* kmalloc_aligned(size_t size, size_t alignment) {
    if (!initialized || size == 0) return NULL;
    
    uint64_t rflags = spinlock_acquire_irqsave(&mm_lock);

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
            // Check if we have enough slots for potential splits
            int extra_needed = 0;
            if (padding > 0) extra_needed++;
            size_t remaining_size = block_size - (size + padding);
            if (remaining_size > 0) extra_needed++;
            
            if (block_count + extra_needed > MAX_ALLOCATIONS) {
                continue; 
            }

            void* ptr = (void*)aligned_addr;
            uint32_t alloc_id = ++allocation_counter;

            // We are splitting block_list[i].
            // Possible outcomes:
            // 1. [Padding (Free)] [Allocated] [Remaining (Free)]
            // 2. [Padding (Free)] [Allocated]
            // 3. [Allocated] [Remaining (Free)]
            // 4. [Allocated]

            // We'll modify block_list[i] and insert others as needed.
            // To keep things simple and maintain sorted order, we update from right to left.
            
            if (padding > 0 && remaining_size > 0) {
                // Case 1: Split into 3
                block_list[i].size = padding; // Padding stays at i
                insert_block_at(i + 1, ptr, size, true, alloc_id);
                insert_block_at(i + 2, (void*)(aligned_addr + size), remaining_size, false, 0);
            } else if (padding > 0) {
                // Case 2: Split into 2
                block_list[i].size = padding;
                insert_block_at(i + 1, ptr, size, true, alloc_id);
            } else if (remaining_size > 0) {
                // Case 3: Split into 2
                block_list[i].address = ptr;
                block_list[i].size = size;
                block_list[i].allocated = true;
                block_list[i].allocation_id = alloc_id;
                block_list[i].timestamp = get_timestamp();
                insert_block_at(i + 1, (void*)(aligned_addr + size), remaining_size, false, 0);
            } else {
                // Case 4: Perfect fit
                block_list[i].allocated = true;
                block_list[i].allocation_id = alloc_id;
                block_list[i].timestamp = get_timestamp();
            }

            total_allocated += size;
            if (total_allocated > peak_allocated) peak_allocated = total_allocated;

            mem_memset(ptr, 0, size);
            spinlock_release_irqrestore(&mm_lock, rflags);
            return ptr;
        }
    }

    spinlock_release_irqrestore(&mm_lock, rflags);
    return NULL;
}

void* kmalloc(size_t size) {
    return kmalloc_aligned(size, 8);
}

void kfree(void *ptr) {
    if (ptr == NULL || !initialized) return;
    
    uint64_t rflags = spinlock_acquire_irqsave(&mm_lock);

    int block_idx = -1;
    for (int i = 0; i < block_count; i++) {
        if (block_list[i].allocated && block_list[i].address == ptr) {
            block_idx = i;
            break;
        }
    }

    if (block_idx == -1) {
        spinlock_release_irqrestore(&mm_lock, rflags);
        return;
    }

    total_allocated -= block_list[block_idx].size;
    block_list[block_idx].allocated = false;
    block_list[block_idx].allocation_id = 0;

    // Merge with next block if possible
    if (block_idx + 1 < block_count && !block_list[block_idx + 1].allocated) {
        uintptr_t current_end = (uintptr_t)block_list[block_idx].address + block_list[block_idx].size;
        uintptr_t next_start = (uintptr_t)block_list[block_idx + 1].address;
        if (current_end == next_start) {
            block_list[block_idx].size += block_list[block_idx + 1].size;
            remove_block_at(block_idx + 1);
        }
    }

    // Merge with previous block if possible
    if (block_idx > 0 && !block_list[block_idx - 1].allocated) {
        uintptr_t prev_end = (uintptr_t)block_list[block_idx - 1].address + block_list[block_idx - 1].size;
        uintptr_t current_start = (uintptr_t)block_list[block_idx].address;
        if (prev_end == current_start) {
            block_list[block_idx - 1].size += block_list[block_idx].size;
            remove_block_at(block_idx);
        }
    }

    spinlock_release_irqrestore(&mm_lock, rflags);
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
    mem_memset(&stats, 0, sizeof(MemStats));
    
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
