#ifndef SERVO_PWM_H
#define SERVO_PWM_H

#include <stdint.h>

/* 在板级时钟初始化后调用。PA0 在首次设定脉宽之前保持低电平。 */
void servo_pwm_init(void);

/* 50 Hz，脉宽单位为 μs。500～2500 μs 之外的命令停止输出。 */
void servo_pwm_set_pulse_us(uint16_t pulse_us);

/* 在下一帧边界停止发脉冲，保留当前帧的完整波形。 */
void servo_pwm_stop(void);

#endif
