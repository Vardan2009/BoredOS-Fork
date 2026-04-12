// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#ifndef PS2_H
#define PS2_H

#include <stdint.h>

void ps2_init(void);
#include "process.h"

uint64_t timer_handler(registers_t *regs);
uint64_t keyboard_handler(registers_t *regs);
uint64_t mouse_handler(registers_t *regs);

bool ps2_shift_pressed(void);

#endif
