/*
 * board.c
 * DM-MC02 公共板级支持：时钟、CAN2 初始化、输出初态和故障入口。
 */
#include "board.h"

#define POWER_OUT2_PIN 13u
#define POWER_5V_PIN 15u

static void (*fault_handler)(uint32_t code);

/* 登记当前功能的故障记录函数；必须在初始化硬件之前调用。 */
void board_set_fault_handler(void (*handler)(uint32_t code))
{
    fault_handler = handler;
}

/* 把故障编号交给当前功能记录，然后停留，避免带故障继续执行。 */
static void board_fault(uint32_t code)
{
    if (fault_handler != 0)
    {
        fault_handler(code);
    }
    for (;;)
    {
        __asm volatile("nop");
    }
}

/* 启动文件使用的唯一 CPU 故障入口；0xFF 表示处理器异常。 */
void Fault_Handler(void)
{
    board_fault(0xFFu);
}

/* 等待寄存器指定位达到期望值；有限次数后仍未就绪则记录 error 并停止。 */
void wait_bits(uint32_t address, uint32_t mask, uint32_t value, uint32_t error)
{
    for (uint32_t n = 0u; n < 8000000u; ++n)
    {
        if ((REG(address) & mask) == value)
        {
            return;
        }
    }
    board_fault(error);
}

/* 保持 CPU 为 HSI 64 MHz，为 CAN 配置 64 MHz 内核时钟；SysTick 每 1 ms 置位一次。 */
void clock_init(void)
{

    REG(RCC + CR) |= 1u;
    wait_bits(RCC + CR, 1u << 2, 1u << 2, 1u);
    REG(RCC + CFGR) &= ~7u;
    wait_bits(RCC + CFGR, 7u << 3, 0u, 2u);
    REG(RCC + CR) &= ~(3u << 3);
    wait_bits(RCC + CR, 1u << 5, 1u << 5, 3u);
    REG(RCC + 0x18u) = 0u;
    REG(RCC + 0x1Cu) = 0u;
    REG(RCC + 0x20u) = 0u;
    REG(RCC + CR) &= ~(1u << 24);
    wait_bits(RCC + CR, 1u << 25, 0u, 4u);
    REG(RCC + CR) &= ~(1u << 18);
    REG(RCC + CR) |= 1u << 16;
    wait_bits(RCC + CR, 1u << 17, 1u << 17, 5u);

    REG(RCC + PLLCKSELR) = (REG(RCC + PLLCKSELR) & ~0x3F3u) | 2u | (3u << 4);
    REG(RCC + PLLCFGR) = (REG(RCC + PLLCFGR) & ~0x7000Fu) | (3u << 2) | (1u << 17);
    REG(RCC + PLL1DIVR) = 31u | (1u << 9) | (3u << 16) | (1u << 24);
    REG(RCC + CR) |= 1u << 24;
    wait_bits(RCC + CR, 1u << 25, 1u << 25, 6u);
    REG(RCC + D2CCIP1R) = (REG(RCC + D2CCIP1R) & ~(3u << 28)) | (1u << 28);

    REG(0xE000E014u) = 64000u - 1u;
    REG(0xE000E018u) = 0u;
    REG(0xE000E010u) = 5u;
}

