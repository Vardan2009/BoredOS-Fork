#include <stdarg.h>
#include <stddef.h>

#include "errno.h"
#include "fcntl.h"
#include "sys/stat.h"
#include "syscall.h"
#include "unistd.h"

static const char *_b_mode_from_flags(int flags) {
    int accmode = (flags & O_RDWR) ? O_RDWR : ((flags & O_WRONLY) ? O_WRONLY : O_RDONLY);

    if (accmode == O_RDONLY) {
        return "rb";
    }

    if (accmode == O_RDWR) {
        if (flags & O_TRUNC) {
            return "w+";
        }
        if (flags & O_CREAT) {
            return "a+";
        }
        return "r+";
    }

    if (flags & O_APPEND) {
        return "ab";
    }
    if (flags & O_TRUNC) {
        return "wb";
    }
    if (flags & O_CREAT) {
        return "wb";
    }
    return "wb";
}

__attribute__((weak)) int open(const char *pathname, int flags, ...) {
    int fd;

    if (!pathname || pathname[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    if (flags & O_CREAT) {
        va_list ap;
        (void)ap;
        va_start(ap, flags);
        (void)va_arg(ap, int);
        va_end(ap);
    }

    fd = sys_open(pathname, _b_mode_from_flags(flags));
    if (fd < 0) {
        errno = ENOENT;
        return -1;
    }

    if (flags & O_APPEND) {
        (void)sys_seek(fd, 0, SEEK_END);
    }

    return fd;
}

__attribute__((weak)) int close(int fd) {
    if (fd < 0) {
        errno = EBADF;
        return -1;
    }
    sys_close(fd);
    return 0;
}

__attribute__((weak)) ssize_t read(int fd, void *buf, size_t count) {
    int n;
    if (fd < 0 || (!buf && count != 0)) {
        errno = EINVAL;
        return -1;
    }
    n = sys_read(fd, buf, (uint32_t)count);
    if (n < 0) {
        errno = EIO;
        return -1;
    }
    return (ssize_t)n;
}

__attribute__((weak)) ssize_t write(int fd, const void *buf, size_t count) {
    int n;
    if (fd < 0 || (!buf && count != 0)) {
        errno = EINVAL;
        return -1;
    }

    if (fd <= 2) {
        n = sys_write(fd, (const char *)buf, (int)count);
    } else {
        n = sys_write_fs(fd, buf, (uint32_t)count);
    }

    if (n < 0) {
        errno = EIO;
        return -1;
    }
    return (ssize_t)n;
}

__attribute__((weak)) off_t lseek(int fd, off_t offset, int whence) {
    if (fd < 0) {
        errno = EBADF;
        return -1;
    }
    if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END) {
        errno = EINVAL;
        return -1;
    }

    if (sys_seek(fd, (int)offset, whence) < 0) {
        errno = EIO;
        return -1;
    }
    return (off_t)sys_tell(fd);
}

__attribute__((weak)) int unlink(const char *pathname) {
    if (!pathname || pathname[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    if (sys_delete(pathname) != 0) {
        errno = ENOENT;
        return -1;
    }
    return 0;
}

__attribute__((weak)) int isatty(int fd) {
    return (fd >= 0 && fd <= 2) ? 1 : 0;
}

__attribute__((weak)) int fstat(int fd, struct stat *statbuf) {
    if (fd < 0 || !statbuf) {
        errno = EINVAL;
        return -1;
    }

    statbuf->st_size = (int)sys_size(fd);
    statbuf->st_mode = 0;
    return 0;
}
