#include "syscall.h"

int strlen(const char* str) {
    int len = 0;
    while(str[len]) len++;
    return len;
}

void print_int(int n) {
    char buf[16];
    if (n == 0) {
        sys_write(1, "0", 1);
        return;
    }
    int i = 0;
    while(n > 0) {
        buf[i++] = (n % 10) + '0';
        n /= 10;
    }
    for(int j = i - 1; j >= 0; j--) {
        sys_write(1, &buf[j], 1);
    }
}

int main(int argc, char** argv) {
    const char* msg = "Hello from Userland ELF!\n";
    sys_write(1, msg, 25);
    
    sys_write(1, "argc: ", 6);
    print_int(argc);
    sys_write(1, "\n", 1);
    
    for (int i = 0; i < argc; i++) {
        sys_write(1, "argv[", 5);
        print_int(i);
        sys_write(1, "]: ", 3);
        sys_write(1, argv[i], strlen(argv[i]));
        sys_write(1, "\n", 1);
    }
    
    return 0;
}
