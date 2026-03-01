// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include <stdlib.h>
#include <syscall.h>

void combine_path(char *dest, const char *path1, const char *path2) {
    int i = 0;
    while (path1[i]) {
        dest[i] = path1[i];
        i++;
    }
    if (i > 0 && dest[i-1] != '/') {
        dest[i++] = '/';
    }
    int j = 0;
    while (path2[j]) {
        dest[i++] = path2[j++];
    }
    dest[i] = 0;
}

const char* get_basename(const char *path) {
    const char *last_slash = NULL;
    int len = 0;
    while (path[len]) {
        if (path[len] == '/') last_slash = path + len;
        len++;
    }
    
    if (!last_slash) return path;
    
    if (last_slash[1] == '\0') {
        if (len <= 1) return path;
        int i = len - 2;
        while (i >= 0 && path[i] != '/') i--;
        if (i < 0) return path; 
        return path + i + 1;
    }
    
    return last_slash + 1;
}

void copy_recursive(const char *src, const char *dst) {
    FAT32_FileInfo info;
    if (sys_get_file_info(src, &info) < 0) return;

    if (info.is_directory) {
        sys_mkdir(dst);
        FAT32_FileInfo entries[64];
        int count = sys_list(src, entries, 64);
        for (int i = 0; i < count; i++) {
            if (strcmp(entries[i].name, ".") == 0 || strcmp(entries[i].name, "..") == 0) continue;
            char sub_src[512], sub_dst[512];
            combine_path(sub_src, src, entries[i].name);
            combine_path(sub_dst, dst, entries[i].name);
            copy_recursive(sub_src, sub_dst);
        }
    } else {
        int fd_in = sys_open(src, "r");
        if (fd_in < 0) return;
        int fd_out = sys_open(dst, "w");
        if (fd_out < 0) { sys_close(fd_in); return; }
        char buffer[4096];
        int bytes;
        while ((bytes = sys_read(fd_in, buffer, sizeof(buffer))) > 0) {
            sys_write_fs(fd_out, buffer, bytes);
        }
        sys_close(fd_in);
        sys_close(fd_out);
    }
}

void delete_recursive(const char *path) {
    FAT32_FileInfo info;
    if (sys_get_file_info(path, &info) < 0) return;

    if (info.is_directory) {
        FAT32_FileInfo entries[64];
        int count = sys_list(path, entries, 64);
        for (int i = 0; i < count; i++) {
            if (strcmp(entries[i].name, ".") == 0 || strcmp(entries[i].name, "..") == 0) continue;
            char sub_path[512];
            combine_path(sub_path, path, entries[i].name);
            delete_recursive(sub_path);
        }
        sys_delete(path);
    } else {
        sys_delete(path);
    }
}

int main(int argc, char **argv) {
    if (argc < 3) {
        printf("Usage: mv <source> <dest>\n");
        return 1;
    }
    
    char *src_path = argv[1];
    char *dst_path = argv[2];

    FAT32_FileInfo info_src;
    if (sys_get_file_info(src_path, &info_src) < 0) {
        printf("Error: Cannot open source %s\n", src_path);
        return 1;
    }

    char actual_dst[512];
    FAT32_FileInfo info_dst;
    if (sys_get_file_info(dst_path, &info_dst) == 0 && info_dst.is_directory) {
        const char *base = get_basename(src_path);
        char clean_base[256];
        int k = 0;
        while (base[k] && base[k] != '/') {
            clean_base[k] = base[k];
            k++;
        }
        clean_base[k] = 0;
        combine_path(actual_dst, dst_path, clean_base);
    } else {
        strcpy(actual_dst, dst_path);
    }

    copy_recursive(src_path, actual_dst);
    delete_recursive(src_path);
    
    return 0;
}
