#ifndef BOREDOS_LIBC_SYS_STAT_H
#define BOREDOS_LIBC_SYS_STAT_H

struct stat {
    int st_size;
    int st_mode;
};

int stat(const char *pathname, struct stat *statbuf);
int fstat(int fd, struct stat *statbuf);
int mkdir(const char *pathname, int mode);

#endif
