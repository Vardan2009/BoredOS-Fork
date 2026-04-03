<div align="center">
  <h1>Userland SDK Reference</h1>
  <p><em>Comprehensive manual for custom libc and system calls in BoredOS.</em></p>
</div>

---

BoredOS provides a custom `libc` implementation necessary for writing userland applications (`.elf` binaries). By avoiding a full-blown standard library like `glibc`, the OS ensures a minimal executable footprint tailored strictly to the existing kernel features.

All headers are located in `src/userland/libc/` (standard functions) and `src/wm/` (UI and widgets).
-   `libui.h`: Core window and drawing API.
-   `libwidget.h`: High-level UI components.

## Standard Library (`stdlib.h` & `string.h`)

The standard library wrappers provide memory management, string manipulation, and basic IO formatting without needing direct syscalls.

### Memory Allocation
*   `void* malloc(size_t size);` - Allocate a block of memory on the heap.
*   `void free(void* ptr);` - Free a previously allocated memory block.
*   `void* calloc(size_t nmemb, size_t size);` - Allocate and zero-out a block of memory for an array.
*   `void* realloc(void* ptr, size_t size);` - Resize an existing memory block.

### Memory Manipulation (`string.h`)
*   `void* memset(void *s, int c, size_t n);` - Fill a block of memory with a specific byte.
*   `void* memcpy(void *dest, const void *src, size_t n);` - Copy memory from source to destination.
*   `void* memmove(void *dest, const void *src, size_t n);` - Safely copy overlapping memory blocks.
*   `int memcmp(const void *s1, const void *s2, size_t n);` - Compare two memory blocks.

### String Utilities
*   `size_t strlen(const char *s);` - Get the length of a string.
*   `int strcmp(const char *s1, const char *s2);` - Compare two strings lexicographically.
*   `char* strcpy(char *dest, const char *src);` - Copy a string to a destination buffer.
*   `char* strcat(char *dest, const char *src);` - Concatenate two strings.

### Conversion and Formatting
*   `int atoi(const char *nptr);` - String to integer conversion.
*   `void itoa(int n, char *buf);` - Integer to string conversion.

### Input / Output
*   `void puts(const char *s);` - Print a string followed by a newline to the standard output.
*   `void printf(const char *fmt, ...);` - Formatted print to standard output (supports `%d`, `%s`, `%x`, etc.).

### Process Control
*   `void exit(int status);` - Terminate the current process.
*   `void sleep(int ms);` - Pause execution for a specified number of milliseconds.

---

## System Calls (`syscall.h`)

For advanced operations, `syscall.h` provides direct wrappers into the kernel.

### Process & System Info
*   `void sys_exit(int status);` - Raw exit syscall.
*   `int sys_write(int fd, const char *buf, int len);` - Write to a file descriptor (usually fd 1 for stdout).
*   `void* sys_sbrk(int incr);` - Expand or shrink the process data segment (used internally by `malloc`).
*   `void sys_kill(int pid);` - Send a kill signal to a process.
*   `void sys_yield(void);` - Yield the remainder of the process's timeslice to the scheduler.
*   `int sys_system(int cmd, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4);` - Execute a privileged system command (e.g., reboot, shutdown, beep).
*   `int sys_get_os_info(os_info_t *info);` - Populate an `os_info_t` struct with version and environment details.
*   `uint64_t sys_get_shell_config(const char *key);` - Retrieve internal shell configuration values.
*   `void sys_set_text_color(uint32_t color);` - Set the raw CLI text output color.

### File System API (VFS)
Interacting with files and directories using the Virtual File System.
*   `int sys_open(const char *path, const char *mode);` - Open a file, returning a file descriptor.
*   `int sys_read(int fd, void *buf, uint32_t len);` - Read from an open file.
*   `int sys_write_fs(int fd, const void *buf, uint32_t len);` - Write to an open file.
*   `void sys_close(int fd);` - Close an open file descriptor.
*   `int sys_seek(int fd, int offset, int whence);` - Reposition the file offset.
*   `uint32_t sys_tell(int fd);` - Get the current file offset.
*   `uint32_t sys_size(int fd);` - Get the total file size.
*   `int sys_delete(const char *path);` - Delete a file.
*   `int sys_mkdir(const char *path);` - Create a new directory.
*   `int sys_exists(const char *path);` - Check if a path exists on the filesystem.
*   `int sys_getcwd(char *buf, int size);` - Get the current working directory string.
*   `int sys_chdir(const char *path);` - Change the current working directory.
*   `int sys_list(const char *path, FAT32_FileInfo *entries, int max_entries);` - List directory contents into an array of `FAT32_FileInfo` structs.
*   `int sys_get_file_info(const char *path, FAT32_FileInfo *info);` - Retrieve metadata for a specific file.

