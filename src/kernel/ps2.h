#ifndef PS2_H
#define PS2_H

#include <stdint.h>

void ps2_init(void);
#include "process.h"

uint64_t timer_handler(registers_t *regs);
uint64_t keyboard_handler(registers_t *regs);
uint64_t mouse_handler(registers_t *regs);

#endif
