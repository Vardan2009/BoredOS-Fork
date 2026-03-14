// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include "virtio_net.h"
#include "io.h"
#include "kutils.h"
#include "platform.h"

#define VIRTIO_PCI_HOST_FEATURES  0x00
#define VIRTIO_PCI_GUEST_FEATURES 0x04
#define VIRTIO_PCI_QUEUE_PFN      0x08
#define VIRTIO_PCI_QUEUE_SIZE     0x0C
#define VIRTIO_PCI_QUEUE_SEL      0x0E
#define VIRTIO_PCI_QUEUE_NOTIFY   0x10
#define VIRTIO_PCI_STATUS         0x12
#define VIRTIO_PCI_ISR            0x13
#define VIRTIO_PCI_CONFIG         0x14

#define VRING_DESC_F_NEXT  1
#define VRING_DESC_F_WRITE 2

struct vq_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed));

struct vq_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[256];
} __attribute__((packed));

struct vq_used_elem {
    uint32_t id;
    uint32_t len;
} __attribute__((packed));

struct vq_used {
    uint16_t flags;
    uint16_t idx;
    struct vq_used_elem ring[256];
} __attribute__((packed));

struct virtqueue {
    struct vq_desc* desc;
    struct vq_avail* avail;
    struct vq_used* used;
    uint16_t q_size;
    uint16_t last_used_idx;
    uint16_t last_avail_idx; // Software tracking of idx
};

// Virtio Network header (typically 12 bytes if VIRTIO_NET_F_MRG_RXBUF is NOT set, 10 bytes legacy without MRG)
struct virtio_net_hdr {
    uint8_t flags;
    uint8_t gso_type;
    uint16_t hdr_len;
    uint16_t gso_size;
    uint16_t csum_start;
    uint16_t csum_offset;
    // uint16_t num_buffers; // if MRG_RXBUF enabled
} __attribute__((packed));

static uint16_t io_base = 0;
static int virtio_initialized = 0;
static uint8_t mac_addr[6];

static struct virtqueue rx_vq;
static struct virtqueue tx_vq;

// Memory buffers for queues
static uint8_t rx_ring_mem[16384] __attribute__((aligned(4096)));
static uint8_t tx_ring_mem[16384] __attribute__((aligned(4096)));

static uint8_t rx_buffers[256][2048];
static struct virtio_net_hdr tx_hdr[256];
static uint8_t tx_buffers[256][2048];

static void virtqueue_init(struct virtqueue* vq, uint8_t* mem, uint16_t qsize) {
    vq->q_size = qsize;
    vq->last_used_idx = 0;
    vq->last_avail_idx = 0;

    vq->desc = (struct vq_desc*)mem;
    vq->avail = (struct vq_avail*)(mem + qsize * sizeof(struct vq_desc));
    
    // used ring is aligned to 4096 after avail
    uintptr_t avail_end = (uintptr_t)vq->avail + sizeof(struct vq_avail) + sizeof(uint16_t); // avail_event
    uintptr_t used_start = (avail_end + 4095) & ~4095;
    vq->used = (struct vq_used*)used_start;
    
    k_memset(mem, 0, 16384);
}

int virtio_net_init(pci_device_t* pci_dev) {
    if (virtio_initialized) return 0;

    uint32_t bar0 = pci_read_config(pci_dev->bus, pci_dev->device, pci_dev->function, 0x10);
    if (!(bar0 & 1)) {
        // We only support legacy I/O virtio
        return -1;
    }
    
    // Enable Bus Master + I/O Space
    uint32_t command = pci_read_config(pci_dev->bus, pci_dev->device, pci_dev->function, 0x04);
    pci_write_config(pci_dev->bus, pci_dev->device, pci_dev->function, 0x04, command | (1 << 2) | (1 << 0));

    io_base = bar0 & ~3;

    extern void serial_write(const char *str);
    serial_write("[VIRTIO-NET] I/O Base: 0x");
    char hex_buf[32]; k_itoa_hex(io_base, hex_buf); serial_write(hex_buf); serial_write("\n");

    // Reset device
    outb(io_base + VIRTIO_PCI_STATUS, 0);
    
    // Acknowledge + Driver
    outb(io_base + VIRTIO_PCI_STATUS, 1 | 2);

    // Read features (negotiate MAC)
    uint32_t features = inl(io_base + VIRTIO_PCI_HOST_FEATURES);
    outl(io_base + VIRTIO_PCI_GUEST_FEATURES, features & 0x01000020); // MAC + STATUS

    // Setup RX Virtqueue (Index 0)
    outw(io_base + VIRTIO_PCI_QUEUE_SEL, 0);
    uint16_t rx_qsize = inw(io_base + VIRTIO_PCI_QUEUE_SIZE);
    if (rx_qsize == 0) return -1;
    if (rx_qsize > 256) rx_qsize = 256;
    virtqueue_init(&rx_vq, rx_ring_mem, rx_qsize);
    outl(io_base + VIRTIO_PCI_QUEUE_PFN, v2p((uint64_t)(uintptr_t)rx_ring_mem) / 4096);

    // Pre-fill RX ring with buffers
    for (int i = 0; i < rx_qsize; i++) {
        rx_vq.desc[i].addr = v2p((uint64_t)(uintptr_t)rx_buffers[i]);
        rx_vq.desc[i].len = 2048;
        rx_vq.desc[i].flags = VRING_DESC_F_WRITE;
        rx_vq.desc[i].next = 0;
        
        rx_vq.avail->ring[i] = i;
    }
    rx_vq.avail->idx = rx_qsize;
    rx_vq.last_avail_idx = rx_qsize;

    // Setup TX Virtqueue (Index 1)
    outw(io_base + VIRTIO_PCI_QUEUE_SEL, 1);
    uint16_t tx_qsize = inw(io_base + VIRTIO_PCI_QUEUE_SIZE);
    if (tx_qsize > 256) tx_qsize = 256;
    virtqueue_init(&tx_vq, tx_ring_mem, tx_qsize);
    outl(io_base + VIRTIO_PCI_QUEUE_PFN, v2p((uint64_t)(uintptr_t)tx_ring_mem) / 4096);

    // Read MAC
    for (int i = 0; i < 6; i++) {
        mac_addr[i] = inb(io_base + VIRTIO_PCI_CONFIG + i);
    }

    serial_write("[VIRTIO-NET] MAC: ");
    for(int i=0; i<6; i++) {
        char buf[4];
        k_itoa_hex(mac_addr[i], buf);
        serial_write(buf);
        if(i<5) serial_write(":");
    }
    serial_write("\n");

    // Device OK
    outb(io_base + VIRTIO_PCI_STATUS, 1 | 2 | 4);

    // Kick RX Queue
    outw(io_base + VIRTIO_PCI_QUEUE_NOTIFY, 0);

    virtio_initialized = 1;
    return 0;
}

