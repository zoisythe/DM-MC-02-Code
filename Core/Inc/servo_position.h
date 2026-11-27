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
    uint32_t candidate_since_ms, last_step_ms;
    size_t candidate;
    bool has_candidate;
};

void servo_position_init(struct servo_position *position, uint16_t pulse_us);
void servo_position_hold(struct servo_position *position);
/* 档位表至少有一项，按输入值升序排列。只有新遥控帧才确认档位，每次轮询都可推进平滑运动。返回值表示脉宽已改变。 */
bool servo_position_update(struct servo_position *position,
                           const struct servo_position_level *levels, size_t level_count,
                           uint16_t raw, bool new_frame, uint32_t now);

#endif
