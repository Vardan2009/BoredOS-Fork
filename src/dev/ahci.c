// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include "ahci.h"
#include "pci.h"
#include "disk.h"
#include "memory_manager.h"
#include "paging.h"
#include "io.h"
#include <stddef.h>
#include "../sys/spinlock.h"

extern void serial_write(const char *str);
extern void serial_write_num(uint64_t num);
extern void serial_write_hex(uint32_t val);

// ============================================================================
// AHCI Driver State
// ============================================================================

static HBA_MEM *abar = NULL;               // MMIO-mapped AHCI Base Address
static bool ahci_initialized = false;
static int active_port_count = 0;

#define MAX_AHCI_PORTS 32

typedef struct {
    bool active;
    int port_num;
    HBA_PORT *port;
    HBA_CMD_HEADER *cmd_list;   // 1KB, 1KB aligned
    void *fis_base;             // 256B, 256B aligned
    HBA_CMD_TBL *cmd_tbl;      // Command table for slot 0
    spinlock_t lock;           // Port-level lock for thread-safety
} ahci_port_state_t;

static ahci_port_state_t ports[MAX_AHCI_PORTS];

// ============================================================================
// String Helpers
// ============================================================================

static void ahci_strcpy(char *d, const char *s) {
    while ((*d++ = *s++));
}

// Kernel virtual to physical address conversion
extern uint64_t v2p(uint64_t vaddr);

// ============================================================================
// Port Setup
// ============================================================================

static void ahci_stop_cmd(HBA_PORT *port) {
    // Clear ST (Start)
    port->cmd &= ~HBA_PORT_CMD_ST;

    // Clear FRE (FIS Receive Enable)
    port->cmd &= ~HBA_PORT_CMD_FRE;

    // Wait until FR and CR clear
    int timeout = 500000;
    while (timeout-- > 0) {
        if (port->cmd & HBA_PORT_CMD_FR) continue;
        if (port->cmd & HBA_PORT_CMD_CR) continue;
        break;
    }
}

static void ahci_start_cmd(HBA_PORT *port) {
    // Wait until CR clears
    while (port->cmd & HBA_PORT_CMD_CR);

    // Set FRE and ST
    port->cmd |= HBA_PORT_CMD_FRE;
    port->cmd |= HBA_PORT_CMD_ST;
}

static int ahci_check_port_type(HBA_PORT *port) {
    uint32_t ssts = port->ssts;
    uint8_t ipm = (ssts >> 8) & 0x0F;
    uint8_t det = ssts & 0x0F;

    if (det != 3) return -1;   // No device detected
    if (ipm != 1) return -1;   // Not in active state

    switch (port->sig) {
        case SATA_SIG_ATA:   return 0;   // SATA drive
        case SATA_SIG_ATAPI: return 1;   // SATAPI drive
        case SATA_SIG_SEMB:  return 2;   // SEMB
        case SATA_SIG_PM:    return 3;   // Port multiplier
        default:             return -1;
    }
}

