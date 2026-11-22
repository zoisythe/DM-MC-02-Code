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
    TIM_CCMR2 = 0x1Cu,
    TIM_CCER = 0x20u,
    TIM_PSC = 0x28u,
    TIM_ARR = 0x2Cu,
    TIM_CCR1 = 0x34u,
    TIM_CCR3 = 0x3Cu,

    TIM_COUNTER_ENABLE = 1u << 0,
    TIM_UPDATE = 1u << 0,
    TIM_OC_PRELOAD = 1u << 3,
    TIM_OC_PWM_MODE_1 = 6u << 4,
    TIM_CH1_ENABLE = 1u << 0,
    TIM_CH3_ENABLE = 1u << 8,

    TIM2_CLOCK_MHZ = 64u,
    SERVO_FRAME_US = 20000u,
    SERVO_MIN_US = 500u,
    SERVO_MAX_US = 2500u,
};

static void configure_pin_low(unsigned int pin)
{
    const unsigned int mode_shift = 2u * pin;
    const unsigned int af_shift = 4u * pin;

    REG(GPIOA_BASE + GPIO_BSRR) = 1u << (16u + pin);
    REG(GPIOA_BASE + GPIO_TYPE) &= ~(1u << pin);
    REG(GPIOA_BASE + GPIO_SPEED) &= ~(3u << mode_shift);
    REG(GPIOA_BASE + GPIO_PULL) &= ~(3u << mode_shift);
    REG(GPIOA_BASE + GPIO_MODE) = (REG(GPIOA_BASE + GPIO_MODE) & ~(3u << mode_shift)) | (1u << mode_shift);
    REG(GPIOA_BASE + GPIO_AFRL) = (REG(GPIOA_BASE + GPIO_AFRL) & ~(15u << af_shift)) | (1u << af_shift);
}

static void connect_timer_to_pin(unsigned int pin)
{
    const unsigned int shift = 2u * pin;
    REG(GPIOA_BASE + GPIO_MODE) = (REG(GPIOA_BASE + GPIO_MODE) & ~(3u << shift)) | (2u << shift);
}

void servo_pwm_init(void)
{
    REG(RCC + AHB4ENR) |= GPIOA_CLOCK;
    (void)REG(RCC + AHB4ENR);
    /* PA0 先作为推挽输出拉低，再切换到 TIM2_CH1。 */
    configure_pin_low(0u);

    REG(RCC + RCC_APB1LENR) |= TIM2_CLOCK;
    (void)REG(RCC + RCC_APB1LENR);
    REG(RCC + RCC_APB1LRSTR) |= TIM2_RESET;
    (void)REG(RCC + RCC_APB1LRSTR);
    REG(RCC + RCC_APB1LRSTR) &= ~(uint32_t)TIM2_RESET;

    /* clock_init() 将 APB1 配成 /1，TIM2 时钟为 64 MHz，每次计数为 1 μs。 */
    REG(TIM2_BASE + TIM_PSC) = TIM2_CLOCK_MHZ - 1u;
    REG(TIM2_BASE + TIM_ARR) = SERVO_FRAME_US - 1u;
    REG(TIM2_BASE + TIM_CCMR1) = TIM_OC_PWM_MODE_1 | TIM_OC_PRELOAD;
    REG(TIM2_BASE + TIM_CCR1) = 0u;
    REG(TIM2_BASE + TIM_EGR) = TIM_UPDATE;
    REG(TIM2_BASE + TIM_CCER) = TIM_CH1_ENABLE;
    REG(TIM2_BASE + TIM_CR1) = TIM_COUNTER_ENABLE;
    connect_timer_to_pin(0u);
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

void servo_pwm_enable_pa2(void)
{
    /* PA2 AF1 = TIM2_CH3，共用 50 Hz 周期，脉宽由 CCR3 独立设置。 */
    configure_pin_low(2u);
    REG(TIM2_BASE + TIM_CCR3) = 0u;
    REG(TIM2_BASE + TIM_CCMR2) = TIM_OC_PWM_MODE_1 | TIM_OC_PRELOAD;
    REG(TIM2_BASE + TIM_CCER) |= TIM_CH3_ENABLE;
    connect_timer_to_pin(2u);
}

void servo_pwm_set_pa2_pulse_us(uint16_t pulse_us)
{
    REG(TIM2_BASE + TIM_CCR3) = (pulse_us >= SERVO_MIN_US && pulse_us <= SERVO_MAX_US) ? pulse_us : 0u;
}