### Networking Stack API
BoredOS includes lwIP for hardware TCP/UDP networking.

#### Configuration and Status
*   `int sys_network_init(void);` - Initialize the network stack.
*   `int sys_network_is_initialized(void);` - Check stack status.
*   `int sys_network_dhcp_acquire(void);` - Request an IP configuration from a DHCP server.
*   `int sys_network_has_ip(void);` - Check if the system has a valid IP address.
*   `int sys_network_get_mac(net_mac_address_t *mac);` - Get the physical MAC address of the NIC.
*   `int sys_network_get_nic_name(char *name_out);` - Get the name of the active network interface card.
*   `int sys_network_get_ip(net_ipv4_address_t *ip);` - Get current local IPv4 address.
*   `int sys_network_set_ip(const net_ipv4_address_t *ip);` - Manually assign a static IP.
*   `int sys_network_get_gateway(net_ipv4_address_t *ip);` - Get the default gateway IP.
*   `int sys_network_get_dns(net_ipv4_address_t *ip);` - Get the primary DNS server IP.
*   `int sys_set_dns_server(const net_ipv4_address_t *ip);` - Set the primary DNS server.
*   `int sys_get_dns_server(net_ipv4_address_t *ip);` - Retrieve configured DNS server.
*   `int sys_network_get_stat(int stat_type);` - Get network statistics (packets in/out, drops, etc.).
*   `void sys_network_force_unlock(void);` - Force release of network stack locks (use with caution).

#### Communication
*   `int sys_icmp_ping(const net_ipv4_address_t *dest_ip);` - Send an ICMP echo request.
*   `int sys_udp_send(const net_ipv4_address_t *dest_ip, uint16_t dest_port, uint16_t src_port, const void *data, size_t data_len);` - Send a raw UDP datagram.
*   `int sys_dns_lookup(const char *name, net_ipv4_address_t *out_ip);` - Resolve a hostname to an IPv4 address.
*   `int sys_tcp_connect(const net_ipv4_address_t *ip, uint16_t port);` - Establish a TCP connection to a remote host.
*   `int sys_tcp_send(const void *data, size_t len);` - Send data over an active TCP socket.
*   `int sys_tcp_recv(void *buf, size_t max_len);` - Receive data from a TCP socket (blocking).
*   `int sys_tcp_recv_nb(void *buf, size_t max_len);` - Receive data from a TCP socket (non-blocking).
*   `int sys_tcp_close(void);` - Close the active TCP socket.

---

## Core Data Structures

### `os_info_t`
Contains detailed build and version information about the OS.
```c
typedef struct {
    char os_name[64];
    char os_version[64];
    char os_codename[64];
    char kernel_name[64];
    char kernel_version[64];
    char build_date[64];
    char build_time[64];
    char build_arch[64];
} os_info_t;
```

### `FAT32_FileInfo`
Represents a filesystem entry.
```c
typedef struct {
    char name[256];
    uint32_t size;
    uint8_t is_directory;
    uint32_t start_cluster;
    uint16_t write_date;
    uint16_t write_time;
} FAT32_FileInfo;
```

### `ProcessInfo`
Provides status information for an active process.
```c
typedef struct {
    uint32_t pid;
    char name[64];
    uint64_t ticks;
    size_t used_memory;
} ProcessInfo;
```

### IP / MAC Addresses
Wrappers for raw byte arrays.
```c
typedef struct { uint8_t bytes[6]; } net_mac_address_t;
typedef struct { uint8_t bytes[4]; } net_ipv4_address_t;
```
