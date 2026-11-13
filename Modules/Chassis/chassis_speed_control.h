/*
 * chassis_speed_control.h
 * 四轮速度 PI。转速单位为转子 rpm，电流为 C620 协议原始值，16384 对应约 20 A。
 */
#ifndef CHASSIS_SPEED_CONTROL_H
#define CHASSIS_SPEED_CONTROL_H
#define CHASSIS_MAX_CURRENT 1638 /* 四轮每个 C620 电流指令上限，约 2 A */
_Static_assert(CHASSIS_MAX_CURRENT > 0 && CHASSIS_MAX_CURRENT <= 16384, "C620 command range");

/* 速度 PI：先计算误差与候选积分；限流且误差继续推向饱和时，冻结积分避免越积越大。 */
int chassis_speed_current(int target, int measured, int *integral_x20);
#endif
