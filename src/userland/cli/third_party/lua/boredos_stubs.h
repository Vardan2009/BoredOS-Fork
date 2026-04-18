#ifndef BOREDOS_LUA_STUBS_H
#define BOREDOS_LUA_STUBS_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>
#include <locale.h>
#include <signal.h>
#include <errno.h>
#include <limits.h>

#include "stdlib.h"
#include "string.h"
#include "syscall.h"
#include "math.h"

int errno = 0;

static FILE boredos_stdin_obj = {0, 0, 0, 0, 0};
static FILE boredos_stdout_obj = {1, 0, 0, 0, 0};
static FILE boredos_stderr_obj = {2, 0, 0, 0, 0};
FILE *stdin = &boredos_stdin_obj;
FILE *stdout = &boredos_stdout_obj;
FILE *stderr = &boredos_stderr_obj;

static int _b_is_leap(int year) {
    return ((year % 4) == 0 && (year % 100) != 0) || ((year % 400) == 0);
}

static int _b_days_in_month(int year, int month) {
    static const int mdays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 1 && _b_is_leap(year)) {
        return 29;
    }
    return mdays[month];
}

static long long _b_days_before_year(int year) {
    long long y = (long long)year - 1;
    return y * 365 + y / 4 - y / 100 + y / 400;
}

static long long _b_days_since_epoch(int year, int month, int day) {
    long long days = _b_days_before_year(year) - _b_days_before_year(1970);
    int m;
    for (m = 0; m < month - 1; m++) {
        days += _b_days_in_month(year, m);
    }
    days += (day - 1);
    return days;
}

static void _b_civil_from_days(long long z, int *year, int *month, int *day) {
    z += 719468;
    long long era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int y = (int)yoe + (int)era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    unsigned d = doy - (153 * mp + 2) / 5 + 1;
    unsigned m = mp + (mp < 10 ? 3 : (unsigned)-9);
    y += (m <= 2);
    *year = y;
    *month = (int)m;
    *day = (int)d;
}

int setjmp(jmp_buf env) {
    __asm__ volatile(
        "movq %%rbx, 0(%0)\n\t"
        "movq %%rbp, 8(%0)\n\t"
        "movq %%r12, 16(%0)\n\t"
        "movq %%r13, 24(%0)\n\t"
        "movq %%r14, 32(%0)\n\t"
        "movq %%r15, 40(%0)\n\t"
        "leaq 8(%%rsp), %%rax\n\t"
        "movq %%rax, 48(%0)\n\t"
        "movq (%%rsp), %%rax\n\t"
        "movq %%rax, 56(%0)\n\t"
        :
        : "r"(env)
        : "rax", "memory");
    return 0;
}

void longjmp(jmp_buf env, int val) {
    int r = (val == 0) ? 1 : val;
    __asm__ volatile(
        "movq 0(%0), %%rbx\n\t"
        "movq 8(%0), %%rbp\n\t"
        "movq 16(%0), %%r12\n\t"
        "movq 24(%0), %%r13\n\t"
        "movq 32(%0), %%r14\n\t"
        "movq 40(%0), %%r15\n\t"
        "movq 48(%0), %%rsp\n\t"
        "movl %1, %%eax\n\t"
        "movq 56(%0), %%rdx\n\t"
        "jmp *%%rdx\n\t"
        :
        : "r"(env), "r"(r)
        : "rax", "rdx", "memory");
    __builtin_unreachable();
}

FILE *fopen(const char *path, const char *mode) {
    int fd = sys_open(path, mode);
    FILE *f;
    if (fd < 0) {
        errno = EINVAL;
        return NULL;
    }
    f = (FILE *)malloc(sizeof(FILE));
    if (!f) {
        sys_close(fd);
        errno = ERANGE;
        return NULL;
    }
    f->fd = fd;
    f->eof = 0;
    f->err = 0;
    f->has_ungetc = 0;
    f->ungetc_char = 0;
    return f;
}

FILE *freopen(const char *path, const char *mode, FILE *stream) {
    int fd;
    if (!stream) {
        return fopen(path, mode);
    }
    if (stream->fd >= 0) {
        sys_close(stream->fd);
    }
    fd = sys_open(path, mode);
    if (fd < 0) {
        stream->err = 1;
        errno = EINVAL;
        return NULL;
    }
    stream->fd = fd;
    stream->eof = 0;
    stream->err = 0;
    stream->has_ungetc = 0;
    return stream;
}

