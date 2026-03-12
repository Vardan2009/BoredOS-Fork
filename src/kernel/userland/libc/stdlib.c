#include "stdlib.h"
#include "syscall.h"

// Block allocator over sys_sbrk with coalescing and splitting
typedef struct BlockMeta {
    size_t size;
    int free;
    struct BlockMeta *next;
    struct BlockMeta *prev;
} BlockMeta;

#define META_SIZE sizeof(BlockMeta)
#define MIN_SPLIT_SIZE 64
#define ALIGN8(x) (((x) + 7) & ~(size_t)7)

static BlockMeta *heap_head = NULL;
static BlockMeta *heap_tail = NULL;

static BlockMeta *request_space(size_t size) {
    BlockMeta *block = (BlockMeta *)sys_sbrk(0);
    void *request = sys_sbrk(size + META_SIZE);
    if (request == (void*)-1) return NULL;

    block->size = size;
    block->free = 0;
    block->next = NULL;
    block->prev = heap_tail;

    if (heap_tail) heap_tail->next = block;
    else heap_head = block;
    heap_tail = block;

    return block;
}

// Split a block if remainder is large enough
static void split_block(BlockMeta *block, size_t size) {
    size_t remain = block->size - size - META_SIZE;
    if (block->size < size + META_SIZE + MIN_SPLIT_SIZE) return;

    BlockMeta *new_block = (BlockMeta *)((char *)(block + 1) + size);
    new_block->size = remain;
    new_block->free = 1;
    new_block->next = block->next;
    new_block->prev = block;

    if (block->next) block->next->prev = new_block;
    else heap_tail = new_block;
    block->next = new_block;
    block->size = size;
}

void *malloc(size_t size) {
    if (size == 0) return NULL;

    size = ALIGN8(size);

    // First-fit search (faster than best-fit for large heaps)
    BlockMeta *current = heap_head;
    while (current) {
        if (current->free && current->size >= size) {
            split_block(current, size);
            current->free = 0;
            return (current + 1);
        }
        current = current->next;
    }

    // No suitable block found, request more space
    BlockMeta *block = request_space(size);
    if (!block) return NULL;
    return (block + 1);
}

void free(void *ptr) {
    if (!ptr) return;

    BlockMeta *block = (BlockMeta *)ptr - 1;
    block->free = 1;

    // Coalesce with next block
    if (block->next && block->next->free) {
        block->size += META_SIZE + block->next->size;
        block->next = block->next->next;
        if (block->next) block->next->prev = block;
        else heap_tail = block;
    }

    // Coalesce with previous block
    if (block->prev && block->prev->free) {
        block->prev->size += META_SIZE + block->size;
        block->prev->next = block->next;
        if (block->next) block->next->prev = block->prev;
        else heap_tail = block->prev;
    }
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
    unsigned char *p = (unsigned char *)s;
    while (n--) *p++ = (unsigned char)c;
    return s;
}

void *memcpy(void *dest, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) *d++ = *s++;
    return dest;
}

void *memmove(void *dest, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--) *--d = *--s;
    }
    return dest;
}

int memcmp(const void *s1, const void *s2, size_t n) {
    const unsigned char *p1 = (const unsigned char *)s1;
    const unsigned char *p2 = (const unsigned char *)s2;
    while (n--) {
        if (*p1 != *p2) return *p1 - *p2;
        p1++;
        p2++;
    }
    return 0;
}

// String functions
size_t strlen(const char *s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

char* strcpy(char *dest, const char *src) {
    char *ret = dest;
    while (*src) *dest++ = *src++;
    *dest = 0;
    return ret;
}

char* strcat(char *dest, const char *src) {
    char *ret = dest;
    while (*dest) dest++;
    while (*src) *dest++ = *src++;
    *dest = 0;
    return ret;
}

int atoi(const char *nptr) {
    int res = 0;
    int sign = 1;
    if (*nptr == '-') {
        sign = -1;
        nptr++;
    }
    while (*nptr >= '0' && *nptr <= '9') {
        res = res * 10 + (*nptr - '0');
        nptr++;
    }
    return sign * res;
}

void itoa(int n, char *buf) {
    if (n == 0) {
        buf[0] = '0'; buf[1] = 0; return;
    }
    int i = 0;
    int sign = n < 0;
    if (sign) n = -n;
    while (n > 0) {
        buf[i++] = (n % 10) + '0';
        n /= 10;
    }
    if (sign) buf[i++] = '-';
    buf[i] = 0;
    // Reverse
    for (int j = 0; j < i / 2; j++) {
        char t = buf[j];
        buf[j] = buf[i - 1 - j];
        buf[i - 1 - j] = t;
    }
}

// IO functions
void puts(const char *s) {
    sys_write(1, s, strlen(s));
    sys_write(1, "\n", 1);
}

void printf(const char *fmt, ...) {
    __builtin_va_list args;
    __builtin_va_start(args, fmt);
    char buf[1024];
    int buf_idx = 0;

    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            // Flush current buffer
            if (buf_idx > 0) {
                sys_write(1, buf, buf_idx);
                buf_idx = 0;
            }

            if (*fmt == 's') {
                char *s = __builtin_va_arg(args, char *);
                if (s) sys_write(1, s, strlen(s));
                else sys_write(1, "(null)", 6);
            } else if (*fmt == 'd') {
                int d = __builtin_va_arg(args, int);
                char ibuf[32];
                itoa(d, ibuf);
                sys_write(1, ibuf, strlen(ibuf));
            } else if (*fmt == 'X' || *fmt == 'x') {
                uint32_t val = __builtin_va_arg(args, uint32_t);
                char xbuf[16];
                int xi = 0;
                if (val == 0) xbuf[xi++] = '0';
                else {
                    while (val > 0) {
                        uint32_t rem = val % 16;
                        xbuf[xi++] = (rem < 10) ? (rem + '0') : (rem - 10 + 'A');
                        val /= 16;
                    }
                }
                while (xi > 0) {
                    char c = xbuf[--xi];
                    sys_write(1, &c, 1);
                }
            } else if (*fmt == 'c') {
                char c = (char)__builtin_va_arg(args, int);
                sys_write(1, &c, 1);
            } else if (*fmt == '%') {
                char c = '%';
                sys_write(1, &c, 1);
            }
        } else {
            buf[buf_idx++] = *fmt;
            if (buf_idx >= 1024) {
                sys_write(1, buf, buf_idx);
                buf_idx = 0;
            }
        }
        fmt++;
    }
    if (buf_idx > 0) {
        sys_write(1, buf, buf_idx);
    }
    __builtin_va_end(args);
}

// System/Process functions
int chdir(const char *path) {
    return sys_chdir(path);
}

char* getcwd(char *buf, int size) {
    if (sys_getcwd(buf, size) == 0) return buf;
    return NULL;
}

void sleep(int ms) {
    sys_system(46, ms, 0, 0, 0);
}

void exit(int status) {
    sys_exit(status);
}
