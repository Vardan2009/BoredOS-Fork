#include "boredos_libc.h"
#include <syscall.h>

int errno = 0;

static FILE _stderr = {2, 0, 0};
static FILE _stdout = {1, 0, 0};
static FILE _stdin  = {0, 0, 0};

FILE* stderr = &_stderr;
FILE* stdout = &_stdout;
FILE* stdin  = &_stdin;

FILE *fopen(const char *path, const char *mode) {
    int fd = sys_open(path, mode);
    if (fd < 0) return NULL;
    FILE *f = malloc(sizeof(FILE));
    f->fd = fd;
    f->eof = 0;
    f->error = 0;
    return f;
}

int fclose(FILE *stream) {
    if (!stream) return EOF;
    if (stream != stderr && stream != stdout && stream != stdin) {
        sys_close(stream->fd);
        free(stream);
    }
    return 0;
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    if (!stream) return 0;
    int bytes = sys_read(stream->fd, ptr, size * nmemb);
    if (bytes < 0) {
        stream->error = 1;
        return 0;
    }
    if (bytes == 0) stream->eof = 1;
    return bytes / size;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream) {
    if (!stream) return 0;
    if (stream == stdout || stream == stderr) {
        sys_write(stream->fd, ptr, size * nmemb);
        return nmemb;
    }
    int bytes = sys_write_fs(stream->fd, ptr, size * nmemb);
    if (bytes < 0) {
        stream->error = 1;
        return 0;
    }
    return bytes / size;
}

int fseek(FILE *stream, long offset, int whence) {
    if (!stream) return -1;
    stream->eof = 0;
    return sys_seek(stream->fd, offset, whence);
}

long ftell(FILE *stream) {
    if (!stream) return -1;
    return sys_tell(stream->fd);
}

int remove(const char *pathname) {
    return sys_delete(pathname);
}

int rename(const char *oldpath, const char *newpath) {
    return -1;
}

int fputc(int c, FILE *stream) {
    unsigned char ch = c;
    if (fwrite(&ch, 1, 1, stream) != 1) return EOF;
    return ch;
}

int fputs(const char *s, FILE *stream) {
    size_t len = strlen(s);
    if (fwrite(s, 1, len, stream) != len) return EOF;
    return 0;
}

long filelength(FILE *f) {
    if (!f) return -1;
    return sys_size(f->fd);
}

int mkdir(const char *pathname, int mode) {
    return sys_mkdir(pathname);
}

int access(const char *pathname, int mode) {
    if (sys_exists(pathname)) return 0;
    return -1;
}

int stat(const char *pathname, struct stat *statbuf) {
    if (sys_exists(pathname)) {
        if (statbuf) {
            statbuf->st_size = 0;
            statbuf->st_mode = 0;
        }
        return 0;
    }
    return -1;
}

int strncasecmp(const char *s1, const char *s2, size_t n) {
    while (n--) {
        char c1 = tolower(*s1++);
        char c2 = tolower(*s2++);
        if (c1 != c2) return c1 - c2;
        if (!c1) break;
    }
    return 0;
}
int strcasecmp(const char *s1, const char *s2) {
    while (1) {
        char c1 = tolower(*s1++);
        char c2 = tolower(*s2++);
        if (c1 != c2) return c1 - c2;
        if (!c1) break;
    }
    return 0;
}
char *strncpy(char *dest, const char *src, size_t n) {
    char *ret = dest;
    while (n && *src) { *dest++ = *src++; n--; }
    while (n) { *dest++ = 0; n--; }
    return ret;
}
int strncmp(const char *s1, const char *s2, size_t n) {
    while (n--) {
        if (*s1 != *s2) return *s1 - *s2;
        if (!*s1) break;
        s1++; s2++;
    }
    return 0;
}
char *strrchr(const char *s, int c) {
    const char *last = NULL;
    while (*s) { if (*s == c) last = s; s++; }
    if (c == 0) last = s;
    return (char*)last;
}
char *strchr(const char *s, int c) {
    while (*s) { if (*s == c) return (char*)s; s++; }
    if (c == 0) return (char*)s;
    return NULL;
}
char *strdup(const char *s) {
    size_t len = strlen(s) + 1;
    char *dup = malloc(len);
    if (dup) memcpy(dup, s, len);
    return dup;
}
char *strstr(const char *haystack, const char *needle) {
    size_t n = strlen(needle);
    if (!n) return (char *)haystack;
    while (*haystack) {
        if (!strncmp(haystack, needle, n)) return (char *)haystack;
        haystack++;
    }
    return NULL;
}

int toupper(int c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }
int tolower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
int isspace(int c) { return c == ' ' || c == '\n' || c == '\t' || c == '\r' || c == '\v' || c == '\f'; }
int isdigit(int c) { return c >= '0' && c <= '9'; }
int isprint(int c) { return c >= 32 && c <= 126; }
int isalpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
int isalnum(int c) { return isalpha(c) || isdigit(c); }
int isgraph(int c) { return c > 32 && c <= 126; }
int ispunct(int c) { return isprint(c) && !isspace(c) && !isalnum(c); }
int isupper(int c) { return c >= 'A' && c <= 'Z'; }

void _exit(int status) {
    exit(status);
}



int fflush(FILE *stream) { return 0; }
int abs(int x) { return x < 0 ? -x : x; }
int putchar(int c) { return fputc(c, stdout); }
int system(const char *command) { return -1; }
#define STB_SPRINTF_IMPLEMENTATION
#define STB_SPRINTF_NOFLOAT
#include "stb_sprintf.h"

int vfprintf(FILE *stream, const char *format, va_list ap) {
    char buf[1024];
    int len = stbsp_vsnprintf(buf, sizeof(buf), format, ap);
    if (len > 0) fwrite(buf, 1, len, stream);
    return len;
}

int fprintf(FILE *stream, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int len = vfprintf(stream, format, ap);
    va_end(ap);
    return len;
}

int sprintf(char *str, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int len = stbsp_vsprintf(str, format, ap);
    va_end(ap);
    return len;
}

int snprintf(char *str, size_t size, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int len = stbsp_vsnprintf(str, size, format, ap);
    va_end(ap);
    return len;
}

int vsnprintf(char *str, size_t size, const char *format, va_list ap) {
    return stbsp_vsnprintf(str, size, format, ap);
}

int sscanf(const char *str, const char *format, ...) { return 0; }
