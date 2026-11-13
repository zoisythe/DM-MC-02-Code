/* M2006 与四轮共享 CAN1/UART；必须先初始化四轮，再初始化本模块。 */
#ifndef M2006_CONTROL_H
#define M2006_CONTROL_H
#include <stdint.h>
void m2006_control_init(void);
void m2006_control_poll(void);
/* 共享硬件的分发接口，不改变四轮算法。 */
void m2006_control_set_time(uint32_t now);
void m2006_control_feedback_lost(void);
void m2006_control_feedback(uint32_t header, uint32_t control, uint32_t a, uint32_t b);
#endif
