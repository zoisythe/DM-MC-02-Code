#include "board.h"
#include "servo_pwm.h"

#define SYSTICK_CONTROL 0xE000E010u

enum
{
    SYSTICK_COUNT_FLAG = 1u << 16,
    PULSE_DURATION_MS = 2000u,
};

/* 用 ST-Link 写入一个非零脉宽（μs），程序接收后清零。 */
volatile uint16_t gripper_bench_request_us;

int main(void)
{
    safe_outputs();
    clock_init();
    servo_pwm_init();

    uint32_t remaining_ms = 0u;
    while (1)
    {
        if ((REG(SYSTICK_CONTROL) & SYSTICK_COUNT_FLAG) != 0u && remaining_ms != 0u)
        {
            --remaining_ms;
            if (remaining_ms == 0u)
            {
                servo_pwm_stop();
            }
        }

        uint16_t pulse_us = gripper_bench_request_us;
        if (pulse_us != 0u)
        {
            gripper_bench_request_us = 0u;
            servo_pwm_set_pulse_us(pulse_us);
            remaining_ms = PULSE_DURATION_MS;
        }
    }
}