/* CAN2：PB5 / PB6 的 AF9 分别连接板载收发器的 RX / TX。 */
void can_init(void)
{
    const uint32_t pins = (1u << 5) | (1u << 6);
    const uint32_t mode_mask = (3u << 10) | (3u << 12);
    const uint32_t mode_af = (2u << 10) | (2u << 12);
    REG(RCC + AHB4ENR) |= 1u << 1;
    (void)REG(RCC + AHB4ENR);
    REG(GPIOB + 0x04u) &= ~pins;
    REG(GPIOB + 0x08u) = (REG(GPIOB + 0x08u) & ~mode_mask) | mode_af;
    REG(GPIOB + 0x0Cu) &= ~mode_mask;
    REG(GPIOB + 0x20u) = (REG(GPIOB + 0x20u) & ~((15u << 20) | (15u << 24))) | (9u << 20) | (9u << 24);
    REG(GPIOB + 0x00u) = (REG(GPIOB + 0x00u) & ~mode_mask) | mode_af;
    REG(RCC + APB1HENR) |= 1u << 8;
    (void)REG(RCC + APB1HENR);
    REG(RCC + APB1HRSTR) |= 1u << 8;
    (void)REG(RCC + APB1HRSTR);
    REG(RCC + APB1HRSTR) &= ~(1u << 8);
    REG(CAN + CCCR) &= ~(1u << 4);
    wait_bits(CAN + CCCR, 1u << 3, 0u, 7u);
    REG(CAN + CCCR) |= 1u;
    wait_bits(CAN + CCCR, 1u, 1u, 8u);
    REG(CAN + CCCR) = 3u;
    wait_bits(CAN + CCCR, 3u, 3u, 9u);

    /* 64 MHz / 4 / (1 + 13 + 2) = 1 Mbps，采样点 87.5%。 */
    REG(CAN + NBTP) = (1u << 25) | (3u << 16) | (12u << 8) | 1u;

    /* 只启用 CAN2，沿用共享消息 RAM 起始位置的 8 个接收和 2 个发送槽。 */
    for (uint32_t i = 0u; i < 2560u; ++i)
    {
        REG(RAM + 4u * i) = 0u;
    }
    REG(CAN + GFC) = (2u << 2) | 3u;
    REG(CAN + SIDFC) = 0u;
    REG(CAN + XIDFC) = 0u;
    REG(CAN + RXF0C) = 8u << 16;
    REG(CAN + RXF1C) = 0u;
    REG(CAN + RXESC) = 0u;
    /* 缓冲区 0 保留四轮 0x200；缓冲区 1 用于 M2006 的 0x1FF。 */
    REG(CAN + TXBC) = TX_OFFSET | (2u << 16);
    REG(CAN + TXESC) = 0u;
    REG(CAN + TXEFC) = 0u;
    REG(CAN + CCCR) &= ~1u;
    wait_bits(CAN + CCCR, 1u, 0u, 10u);
}

/* 先关闭加热、蜂鸣器和受控电源输出，沿用已跑通版本的启动初态。 */
void safe_outputs(void)
{
    const uint32_t power_pins = (1u << POWER_OUT2_PIN) | (1u << POWER_5V_PIN);
    const uint32_t power_mode_mask = (3u << (2u * POWER_OUT2_PIN)) | (3u << (2u * POWER_5V_PIN));
    const uint32_t power_mode_output = (1u << (2u * POWER_OUT2_PIN)) | (1u << (2u * POWER_5V_PIN));

    REG(RCC + AHB4ENR) |= 6u;
    (void)REG(RCC + AHB4ENR);
    REG(GPIOB + 0x18u) = (1u << 17) | (1u << 31);
    REG(GPIOB + 4u) &= ~((1u << 1) | (1u << 15));
    REG(GPIOB + 0xCu) &= ~((3u << 2) | (3u << 30));
    REG(GPIOB) = (REG(GPIOB) & ~((3u << 2) | (3u << 30))) | (1u << 2) | (1u << 30);
    REG(GPIOC + 0x18u) = power_pins << 16;
    REG(GPIOC + 4u) &= ~power_pins;
    REG(GPIOC + 0xCu) &= ~power_mode_mask;
    REG(GPIOC) = (REG(GPIOC) & ~power_mode_mask) | power_mode_output;
    REG(0x5C001004u) |= (1u << 21) | (1u << 22) | 1u;
}

void board_enable_power_out2(void)
{
    REG(GPIOC + 0x18u) = 1u << POWER_OUT2_PIN;
}

void board_enable_5v_output(void)
{
    REG(GPIOC + 0x18u) = 1u << POWER_5V_PIN;
}
