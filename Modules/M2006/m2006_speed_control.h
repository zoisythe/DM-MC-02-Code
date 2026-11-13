/*
 * m2006_speed_control.h
 * M2006 速度 PI。转速单位为转子 rpm，电流为 C610 协议原始值，1000 对应约 1 A。
 */
#ifndef M2006_SPEED_CONTROL_H
#define M2006_SPEED_CONTROL_H
#define M2006_MAX_CURRENT 10000      /* C610 电流指令上限，10000 对应约 10 A */
_Static_assert(M2006_MAX_CURRENT > 0 && M2006_MAX_CURRENT <= 10000, "C610 maximum 10 A");

/* 速度 PI：先计算误差与候选积分；限流且误差继续推向饱和时，冻结积分避免越积越大。 */
int m2006_speed_current(int target, int measured, int *integral_x20);
#endif