static void ahci_port_rebase(ahci_port_state_t *ps) {
    HBA_PORT *port = ps->port;

    ahci_stop_cmd(port);

    // Allocate command list (1KB, 1024-byte aligned)
    ps->cmd_list = (HBA_CMD_HEADER*)kmalloc_aligned(1024, 1024);
    if (!ps->cmd_list) return;
    mem_memset(ps->cmd_list, 0, 1024);

    uint64_t clb_phys = v2p((uint64_t)ps->cmd_list);
    port->clb = (uint32_t)(clb_phys & 0xFFFFFFFF);
    port->clbu = (uint32_t)(clb_phys >> 32);

    // Allocate FIS receive area (256 bytes, 256-byte aligned)
    ps->fis_base = kmalloc_aligned(256, 256);
    if (!ps->fis_base) return;
    mem_memset(ps->fis_base, 0, 256);

    uint64_t fb_phys = v2p((uint64_t)ps->fis_base);
    port->fb = (uint32_t)(fb_phys & 0xFFFFFFFF);
    port->fbu = (uint32_t)(fb_phys >> 32);

    // Allocate command table for slot 0 (256-byte aligned, room for 8 PRDT entries)
    int cmd_tbl_size = sizeof(HBA_CMD_TBL) + 8 * sizeof(HBA_PRDT_ENTRY);
    ps->cmd_tbl = (HBA_CMD_TBL*)kmalloc_aligned(cmd_tbl_size, 256);
    if (!ps->cmd_tbl) return;
    mem_memset(ps->cmd_tbl, 0, cmd_tbl_size);

    // Set command header 0 to point to our command table
    uint64_t ctba_phys = v2p((uint64_t)ps->cmd_tbl);
    ps->cmd_list[0].ctba = (uint32_t)(ctba_phys & 0xFFFFFFFF);
    ps->cmd_list[0].ctbau = (uint32_t)(ctba_phys >> 32);
    ps->cmd_list[0].prdtl = 1;  // 1 PRDT entry default

    // Clear error and interrupt status
    port->serr = 0xFFFFFFFF;
    port->is = 0xFFFFFFFF;

    ahci_start_cmd(port);
}

// ============================================================================
// Sector I/O
// ============================================================================

static int ahci_find_free_slot(HBA_PORT *port) {
    uint32_t slots = (port->sact | port->ci);
    for (int i = 0; i < 32; i++) {
        if (!(slots & (1 << i))) return i;
    }
    return -1;
}

int ahci_read_sectors(int port_num, uint64_t lba, uint32_t count, uint8_t *buffer) {
    if (!ahci_initialized || port_num < 0 || port_num >= MAX_AHCI_PORTS) return -1;
    ahci_port_state_t *ps = &ports[port_num];
    if (!ps->active) return -1;

    uint64_t rflags = spinlock_acquire_irqsave(&ps->lock);
    HBA_PORT *port = ps->port;

    // Clear any pending interrupts/errors
    port->is = 0xFFFFFFFF;

    int slot = ahci_find_free_slot(port);
    if (slot < 0) return -1;

    HBA_CMD_HEADER *cmd_hdr = &ps->cmd_list[slot];
    cmd_hdr->cfl = sizeof(FIS_REG_H2D) / sizeof(uint32_t);
    cmd_hdr->w = 0;   // Read
    cmd_hdr->prdtl = 1;

    HBA_CMD_TBL *cmd_tbl = ps->cmd_tbl;
    mem_memset(cmd_tbl, 0, sizeof(HBA_CMD_TBL) + sizeof(HBA_PRDT_ENTRY));

    // Setup PRDT
    uint64_t buf_phys = v2p((uint64_t)buffer);
    cmd_tbl->prdt[0].dba = (uint32_t)(buf_phys & 0xFFFFFFFF);
    cmd_tbl->prdt[0].dbau = (uint32_t)(buf_phys >> 32);
    cmd_tbl->prdt[0].dbc = (count * 512) - 1;   // 0-based byte count
    cmd_tbl->prdt[0].i = 1;

    // Setup Command FIS
    FIS_REG_H2D *fis = (FIS_REG_H2D*)&cmd_tbl->cfis;
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->c = 1;  // Command
    fis->command = ATA_CMD_READ_DMA_EX;

    fis->lba0 = (uint8_t)(lba);
    fis->lba1 = (uint8_t)(lba >> 8);
    fis->lba2 = (uint8_t)(lba >> 16);
    fis->device = 1 << 6;  // LBA mode
    fis->lba3 = (uint8_t)(lba >> 24);
    fis->lba4 = (uint8_t)(lba >> 32);
    fis->lba5 = (uint8_t)(lba >> 40);

    fis->countl = (uint8_t)(count);
    fis->counth = (uint8_t)(count >> 8);

    // Issue command
    port->ci = (1 << slot);

    // Wait for completion
    int timeout = 1000000;
    while (timeout-- > 0) {
        if (!(port->ci & (1 << slot))) break;
        if (port->is & (1 << 30)) {  // Task File Error
            serial_write("\n");
            spinlock_release_irqrestore(&ps->lock, rflags);
            return -1;
        }
    }

    if (timeout <= 0) {
        serial_write("[AHCI] Read timeout on port ");
        serial_write_num(port_num);
        serial_write("\n");
        spinlock_release_irqrestore(&ps->lock, rflags);
        return -1;
    }

    spinlock_release_irqrestore(&ps->lock, rflags);
    return 0;
}