int virtio_net_send_packet(const void* data, size_t length) {
    if (!virtio_initialized) return -1;
    if (length > 1514) return -1;

    uint16_t head = tx_vq.last_avail_idx % tx_vq.q_size;
    uint16_t d_idx = head * 2; // We use 2 descriptors per packet (Header, Data)

    // Setup header
    k_memset(&tx_hdr[head], 0, sizeof(struct virtio_net_hdr));
    tx_vq.desc[d_idx].addr = v2p((uint64_t)(uintptr_t)&tx_hdr[head]);
    tx_vq.desc[d_idx].len = sizeof(struct virtio_net_hdr);
    tx_vq.desc[d_idx].flags = VRING_DESC_F_NEXT;
    tx_vq.desc[d_idx].next = d_idx + 1;

    // Setup data
    uint8_t* src = (uint8_t*)data;
    for (size_t i = 0; i < length; i++) tx_buffers[head][i] = src[i];
    
    tx_vq.desc[d_idx + 1].addr = v2p((uint64_t)(uintptr_t)tx_buffers[head]);
    tx_vq.desc[d_idx + 1].len = length;
    tx_vq.desc[d_idx + 1].flags = 0; // Not NEXT, Not WRITE
    
    tx_vq.avail->ring[tx_vq.avail->idx % tx_vq.q_size] = d_idx;

    __asm__ __volatile__ ("mfence"); // Memory barrier
    tx_vq.avail->idx++;
    __asm__ __volatile__ ("mfence");
    
    tx_vq.last_avail_idx++;

    // Notify device
    outw(io_base + VIRTIO_PCI_QUEUE_NOTIFY, 1);

    return 0;
}

int virtio_net_receive_packet(void* buffer, size_t buffer_size) {
    if (!virtio_initialized) return 0;

    if (rx_vq.last_used_idx == rx_vq.used->idx) {
        return 0; // No new packets
    }

    uint16_t u_idx = rx_vq.last_used_idx % rx_vq.q_size;
    uint32_t d_idx = rx_vq.used->ring[u_idx].id;
    uint32_t len = rx_vq.used->ring[u_idx].len;

    // The length includes the virtio_net_hdr (10 bytes)
    // We must strip it for the caller
    uint16_t net_len = 0;
    if (len > sizeof(struct virtio_net_hdr)) {
        net_len = len - sizeof(struct virtio_net_hdr);
        if (net_len > buffer_size) net_len = buffer_size;
        
        uint8_t* pkt = rx_buffers[d_idx] + sizeof(struct virtio_net_hdr);
        uint8_t* dest = (uint8_t*)buffer;
        
        for (int i = 0; i < net_len; i++) {
            dest[i] = pkt[i];
        }
    }

    // Put buffer back directly in avail ring
    rx_vq.avail->ring[rx_vq.avail->idx % rx_vq.q_size] = d_idx;
    
    __asm__ __volatile__ ("mfence");
    rx_vq.avail->idx++;
    rx_vq.last_used_idx++;
    
    __asm__ __volatile__ ("mfence");
    outw(io_base + VIRTIO_PCI_QUEUE_NOTIFY, 0);

    return net_len;
}

int virtio_net_get_mac(uint8_t* mac_out) {
    if (!virtio_initialized) return -1;
    for (int i = 0; i < 6; i++) {
        mac_out[i] = mac_addr[i];
    }
    return 0;
}
