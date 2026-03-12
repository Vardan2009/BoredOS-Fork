#include <stdint.h>
#include <stdbool.h>
#include "stdlib.h"
#include "libui.h"
#include "syscall_user.h"

#define STBI_WRITE_NO_STDIO
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include <string.h>

#define GUI_CMD_GET_SCREEN_SIZE   17
#define GUI_CMD_GET_SCREENBUFFER  18
#define GUI_CMD_SHOW_NOTIFICATION 19
#define GUI_CMD_GET_DATETIME      20

void png_write_func(void *context, void *data, int size) {
    int fd = *(int*)context;
    sys_write_fs(fd, data, size);
}


int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    
    // 1. Get screen size
    uint64_t w = 0, h = 0;
    syscall3(SYS_GUI, GUI_CMD_GET_SCREEN_SIZE, (uint64_t)&w, (uint64_t)&h);
    
    if (w == 0 || h == 0 || w > 4096 || h > 4096) {
        printf("Failed to get screen size %d x %d\n", (int)w, (int)h);
        return 1;
    }
    
    // 2. Allocate buffer for 0xAARRGGBB
    uint32_t *pixels = (uint32_t *)malloc(w * h * sizeof(uint32_t));
    if (!pixels) {
        printf("Failed to allocate memory for %d x %d pixels\n", (int)w, (int)h);
        return 1;
    }
    
    // 3. Request screenbuffer
    syscall2(SYS_GUI, GUI_CMD_GET_SCREENBUFFER, (uint64_t)pixels);
    
    // 4. Convert 0xAARRGGBB to RGB for stb_image_write
    uint8_t *rgb_pixels = (uint8_t *)malloc(w * h * 3);
    if (!rgb_pixels) {
        printf("Failed to allocate RGB buffer\n");
        free(pixels);
        return 1;
    }
    
    for (int y = 0; y < (int)h; y++) {
        for (int x = 0; x < (int)w; x++) {
            uint32_t px = pixels[y * w + x];
            int idx = (y * w + x) * 3;
            rgb_pixels[idx + 0] = (px >> 16) & 0xFF; // R
            rgb_pixels[idx + 1] = (px >> 8) & 0xFF;  // G
            rgb_pixels[idx + 2] = (px) & 0xFF;       // B
        }
    }
    
    // 5. Get Datetime for filename
    uint64_t dt[6] = {0};
    syscall2(SYS_GUI, GUI_CMD_GET_DATETIME, (uint64_t)dt);
    
    char filename[128] = "A:/Desktop/screenshot-";
    
    // Quick helper to append 4-digit and 2-digit numbers
    auto void append_num(int num, int digits);
    void append_num(int num, int digits) {
        int len = 0; while (filename[len]) len++;
        if (digits == 4) {
            filename[len++] = '0' + (num / 1000) % 10;
            filename[len++] = '0' + (num / 100) % 10;
        }
        filename[len++] = '0' + (num / 10) % 10;
        filename[len++] = '0' + (num % 10);
        filename[len] = '\0';
    }
    
    append_num((int)dt[0], 4);
    append_num((int)dt[1], 2);
    append_num((int)dt[2], 2);
    int len = 0; while (filename[len]) len++;
    filename[len++] = '-'; filename[len] = '\0';
    append_num((int)dt[3], 2);
    append_num((int)dt[4], 2);
    append_num((int)dt[5], 2);
    len = 0; while (filename[len]) len++;
    filename[len++] = '.'; filename[len++] = 'p'; filename[len++] = 'n'; filename[len++] = 'g'; filename[len] = '\0';
    
    // 6. Write to PNG
    int fd = sys_open(filename, "w"); // Open file
    int res = 0;
    if (fd >= 0) {
        res = stbi_write_png_to_func(png_write_func, &fd, (int)w, (int)h, 3, rgb_pixels, (int)w * 3);
        sys_close(fd); // Close file
    }
    
    free(rgb_pixels);
    free(pixels);
    
    if (res) {
        char notif[256] = "Saved ";
        int nlen = 6;
        int flen = 0;
        while (filename[11 + flen]) {
            notif[nlen + flen] = filename[11 + flen]; 
            flen++;
        }
        notif[nlen + flen] = '\0';
        
        syscall2(SYS_GUI, GUI_CMD_SHOW_NOTIFICATION, (uint64_t)notif);
    } else {
        syscall2(SYS_GUI, GUI_CMD_SHOW_NOTIFICATION, (uint64_t)"Failed to save screenshot");
        return 1;
    }
    
    return 0;
}
