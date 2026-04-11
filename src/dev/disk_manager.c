// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include "disk.h"
#include "pci.h"
#include "memory_manager.h"
#include "io.h"
#include "wm.h"
#include "ahci.h"
#include "../fs/vfs.h"
#include "../fs/fat32.h"
#include "../sys/spinlock.h"
#include <stddef.h>

static spinlock_t ide_lock = SPINLOCK_INIT;

static Disk *disks[MAX_DISKS];
static int disk_count = 0;
static int next_drive_letter_idx = 0;  // For backward compat
static int next_sd_index = 0;  // For sda, sdb, sdc...

extern void serial_write(const char *str);
extern void serial_write_num(uint64_t num);

// === String Helpers ===

static void dm_strcpy(char *dest, const char *src) {
    while (*src) *dest++ = *src++;
    *dest = 0;
}

static int dm_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static int dm_strlen(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

// === ATA Definitions (Legacy IDE PIO — kept as fallback) ===

#define ATA_PRIMARY_IO   0x1F0
#define ATA_PRIMARY_CTRL 0x3F6
#define ATA_SECONDARY_IO 0x170
#define ATA_SECONDARY_CTRL 0x376

#define ATA_REG_DATA       0x00
#define ATA_REG_ERROR      0x01
#define ATA_REG_FEATURES   0x01
#define ATA_REG_SEC_COUNT0 0x02
#define ATA_REG_LBA0       0x03
#define ATA_REG_LBA1       0x04
#define ATA_REG_LBA2       0x05
#define ATA_REG_HDDEVSEL   0x06
#define ATA_REG_COMMAND    0x07
#define ATA_REG_STATUS     0x07

#define ATA_CMD_READ_PIO   0x20
#define ATA_CMD_WRITE_PIO  0x30
#define ATA_CMD_IDENTIFY   0xEC

#define ATA_SR_BSY     0x80
#define ATA_SR_DRDY    0x40
#define ATA_SR_DF      0x20
#define ATA_SR_DSC     0x10
#define ATA_SR_DRQ     0x08
#define ATA_SR_CORR    0x04
#define ATA_SR_IDX     0x02
#define ATA_SR_ERR     0x01

typedef struct {
    uint16_t port_base;
    bool slave;
} ATADriverData;

// === ATA PIO Driver ===

static int ata_wait_bsy(uint16_t port_base) {
    int timeout = 10000000;
    while ((inb(port_base + ATA_REG_STATUS) & ATA_SR_BSY) && --timeout > 0);
    return timeout <= 0 ? -1 : 0;
}

static int ata_wait_drq(uint16_t port_base) {
    int timeout = 10000000;
    while (!(inb(port_base + ATA_REG_STATUS) & (ATA_SR_DRQ | ATA_SR_ERR)) && --timeout > 0);
    if (timeout <= 0 || (inb(port_base + ATA_REG_STATUS) & ATA_SR_ERR)) return -1;
    return 0;
}

static int ata_identify(uint16_t port_base, bool slave) {
    outb(port_base + ATA_REG_HDDEVSEL, slave ? 0xB0 : 0xA0);
    outb(port_base + ATA_REG_SEC_COUNT0, 0);
    outb(port_base + ATA_REG_LBA0, 0);
    outb(port_base + ATA_REG_LBA1, 0);
    outb(port_base + ATA_REG_LBA2, 0);

    outb(port_base + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);

    uint8_t status = inb(port_base + ATA_REG_STATUS);
    if (status == 0) return 0;

    int timeout = 10000;
    while ((inb(port_base + ATA_REG_STATUS) & ATA_SR_BSY) && --timeout > 0) {
        status = inb(port_base + ATA_REG_STATUS);
        if (status == 0) return 0;
    }
    if (timeout <= 0) return 0;

    if (inb(port_base + ATA_REG_STATUS) & ATA_SR_ERR) return 0;

    if (ata_wait_drq(port_base) != 0) return 0;

    if (inb(port_base + ATA_REG_STATUS) & ATA_SR_ERR) return 0;

    uint32_t sectors = 0;
    for (int i = 0; i < 256; i++) {
        uint16_t data = inw(port_base + ATA_REG_DATA);
        if (i == 60) sectors |= (uint32_t)data;
        if (i == 61) sectors |= (uint32_t)data << 16;
    }

    return sectors;
}

static int ata_read_sector(Disk *disk, uint32_t lba, uint8_t *buffer) {
    ATADriverData *data = (ATADriverData*)disk->driver_data;
    uint16_t port_base = data->port_base;
    bool slave = data->slave;

    // For partition reads, add the partition LBA offset
    if (disk->is_partition && disk->parent) {
        lba += disk->partition_lba_offset;
        // Use parent's driver
        data = (ATADriverData*)disk->parent->driver_data;
        port_base = data->port_base;
        slave = data->slave;
    }

    uint64_t flags = spinlock_acquire_irqsave(&ide_lock);

    if (ata_wait_bsy(port_base) != 0) {
        spinlock_release_irqrestore(&ide_lock, flags);
        return -1;
    }

    outb(port_base + ATA_REG_HDDEVSEL, 0xE0 | (slave << 4) | ((lba >> 24) & 0x0F));
    outb(port_base + ATA_REG_FEATURES, 0x00);
    outb(port_base + ATA_REG_SEC_COUNT0, 1);
    outb(port_base + ATA_REG_LBA0, (uint8_t)(lba));
    outb(port_base + ATA_REG_LBA1, (uint8_t)(lba >> 8));
    outb(port_base + ATA_REG_LBA2, (uint8_t)(lba >> 16));
    outb(port_base + ATA_REG_COMMAND, ATA_CMD_READ_PIO);

    if (ata_wait_bsy(port_base) != 0) {
        spinlock_release_irqrestore(&ide_lock, flags);
        return -1;
    }
    if (ata_wait_drq(port_base) != 0) {
        spinlock_release_irqrestore(&ide_lock, flags);
        return -1;
    }

    uint16_t *ptr = (uint16_t*)buffer;
    for (int i = 0; i < 256; i++) {
        ptr[i] = inw(port_base + ATA_REG_DATA);
    }

    spinlock_release_irqrestore(&ide_lock, flags);
    return 0;
}

static int ata_write_sector(Disk *disk, uint32_t lba, const uint8_t *buffer) {
    ATADriverData *data = (ATADriverData*)disk->driver_data;
    uint16_t port_base = data->port_base;
    bool slave = data->slave;

    // For partition writes, add the partition LBA offset
    if (disk->is_partition && disk->parent) {
        lba += disk->partition_lba_offset;
        data = (ATADriverData*)disk->parent->driver_data;
        port_base = data->port_base;
        slave = data->slave;
    }

    uint64_t flags = spinlock_acquire_irqsave(&ide_lock);

    if (ata_wait_bsy(port_base) != 0) {
        spinlock_release_irqrestore(&ide_lock, flags);
        return -1;
    }

    outb(port_base + ATA_REG_HDDEVSEL, 0xE0 | (slave << 4) | ((lba >> 24) & 0x0F));
    outb(port_base + ATA_REG_FEATURES, 0x00);
    outb(port_base + ATA_REG_SEC_COUNT0, 1);
    outb(port_base + ATA_REG_LBA0, (uint8_t)(lba));
    outb(port_base + ATA_REG_LBA1, (uint8_t)(lba >> 8));
    outb(port_base + ATA_REG_LBA2, (uint8_t)(lba >> 16));
    outb(port_base + ATA_REG_COMMAND, ATA_CMD_WRITE_PIO);

    if (ata_wait_bsy(port_base) != 0) {
        spinlock_release_irqrestore(&ide_lock, flags);
        return -1;
    }
    if (ata_wait_drq(port_base) != 0) {
        spinlock_release_irqrestore(&ide_lock, flags);
        return -1;
    }

    const uint16_t *ptr = (const uint16_t*)buffer;
    for (int i = 0; i < 256; i++) {
        outw(port_base + ATA_REG_DATA, ptr[i]);
    }

    outb(port_base + ATA_REG_COMMAND, 0xE7); // Cache Flush
    if (ata_wait_bsy(port_base) != 0) {
        spinlock_release_irqrestore(&ide_lock, flags);
        return -1;
    }

    spinlock_release_irqrestore(&ide_lock, flags);
    return 0;
}

// === Device Naming ===

const char* disk_get_next_dev_name(void) {
    static char name[8];
    name[0] = 's';
    name[1] = 'd';
    name[2] = 'a' + next_sd_index;
    name[3] = 0;
    next_sd_index++;
    return name;
}

// === Registration ===

void disk_register(Disk *disk) {
    if (disk_count >= MAX_DISKS) return;

    // Auto-assign devname if empty
    if (disk->devname[0] == 0) {
        const char *n = disk_get_next_dev_name();
        dm_strcpy(disk->devname, n);
    }

    disk->registered = true;
    disks[disk_count++] = disk;

    serial_write("[DISK] Registered /dev/");
    serial_write(disk->devname);
    serial_write(" (");
    serial_write(disk->label);
    serial_write(")\n");
}

void disk_register_partition(Disk *parent, uint32_t lba_offset, uint32_t sector_count,
                             bool is_fat32, int part_num) {
    if (disk_count >= MAX_DISKS) return;

    Disk *part = (Disk*)kmalloc(sizeof(Disk));
    if (!part) return;

    // Build name: parent_devname + partition number (e.g. "sda1")
    int len = dm_strlen(parent->devname);
    for (int i = 0; i < len; i++) part->devname[i] = parent->devname[i];
    part->devname[len] = '0' + part_num;
    part->devname[len + 1] = 0;

    part->type = parent->type;
    part->is_fat32 = is_fat32;
    dm_strcpy(part->label, is_fat32 ? "FAT32 Partition" : "Unknown Partition");
    part->partition_lba_offset = lba_offset;
    part->total_sectors = sector_count;
    part->read_sector = parent->read_sector;
    part->write_sector = parent->write_sector;
    part->driver_data = parent->driver_data;
    part->parent = parent;
    part->is_partition = true;
    part->registered = true;

    disks[disk_count++] = part;

    serial_write("[DISK] Registered /dev/");
    serial_write(part->devname);
    serial_write(" (LBA offset ");
    serial_write_num(lba_offset);
    serial_write(", ");
    serial_write_num(sector_count);
    serial_write(" sectors, FAT32=");
    serial_write(" sectors, FAT32=");
    serial_write(is_fat32 ? "yes" : "no");
    serial_write(")\n");

    if (is_fat32) {
        // Try to initialize and mount FAT32 volume to VFS
        void *vol = fat32_mount_volume(part);
        if (vol) {
            char mount_path[32];
            mount_path[0] = '/';
            mount_path[1] = 'd'; mount_path[2] = 'e'; mount_path[3] = 'v'; mount_path[4] = '/';
            dm_strcpy(mount_path + 5, part->devname);
            
            if (vfs_mount(mount_path, part->devname, "fat32", fat32_get_realfs_ops(), vol)) {
                serial_write("[VFS] Mounted ");
                serial_write(mount_path);
                serial_write(" to VFS\n");
                wm_notify_fs_change();
            } else {
                serial_write("[VFS] Failed to mount ");
                serial_write(mount_path);
                serial_write("\n");
            }
        }
    }
}

// === Lookup ===

Disk* disk_get_by_name(const char *devname) {
    if (!devname) return NULL;
    for (int i = 0; i < disk_count; i++) {
        if (dm_strcmp(disks[i]->devname, devname) == 0) {
            return disks[i];
        }
    }
    return NULL;
}

int disk_get_count(void) {
    return disk_count;
}

Disk* disk_get_by_index(int index) {
    if (index < 0 || index >= disk_count) return NULL;
    return disks[index];
}

// === Backward Compat (deprecated) ===

char disk_get_next_free_letter(void) {
    char letter = 'B' + next_drive_letter_idx++;
    if (letter > 'Z') return 0;
    return letter;
}

Disk* disk_get_by_letter(char letter) {
    // Maps old letter scheme: A=ramfs (not a block device), B+=first real disk, etc.
    if (letter >= 'a' && letter <= 'z') letter -= 32;

    // A: was the ramdisk — return NULL since ramfs is now VFS-managed
    if (letter == 'A') return NULL;

    // B-Z map to disk indices 0, 1, 2...
    // Find real disks (non-RAM, non-partition-parent)
    int real_idx = 0;
    for (int i = 0; i < disk_count; i++) {
        if (disks[i]->is_partition && disks[i]->is_fat32) {
            if (real_idx == (letter - 'B')) {
                return disks[i];
            }
            real_idx++;
        }
    }
    return NULL;
}

// === MBR Partition Table ===

typedef struct {
    uint8_t  status;
    uint8_t  chs_first[3];
    uint8_t  type;
    uint8_t  chs_last[3];
    uint32_t lba_start;
    uint32_t sector_count;
} __attribute__((packed)) MBR_PartitionEntry;

#define PART_TYPE_FAT32     0x0B
#define PART_TYPE_FAT32_LBA 0x0C

static bool is_fat32_bpb(const uint8_t *sector) {
    if (sector[510] != 0x55 || sector[511] != 0xAA) return false;

    if (sector[82] == 'F' && sector[83] == 'A' && sector[84] == 'T' &&
        sector[85] == '3' && sector[86] == '2') {
        return true;
    }

    uint16_t bps = *(uint16_t*)&sector[11];
    uint16_t spf16 = *(uint16_t*)&sector[22];
    uint32_t spf32 = *(uint32_t*)&sector[36];
    if (bps == 512 && spf16 == 0 && spf32 > 0) {
        return true;
    }

    return false;
}

// Parse MBR and register each partition as a child block device
static void parse_mbr_partitions(Disk *disk) {
    uint8_t *buffer = (uint8_t*)kmalloc(512);
    if (!buffer) return;

    if (disk->read_sector(disk, 0, buffer) != 0) {
        kfree(buffer);
        return;
    }

    // Check for valid MBR signature
    if (buffer[510] != 0x55 || buffer[511] != 0xAA) {
        kfree(buffer);
        return;
    }

    MBR_PartitionEntry *partitions = (MBR_PartitionEntry*)&buffer[446];
    int part_num = 1;

    for (int i = 0; i < 4; i++) {
        uint32_t start = partitions[i].lba_start;
        uint32_t size = partitions[i].sector_count;
        uint8_t type = partitions[i].type;

        if (type == 0x00) continue; // Empty entry
        if (size == 0) continue;
        if (start >= disk->total_sectors) continue; // Invalid start
        
        bool fat32 = false;
        if (type == PART_TYPE_FAT32 || type == PART_TYPE_FAT32_LBA) {
            // Verify by reading the BPB
            uint8_t *pbuf = (uint8_t*)kmalloc(512);
            if (pbuf) {
                if (disk->read_sector(disk, start, pbuf) == 0) {
                    fat32 = is_fat32_bpb(pbuf);
                }
                kfree(pbuf);
            }
        }

        disk_register_partition(disk, partitions[i].lba_start,
                                partitions[i].sector_count, fat32, part_num);
        part_num++;
    }

    // Fallback: if no partitions found, check if entire disk is a raw FAT32 volume
    if (part_num == 1 && is_fat32_bpb(buffer)) {
        serial_write("[DISK] No MBR partitions — raw FAT32 volume on /dev/");
        serial_write(disk->devname);
        serial_write("\n");
        disk->is_fat32 = true;
        disk->partition_lba_offset = 0;
    }

    kfree(buffer);
}

// === ATA Drive Discovery ===

static void try_add_ata_drive(uint16_t port, bool slave, const char *name) {
    uint32_t sectors = ata_identify(port, slave);
    if (sectors > 0) {
        Disk *new_disk = (Disk*)kmalloc(sizeof(Disk));
        if (!new_disk) return;

        ATADriverData *data = (ATADriverData*)kmalloc(sizeof(ATADriverData));
        data->port_base = port;
        data->slave = slave;

        new_disk->devname[0] = 0; // Auto-assign
        new_disk->type = DISK_TYPE_IDE;
        dm_strcpy(new_disk->label, name);
        new_disk->read_sector = ata_read_sector;
        new_disk->write_sector = ata_write_sector;
        new_disk->driver_data = data;
        new_disk->partition_lba_offset = 0;
        new_disk->total_sectors = sectors;
        new_disk->parent = NULL;
        new_disk->is_partition = false;
        new_disk->is_fat32 = false;

        disk_register(new_disk);

        // Parse MBR to find partitions
        parse_mbr_partitions(new_disk);
    }
}

// === Init & Scan ===

void disk_manager_init(void) {
    for (int i = 0; i < MAX_DISKS; i++) {
        disks[i] = NULL;
    }
    disk_count = 0;
    next_sd_index = 0;
    next_drive_letter_idx = 0;

    serial_write("[DISK] Disk manager initialized (VFS mode)\n");
    // NOTE: Ramdisk (A:) is no longer registered here.
    // RAMFS is managed directly by fat32.c and mounted at "/" via VFS.
}

void disk_manager_scan(void) {
    serial_write("[DISK] Initializing AHCI (SATA DMA)...\n");
    ahci_init();
    
    if (ahci_get_port_count() == 0) {
        serial_write("[DISK] No AHCI ports found, falling back to legacy IDE PIO...\n");
        try_add_ata_drive(ATA_PRIMARY_IO, false, "IDE Primary Master");
        try_add_ata_drive(ATA_PRIMARY_IO, true, "IDE Primary Slave");
        try_add_ata_drive(ATA_SECONDARY_IO, false, "IDE Secondary Master");
        try_add_ata_drive(ATA_SECONDARY_IO, true, "IDE Secondary Slave");
    } else {
        serial_write("[DISK] AHCI initialized successfully, skipping legacy IDE.\n");
    }
}