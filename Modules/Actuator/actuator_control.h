#ifndef ACTUATOR_CONTROL_H
#define ACTUATOR_CONTROL_H

#include <stdint.h>

void actuator_control_init(void);
void actuator_control_poll(uint32_t now);

#endif
