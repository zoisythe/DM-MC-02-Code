/* M2006 的 CH5 / VRA 有符号速度输入。 */
#ifndef M2006_REMOTE_H
#define M2006_REMOTE_H
#include <stdint.h>
#define M2006_VRA_CHANNEL 4u
/* 沿用现有 SBUS 端点；如遥控器调整过行程/反向，在此校准。 */
#define M2006_VRA_LEFT 240u
#define M2006_VRA_CENTER 1024u
#define M2006_VRA_RIGHT 1807u
/* 单侧死区取 VRA 总行程的 1/4，四舍五入到整数。 */
#define M2006_VRA_DEADBAND ((M2006_VRA_RIGHT - M2006_VRA_LEFT + 2u) / 4u)
/* 返回目标转子 rpm，死区内为零，两侧线性映射到各自的满速。 */
int m2006_remote_speed_rpm(uint16_t raw);
#endif
