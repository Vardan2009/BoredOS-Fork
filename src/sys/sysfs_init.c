#include "kernel_subsystem.h"
#include "smp.h"
#include "pci.h"
#include "memory_manager.h"
#include "module_manager.h"
#include "io.h"
#include "core/kutils.h"
#include "wm/graphics.h"
#include "core/platform.h"
#include "dev/disk.h"

// --- Helper: itoa ---
static void sys_itoa(int n, char *s) {
    k_itoa(n, s);
}

// --- Graphics Implementation ---
static int read_gfx_drm(char *buf, int size, int offset) {
    char out[512];
    k_memset(out, 0, 512);
    k_strcpy(out, "Driver: Simple Framebuffer\n");
    k_strcpy(out + k_strlen(out), "Resolution: ");
    char s[32]; k_itoa(get_screen_width(), s);
    k_strcpy(out + k_strlen(out), s);
    k_strcpy(out + k_strlen(out), "x");
    k_itoa(get_screen_height(), s);
    k_strcpy(out + k_strlen(out), s);
    k_strcpy(out + k_strlen(out), "\nDepth: ");
    k_itoa(graphics_get_fb_bpp(), s);
    k_strcpy(out + k_strlen(out), s);
    k_strcpy(out + k_strlen(out), " bpp\nAddress: 0x");
    k_itoa_hex(graphics_get_fb_addr(), s);
    k_strcpy(out + k_strlen(out), s);
    k_strcpy(out + k_strlen(out), "\n");

    int len = (int)k_strlen(out);
    if (offset >= len) return 0;
    int to_copy = len - offset;
    if (to_copy > size) to_copy = size;
    k_memcpy(buf, out + offset, to_copy);
    return to_copy;
}

// --- Memory Tracking Implementation ---
static int read_mem_tracking(char *buf, int size, int offset) {
    MemStats stats = memory_get_stats();
    char out[1024];
    k_memset(out, 0, 1024);
    
    k_strcpy(out, "--- Kernel Heap Tracking ---\n");
    k_strcpy(out + k_strlen(out), "Allocated Blocks: ");
    char s[32]; k_itoa(stats.allocated_blocks, s);
    k_strcpy(out + k_strlen(out), s);
    k_strcpy(out + k_strlen(out), "\nFragmentation: ");
    k_itoa(stats.fragmentation_percent, s);
    k_strcpy(out + k_strlen(out), s);
    k_strcpy(out + k_strlen(out), "%\n");

    int len = (int)k_strlen(out);
    if (offset >= len) return 0;
    int to_copy = len - offset;
    if (to_copy > size) to_copy = size;
    k_memcpy(buf, out + offset, to_copy);
    return to_copy;
}

// --- Module Implementation ---
static int read_sys_modules(char *buf, int size, int offset) {
    int count = module_manager_get_count();
    char out[2048] = "Loaded Modules:\n";
    
    for (int i = 0; i < count; i++) {
        kernel_module_t *mod = module_manager_get_index(i);
        k_strcpy(out + k_strlen(out), "  - ");
        k_strcpy(out + k_strlen(out), mod->name);
        k_strcpy(out + k_strlen(out), " (");
        char sz_s[16]; k_itoa(mod->size / 1024, sz_s);
        k_strcpy(out + k_strlen(out), sz_s);
        k_strcpy(out + k_strlen(out), " KB)\n");
    }

    int len = k_strlen(out);
    if (offset >= len) return 0;
    int to_copy = len - offset;
    if (to_copy > size) to_copy = size;
    k_memcpy(buf, out + offset, to_copy);
    return to_copy;
}

