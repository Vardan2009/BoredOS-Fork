// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include "smp.h"
#include "limine.h"
#include "memory_manager.h"
#include "gdt.h"
#include "idt.h"
#include "platform.h"
#include "paging.h"

extern void serial_write(const char *str);
extern void serial_write_num(uint32_t n);
extern void serial_write_hex(uint64_t n);

// --- Dynamically allocated per-CPU state ---
static cpu_state_t *cpu_states = NULL;  // Array[cpu_count]
static uint32_t total_cpus = 0;
static uint32_t bsp_lapic_id = 0;

// Get LAPIC ID via CPUID leaf 0x01 (works on all x86_64)
static uint32_t read_lapic_id(void) {
    uint32_t eax, ebx, ecx, edx;
    asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1));
    return (ebx >> 24) & 0xFF;
}

uint32_t smp_this_cpu_id(void) {
    if (total_cpus <= 1) return 0;
    uint32_t lapic = read_lapic_id();
    for (uint32_t i = 0; i < total_cpus; i++) {
        if (cpu_states[i].lapic_id == lapic) return i;
    }
    return 0; // Fallback to BSP
}

uint32_t smp_cpu_count(void) {
    return total_cpus;
}

cpu_state_t *smp_get_cpu(uint32_t cpu_id) {
    if (cpu_id >= total_cpus) return NULL;
    return &cpu_states[cpu_id];
}

// --- AP Entry Point ---
// Called by Limine on each Application Processor.
// The limine_smp_info* is passed as a parameter.
static void ap_entry(struct limine_smp_info *info) {
    // 1. Figure out which CPU we are
    uint32_t my_id = (uint32_t)(info->extra_argument);

    // 2. Enable FPU/SSE on this core (same as BSP does in platform_init)
    uint64_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1ULL << 2); // Clear EM
    cr0 |= (1ULL << 1);  // Set MP
    cr0 |= (1ULL << 5);  // Set NE
    asm volatile("mov %0, %%cr0" : : "r"(cr0));

    uint64_t cr4;
    asm volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1ULL << 9);  // OSFXSR
    cr4 |= (1ULL << 10); // OSXMMEXCPT
    asm volatile("mov %0, %%cr4" : : "r"(cr4));
    asm volatile("fninit");

    // 3. Load the shared GDT (same one the BSP uses — all CPUs share kernel mappings)
    extern struct gdt_ptr gdtr;
    asm volatile("lgdt %0" : : "m"(gdtr));

    // 4. Load per-CPU TSS
    gdt_load_ap_tss(my_id);

    // 5. Load the shared IDT
    extern void idt_load(void);
    idt_load();

    // 6. Load the kernel page tables (same CR3 as BSP — shared kernel space)
    uint64_t kernel_cr3 = paging_get_pml4_phys();
    asm volatile("mov %0, %%cr3" : : "r"(kernel_cr3));

    // 7. Mark ourselves as online
    cpu_states[my_id].online = true;

    serial_write("[SMP] AP ");
    serial_write_num(my_id);
    serial_write(" online (LAPIC ");
    serial_write_num(cpu_states[my_id].lapic_id);
    serial_write(")\n");

    // 8. Enable interrupts and enter idle halt loop.
    // APs will be woken by scheduling IPIs from BSP (vector 0x41).
    // The IPI handler does context switching for this CPU's processes.
    asm volatile("sti");

    // Idle loop — APs halt and wait for IPI
    for (;;) { asm volatile("hlt"); }
}

// --- SMP Initialization ---
uint32_t smp_init(struct limine_smp_response *smp_resp) {
    if (!smp_resp || smp_resp->cpu_count <= 1) {
        // Single CPU system — just set up the BSP entry
        total_cpus = 1;
        cpu_states = (cpu_state_t *)kmalloc(sizeof(cpu_state_t));
        if (!cpu_states) return 1;
        extern void mem_memset(void *, int, size_t);
        mem_memset(cpu_states, 0, sizeof(cpu_state_t));
        cpu_states[0].cpu_id = 0;
        cpu_states[0].lapic_id = read_lapic_id();
        cpu_states[0].online = true;
        serial_write("[SMP] Single CPU mode\n");
        return 1;
    }

    total_cpus = (uint32_t)smp_resp->cpu_count;
    bsp_lapic_id = smp_resp->bsp_lapic_id;

    serial_write("[SMP] Detected ");
    serial_write_num(total_cpus);
    serial_write(" CPUs. BSP LAPIC ID: ");
    serial_write_num(bsp_lapic_id);
    serial_write("\n");

    // Allocate per-CPU state array
    cpu_states = (cpu_state_t *)kmalloc(total_cpus * sizeof(cpu_state_t));
    if (!cpu_states) {
        serial_write("[SMP] ERROR: Failed to allocate CPU state array!\n");
        total_cpus = 1;
        return 1;
    }
    extern void mem_memset(void *, int, size_t);
    mem_memset(cpu_states, 0, total_cpus * sizeof(cpu_state_t));

    // Initialize per-CPU GDT/TSS entries for all CPUs
    gdt_init_ap_tss(total_cpus);

    // Fill in CPU state and start APs
    uint32_t bsp_index = 0;
    for (uint32_t i = 0; i < total_cpus; i++) {
        struct limine_smp_info *cpu = smp_resp->cpus[i];
        cpu_states[i].cpu_id = i;
        cpu_states[i].lapic_id = cpu->lapic_id;

        if (cpu->lapic_id == bsp_lapic_id) {
            // This is the BSP — already running
            cpu_states[i].online = true;
            bsp_index = i;
            serial_write("[SMP] BSP CPU ");
            serial_write_num(i);
            serial_write(" (LAPIC ");
            serial_write_num(cpu->lapic_id);
            serial_write(") online\n");
        } else {
            // Allocate a kernel stack for this AP
            void *ap_stack = kmalloc_aligned(65536, 65536);
            if (!ap_stack) {
                serial_write("[SMP] ERROR: Failed to allocate AP stack!\n");
                continue;
            }
            cpu_states[i].kernel_stack = (uint64_t)ap_stack + 65536;
            cpu_states[i].kernel_stack_alloc = ap_stack;
            cpu_states[i].online = false;

            // Set extra_argument so the AP knows its index
            cpu->extra_argument = i;

            // Tell Limine to start this AP. Limine sets up the AP's stack
            // from extra_argument's stack, but we need the goto_address.
            // Limine will jump to ap_entry with the AP's limine_smp_info*.
            // Important: Limine creates a temporary stack for the AP, and the
            // goto_address is where the AP starts executing.

            serial_write("[SMP] Starting AP ");
            serial_write_num(i);
            serial_write(" (LAPIC ");
            serial_write_num(cpu->lapic_id);
            serial_write(")...\n");

            // This atomic write triggers the AP to start executing at ap_entry
            __atomic_store_n(&cpu->goto_address, ap_entry, __ATOMIC_SEQ_CST);
        }
    }

    // Wait for all APs to come online (with timeout)
    volatile uint32_t timeout = 10000000;
    uint32_t online_count = 0;
    while (timeout-- > 0) {
        online_count = 0;
        for (uint32_t i = 0; i < total_cpus; i++) {
            if (cpu_states[i].online) online_count++;
        }
        if (online_count == total_cpus) break;
        asm volatile("pause");
    }

    serial_write("[SMP] All ");
    serial_write_num(online_count);
    serial_write(" of ");
    serial_write_num(total_cpus);
    serial_write(" CPUs online\n");

    return online_count;
}
