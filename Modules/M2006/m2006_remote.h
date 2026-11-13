/* M2006 的 CH5 / VRA 有符号速度输入。 */
#ifndef M2006_REMOTE_H
#define M2006_REMOTE_H
#include <stdint.h>
#define M2006_VRA_CHANNEL 4u
/* 沿用现有 SBUS 端点；如遥控器调整过行程/反向，在此校准。 */
#define M2006_VRA_LEFT 240u
#define M2006_VRA_CENTER 1024u
#define M2006_VRA_RIGHT 1807u
#define M2006_VRA_DEADBAND 40u
/* 正值上升，负值下降，单位为转子 rpm。仅对有效 SBUS 输入调用。 */
int m2006_remote_speed_rpm(uint16_t raw);
#endif
