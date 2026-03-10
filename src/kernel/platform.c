// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include <stdint.h>
#include "limine.h"
#include <stddef.h>
static volatile struct limine_hhdm_request hhdm_request __attribute__((used, section(".requests"))) = {
    .id = LIMINE_HHDM_REQUEST,
    .revision = 0,
    .response = NULL
};
static volatile struct limine_kernel_address_request kernel_addr_request __attribute__((used, section(".requests"))) = {
    .id = LIMINE_KERNEL_ADDRESS_REQUEST,
    .revision = 0,
    .response = NULL
};
uint64_t hhdm_offset = 0;
uint64_t kernel_phys_base = 0;
uint64_t kernel_virt_base = 0;
void platform_init(void) {
    if (hhdm_request.response) { hhdm_offset = hhdm_request.response->offset; }
    if (kernel_addr_request.response) {
        kernel_phys_base = kernel_addr_request.response->physical_base;
        kernel_virt_base = kernel_addr_request.response->virtual_base;
    }

    // Enable FPU and SSE
    uint64_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1ULL << 2); // Clear EM (Emulation)
    cr0 |= (1ULL << 1);  // Set MP (Monitor Coprocessor)
    cr0 |= (1ULL << 5);  // Set NE (Numeric Error)
    asm volatile("mov %0, %%cr0" : : "r"(cr0));

    uint64_t cr4;
    asm volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1ULL << 9);  // Set OSFXSR (FXSAVE/FXRSTOR support)
    cr4 |= (1ULL << 10); // Set OSXMMEXCPT (SIMD exception support)
    asm volatile("mov %0, %%cr4" : : "r"(cr4));

    // Initialize FPU
    asm volatile("fninit");
}
uint64_t p2v(uint64_t phys) { return phys + hhdm_offset; }
uint64_t v2p(uint64_t virt) {
    if (kernel_virt_base && virt >= kernel_virt_base) {
        return virt - kernel_virt_base + kernel_phys_base;
    }
    if (hhdm_offset && virt >= hhdm_offset) {
        return virt - hhdm_offset;
    }
    return virt;
}
void platform_get_cpu_model(char *model) {
    uint32_t brand[12];
    uint32_t eax, ebx, ecx, edx;

    for (uint32_t i = 0; i < 3; i++) {
        asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0x80000002 + i));
        brand[i * 4 + 0] = eax;
        brand[i * 4 + 1] = ebx;
        brand[i * 4 + 2] = ecx;
        brand[i * 4 + 3] = edx;
    }
    
    char *p = (char *)brand;
    for (int i = 0; i < 48; i++) {
        model[i] = p[i];
    }
    model[48] = '\0';
}
