/*
 * remote_uart.h
 * 公共 SBUS 串口接收层：P6 经板载反相连接 UART5/PD2，100000 baud、偶校验、2 停止位。
 */
#ifndef REMOTE_UART_H
#define REMOTE_UART_H
#include <stdint.h>
/* 设置 P6 对应 UART5 接收。接收机已经由板载电路反相，不再软件反相。 */
void remote_uart_init(void);
/* 尽快取走 UART 接收字节并交给 SBUS 解析器，一次最多处理 32 次。 */
void poll_remote(uint32_t now);
#endif
