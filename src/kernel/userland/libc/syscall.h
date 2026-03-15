#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>
#include <stddef.h>

// Standard syscalls available from Kernel mode
#define SYS_EXIT  0
#define SYS_WRITE 1
#define SYS_GUI   3
#define SYS_FS    4
#define SYS_SYSTEM 5
#define SYS_KILL   10
#define SYS_SBRK  9

// FS Commands
#define FS_CMD_OPEN 1
#define FS_CMD_READ 2
#define FS_CMD_WRITE 3
#define FS_CMD_CLOSE 4
#define FS_CMD_SEEK 5
#define FS_CMD_TELL 6
#define FS_CMD_LIST 7
#define FS_CMD_DELETE 8
#define FS_CMD_SIZE 9
#define FS_CMD_MKDIR 10
#define FS_CMD_EXISTS 11
#define FS_CMD_GETCWD 12
#define FS_CMD_CHDIR 13
#define FS_CMD_GET_INFO 14

// System Commands (via SYS_SYSTEM)
#define SYSTEM_CMD_SET_BG_COLOR 1
#define SYSTEM_CMD_SET_BG_PATTERN 2
#define SYSTEM_CMD_SET_WALLPAPER 3
#define SYSTEM_CMD_SET_DESKTOP_PROP 4
#define SYSTEM_CMD_SET_MOUSE_SPEED 5
#define SYSTEM_CMD_NETWORK_INIT 6
#define SYSTEM_CMD_GET_DESKTOP_PROP 7
#define SYSTEM_CMD_GET_MOUSE_SPEED 8
#define SYSTEM_CMD_GET_WALLPAPER_THUMB 9
#define SYSTEM_CMD_CLEAR_SCREEN 10
#define SYSTEM_CMD_RTC_GET 11
#define SYSTEM_CMD_REBOOT 12
#define SYSTEM_CMD_SHUTDOWN 13
#define SYSTEM_CMD_BEEP 14
#define SYSTEM_CMD_MEMINFO 15
#define SYSTEM_CMD_UPTIME 16
#define SYSTEM_CMD_PCI_LIST 17
#define SYSTEM_CMD_NETWORK_DHCP 18
#define SYSTEM_CMD_NETWORK_GET_MAC 19
#define SYSTEM_CMD_NETWORK_GET_IP 20
#define SYSTEM_CMD_NETWORK_SET_IP 21
#define SYSTEM_CMD_UDP_SEND 22
#define SYSTEM_CMD_NETWORK_GET_STATS 23
#define SYSTEM_CMD_NETWORK_GET_GATEWAY 24
#define SYSTEM_CMD_NETWORK_GET_DNS 25
#define SYSTEM_CMD_ICMP_PING 26
#define SYSTEM_CMD_NETWORK_IS_INIT 27
#define SYSTEM_CMD_NETWORK_HAS_IP 30
#define SYSTEM_CMD_GET_SHELL_CONFIG 28
#define SYSTEM_CMD_NETWORK_GET_NIC_NAME 48
#define SYSTEM_CMD_SET_TEXT_COLOR 29
#define SYSTEM_CMD_SET_WALLPAPER_PATH 31
#define SYSTEM_CMD_TCP_CONNECT 33
#define SYSTEM_CMD_TCP_SEND 34
#define SYSTEM_CMD_TCP_RECV 35
#define SYSTEM_CMD_TCP_CLOSE 36
#define SYSTEM_CMD_DNS_LOOKUP 37
#define SYSTEM_CMD_SET_DNS 38
#define SYSTEM_CMD_NET_UNLOCK 39
#define SYSTEM_CMD_PROCESS_LIST 44
#define SYSTEM_CMD_GET_CPU_MODEL 45
#define SYSTEM_CMD_SLEEP 46
#define SYSTEM_CMD_SET_RAW_MODE 41
#define SYSTEM_CMD_TCP_RECV_NB 42
#define SYSTEM_CMD_YIELD 43

// Internal assembly entry into Ring 0
extern uint64_t syscall0(uint64_t sys_num);
extern uint64_t syscall1(uint64_t sys_num, uint64_t arg1);
extern uint64_t syscall2(uint64_t sys_num, uint64_t arg1, uint64_t arg2);
extern uint64_t syscall3(uint64_t sys_num, uint64_t arg1, uint64_t arg2, uint64_t arg3);
extern uint64_t syscall4(uint64_t sys_num, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4);
extern uint64_t syscall5(uint64_t sys_num, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5);

// Public API
void sys_exit(int status);
int sys_write(int fd, const char *buf, int len);
void *sys_sbrk(int incr);
void sys_kill(int pid);
int sys_system(int cmd, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4);

// FS API
int sys_open(const char *path, const char *mode);
int sys_read(int fd, void *buf, uint32_t len);
int sys_write_fs(int fd, const void *buf, uint32_t len);
void sys_close(int fd);
int sys_seek(int fd, int offset, int whence);
uint32_t sys_tell(int fd);
uint32_t sys_size(int fd);
int sys_delete(const char *path);
int sys_mkdir(const char *path);
int sys_exists(const char *path);
int sys_getcwd(char *buf, int size);
int sys_chdir(const char *path);

typedef struct {
    char name[256];
    uint32_t size;
    uint8_t is_directory;
    uint32_t start_cluster;
    uint16_t write_date;
    uint16_t write_time;
} FAT32_FileInfo;

int sys_list(const char *path, FAT32_FileInfo *entries, int max_entries);
int sys_get_file_info(const char *path, FAT32_FileInfo *info);

typedef struct {
    uint32_t pid;
    char name[64];
    uint64_t ticks;
    size_t used_memory;
} ProcessInfo;

// Network API
typedef struct { uint8_t bytes[6]; } net_mac_address_t;
typedef struct { uint8_t bytes[4]; } net_ipv4_address_t;

int sys_network_init(void);
int sys_network_dhcp_acquire(void);
int sys_network_get_mac(net_mac_address_t *mac);
int sys_network_get_nic_name(char *name_out);
int sys_network_get_ip(net_ipv4_address_t *ip);
int sys_network_set_ip(const net_ipv4_address_t *ip);
int sys_network_get_stat(int stat_type);
int sys_network_get_gateway(net_ipv4_address_t *ip);
int sys_network_get_dns(net_ipv4_address_t *ip);
int sys_get_dns_server(net_ipv4_address_t *ip);
int sys_udp_send(const net_ipv4_address_t *dest_ip, uint16_t dest_port, uint16_t src_port, const void *data, size_t data_len);
int sys_icmp_ping(const net_ipv4_address_t *dest_ip);
int sys_network_is_initialized(void);
int sys_network_has_ip(void);
uint64_t sys_get_shell_config(const char *key);
void sys_set_text_color(uint32_t color);

int sys_tcp_connect(const net_ipv4_address_t *ip, uint16_t port);
int sys_tcp_send(const void *data, size_t len);
int sys_tcp_recv(void *buf, size_t max_len);
int sys_tcp_recv_nb(void *buf, size_t max_len);
int sys_tcp_close(void);
int sys_dns_lookup(const char *name, net_ipv4_address_t *out_ip);
int sys_set_dns_server(const net_ipv4_address_t *ip);
void sys_network_force_unlock(void);
void sys_yield(void);


#endif
