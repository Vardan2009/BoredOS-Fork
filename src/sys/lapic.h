// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#ifndef LAPIC_H
#define LAPIC_H

#include <stdint.h>

// IPI vector used for scheduling on APs
#define IPI_SCHED_VECTOR 0x41

// Initialize LAPIC access (maps registers via HHDM)
void lapic_init(void);

// Send End-of-Interrupt to the local APIC
void lapic_eoi(void);

// Send a scheduling IPI to all APs (excludes self)
void lapic_send_ipi_all(void);

#endif
