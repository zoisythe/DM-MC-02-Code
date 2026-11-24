#include "servo_pwm.h"
#include "board_regs.h"

#define TIM1_BASE 0x40010000u
#define TIM2_BASE 0x40000000u
#define GPIOE_BASE 0x58021000u

enum
{
    RCC_APB1LRSTR = 0x90u,
    RCC_APB2RSTR = 0x98u,
    RCC_APB1LENR = 0xE8u,
    RCC_APB2ENR = 0xF0u,
    TIM1_RESET = 1u << 0,
    TIM1_CLOCK = 1u << 0,
    TIM2_RESET = 1u << 0,
    TIM2_CLOCK = 1u << 0,
    GPIOA_CLOCK = 1u << 0,
    GPIOE_CLOCK = 1u << 4,

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
    TIM_BDTR = 0x44u,

    TIM_COUNTER_ENABLE = 1u << 0,
    TIM_UPDATE = 1u << 0,
    TIM_OC_PRELOAD = 1u << 3,
    TIM_OC_PWM_MODE_1 = 6u << 4,
    TIM_CH1_ENABLE = 1u << 0,
    TIM_CH3_ENABLE = 1u << 8,
    TIM_MAIN_OUTPUT_ENABLE = 1u << 15,

    TIMER_CLOCK_MHZ = 64u,
    SERVO_FRAME_US = 20000u,
    SERVO_MIN_US = 500u,
    SERVO_MAX_US = 2500u,
};

static void configure_pin_low(uint32_t port, unsigned int pin)
{
    const unsigned int mode_shift = 2u * pin;
    const unsigned int af_shift = 4u * (pin % 8u);
    const uint32_t af_register = port + GPIO_AFRL + 4u * (pin / 8u);

    REG(port + GPIO_BSRR) = 1u << (16u + pin);
    REG(port + GPIO_TYPE) &= ~(1u << pin);
    REG(port + GPIO_SPEED) &= ~(3u << mode_shift);
    REG(port + GPIO_PULL) &= ~(3u << mode_shift);
    REG(port + GPIO_MODE) = (REG(port + GPIO_MODE) & ~(3u << mode_shift)) | (1u << mode_shift);
    REG(af_register) = (REG(af_register) & ~(15u << af_shift)) | (1u << af_shift);
}

static void connect_timer_to_pin(uint32_t port, unsigned int pin)
{
    const unsigned int shift = 2u * pin;
    REG(port + GPIO_MODE) = (REG(port + GPIO_MODE) & ~(3u << shift)) | (2u << shift);
}

void servo_pwm_init(void)
{
    REG(RCC + AHB4ENR) |= GPIOA_CLOCK;
    (void)REG(RCC + AHB4ENR);
    /* PA0 先作为推挽输出拉低，再切换到 TIM2_CH1。 */
    configure_pin_low(GPIOA_BASE, 0u);

    REG(RCC + RCC_APB1LENR) |= TIM2_CLOCK;
    (void)REG(RCC + RCC_APB1LENR);
    REG(RCC + RCC_APB1LRSTR) |= TIM2_RESET;
    (void)REG(RCC + RCC_APB1LRSTR);
    REG(RCC + RCC_APB1LRSTR) &= ~(uint32_t)TIM2_RESET;

    /* clock_init() 将 APB1 和 APB2 配成 /1，定时器每次计数为 1 μs。 */
    REG(TIM2_BASE + TIM_PSC) = TIMER_CLOCK_MHZ - 1u;
    REG(TIM2_BASE + TIM_ARR) = SERVO_FRAME_US - 1u;
    REG(TIM2_BASE + TIM_CCMR1) = TIM_OC_PWM_MODE_1 | TIM_OC_PRELOAD;
    REG(TIM2_BASE + TIM_CCR1) = 0u;
    REG(TIM2_BASE + TIM_EGR) = TIM_UPDATE;
    REG(TIM2_BASE + TIM_CCER) = TIM_CH1_ENABLE;
    REG(TIM2_BASE + TIM_CR1) = TIM_COUNTER_ENABLE;
    connect_timer_to_pin(GPIOA_BASE, 0u);
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
    configure_pin_low(GPIOA_BASE, 2u);
    REG(TIM2_BASE + TIM_CCR3) = 0u;
    REG(TIM2_BASE + TIM_CCMR2) = TIM_OC_PWM_MODE_1 | TIM_OC_PRELOAD;
    REG(TIM2_BASE + TIM_CCER) |= TIM_CH3_ENABLE;
    connect_timer_to_pin(GPIOA_BASE, 2u);
}

void servo_pwm_enable_slide(void)
{
    REG(RCC + AHB4ENR) |= GPIOE_CLOCK;
    (void)REG(RCC + AHB4ENR);
    configure_pin_low(GPIOE_BASE, 9u);
    configure_pin_low(GPIOE_BASE, 13u);

    REG(RCC + RCC_APB2ENR) |= TIM1_CLOCK;
    (void)REG(RCC + RCC_APB2ENR);
    REG(RCC + RCC_APB2RSTR) |= TIM1_RESET;
    (void)REG(RCC + RCC_APB2RSTR);
    REG(RCC + RCC_APB2RSTR) &= ~(uint32_t)TIM1_RESET;

    REG(TIM1_BASE + TIM_PSC) = TIMER_CLOCK_MHZ - 1u;
    REG(TIM1_BASE + TIM_ARR) = SERVO_FRAME_US - 1u;
    REG(TIM1_BASE + TIM_CCMR1) = TIM_OC_PWM_MODE_1 | TIM_OC_PRELOAD;
    REG(TIM1_BASE + TIM_CCMR2) = TIM_OC_PWM_MODE_1 | TIM_OC_PRELOAD;
    REG(TIM1_BASE + TIM_CCR1) = 0u;
    REG(TIM1_BASE + TIM_CCR3) = 0u;
    REG(TIM1_BASE + TIM_EGR) = TIM_UPDATE;
    REG(TIM1_BASE + TIM_CCER) = TIM_CH1_ENABLE | TIM_CH3_ENABLE;
    REG(TIM1_BASE + TIM_BDTR) = TIM_MAIN_OUTPUT_ENABLE;
    REG(TIM1_BASE + TIM_CR1) = TIM_COUNTER_ENABLE;
    connect_timer_to_pin(GPIOE_BASE, 9u);
    connect_timer_to_pin(GPIOE_BASE, 13u);
}

void servo_pwm_set_slide_travel_us(uint16_t pulse_us)
{
    REG(TIM1_BASE + TIM_CCR1) = (pulse_us >= SERVO_MIN_US && pulse_us <= SERVO_MAX_US) ? pulse_us : 0u;
}

void servo_pwm_set_slide_pitch_us(uint16_t pulse_us)
{
    REG(TIM1_BASE + TIM_CCR3) = (pulse_us >= SERVO_MIN_US && pulse_us <= SERVO_MAX_US) ? pulse_us : 0u;
}
