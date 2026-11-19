#include "servo_pwm.h"
#include "board_regs.h"

#define TIM2_BASE 0x40000000u

enum
{
    RCC_APB1LRSTR = 0x90u,
    RCC_APB1LENR = 0xE8u,
    TIM2_RESET = 1u << 0,
    TIM2_CLOCK = 1u << 0,
    GPIOA_CLOCK = 1u << 0,

    GPIO_MODE = 0x00u,
    GPIO_TYPE = 0x04u,
    GPIO_SPEED = 0x08u,
    GPIO_PULL = 0x0Cu,
    GPIO_BSRR = 0x18u,
    GPIO_AFRL = 0x20u,

    TIM_CR1 = 0x00u,
    TIM_EGR = 0x14u,
    TIM_CCMR1 = 0x18u,
    TIM_CCER = 0x20u,
    TIM_PSC = 0x28u,
    TIM_ARR = 0x2Cu,
    TIM_CCR1 = 0x34u,

    TIM_COUNTER_ENABLE = 1u << 0,
    TIM_UPDATE = 1u << 0,
    TIM_CH1_PRELOAD = 1u << 3,
    TIM_CH1_PWM_MODE_1 = 6u << 4,
    TIM_CH1_ENABLE = 1u << 0,

    TIM2_CLOCK_MHZ = 64u,
    SERVO_FRAME_US = 20000u,
    SERVO_MIN_US = 500u,
    SERVO_MAX_US = 2500u,
};

void servo_pwm_init(void)
{
    REG(RCC + AHB4ENR) |= GPIOA_CLOCK;
    (void)REG(RCC + AHB4ENR);
    /* PA0 先作为推挽输出拉低，再切换到 TIM2_CH1。 */
    REG(GPIOA_BASE + GPIO_BSRR) = 1u << 16;
    REG(GPIOA_BASE + GPIO_TYPE) &= ~1u;
    REG(GPIOA_BASE + GPIO_SPEED) &= ~3u;
    REG(GPIOA_BASE + GPIO_PULL) &= ~3u;
    REG(GPIOA_BASE + GPIO_MODE) = (REG(GPIOA_BASE + GPIO_MODE) & ~3u) | 1u;
    REG(GPIOA_BASE + GPIO_AFRL) = (REG(GPIOA_BASE + GPIO_AFRL) & ~15u) | 1u; /* PA0 AF1 */

    REG(RCC + RCC_APB1LENR) |= TIM2_CLOCK;
    (void)REG(RCC + RCC_APB1LENR);
    REG(RCC + RCC_APB1LRSTR) |= TIM2_RESET;
    (void)REG(RCC + RCC_APB1LRSTR);
    REG(RCC + RCC_APB1LRSTR) &= ~(uint32_t)TIM2_RESET;

    /* clock_init() 将 APB1 配成 /1，TIM2 时钟为 64 MHz，每次计数为 1 μs。 */
    REG(TIM2_BASE + TIM_PSC) = TIM2_CLOCK_MHZ - 1u;
    REG(TIM2_BASE + TIM_ARR) = SERVO_FRAME_US - 1u;
    REG(TIM2_BASE + TIM_CCMR1) = TIM_CH1_PWM_MODE_1 | TIM_CH1_PRELOAD;
    REG(TIM2_BASE + TIM_CCR1) = 0u;
    REG(TIM2_BASE + TIM_EGR) = TIM_UPDATE;
    REG(TIM2_BASE + TIM_CCER) = TIM_CH1_ENABLE;
    REG(TIM2_BASE + TIM_CR1) = TIM_COUNTER_ENABLE;
    REG(GPIOA_BASE + GPIO_MODE) = (REG(GPIOA_BASE + GPIO_MODE) & ~3u) | 2u;
}

void servo_pwm_set_pulse_us(uint16_t pulse_us)
{
    if (pulse_us < SERVO_MIN_US || pulse_us > SERVO_MAX_US)
    {
        servo_pwm_stop();
        return;
    }
    REG(TIM2_BASE + TIM_CCR1) = pulse_us;
}

void servo_pwm_stop(void)
{
    REG(TIM2_BASE + TIM_CCR1) = 0u;
}
