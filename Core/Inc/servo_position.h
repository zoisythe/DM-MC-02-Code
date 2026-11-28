#ifndef SERVO_POSITION_H
#define SERVO_POSITION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct servo_position_level
{
    uint16_t raw_center;
    uint16_t pulse_us;
};

struct servo_position
{
    uint16_t pulse_us, target_us;
    int32_t position_milli_us, velocity_milli_us;
    int32_t max_velocity_milli_us, acceleration_milli_us;
    uint32_t candidate_since_ms, last_step_ms;
    size_t candidate;
    bool has_candidate;
};

void servo_position_init(struct servo_position *position, uint16_t pulse_us,
                         uint16_t max_speed_us_per_s, uint16_t acceleration_us_per_s2);
void servo_position_hold(struct servo_position *position);
/* 档位表按输入值升序排列，至少有一项。只在收到有效新遥控帧时确认档位。 */
void servo_position_select_level(struct servo_position *position,
                                 const struct servo_position_level *levels, size_t level_count,
                                 uint16_t raw, uint32_t now, uint32_t settle_ms);
/* 每次轮询都可调用，返回值表示输出脉宽已改变。 */
bool servo_position_step(struct servo_position *position, uint32_t now);

#endif