// --- PCI Bus Implementation ---
static int read_pci_bus(char *buf, int size, int offset) {
    pci_device_t devices[64];
    int count = pci_enumerate_devices(devices, 64);
    
    char out[4096];
    k_memset(out, 0, 4096);
    k_strcpy(out, "PCI Bus Devices:\n");
    for (int i = 0; i < count; i++) {
        char line[128];
        k_strcpy(line, " [");
        char b_s[8]; k_itoa(devices[i].bus, b_s);
        k_strcpy(line + k_strlen(line), b_s);
        k_strcpy(line + k_strlen(line), ":");
        k_itoa(devices[i].device, b_s);
        k_strcpy(line + k_strlen(line), b_s);
        k_strcpy(line + k_strlen(line), ":");
        k_itoa(devices[i].function, b_s);
        k_strcpy(line + k_strlen(line), b_s);
        k_strcpy(line + k_strlen(line), "] Vendor:");
        k_itoa_hex(devices[i].vendor_id, b_s);
        k_strcpy(line + k_strlen(line), b_s);
        k_strcpy(line + k_strlen(line), " Device:");
        k_itoa_hex(devices[i].device_id, b_s);
        k_strcpy(line + k_strlen(line), b_s);
        k_strcpy(line + k_strlen(line), " Class:");
        k_itoa_hex(devices[i].class_code, b_s);
        k_strcpy(line + k_strlen(line), b_s);
        k_strcpy(line + k_strlen(line), "\n");
        
        if (k_strlen(out) + k_strlen(line) < 4095) {
            k_strcpy(out + k_strlen(out), line);
        }
    }

    int len = (int)k_strlen(out);
    if (offset >= len) return 0;
    int to_copy = len - offset;
    if (to_copy > size) to_copy = size;
    k_memcpy(buf, out + offset, to_copy);
    return to_copy;
}

// --- CPU System Implementation ---
static int read_cpu_info(char *buf, int size, int offset) {
    char out[2048];
    k_memset(out, 0, 2048);
    
    char vendor[16];
    char model[64];
    char flags[1024];
    cpu_info_t info;
    
    platform_get_cpu_vendor(vendor);
    platform_get_cpu_model(model);
    platform_get_cpu_info(&info);
    platform_get_cpu_flags(flags);
    
    uint32_t cpu_count = smp_cpu_count();
    
    k_strcpy(out, "Vendor: ");
    k_strcpy(out + k_strlen(out), vendor);
    k_strcpy(out + k_strlen(out), "\nModel: ");
    k_strcpy(out + k_strlen(out), model);
    k_strcpy(out + k_strlen(out), "\nCores: ");
    char c_s[16]; k_itoa(cpu_count, c_s);
    k_strcpy(out + k_strlen(out), c_s);
    k_strcpy(out + k_strlen(out), "\nCPU Family: ");
    k_itoa(info.family, c_s);
    k_strcpy(out + k_strlen(out), c_s);
    k_strcpy(out + k_strlen(out), "\nModel Number: ");
    k_itoa(info.model, c_s);
    k_strcpy(out + k_strlen(out), c_s);
    k_strcpy(out + k_strlen(out), "\nStepping: ");
    k_itoa(info.stepping, c_s);
    k_strcpy(out + k_strlen(out), c_s);
    k_strcpy(out + k_strlen(out), "\nCache Size: ");
    k_itoa(info.cache_size, c_s);
    k_strcpy(out + k_strlen(out), c_s);
    k_strcpy(out + k_strlen(out), " KB\nSpeed: ~3.00 GHz\nFlags: ");
    k_strcpy(out + k_strlen(out), flags);
    k_strcpy(out + k_strlen(out), "\n");
    
    int len = (int)k_strlen(out);
    if (offset >= len) return 0;
    int to_copy = len - offset;
    if (to_copy > size) to_copy = size;
    k_memcpy(buf, out + offset, to_copy);
    return to_copy;
}

