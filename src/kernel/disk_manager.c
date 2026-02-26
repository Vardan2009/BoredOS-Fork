#include "disk.h"
#include "pci.h"
#include "memory_manager.h"
#include "io.h"
#include "wm.h" 
#include <stddef.h>

#define MAX_DISKS 26

static Disk *disks[MAX_DISKS];
static int disk_count = 0;

// === ATA Definitions ===

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

#define ATA_SR_BSY     0x80    // Busy
#define ATA_SR_DRDY    0x40    // Drive ready
#define ATA_SR_DF      0x20    // Drive write fault
#define ATA_SR_DSC     0x10    // Drive seek complete
#define ATA_SR_DRQ     0x08    // Data request ready
#define ATA_SR_CORR    0x04    // Corrected data
#define ATA_SR_IDX     0x02    // Index
#define ATA_SR_ERR     0x01    // Error

typedef struct {
    uint16_t port_base;
    bool slave;
} ATADriverData;

// === Helpers ===

static void dm_strcpy(char *dest, const char *src) {
    while (*src) *dest++ = *src++;
    *dest = 0;
}

void disk_register(Disk *disk);


static int ramdisk_read(Disk *disk, uint32_t sector, uint8_t *buffer) {
    (void)disk; (void)sector; (void)buffer;
    return 0; 
}

static int ramdisk_write(Disk *disk, uint32_t sector, const uint8_t *buffer) {
    (void)disk; (void)sector; (void)buffer;
    return 0;
}


static void ata_wait_bsy(uint16_t port_base) {
    while (inb(port_base + ATA_REG_STATUS) & ATA_SR_BSY);
}

static void ata_wait_drq(uint16_t port_base) {
    while (!(inb(port_base + ATA_REG_STATUS) & ATA_SR_DRQ));
}

