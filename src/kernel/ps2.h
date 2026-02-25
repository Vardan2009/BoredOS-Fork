#ifndef PS2_H
#define PS2_H

#include <stdint.h>

void ps2_init(void);
uint64_t timer_handler(uint64_t rsp);
uint64_t keyboard_handler(uint64_t rsp);
uint64_t mouse_handler(uint64_t rsp);

#endif
