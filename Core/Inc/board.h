/*
 * board.h
 * DM-MC02 公共板级支持：时钟、CAN1 初始化、输出初态和故障入口。
 */
#ifndef BOARD_H
#define BOARD_H
#include "board_regs.h"

/* 登记当前功能的故障记录函数；必须在初始化硬件之前调用。 */
void board_set_fault_handler(void (*handler)(uint32_t code));
/* 等待寄存器指定位达到期望值；有限次数后仍未就绪则记录 error 并停止。 */
void wait_bits(uint32_t address, uint32_t mask, uint32_t value, uint32_t error);
/* 保持 CPU 为 HSI 64 MHz，为 CAN 配置 64 MHz 内核时钟；SysTick 每 1 ms 置位一次。 */
void clock_init(void);
/* 初始化 CAN1：PD0 接收、PD1 发送，经典 CAN 1 Mbps；只供当前选择的控制模块调用一次。 */
void can_init(void);
/* 先关闭加热、蜂鸣器和受控电源输出，沿用已跑通版本的启动初态。 */
void safe_outputs(void);
#endif
