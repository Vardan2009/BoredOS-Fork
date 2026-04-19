#include "errno.h"
#include "syscall.h"
#include "sys/stat.h"

__attribute__((weak)) int mkdir(const char *pathname, int mode) {
    (void)mode;
    if (!pathname || pathname[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    if (sys_mkdir(pathname) == 0) {
        return 0;
    }
    errno = EIO;
    return -1;
}

__attribute__((weak)) int access(const char *pathname, int mode) {
    (void)mode;
    if (!pathname || pathname[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    if (sys_exists(pathname)) {
        return 0;
    }
    errno = ENOENT;
    return -1;
}

__attribute__((weak)) int stat(const char *pathname, struct stat *statbuf) {
    if (!pathname || pathname[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    if (!sys_exists(pathname)) {
        errno = ENOENT;
        return -1;
    }

    if (statbuf) {
        int fd = sys_open(pathname, "rb");
        if (fd >= 0) {
            statbuf->st_size = (int)sys_size(fd);
            sys_close(fd);
        } else {
            statbuf->st_size = 0;
        }
        statbuf->st_mode = 0;
    }

    return 0;
}