// Returns 1 if drive exists, 0 otherwise
static int ata_identify(uint16_t port_base, bool slave) {
    // Select Drive
    outb(port_base + ATA_REG_HDDEVSEL, slave ? 0xB0 : 0xA0);
    // Zero out sector count and LBA registers
    outb(port_base + ATA_REG_SEC_COUNT0, 0);
    outb(port_base + ATA_REG_LBA0, 0);
    outb(port_base + ATA_REG_LBA1, 0);
    outb(port_base + ATA_REG_LBA2, 0);
    
    // Send Identify command
    outb(port_base + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
    
    // Check if status is 0 (no drive)
    uint8_t status = inb(port_base + ATA_REG_STATUS);
    if (status == 0) return 0;
    
    // Wait until BSY clears
    while (inb(port_base + ATA_REG_STATUS) & ATA_SR_BSY) {
        // Simple timeout could be added here
        status = inb(port_base + ATA_REG_STATUS);
        if (status == 0) return 0; // Check again
    }
    
    // Check for error
    if (inb(port_base + ATA_REG_STATUS) & ATA_SR_ERR) {
        return 0; // Error, likely not ATA
    }
    
    // Wait for DRQ or ERR
    while (!(inb(port_base + ATA_REG_STATUS) & (ATA_SR_DRQ | ATA_SR_ERR)));
    
    if (inb(port_base + ATA_REG_STATUS) & ATA_SR_ERR) return 0;

    // Read 256 words (512 bytes) of identity data
    for (int i = 0; i < 256; i++) {
        uint16_t data = inw(port_base + ATA_REG_DATA);
        (void)data;
    }
    
    return 1;
}

static int ata_read_sector(Disk *disk, uint32_t lba, uint8_t *buffer) {
    ATADriverData *data = (ATADriverData*)disk->driver_data;
    uint16_t port_base = data->port_base;
    bool slave = data->slave;
    
    ata_wait_bsy(port_base);
    
    // Select drive and send highest 4 bits of LBA
    outb(port_base + ATA_REG_HDDEVSEL, 0xE0 | (slave << 4) | ((lba >> 24) & 0x0F));
    outb(port_base + ATA_REG_FEATURES, 0x00);
    outb(port_base + ATA_REG_SEC_COUNT0, 1);
    outb(port_base + ATA_REG_LBA0, (uint8_t)(lba));
    outb(port_base + ATA_REG_LBA1, (uint8_t)(lba >> 8));
    outb(port_base + ATA_REG_LBA2, (uint8_t)(lba >> 16));
    outb(port_base + ATA_REG_COMMAND, ATA_CMD_READ_PIO);
    
    ata_wait_bsy(port_base);
    ata_wait_drq(port_base);
    
    uint16_t *ptr = (uint16_t*)buffer;
    for (int i = 0; i < 256; i++) {
        ptr[i] = inw(port_base + ATA_REG_DATA);
    }
    
    return 0; // Success
}

static int ata_write_sector(Disk *disk, uint32_t lba, const uint8_t *buffer) {
    ATADriverData *data = (ATADriverData*)disk->driver_data;
    uint16_t port_base = data->port_base;
    bool slave = data->slave;
    
    ata_wait_bsy(port_base);
    
    outb(port_base + ATA_REG_HDDEVSEL, 0xE0 | (slave << 4) | ((lba >> 24) & 0x0F));
    outb(port_base + ATA_REG_FEATURES, 0x00);
    outb(port_base + ATA_REG_SEC_COUNT0, 1);
    outb(port_base + ATA_REG_LBA0, (uint8_t)(lba));
    outb(port_base + ATA_REG_LBA1, (uint8_t)(lba >> 8));
    outb(port_base + ATA_REG_LBA2, (uint8_t)(lba >> 16));
    outb(port_base + ATA_REG_COMMAND, ATA_CMD_WRITE_PIO);
    
    ata_wait_bsy(port_base);
    ata_wait_drq(port_base);
    
    const uint16_t *ptr = (const uint16_t*)buffer;
    for (int i = 0; i < 256; i++) {
        outw(port_base + ATA_REG_DATA, ptr[i]);
    }
    
    // Flush / Sync
    outb(port_base + ATA_REG_COMMAND, 0xE7); // Cache Flush
    ata_wait_bsy(port_base);
    
    return 0;
}


char disk_get_next_free_letter(void) {
    for (int i = 0; i < MAX_DISKS; i++) {
        char letter = 'A' + i;
        bool used = false;
        for (int j = 0; j < disk_count; j++) {
            if (disks[j]->letter == letter) {
                used = true;
                break;
            }
        }
        if (!used) return letter;
    }
    return 0; // No free letters
}

void disk_register(Disk *disk) {
    if (disk_count >= MAX_DISKS) return;
    
    // Ensure letter is unique
    if (disk->letter == 0) {
        disk->letter = disk_get_next_free_letter();
    }
    
    disks[disk_count++] = disk;
}

void disk_manager_init(void) {
    for (int i = 0; i < MAX_DISKS; i++) {
        disks[i] = NULL;
    }
    disk_count = 0;

    // Register A: (Ramdisk)
    Disk *ramdisk = (Disk*)kmalloc(sizeof(Disk));
    ramdisk->letter = 'A';
    ramdisk->type = DISK_TYPE_RAM;
    ramdisk->is_fat32 = true; // Ramdisk is always formatted
    dm_strcpy(ramdisk->name, "RAM");
    ramdisk->read_sector = ramdisk_read;
    ramdisk->write_sector = ramdisk_write;
    ramdisk->driver_data = NULL;
    ramdisk->partition_lba_offset = 0;
    
    disk_register(ramdisk);
}

Disk* disk_get_by_letter(char letter) {
    // Uppercase
    if (letter >= 'a' && letter <= 'z') letter -= 32;
    
    for (int i = 0; i < disk_count; i++) {
        if (disks[i]->letter == letter) {
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


// === MBR Partition Table Structures ===

typedef struct {
    uint8_t  status;           // 0x80 = bootable, 0x00 = inactive
    uint8_t  chs_first[3];    // CHS of first sector
    uint8_t  type;             // Partition type
    uint8_t  chs_last[3];     // CHS of last sector
    uint32_t lba_start;        // LBA of first sector
    uint32_t sector_count;     // Number of sectors
} __attribute__((packed)) MBR_PartitionEntry;

// FAT32 partition type codes
#define PART_TYPE_FAT32     0x0B
#define PART_TYPE_FAT32_LBA 0x0C

// Check if sector contains a valid FAT32 BPB (Volume Boot Record)
static bool is_fat32_bpb(const uint8_t *sector) {
    // Must have 0xAA55 boot signature
    if (sector[510] != 0x55 || sector[511] != 0xAA) return false;
    
    // Check for FAT32 filesystem string at offset 82
    // "FAT32   " in the fs_type field of the BPB
    if (sector[82] == 'F' && sector[83] == 'A' && sector[84] == 'T' &&
        sector[85] == '3' && sector[86] == '2') {
        return true;
    }
    
    // Also accept if bytes_per_sector is 512 and sectors_per_fat_16 is 0
    // (FAT32 always has sectors_per_fat_16 == 0)
    uint16_t bps = *(uint16_t*)&sector[11];
    uint16_t spf16 = *(uint16_t*)&sector[22];
    uint32_t spf32 = *(uint32_t*)&sector[36];
    if (bps == 512 && spf16 == 0 && spf32 > 0) {
        return true;
    }
    
    return false;
}

// Parse MBR partition table and find a FAT32 partition.
// Sets disk->partition_lba_offset and returns true if found.
static bool detect_fat32_partition(Disk *disk) {
    uint8_t *buffer = (uint8_t*)kmalloc(512);
    if (!buffer) return false;
    
    // Read sector 0 (MBR or raw BPB)
    if (disk->read_sector(disk, 0, buffer) != 0) {
        kfree(buffer);
        return false;
    }
    
    // Must have 0xAA55 boot signature
    if (buffer[510] != 0x55 || buffer[511] != 0xAA) {
        kfree(buffer);
        return false;
    }
    
    // Check MBR partition table entries (4 entries at offset 446)
    MBR_PartitionEntry *partitions = (MBR_PartitionEntry*)&buffer[446];
    
    for (int i = 0; i < 4; i++) {
        if (partitions[i].type == PART_TYPE_FAT32 ||
            partitions[i].type == PART_TYPE_FAT32_LBA) {
            
            uint32_t part_lba = partitions[i].lba_start;
            
            // Read the partition's first sector to verify it's a valid FAT32 BPB
            uint8_t *pbuf = (uint8_t*)kmalloc(512);
            if (!pbuf) { kfree(buffer); return false; }
            
            if (disk->read_sector(disk, part_lba, pbuf) == 0 && is_fat32_bpb(pbuf)) {
                disk->partition_lba_offset = part_lba;
                kfree(pbuf);
                kfree(buffer);
                return true;
            }
            kfree(pbuf);
        }
    }
    
    // Fallback: check if sector 0 itself is a raw FAT32 BPB (no partition table)
    if (is_fat32_bpb(buffer)) {
        disk->partition_lba_offset = 0;
        kfree(buffer);
        return true;
    }
    
    kfree(buffer);
    return false;
}

static void try_add_ata_drive(uint16_t port, bool slave, const char *name) {
    if (ata_identify(port, slave)) {
        Disk *new_disk = (Disk*)kmalloc(sizeof(Disk));
        if (!new_disk) return;
        
        ATADriverData *data = (ATADriverData*)kmalloc(sizeof(ATADriverData));
        data->port_base = port;
        data->slave = slave;
        
        new_disk->letter = 0; // Auto-assign
        new_disk->type = DISK_TYPE_IDE;
        dm_strcpy(new_disk->name, name);
        new_disk->read_sector = ata_read_sector;
        new_disk->write_sector = ata_write_sector;
        new_disk->driver_data = data;
        new_disk->partition_lba_offset = 0;
        
        // Detect FAT32 (with MBR partition support)
        if (detect_fat32_partition(new_disk)) {
            new_disk->is_fat32 = true;
            disk_register(new_disk);
        } else {
            kfree(data);
            kfree(new_disk);
        }
    }
}

void disk_manager_scan(void) {
    // Probe Standard ATA Ports
    try_add_ata_drive(ATA_PRIMARY_IO, false, "IDE1");
    try_add_ata_drive(ATA_PRIMARY_IO, true, "IDE2");
    try_add_ata_drive(ATA_SECONDARY_IO, false, "IDE3");
    try_add_ata_drive(ATA_SECONDARY_IO, true, "IDE4");
}