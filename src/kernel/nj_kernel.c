// nj_kernel.c - Kernel adapter for NanoJPEG memory functions
// Provides njAllocMem, njFreeMem, njFillMem, njCopyMem for NJ_USE_LIBC=0 mode

#include "memory_manager.h"
#include <stddef.h>

void* njAllocMem(int size) {
    return kmalloc((size_t)size);
}

void njFreeMem(void* block) {
    if (block) kfree(block);
}

void njFillMem(void* block, unsigned char byte, int size) {
    unsigned char *p = (unsigned char*)block;
    for (int i = 0; i < size; i++) {
        p[i] = byte;
    }
}

void njCopyMem(void* dest, const void* src, int size) {
    unsigned char *d = (unsigned char*)dest;
    const unsigned char *s = (const unsigned char*)src;
    for (int i = 0; i < size; i++) {
        d[i] = s[i];
    }
}