int fclose(FILE *stream) {
    if (!stream) {
        return EOF;
    }
    if (stream != stdin && stream != stdout && stream != stderr) {
        if (stream->fd >= 0) {
            sys_close(stream->fd);
        }
        free(stream);
    }
    return 0;
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    size_t total;
    int n;
    if (!stream || !ptr || size == 0 || nmemb == 0) {
        return 0;
    }
    total = size * nmemb;
    n = sys_read(stream->fd, ptr, (uint32_t)total);
    if (n <= 0) {
        if (n == 0) {
            stream->eof = 1;
        } else {
            stream->err = 1;
        }
        return 0;
    }
    if ((size_t)n < total) {
        stream->eof = 1;
    }
    return (size_t)n / size;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream) {
    size_t total;
    int n;
    if (!stream || !ptr || size == 0 || nmemb == 0) {
        return 0;
    }
    total = size * nmemb;
    if (stream->fd <= 2) {
        n = sys_write(stream->fd, (const char *)ptr, (int)total);
    } else {
        n = sys_write_fs(stream->fd, ptr, (uint32_t)total);
    }
    if (n < 0) {
        stream->err = 1;
        return 0;
    }
    return (size_t)n / size;
}

int fseek(FILE *stream, long offset, int whence) {
    if (!stream) {
        return -1;
    }
    if (sys_seek(stream->fd, (int)offset, whence) < 0) {
        stream->err = 1;
        return -1;
    }
    stream->eof = 0;
    stream->has_ungetc = 0;
    return 0;
}

long ftell(FILE *stream) {
    if (!stream) {
        return -1;
    }
    return (long)sys_tell(stream->fd);
}

int getc(FILE *stream) {
    unsigned char ch;
    int n;
    if (!stream) {
        return EOF;
    }
    if (stream->has_ungetc) {
        stream->has_ungetc = 0;
        return stream->ungetc_char;
    }
    n = sys_read(stream->fd, &ch, 1);
    if (n <= 0) {
        if (n == 0) {
            stream->eof = 1;
        } else {
            stream->err = 1;
        }
        return EOF;
    }
    return (int)ch;
}

int ungetc(int c, FILE *stream) {
    if (!stream || c == EOF) {
        return EOF;
    }
    stream->has_ungetc = 1;
    stream->ungetc_char = (unsigned char)c;
    stream->eof = 0;
    return c;
}

char *fgets(char *s, int n, FILE *stream) {
    int i;
    if (!s || n <= 0 || !stream) {
        return NULL;
    }
    for (i = 0; i < n - 1; i++) {
        int c = getc(stream);
        if (c == EOF) {
            break;
        }
        s[i] = (char)c;
        if (c == '\n') {
            i++;
            break;
        }
    }
    if (i == 0) {
        return NULL;
    }
    s[i] = '\0';
    return s;
}

int fputs(const char *s, FILE *stream) {
    size_t len;
    size_t written;
    if (!s || !stream) {
        return EOF;
    }
    len = strlen(s);
    written = fwrite(s, 1, len, stream);
    return (written == len) ? (int)len : EOF;
}

int feof(FILE *stream) {
    return stream ? stream->eof : 1;
}

int ferror(FILE *stream) {
    return stream ? stream->err : 1;
}

void clearerr(FILE *stream) {
    if (stream) {
        stream->eof = 0;
        stream->err = 0;
    }
}

int fflush(FILE *stream) {
    (void)stream;
    return 0;
}

int remove(const char *path) {
    return sys_delete(path);
}

int rename(const char *oldpath, const char *newpath) {
    (void)oldpath;
    (void)newpath;
    errno = EINVAL;
    return -1;
}

FILE *tmpfile(void) {
    errno = EINVAL;
    return NULL;
}

char *tmpnam(char *s) {
    (void)s;
    errno = EINVAL;
    return NULL;
}

