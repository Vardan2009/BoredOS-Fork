// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>

void platform_init(void);
uint64_t p2v(uint64_t phys);
uint64_t v2p(uint64_t virt);
void platform_get_cpu_model(char *model);

#endif
