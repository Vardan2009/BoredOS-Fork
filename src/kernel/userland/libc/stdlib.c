#include "stdlib.h"
#include "syscall.h"

// Simple block allocator over sys_sbrk
typedef struct BlockMeta {
    size_t size;
    int free;
    struct BlockMeta *next;
} BlockMeta;

#define META_SIZE sizeof(BlockMeta)

static void *global_base = NULL;

static BlockMeta *find_free_block(BlockMeta **last, size_t size) {
    BlockMeta *current = global_base;
    while (current && !(current->free && current->size >= size)) {
        *last = current;
        current = current->next;
    }
    return current;
}

static BlockMeta *request_space(BlockMeta* last, size_t size) {
    BlockMeta *block;
    block = (BlockMeta *)sys_sbrk(0);
    // Ask for space, ensuring everything stays 8-byte aligned if sizes are odd.
    // For simplicity, we just request exactly what is needed, 
    // but typically `size` should be aligned.
    size_t align = 8;
    if (size % align != 0) {
        size += align - (size % align);
    }
    
    void *request = sys_sbrk(size + META_SIZE);
    if (request == (void*)-1) {
        return NULL; // sbrk failed
    }

    if (last) { // NULL on first request
        last->next = block;
    }
    block->size = size;
    block->next = NULL;
    block->free = 0;
    return block;
}

void *malloc(size_t size) {
    BlockMeta *block;
    if (size <= 0) {
        return NULL;
    }

    // Align size to 8 bytes for safety
    size_t align = 8;
    if (size % align != 0) {
        size += align - (size % align);
    }

    if (!global_base) { // First call
        block = request_space(NULL, size);
        if (!block) return NULL;
        global_base = block;
    } else {
        BlockMeta *last = global_base;
        block = find_free_block(&last, size);
        if (!block) { // Failed to find free block
            block = request_space(last, size);
            if (!block) return NULL;
        } else { // Found free block
            block->free = 0;
            // We could split the block here if block->size is much larger than size...
        }
    }

    return (block + 1);
}

void free(void *ptr) {
    if (!ptr) {
        return;
    }

    BlockMeta *block = (BlockMeta*)ptr - 1;
    block->free = 1;
}

void *calloc(size_t nelem, size_t elsize) {
    size_t size = nelem * elsize;
    void *ptr = malloc(size);
    if (ptr) {
        char *p = ptr;
        for (size_t i = 0; i < size; i++) {
            p[i] = 0;
        }
    }
    return ptr;
}

void *realloc(void *ptr, size_t size) {
    if (!ptr) {
        return malloc(size);
    }
    if (size == 0) {
        free(ptr);
        return NULL;
    }

    BlockMeta *block = (BlockMeta*)ptr - 1;
    if (block->size >= size) {
        return ptr;
    }

    void *new_ptr = malloc(size);
    if (!new_ptr) {
        return NULL;
    }

    char *src = ptr;
    char *dst = new_ptr;
    for (size_t i = 0; i < block->size; i++) {
        dst[i] = src[i];
    }
    free(ptr);
    return new_ptr;
}

void *memset(void *s, int c, size_t n) {
    unsigned char *p = s;
    while (n--) *p++ = (unsigned char)c;
    return s;
}

void *memcpy(void *dest, const void *src, size_t n) {
    unsigned char *d = dest;
    const unsigned char *s = src;
    while (n--) *d++ = *s++;
    return dest;
}