int ahci_write_sectors(int port_num, uint64_t lba, uint32_t count, const uint8_t *buffer) {
    if (!ahci_initialized || port_num < 0 || port_num >= MAX_AHCI_PORTS) return -1;
    ahci_port_state_t *ps = &ports[port_num];
    if (!ps->active) return -1;

    uint64_t rflags = spinlock_acquire_irqsave(&ps->lock);
    HBA_PORT *port = ps->port;

    port->is = 0xFFFFFFFF;

    int slot = ahci_find_free_slot(port);
    if (slot < 0) return -1;

    HBA_CMD_HEADER *cmd_hdr = &ps->cmd_list[slot];
    cmd_hdr->cfl = sizeof(FIS_REG_H2D) / sizeof(uint32_t);
    cmd_hdr->w = 1;   // Write
    cmd_hdr->prdtl = 1;

    HBA_CMD_TBL *cmd_tbl = ps->cmd_tbl;
    mem_memset(cmd_tbl, 0, sizeof(HBA_CMD_TBL) + sizeof(HBA_PRDT_ENTRY));

    uint64_t buf_phys = v2p((uint64_t)buffer);
    cmd_tbl->prdt[0].dba = (uint32_t)(buf_phys & 0xFFFFFFFF);
    cmd_tbl->prdt[0].dbau = (uint32_t)(buf_phys >> 32);
    cmd_tbl->prdt[0].dbc = (count * 512) - 1;
    cmd_tbl->prdt[0].i = 1;

    FIS_REG_H2D *fis = (FIS_REG_H2D*)&cmd_tbl->cfis;
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->c = 1;
    fis->command = ATA_CMD_WRITE_DMA_EX;

    fis->lba0 = (uint8_t)(lba);
    fis->lba1 = (uint8_t)(lba >> 8);
    fis->lba2 = (uint8_t)(lba >> 16);
    fis->device = 1 << 6;
    fis->lba3 = (uint8_t)(lba >> 24);
    fis->lba4 = (uint8_t)(lba >> 32);
    fis->lba5 = (uint8_t)(lba >> 40);

    fis->countl = (uint8_t)(count);
    fis->counth = (uint8_t)(count >> 8);

    port->ci = (1 << slot);

    int timeout = 1000000;
    while (timeout-- > 0) {
        if (!(port->ci & (1 << slot))) break;
        if (port->is & (1 << 30)) {
            serial_write("[AHCI] Write error on port ");
            serial_write_num(port_num);
            serial_write("\n");
            spinlock_release_irqrestore(&ps->lock, rflags);
            return -1;
        }
    }

    if (timeout <= 0) {
        serial_write("[AHCI] Write timeout on port ");
        serial_write_num(port_num);
        serial_write("\n");
        spinlock_release_irqrestore(&ps->lock, rflags);
        return -1;
    }

    spinlock_release_irqrestore(&ps->lock, rflags);
    return 0;
}

// ============================================================================
// AHCI Disk Integration — wrap AHCI into Disk read/write_sector
// ============================================================================

typedef struct {
    int ahci_port;
} AHCIDriverData;

static int ahci_disk_read_sector(Disk *disk, uint32_t sector, uint8_t *buffer) {
    AHCIDriverData *data = (AHCIDriverData*)disk->driver_data;

    // For partitions, add offset and use parent's port
    if (disk->is_partition && disk->parent) {
        AHCIDriverData *pdata = (AHCIDriverData*)disk->parent->driver_data;
        return ahci_read_sectors(pdata->ahci_port,
                                 (uint64_t)sector + disk->partition_lba_offset, 1, buffer);
    }

    return ahci_read_sectors(data->ahci_port, (uint64_t)sector, 1, buffer);
}

