/*
 * board_regs.h
 * STM32H723 寄存器地址表。基地址加偏移定位寄存器，REG 用 volatile 保证真实读写硬件。
 */
#ifndef BOARD_REGS_H
#define BOARD_REGS_H
#include <stdint.h>

_Static_assert(sizeof(uint32_t) == 4, "32-bit registers required");
#define REG(a)                                                                                     \
    (*(volatile uint32_t *)(a)) /* 按地址访问 32 位硬件寄存器，volatile 禁止优化掉真实访问 */
#define RCC 0x58024400u         /* 复位与时钟控制器基地址 */
#define GPIOA_BASE 0x58020000u   /* A 组 GPIO 基地址 */
#define GPIOB 0x58020400u       /* B 组 GPIO 基地址 */
#define GPIOC 0x58020800u       /* C 组 GPIO 基地址 */
#define GPIOD 0x58020C00u       /* D 组 GPIO 基地址 */
#define UART5 0x40005000u       /* SBUS 接收串口基地址 */
#define CAN 0x4000A400u         /* FDCAN2 寄存器基地址 */
#define RAM 0x4000AC00u         /* FDCAN 共享消息 RAM 基地址 */
#define CR 0x00u                /* RCC 时钟控制偏移 */
#define CFGR 0x10u              /* 系统时钟配置偏移 */
#define PLLCKSELR 0x28u         /* PLL 输入与分频配置 */
#define PLLCFGR 0x2Cu           /* PLL 模式与输出使能配置 */
#define PLL1DIVR 0x30u          /* PLL1 倍频和输出分频 */
#define D2CCIP1R 0x50u          /* CAN 内核时钟选择 */
#define APB1HRSTR 0x94u         /* CAN 外设复位 */
#define AHB4ENR 0xE0u           /* GPIO 时钟使能 */
#define APB1HENR 0xECu          /* CAN 外设时钟使能 */
#define CCCR 0x18u              /* CAN 初始化与模式控制 */
#define NBTP 0x1Cu              /* 经典 CAN 标称位时序 */
#define ECR 0x40u               /* CAN 错误计数 */
#define PSR 0x44u               /* CAN 协议状态 */
#define GFC 0x80u               /* 全局接收过滤规则 */
#define SIDFC 0x84u             /* 标准 ID 过滤表配置 */
#define XIDFC 0x88u             /* 扩展 ID 过滤表配置 */
#define RXF0C 0xA0u             /* 接收 FIFO0 配置 */
#define RXF0S 0xA4u             /* 接收 FIFO0 状态 */
#define RXF0A 0xA8u             /* 确认取走 FIFO0 帧 */
#define RXF1C 0xB0u             /* 接收 FIFO1 配置 */
#define RXESC 0xBCu             /* 接收帧数据区大小 */
#define TXBC 0xC0u              /* 发送缓冲区配置 */
#define TXESC 0xC8u             /* 发送数据区大小 */
#define TXBRP 0xCCu             /* 待发送请求标记 */
#define TXBAR 0xD0u             /* 新增发送请求 */
#define TXBCR 0xD4u             /* 取消发送请求 */
#define TXBTO 0xD8u             /* 已成功发送标记 */
#define TXEFC 0xF0u             /* 发送事件 FIFO 配置 */
#define TX_OFFSET 128u          /* 发送缓冲区在共享 RAM 中的字节偏移 */

#endif
