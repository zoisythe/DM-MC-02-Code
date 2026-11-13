/*
 * remote_uart.c
 * 公共 SBUS 串口接收层：P6 经板载反相连接 UART5/PD2，100000 baud、偶校验、2 停止位。
 */
#include "board.h"
#include "remote_uart.h"
#include "sbus.h"

/* 设置 P6 对应 UART5 接收。接收机已经由板载电路反相，不再软件反相。 */
void remote_uart_init(void)
{
    REG(RCC + AHB4ENR) |= 1u << 3;
    (void)REG(RCC + AHB4ENR);
    REG(GPIOD) = (REG(GPIOD) & ~(3u << 4)) | (2u << 4);
    REG(GPIOD + 0x0Cu) &= ~(3u << 4);
    REG(GPIOD + 0x20u) = (REG(GPIOD + 0x20u) & ~(15u << 8)) | (8u << 8);

    REG(RCC + 0x54u) &= ~7u;
    REG(RCC + 0xE8u) |= 1u << 20;
    (void)REG(RCC + 0xE8u);
    REG(RCC + 0x90u) |= 1u << 20;
    REG(RCC + 0x90u) &= ~(1u << 20);
    REG(UART5) = 0;
    REG(UART5 + 4u) = 2u << 12;
    REG(UART5 + 8u) = 0;
    REG(UART5 + 0x2Cu) = 0;
    /* UART 内核 64 MHz，除以 640 得到 100000 baud。 */
    REG(UART5 + 0x0Cu) = 640u;
    REG(UART5) = (1u << 12) | (1u << 10) | (1u << 2) | 1u;
    wait_bits(UART5 + 0x1Cu, 1u << 22, 1u << 22, 14u);
}
/* 尽快取走 UART 接收字节并交给 SBUS 解析器，一次最多处理 32 次。 */
void poll_remote(uint32_t now)
{
    for (uint32_t n = 0; n < 32u; ++n)
    {
        uint32_t state = REG(UART5 + 0x1Cu);
        if ((state & 15u) != 0u)
        {
            if ((state & (1u << 5)) != 0u)
            {
                (void)REG(UART5 + 0x24u);
            }
            REG(UART5 + 0x20u) = state & 15u;
            sbus_receive_error(&sbus_status);
            continue;
        }
        if ((state & (1u << 5)) == 0u)
        {
            return;
        }
        uint8_t byte = (uint8_t)REG(UART5 + 0x24u);
        sbus_receive_byte(&sbus_status, byte, now);
    }
}