static int ahci_disk_write_sector(Disk *disk, uint32_t sector, const uint8_t *buffer) {
    AHCIDriverData *data = (AHCIDriverData*)disk->driver_data;

    if (disk->is_partition && disk->parent) {
        AHCIDriverData *pdata = (AHCIDriverData*)disk->parent->driver_data;
        return ahci_write_sectors(pdata->ahci_port,
                                  (uint64_t)sector + disk->partition_lba_offset, 1, buffer);
    }

    return ahci_write_sectors(data->ahci_port, (uint64_t)sector, 1, buffer);
}

// ============================================================================
// Initialization
// ============================================================================

int ahci_get_port_count(void) {
    return active_port_count;
}

bool ahci_port_is_active(int port_num) {
    if (port_num < 0 || port_num >= MAX_AHCI_PORTS) return false;
    return ports[port_num].active;
}

void ahci_init(void) {
    serial_write("[AHCI] Scanning PCI for AHCI controller...\n");

    // Find AHCI controller (Class 0x01, Subclass 0x06)
    pci_device_t pci_dev;
    if (!pci_find_device_by_class(PCI_CLASS_MASS_STORAGE, PCI_SUBCLASS_SATA, &pci_dev)) {
        serial_write("[AHCI] No AHCI controller found\n");
        return;
    }

    serial_write("[AHCI] Found AHCI controller (");
    serial_write("vendor=0x");
    serial_write_hex(pci_dev.vendor_id);
    serial_write(", device=0x");
    serial_write_hex(pci_dev.device_id);
    serial_write(")\n");

    // Enable Bus Mastering and MMIO
    pci_enable_bus_mastering(&pci_dev);
    pci_enable_mmio(&pci_dev);

    // Read ABAR (BAR5)
    uint32_t abar_raw = pci_get_bar(&pci_dev, 5);
    uint64_t abar_phys = abar_raw & 0xFFFFF000;  // Mask out lower bits

    if (abar_phys == 0) {
        serial_write("[AHCI] Invalid ABAR address\n");
        return;
    }

    serial_write("[AHCI] ABAR physical address: 0x");
    serial_write_hex((uint32_t)abar_phys);
    serial_write("\n");

    // Map ABAR region into kernel virtual address space
    // Identity-map several pages to cover the HBA memory (at least 0x1100 bytes)
    uint64_t abar_virt = abar_phys; // Use identity mapping
    for (uint64_t offset = 0; offset < 0x2000; offset += 4096) {
        paging_map_page(paging_get_pml4_phys(), abar_virt + offset,
                        abar_phys + offset,
                        PT_PRESENT | PT_RW | PT_CACHE_DISABLE);
    }

    abar = (HBA_MEM*)abar_virt;

    // Enable AHCI mode
    abar->ghc |= (1 << 31);  // AE (AHCI Enable)

    serial_write("[AHCI] Version: ");
    serial_write_num(abar->vs >> 16);
    serial_write(".");
    serial_write_num(abar->vs & 0xFFFF);
    serial_write("\n");

    // Probe ports
    uint32_t pi = abar->pi;
    active_port_count = 0;

    for (int i = 0; i < 32; i++) {
        ports[i].active = false;

        HBA_PORT *port = &abar->ports[i];
        ports[i].lock = SPINLOCK_INIT;
        int type = ahci_check_port_type(port);

        if (type == 0) { // SATA drive
            serial_write("[AHCI] Port ");
            serial_write_num(i);
            serial_write(": SATA drive detected\n");

            ports[i].port_num = i;
            ports[i].port = port;
            ahci_port_rebase(&ports[i]);
            ports[i].active = true;
            active_port_count++;

            // Register as a block device
            Disk *disk = (Disk*)kmalloc(sizeof(Disk));
            if (disk) {
                AHCIDriverData *drv = (AHCIDriverData*)kmalloc(sizeof(AHCIDriverData));
                drv->ahci_port = i;

                disk->devname[0] = 0; // Auto-assign
                disk->type = DISK_TYPE_SATA;
                ahci_strcpy(disk->label, "SATA Drive");
                disk->read_sector = ahci_disk_read_sector;
                disk->write_sector = ahci_disk_write_sector;
                disk->driver_data = drv;
                disk->partition_lba_offset = 0;
                disk->total_sectors = 0;
                disk->parent = NULL;
                disk->is_partition = false;
                disk->is_fat32 = false;

                disk_register(disk);

                // Let disk_manager parse partitions — we call a scan function
                extern void disk_manager_scan_partitions(Disk *disk);
                // Inline MBR parse for this disk
                extern void serial_write(const char *str);
                serial_write("[AHCI] Probing partitions on /dev/");
                serial_write(disk->devname);
                serial_write("...\n");

                // Read MBR sector 0
                uint8_t *mbr_buf = (uint8_t*)kmalloc(512);
                if (mbr_buf) {
                    if (ahci_disk_read_sector(disk, 0, mbr_buf) == 0) {
                        if (mbr_buf[510] == 0x55 && mbr_buf[511] == 0xAA) {
                            // Parse MBR partition table
                            typedef struct {
                                uint8_t  status;
                                uint8_t  chs_first[3];
                                uint8_t  type;
                                uint8_t  chs_last[3];
                                uint32_t lba_start;
                                uint32_t sector_count;
                            } __attribute__((packed)) MBR_Part;

                            MBR_Part *parts = (MBR_Part*)&mbr_buf[446];
                            int pn = 1;
                            for (int p = 0; p < 4; p++) {
                                if (parts[p].type == 0x00 || parts[p].sector_count == 0)
                                    continue;

                                bool fat32 = false;
                                if (parts[p].type == 0x0B || parts[p].type == 0x0C) {
                                    // Verify BPB
                                    uint8_t *pbuf = (uint8_t*)kmalloc(512);
                                    if (pbuf) {
                                        if (ahci_disk_read_sector(disk, parts[p].lba_start, pbuf) == 0) {
                                            if (pbuf[510] == 0x55 && pbuf[511] == 0xAA) {
                                                uint16_t bps = *(uint16_t*)&pbuf[11];
                                                uint16_t spf16 = *(uint16_t*)&pbuf[22];
                                                uint32_t spf32 = *(uint32_t*)&pbuf[36];
                                                if (bps == 512 && spf16 == 0 && spf32 > 0)
                                                    fat32 = true;
                                            }
                                        }
                                        kfree(pbuf);
                                    }
                                }

                                disk_register_partition(disk, parts[p].lba_start,
                                                        parts[p].sector_count, fat32, pn);
                                pn++;
                            }

                            // Fallback: raw FAT32
                            if (pn == 1) {
                                uint16_t bps = *(uint16_t*)&mbr_buf[11];
                                uint16_t spf16 = *(uint16_t*)&mbr_buf[22];
                                uint32_t spf32 = *(uint32_t*)&mbr_buf[36];
                                if (bps == 512 && spf16 == 0 && spf32 > 0) {
                                    disk->is_fat32 = true;
                                    disk->partition_lba_offset = 0;
                                    serial_write("[AHCI] Raw FAT32 volume detected\n");
                                }
                            }
                        }
                    }
                    kfree(mbr_buf);
                }
            }
        } else if (type == 1) {
            serial_write("[AHCI] Port ");
            serial_write_num(i);
            serial_write(": SATAPI drive (ignored)\n");
        }
    }

    if (active_port_count > 0) {
        ahci_initialized = true;
        serial_write("[AHCI] Initialization complete: ");
        serial_write_num(active_port_count);
        serial_write(" SATA port(s) active\n");
    } else {
        serial_write("[AHCI] No active SATA ports found\n");
    }
}
