// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include <stdlib.h>
#include <syscall.h>

int main(int argc, char **argv) {
    uint64_t dir_color = sys_get_shell_config("dir_color");
    uint64_t file_color = sys_get_shell_config("file_color");
    uint64_t size_color = sys_get_shell_config("size_color");
    uint64_t error_color = sys_get_shell_config("error_color");
    uint64_t default_color = sys_get_shell_config("default_text_color");

    char path[256];
    if (argc > 1) {
        strcpy(path, argv[1]);
    } else {
        if (!sys_getcwd(path, sizeof(path))) {
            strcpy(path, "/");
        }
    }
    
    FAT32_FileInfo info;
    if (sys_get_file_info(path, &info) < 0) {
        sys_set_text_color(error_color);
        printf("Error: Path '%s' does not exist\n", path);
        sys_set_text_color(default_color);
        return 1;
    }

    if (!info.is_directory) {
        sys_set_text_color(file_color);
        printf("[FILE] %s", info.name);
        sys_set_text_color(size_color);
        printf(" (%d bytes)\n", info.size);
        sys_set_text_color(default_color);
        printf("\nTotal: 1 items\n");
        return 0;
    }
    
    FAT32_FileInfo entries[128];
    int count = sys_list(path, entries, 128);
    
    if (count < 0) {
        sys_set_text_color(error_color);
        printf("Error: Cannot list directory %s\n", path);
        sys_set_text_color(default_color);
        return 1;
    }
    
    for (int i = 0; i < count; i++) {
        if (entries[i].is_directory) {
            sys_set_text_color(dir_color);
            printf("[DIR]  %s\n", entries[i].name);
        } else {
            sys_set_text_color(file_color);
            printf("[FILE] %s", entries[i].name);
            sys_set_text_color(size_color);
            printf(" (%d bytes)\n", entries[i].size);
        }
    }
    
    sys_set_text_color(default_color);
    printf("\nTotal: %d items\n", count);
    return 0;
}
