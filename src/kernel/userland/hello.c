#include "syscall.h"

int main() {
    const char* msg = "Hello from Userland ELF!\n";
    sys_write(1, msg, 25);
    return 0;
}
