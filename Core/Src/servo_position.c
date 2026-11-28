#include "servo_position.h"

enum
{
    POSITION_HYSTERESIS_RAW = 24,
    POSITION_STEP_MS = 20,
    MILLI_US_PER_US = 1000,
    MS_PER_SECOND = 1000,
};

void servo_position_init(struct servo_position *position, uint16_t pulse_us,
                         uint16_t max_speed_us_per_s, uint16_t acceleration_us_per_s2)
{
    *position = (struct servo_position){
        .pulse_us = pulse_us,
        .target_us = pulse_us,
        .position_milli_us = (int32_t)pulse_us * MILLI_US_PER_US,
        .max_velocity_milli_us = (int32_t)max_speed_us_per_s * POSITION_STEP_MS,
        .acceleration_milli_us =
            ((int32_t)acceleration_us_per_s2 * POSITION_STEP_MS * POSITION_STEP_MS +
             MS_PER_SECOND - 1) / MS_PER_SECOND,
    };
}

void servo_position_hold(struct servo_position *position)
{
    position->has_candidate = false;
    position->target_us = position->pulse_us;
    position->position_milli_us = (int32_t)position->pulse_us * MILLI_US_PER_US;
    position->velocity_milli_us = 0;
}

static int boundary(const struct servo_position_level *levels, size_t lower)
{
    return ((int)levels[lower].raw_center + (int)levels[lower + 1u].raw_center) / 2;
}

void servo_position_select_level(struct servo_position *position,
                                 const struct servo_position_level *levels, size_t level_count,
                                 uint16_t raw, uint32_t now, uint32_t settle_ms)
{
    size_t candidate = position->has_candidate ? position->candidate : 0u;
    int hysteresis = position->has_candidate ? POSITION_HYSTERESIS_RAW : 0;
    /* 两档中心的中点为分界，跨越回差后才切换候选档位。 */
    while (candidate + 1u < level_count &&
           (int)raw > boundary(levels, candidate) + hysteresis)
    {
        ++candidate;
    }
    while (candidate > 0u && (int)raw < boundary(levels, candidate - 1u) - hysteresis)
    {
        --candidate;
    }
    if (!position->has_candidate || candidate != position->candidate)
    {
        position->candidate = candidate;
        position->candidate_since_ms = now;
        position->has_candidate = true;
    }
    if (now - position->candidate_since_ms >= settle_ms)
    {
        position->target_us = levels[candidate].pulse_us;
    }
}

static int64_t stopping_distance(int32_t speed, int32_t acceleration)
{
    /* 累加本步位移与后续各步减速后的位移。 */
    int32_t steps = (speed + acceleration - 1) / acceleration;
    return (int64_t)steps * (2 * speed - (steps - 1) * acceleration) / 2;
}

bool servo_position_step(struct servo_position *position, uint32_t now)
{
    int32_t distance = (int32_t)position->target_us * MILLI_US_PER_US -
                       position->position_milli_us;
    if (now - position->last_step_ms < POSITION_STEP_MS ||
        (distance == 0 && position->velocity_milli_us == 0))
    {
        return false;
    }

    int32_t direction = (distance < 0 || (distance == 0 && position->velocity_milli_us < 0))
                            ? -1 : 1;
    int32_t speed = direction * position->velocity_milli_us;
    int32_t acceleration = position->acceleration_milli_us;
    if (speed < 0)
    {
        /* 反向指令先将原方向的速度降到零。 */
        speed += acceleration;
        if (speed > 0)
        {
            speed = 0;
        }
    }
    else
    {
        int32_t minimum = speed > acceleration ? speed - acceleration : 0;
        int32_t maximum = speed + acceleration;
        if (maximum > position->max_velocity_milli_us)
        {
            maximum = position->max_velocity_milli_us;
        }

        /* 选择能在目标处停下的最快速度。目标突然移进制动距离时按最大减速度制动。 */
        while (minimum < maximum)
        {
            int32_t candidate = minimum + (maximum - minimum + 1) / 2;
            if (stopping_distance(candidate, acceleration) <= direction * distance)
            {
                minimum = candidate;
            }
            else
            {
                maximum = candidate - 1;
            }
        }
        speed = minimum;
    }

    position->velocity_milli_us = direction * speed;
    position->position_milli_us += position->velocity_milli_us;
    position->last_step_ms = now;
    uint16_t pulse_us = (uint16_t)((position->position_milli_us + MILLI_US_PER_US / 2) /
                                   MILLI_US_PER_US);
    bool changed = pulse_us != position->pulse_us;
    position->pulse_us = pulse_us;
    return changed;
}
