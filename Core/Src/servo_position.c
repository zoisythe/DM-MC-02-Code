#include "servo_position.h"

enum
{
    POSITION_SETTLE_MS = 150u,
    POSITION_HYSTERESIS_RAW = 24,
    POSITION_STEP_MS = 20u,
    POSITION_STEP_US = 8,
};

void servo_position_init(struct servo_position *position, uint16_t pulse_us)
{
    position->pulse_us = pulse_us;
    position->target_us = pulse_us;
    position->candidate_since_ms = 0u;
    position->last_step_ms = 0u;
    position->candidate = 0u;
    position->has_candidate = false;
}

void servo_position_hold(struct servo_position *position)
{
    position->has_candidate = false;
    position->target_us = position->pulse_us;
}

static int boundary(const struct servo_position_level *levels, size_t lower)
{
    return ((int)levels[lower].raw_center + (int)levels[lower + 1u].raw_center) / 2;
}

bool servo_position_update(struct servo_position *position,
                           const struct servo_position_level *levels, size_t level_count,
                           uint16_t raw, bool new_frame, uint32_t now)
{
    if (new_frame)
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
        else if (now - position->candidate_since_ms >= POSITION_SETTLE_MS)
        {
            position->target_us = levels[candidate].pulse_us;
        }
    }

    int delta = (int)position->target_us - (int)position->pulse_us;
    if (delta == 0 || now - position->last_step_ms < POSITION_STEP_MS)
    {
        return false;
    }
    if (delta > POSITION_STEP_US)
    {
        delta = POSITION_STEP_US;
    }
    else if (delta < -POSITION_STEP_US)
    {
        delta = -POSITION_STEP_US;
    }
    position->pulse_us = (uint16_t)((int)position->pulse_us + delta);
    position->last_step_ms = now;
    return true;
}
