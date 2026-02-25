#include "syscall.h"

int main() {
    const char* msg = "Attempting to crash via null dereference...\n";
    sys_write(1, msg, 45);
    
    // Null pointer dereference
    volatile int* p = (int*)0;
    *p = 123;
    
    return 0;
}
