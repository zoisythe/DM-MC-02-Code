/*
 * chassis_control.h
 * 四轮控制流程：接收遥控与四电机反馈，计算速度 PI，发送 CAN 电流指令并处理通信恢复。
 */
#ifndef CHASSIS_CONTROL_H
#define CHASSIS_CONTROL_H

#include <stdint.h>

/* 启动四轮控制和其遥控接收。上电仅调用一次，不在循环中重复初始化。 */
void chassis_control_init(void);

/* 持续轮询遥控/CAN，每 1 ms 推进四轮控制，返回当前毫秒计数。 */
uint32_t chassis_control_poll(void);
#endif