// --- Devices Implementation ---
static int read_sys_devices(char *buf, int size, int offset) {
    char out[2048];
    k_memset(out, 0, 2048);
    
    extern int disk_get_count(void);
    extern Disk* disk_get_by_index(int index);
    
    int dcount = disk_get_count();
    k_strcpy(out, "Block Devices:\n");
    for (int i = 0; i < dcount; i++) {
        Disk *d = disk_get_by_index(i);
        if (d && !d->is_partition) {
            k_strcpy(out + k_strlen(out), "  ");
            k_strcpy(out + k_strlen(out), d->devname);
            k_strcpy(out + k_strlen(out), " - ");
            k_strcpy(out + k_strlen(out), d->label);
            k_strcpy(out + k_strlen(out), "\n");
        }
    }
    
    k_strcpy(out + k_strlen(out), "\nCharacter Devices:\n");
    k_strcpy(out + k_strlen(out), "  console - System console\n");
    k_strcpy(out + k_strlen(out), "  tty - Terminal devices\n");
    k_strcpy(out + k_strlen(out), "  psmouse - Mouse input\n");
    k_strcpy(out + k_strlen(out), "  keyboard - Keyboard input\n");
    k_strcpy(out + k_strlen(out), "  framebuffer - Framebuffer device\n");
    
    int len = (int)k_strlen(out);
    if (offset >= len) return 0;
    int to_copy = len - offset;
    if (to_copy > size) to_copy = size;
    k_memcpy(buf, out + offset, to_copy);
    return to_copy;
}

// --- Class Implementation ---
static int read_sys_class(char *buf, int size, int offset) {
    char out[1024];
    k_memset(out, 0, 1024);
    
    k_strcpy(out, "Classes:\n");
    k_strcpy(out + k_strlen(out), "  block - Block device class\n");
    k_strcpy(out + k_strlen(out), "  input - Input device class\n");
    k_strcpy(out + k_strlen(out), "  tty - TTY device class\n");
    k_strcpy(out + k_strlen(out), "  sound - Sound device class\n");
    k_strcpy(out + k_strlen(out), "  video - Video device class\n");
    k_strcpy(out + k_strlen(out), "  net - Network device class\n");
    
    int len = (int)k_strlen(out);
    if (offset >= len) return 0;
    int to_copy = len - offset;
    if (to_copy > size) to_copy = size;
    k_memcpy(buf, out + offset, to_copy);
    return to_copy;
}

// --- GPIO Implementation ---
static int read_gpio_debug(char *buf, int size, int offset) {
    uint8_t p64 = inb(0x64);
    char out[64] = "Port 0x64 Status: ";
    char s[16]; k_itoa(p64, s);
    k_strcpy(out + k_strlen(out), s);
    k_strcpy(out + k_strlen(out), "\n");
    
    int len = k_strlen(out);
    if (offset >= len) return 0;
    int to_copy = len - offset;
    if (to_copy > size) to_copy = size;
    k_memcpy(buf, out + offset, to_copy);
    return to_copy;
}

void sysfs_init_subsystems(void) {
    kernel_subsystem_t *kernel, *devices, *bus, *class, *debug, *mem_debug;
    
    subsystem_register("kernel", &kernel);
    subsystem_register("devices", &devices);
    subsystem_register("bus", &bus);
    subsystem_register("class", &class);
    subsystem_register("kernel/debug", &debug);
    
    // Devices info
    subsystem_add_file(devices, "list", read_sys_devices, NULL);
    
    // Class info
    subsystem_add_file(class, "list", read_sys_class, NULL);
    
    // CPU info
    subsystem_add_file(kernel, "cpuinfo", read_cpu_info, NULL);
    
    // Bus info
    kernel_subsystem_t *pci_bus;
    subsystem_register("bus/pci", &pci_bus);
    subsystem_add_file(pci_bus, "devices", read_pci_bus, NULL);

    // Module info
    kernel_subsystem_t *modules_sub;
    subsystem_register("module", &modules_sub);
    subsystem_add_file(modules_sub, "loaded", read_sys_modules, NULL);

    // Memory Tracking
    subsystem_register("kernel/debug/memory", &mem_debug);
    subsystem_add_file(mem_debug, "tracking", read_mem_tracking, NULL);
    
    // Graphics DRM
    kernel_subsystem_t *gfx_debug;
    subsystem_register("kernel/debug/graphics", &gfx_debug);
    subsystem_add_file(gfx_debug, "drm", read_gfx_drm, NULL);

    // GPIO
    subsystem_add_file(debug, "gpio", read_gpio_debug, NULL);
}
