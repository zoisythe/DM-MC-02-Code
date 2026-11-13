/* M2006 的 CH5 / VRA 绝对位置输入，不使用 SWB/SWD。 */
#ifndef M2006_REMOTE_H
#define M2006_REMOTE_H
#include "sbus.h"
#define M2006_VRA_CHANNEL 4u
/* 沿用现有 SBUS 端点；如遥控器调整过行程/反向，在此校准。 */
#define M2006_VRA_LEFT 240u
#define M2006_VRA_RIGHT 1807u
/* 返回 0..100 整数百分比；失联返回 -1。 */
int m2006_remote_percent(const struct sbus_receiver *receiver, uint32_t now);
#endif