int isdigit(int c) { return (c >= '0' && c <= '9'); }
int isalpha(int c) { return ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')); }
int isalnum(int c) { return isalpha(c) || isdigit(c); }
int isspace(int c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}
int isupper(int c) { return (c >= 'A' && c <= 'Z'); }
int islower(int c) { return (c >= 'a' && c <= 'z'); }
int isxdigit(int c) {
    return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
int iscntrl(int c) { return ((c >= 0 && c < 32) || c == 127); }
int ispunct(int c) {
    return isprint(c) && !isalnum(c) && !isspace(c);
}
int isprint(int c) { return (c >= 32 && c < 127); }
int isgraph(int c) { return (c > 32 && c < 127); }
int tolower(int c) { return isupper(c) ? (c - 'A' + 'a') : c; }
int toupper(int c) { return islower(c) ? (c - 'a' + 'A') : c; }

int abs(int x) {
    return (x < 0) ? -x : x;
}

int strncmp(const char *s1, const char *s2, size_t n) {
    size_t i;
    for (i = 0; i < n; i++) {
        unsigned char c1 = (unsigned char)s1[i];
        unsigned char c2 = (unsigned char)s2[i];
        if (c1 != c2) {
            return (int)c1 - (int)c2;
        }
        if (c1 == '\0') {
            return 0;
        }
    }
    return 0;
}

char *strncpy(char *dest, const char *src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    for (; i < n; i++) {
        dest[i] = '\0';
    }
    return dest;
}

char *strncat(char *dest, const char *src, size_t n) {
    size_t dlen = strlen(dest);
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++) {
        dest[dlen + i] = src[i];
    }
    dest[dlen + i] = '\0';
    return dest;
}

char *strrchr(const char *s, int c) {
    const char *last = NULL;
    for (;; s++) {
        if (*s == (char)c) {
            last = s;
        }
        if (*s == '\0') {
            break;
        }
    }
    return (char *)last;
}

char *strpbrk(const char *s, const char *accept) {
    for (; *s; s++) {
        const char *a;
        for (a = accept; *a; a++) {
            if (*s == *a) {
                return (char *)s;
            }
        }
    }
    return NULL;
}

size_t strspn(const char *s, const char *accept) {
    size_t n = 0;
    while (*s) {
        if (!strchr(accept, *s)) {
            break;
        }
        n++;
        s++;
    }
    return n;
}

size_t strcspn(const char *s, const char *reject) {
    size_t n = 0;
    while (*s) {
        if (strchr(reject, *s)) {
            break;
        }
        n++;
        s++;
    }
    return n;
}

void *memchr(const void *s, int c, size_t n) {
    const unsigned char *p = (const unsigned char *)s;
    size_t i;
    for (i = 0; i < n; i++) {
        if (p[i] == (unsigned char)c) {
            return (void *)(p + i);
        }
    }
    return NULL;
}

int strcoll(const char *s1, const char *s2) {
    return strcmp(s1, s2);
}

char *strerror(int errnum) {
    switch (errnum) {
        case 0: return "no error";
        case EDOM: return "domain error";
        case ERANGE: return "range error";
        case EINVAL: return "invalid argument";
        default: return "unknown error";
    }
}

static int _b_hex_digit(unsigned value, int upper) {
    if (value < 10U) {
        return (int)('0' + value);
    }
    return (int)((upper ? 'A' : 'a') + (value - 10U));
}

static void _b_append_char(char *out, size_t cap, size_t *idx, int c) {
    if (*idx + 1 < cap) {
        out[*idx] = (char)c;
    }
    (*idx)++;
}

static void _b_append_strn(char *out, size_t cap, size_t *idx, const char *s, size_t n) {
    size_t i;
    for (i = 0; i < n; i++) {
        _b_append_char(out, cap, idx, s[i]);
    }
}

static void _b_utoa(unsigned long long v, unsigned base, int upper, char *buf, size_t *len) {
    char tmp[64];
    size_t i = 0;
    if (v == 0) {
        tmp[i++] = '0';
    } else {
        while (v && i < sizeof(tmp)) {
            tmp[i++] = (char)_b_hex_digit((unsigned)(v % base), upper);
            v /= base;
        }
    }
    *len = i;
    while (i > 0) {
        *buf++ = tmp[--i];
    }
}

static void _b_itoa(long long v, char *buf, size_t *len) {
    unsigned long long uv;
    size_t n = 0;
    if (v < 0) {
        *buf++ = '-';
        n++;
        uv = (unsigned long long)(-(v + 1)) + 1ULL;
    } else {
        uv = (unsigned long long)v;
    }
    _b_utoa(uv, 10U, 0, buf, len);
    *len += n;
}

static void _b_ftoa(double d, int precision, char *buf, size_t *len) {
    long long ip;
    double frac;
    size_t n = 0;
    if (precision < 0) {
        precision = 6;
    }
    if (d < 0.0) {
        buf[n++] = '-';
        d = -d;
    }
    ip = (long long)d;
    frac = d - (double)ip;
    {
        char ibuf[64];
        size_t ilen = 0;
        _b_utoa((unsigned long long)ip, 10U, 0, ibuf, &ilen);
        memcpy(buf + n, ibuf, ilen);
        n += ilen;
    }
    if (precision > 0) {
        int i;
        buf[n++] = '.';
        for (i = 0; i < precision; i++) {
            int digit;
            frac *= 10.0;
            digit = (int)frac;
            if (digit < 0) digit = 0;
            if (digit > 9) digit = 9;
            buf[n++] = (char)('0' + digit);
            frac -= (double)digit;
        }
    }
    *len = n;
}

int vsnprintf(char *str, size_t size, const char *fmt, va_list ap) {
    size_t out_i = 0;
    while (*fmt) {
        if (*fmt != '%') {
            _b_append_char(str, size, &out_i, *fmt++);
            continue;
        }

        fmt++;
        if (*fmt == '%') {
            _b_append_char(str, size, &out_i, '%');
            fmt++;
            continue;
        }

        while (*fmt == '-' || *fmt == '+' || *fmt == ' ' || *fmt == '#' || *fmt == '0') {
            fmt++;
        }

        if (*fmt == '*') {
            (void)va_arg(ap, int);
            fmt++;
        } else {
            while (isdigit((unsigned char)*fmt)) {
                fmt++;
            }
        }

        {
            int precision = -1;
            if (*fmt == '.') {
                fmt++;
                precision = 0;
                if (*fmt == '*') {
                    precision = va_arg(ap, int);
                    fmt++;
                } else {
                    while (isdigit((unsigned char)*fmt)) {
                        precision = precision * 10 + (*fmt - '0');
                        fmt++;
                    }
                }
            }

            int lcount = 0;
            while (*fmt == 'l') {
                lcount++;
                fmt++;
            }

            switch (*fmt) {
                case 'd':
                case 'i': {
                    long long v;
                    char nbuf[64];
                    size_t nlen = 0;
                    if (lcount >= 2) v = va_arg(ap, long long);
                    else if (lcount == 1) v = va_arg(ap, long);
                    else v = va_arg(ap, int);
                    _b_itoa(v, nbuf, &nlen);
                    _b_append_strn(str, size, &out_i, nbuf, nlen);
                    break;
                }
                case 'u':
                case 'x':
                case 'X':
                case 'o': {
                    unsigned long long v;
                    unsigned base = (*fmt == 'o') ? 8U : ((*fmt == 'u') ? 10U : 16U);
                    char nbuf[64];
                    size_t nlen = 0;
                    int upper = (*fmt == 'X');
                    if (lcount >= 2) v = va_arg(ap, unsigned long long);
                    else if (lcount == 1) v = va_arg(ap, unsigned long);
                    else v = va_arg(ap, unsigned int);
                    _b_utoa(v, base, upper, nbuf, &nlen);
                    _b_append_strn(str, size, &out_i, nbuf, nlen);
                    break;
                }
                case 'c': {
                    int c = va_arg(ap, int);
                    _b_append_char(str, size, &out_i, c);
                    break;
                }
                case 's': {
                    const char *s = va_arg(ap, const char *);
                    size_t slen;
                    if (!s) s = "(null)";
                    slen = strlen(s);
                    if (precision >= 0 && (size_t)precision < slen) {
                        slen = (size_t)precision;
                    }
                    _b_append_strn(str, size, &out_i, s, slen);
                    break;
                }
                case 'p': {
                    uintptr_t v = (uintptr_t)va_arg(ap, void *);
                    char nbuf[32];
                    size_t nlen = 0;
                    _b_append_strn(str, size, &out_i, "0x", 2);
                    _b_utoa((unsigned long long)v, 16U, 0, nbuf, &nlen);
                    _b_append_strn(str, size, &out_i, nbuf, nlen);
                    break;
                }
                case 'f':
                case 'g':
                case 'e': {
                    double v = va_arg(ap, double);
                    char nbuf[96];
                    size_t nlen = 0;
                    _b_ftoa(v, precision, nbuf, &nlen);
                    _b_append_strn(str, size, &out_i, nbuf, nlen);
                    break;
                }
                default:
                    _b_append_char(str, size, &out_i, '%');
                    _b_append_char(str, size, &out_i, *fmt);
                    break;
            }
        }
        if (*fmt) {
            fmt++;
        }
    }

    if (size > 0) {
        size_t term = (out_i < size - 1) ? out_i : (size - 1);
        str[term] = '\0';
    }
    return (int)out_i;
}

int snprintf(char *str, size_t size, const char *fmt, ...) {
    int n;
    va_list ap;
    va_start(ap, fmt);
    n = vsnprintf(str, size, fmt, ap);
    va_end(ap);
    return n;
}

int sprintf(char *str, const char *fmt, ...) {
    int n;
    va_list ap;
    va_start(ap, fmt);
    n = vsnprintf(str, (size_t)-1, fmt, ap);
    va_end(ap);
    return n;
}

int fprintf(FILE *stream, const char *fmt, ...) {
    char buf[1024];
    int len;
    va_list ap;
    va_start(ap, fmt);
    len = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (len <= 0) {
        return len;
    }
    if ((size_t)len > sizeof(buf)) {
        len = (int)sizeof(buf);
    }
    if (fwrite(buf, 1, (size_t)len, stream) == 0) {
        return -1;
    }
    return len;
}

int sscanf(const char *str, const char *fmt, ...) {
    (void)str;
    (void)fmt;
    return 0;
}

double ldexp(double x, int expn) {
    double v = x;
    int i;
    if (expn >= 0) {
        for (i = 0; i < expn; i++) {
            v *= 2.0;
        }
    } else {
        for (i = 0; i < -expn; i++) {
            v *= 0.5;
        }
    }
    return v;
}

double frexp(double x, int *expn) {
    int e = 0;
    double v = x;
    if (x == 0.0) {
        *expn = 0;
        return 0.0;
    }
    while (fabs(v) >= 1.0) {
        v *= 0.5;
        e++;
    }
    while (fabs(v) > 0.0 && fabs(v) < 0.5) {
        v *= 2.0;
        e--;
    }
    *expn = e;
    return v;
}

static double _b_atan_series(double x) {
    double x2 = x * x;
    double term = x;
    double sum = x;
    for (int n = 3; n <= 23; n += 2) {
        term *= -x2;
        sum += term / (double)n;
    }
    return sum;
}

static double _b_atan_precise(double x) {
    if (x < 0.0) {
        return -_b_atan_precise(-x);
    }
    if (x > 1.0) {
        return (M_PI / 2.0) - _b_atan_precise(1.0 / x);
    }
    if (x > 0.5) {
        double y = (x - 1.0) / (x + 1.0);
        return (M_PI / 4.0) + _b_atan_series(y);
    }
    return _b_atan_series(x);
}

double atan2(double y, double x) {
    if (x > 0.0) {
        return _b_atan_precise(y / x);
    }
    if (x < 0.0) {
        if (y >= 0.0) return _b_atan_precise(y / x) + M_PI;
        return _b_atan_precise(y / x) - M_PI;
    }
    if (y > 0.0) return M_PI / 2.0;
    if (y < 0.0) return -M_PI / 2.0;
    return 0.0;
}

double asin(double x) {
    if (x > 1.0 || x < -1.0) {
        errno = EDOM;
        return 0.0;
    }
    return atan2(x, sqrt(1.0 - x * x));
}

double acos(double x) {
    if (x > 1.0 || x < -1.0) {
        errno = EDOM;
        return 0.0;
    }
    return M_PI / 2.0 - asin(x);
}

static time_t _b_seconds_from_ymdhms(int year, int month, int day, int hour, int minute, int second) {
    long long days = _b_days_since_epoch(year, month, day);
    return (time_t)(days * 86400LL + hour * 3600LL + minute * 60LL + second);
}

static void _b_fill_tm_from_epoch(time_t t, struct tm *out) {
    long long sec = (long long)t;
    long long days;
    int sod;
    int year, month, day;

    if (sec < 0) {
        long long d = ((-sec) + 86399LL) / 86400LL;
        sec += d * 86400LL;
    }

    days = sec / 86400LL;
    sod = (int)(sec % 86400LL);
    if (sod < 0) {
        sod += 86400;
        days--;
    }

    _b_civil_from_days(days, &year, &month, &day);

    out->tm_year = year - 1900;
    out->tm_mon = month - 1;
    out->tm_mday = day;
    out->tm_hour = sod / 3600;
    out->tm_min = (sod % 3600) / 60;
    out->tm_sec = sod % 60;
    out->tm_wday = (int)((days + 4) % 7);
    if (out->tm_wday < 0) out->tm_wday += 7;

    {
        long long jan1 = _b_days_since_epoch(year, 1, 1);
        out->tm_yday = (int)(days - jan1);
    }
    out->tm_isdst = 0;
}

time_t time(time_t *out) {
    int dt[6] = {1970, 1, 1, 0, 0, 0};
    time_t t;
    if (sys_system(SYSTEM_CMD_RTC_GET, 0, (uint64_t)dt, 0, 0) != 0) {
        t = 0;
    } else {
        t = _b_seconds_from_ymdhms(dt[0], dt[1], dt[2], dt[3], dt[4], dt[5]);
    }
    if (out) {
        *out = t;
    }
    return t;
}

clock_t clock(void) {
    static uint64_t start_tsc = 0;
    unsigned int lo;
    unsigned int hi;
    uint64_t now_tsc;

    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    now_tsc = ((uint64_t)hi << 32) | (uint64_t)lo;

    if (start_tsc == 0) {
        start_tsc = now_tsc;
        __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
        now_tsc = ((uint64_t)hi << 32) | (uint64_t)lo;
    }

    return (clock_t)(now_tsc - start_tsc);
}

struct tm *gmtime(const time_t *timer) {
    static struct tm tmv;
    if (!timer) {
        return NULL;
    }
    _b_fill_tm_from_epoch(*timer, &tmv);
    return &tmv;
}

struct tm *localtime(const time_t *timer) {
    return gmtime(timer);
}

size_t strftime(char *s, size_t max, const char *fmt, const struct tm *tm) {
    (void)fmt;
    if (!s || max == 0 || !tm) {
        return 0;
    }
    {
        int n = snprintf(s, max, "%04d-%02d-%02d %02d:%02d:%02d",
            tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
            tm->tm_hour, tm->tm_min, tm->tm_sec);
        if (n < 0 || (size_t)n >= max) {
            if (max > 0) s[0] = '\0';
            return 0;
        }
        return (size_t)n;
    }
}

time_t mktime(struct tm *tm) {
    if (!tm) {
        return (time_t)-1;
    }
    return _b_seconds_from_ymdhms(
        tm->tm_year + 1900,
        tm->tm_mon + 1,
        tm->tm_mday,
        tm->tm_hour,
        tm->tm_min,
        tm->tm_sec);
}

static struct lconv _b_lconv = {
    ".", "", "", "", "", ".", "", "", "", "",
    0, 0, 0, 0, 0, 0, 0, 0
};

char *setlocale(int category, const char *locale) {
    (void)category;
    if (locale == NULL || strcmp(locale, "C") == 0 || strcmp(locale, "") == 0) {
        return "C";
    }
    return NULL;
}

struct lconv *localeconv(void) {
    return &_b_lconv;
}

sighandler_t signal(int sig, sighandler_t handler) {
    (void)sig;
    return handler;
}

int system(const char *command) {
    (void)command;
    errno = EINVAL;
    return -1;
}

char *getenv(const char *name) {
    (void)name;
    return NULL;
}

void abort(void) {
    sys_exit(1);
    while (1) {}
}

double strtod(const char *nptr, char **endptr) {
    const char *p = nptr;
    int sign = 1;
    double value = 0.0;
    double frac = 0.0;
    double scale = 1.0;
    int exp_sign = 1;
    int exp_val = 0;

    while (isspace((unsigned char)*p)) p++;

    if (*p == '-') {
        sign = -1;
        p++;
    } else if (*p == '+') {
        p++;
    }

    while (isdigit((unsigned char)*p)) {
        value = value * 10.0 + (double)(*p - '0');
        p++;
    }

    if (*p == '.') {
        p++;
        while (isdigit((unsigned char)*p)) {
            frac = frac * 10.0 + (double)(*p - '0');
            scale *= 10.0;
            p++;
        }
        value += frac / scale;
    }

    if (*p == 'e' || *p == 'E') {
        const char *ep = p + 1;
        if (*ep == '-') {
            exp_sign = -1;
            ep++;
        } else if (*ep == '+') {
            ep++;
        }
        if (isdigit((unsigned char)*ep)) {
            p = ep;
            while (isdigit((unsigned char)*p)) {
                exp_val = exp_val * 10 + (*p - '0');
                p++;
            }
        }
    }

    if (endptr) {
        *endptr = (char *)p;
    }

    if (exp_val != 0) {
        value = ldexp(value, exp_sign * exp_val);
    }
    return sign * value;
}

#endif
