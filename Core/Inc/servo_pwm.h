#ifndef SERVO_PWM_H
#define SERVO_PWM_H

#include <stdint.h>

/* 在板级时钟初始化后调用。PA0 在首次设定脉宽之前保持低电平。 */
void servo_pwm_init(void);

/* 50 Hz，脉宽单位为 μs。500～2500 μs 之外的命令停止输出。 */
void servo_pwm_set_pulse_us(uint16_t pulse_us);

/* 在下一帧边界停止发脉冲，保留当前帧的完整波形。 */
void servo_pwm_stop(void);

/* 在 servo_pwm_init() 之后调用，配置夹爪俯仰 PA2，初始无脉冲。 */
void servo_pwm_enable_pa2(void);

/* 启用滑轨平移 PE9 和俯仰 PE13，两路初始无脉冲。 */
void servo_pwm_enable_slide(void);

/* 50 Hz，单位为 μs。传入 0 或超出 500～2500 μs 时停止发脉冲。 */
void servo_pwm_set_slide_travel_us(uint16_t pulse_us);
void servo_pwm_set_slide_pitch_us(uint16_t pulse_us);

#endif
