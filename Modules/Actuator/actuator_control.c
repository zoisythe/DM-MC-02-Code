#include "actuator_control.h"
#include "board_regs.h"
#include "sbus.h"

#define GPIOE_BASE 0x58021000u

enum
{
    SWB_CHANNEL = 7,  /* CH8：上位选择 B，下位选择 A。 */
    SWC_CHANNEL = 8,  /* CH9：中位停止，A 上收下伸，B 上伸下收。 */
    SWITCH_UP_MAX = 632,
    SWITCH_DOWN_MIN = 1416,
    REVERSE_BRAKE_MS = 100, /* XY-160D 换向前至少制动 100 ms。 */

    A_EXTEND = 1u << 7,   /* PE7 -> IN1 */
    A_RETRACT = 1u << 8,  /* PE8 -> IN2 */
    B_EXTEND = 1u << 2,   /* PE2 -> IN3 */
    B_RETRACT = 1u << 3,  /* PE3 -> IN4 */
    OUTPUT_MASK = A_EXTEND | A_RETRACT | B_EXTEND | B_RETRACT,

    GPIOE_CLOCK = 1u << 4,
    GPIO_MODE = 0x00u,
    GPIO_TYPE = 0x04u,
    GPIO_SPEED = 0x08u,
    GPIO_PULL = 0x0Cu,
    GPIO_BSRR = 0x18u,
    MODE_MASK = (3u << 4) | (3u << 6) | (3u << 14) | (3u << 16),
    OUTPUT_MODE = (1u << 4) | (1u << 6) | (1u << 14) | (1u << 16),
    PULL_DOWN = OUTPUT_MODE << 1,
};

static uint32_t last_frame, link_interruptions;
static bool armed;
static uint32_t active_output;
static struct
{
    uint32_t last_direction;
    uint32_t stopped_at;
} brake_state[2];

static void set_outputs(uint32_t active)
{
    /* 一次写入置位选中方向，并将其余三个输入拉低。0 表示两个推杆制动。 */
    REG(GPIOE_BASE + GPIO_BSRR) = active | (((uint32_t)OUTPUT_MASK & ~active) << 16);
}

static void apply_outputs(uint32_t requested, uint32_t now)
{
    const uint32_t masks[] = { A_EXTEND | A_RETRACT, B_EXTEND | B_RETRACT };
    for (unsigned int i = 0; i < 2u; ++i)
    {
        uint32_t running = active_output & masks[i];
        uint32_t direction = requested & masks[i];
        if (running != 0u && direction != running)
        {
            brake_state[i].stopped_at = now;
        }
        if (direction != 0u)
        {
            if (brake_state[i].last_direction != 0u && direction != brake_state[i].last_direction &&
                now - brake_state[i].stopped_at < REVERSE_BRAKE_MS)
            {
                requested &= ~masks[i];
            }
            else
            {
                brake_state[i].last_direction = direction;
            }
        }
    }

    set_outputs(requested);
    active_output = requested;
}

void actuator_control_init(void)
{
    REG(RCC + AHB4ENR) |= GPIOE_CLOCK;
    (void)REG(RCC + AHB4ENR);

    set_outputs(0u);
    REG(GPIOE_BASE + GPIO_TYPE) &= ~(uint32_t)OUTPUT_MASK;
    REG(GPIOE_BASE + GPIO_SPEED) &= ~(uint32_t)MODE_MASK;
    REG(GPIOE_BASE + GPIO_PULL) = (REG(GPIOE_BASE + GPIO_PULL) & ~(uint32_t)MODE_MASK) | PULL_DOWN;
    REG(GPIOE_BASE + GPIO_MODE) = (REG(GPIOE_BASE + GPIO_MODE) & ~(uint32_t)MODE_MASK) | OUTPUT_MODE;
}

void actuator_control_poll(uint32_t now)
{
    if (sbus_status.failsafe_active || !sbus_has_recent_command(&sbus_status, now) ||
        link_interruptions != sbus_status.link_interruptions)
    {
        link_interruptions = sbus_status.link_interruptions;
        last_frame = sbus_status.good_frames;
        armed = false;
        apply_outputs(0u, now);
        return;
    }
    if (last_frame == sbus_status.good_frames)
    {
        return;
    }
    last_frame = sbus_status.good_frames;

    uint16_t swb = sbus_status.channels[SWB_CHANNEL];
    uint16_t swc = sbus_status.channels[SWC_CHANNEL];
    uint32_t active = 0u;

    if (swb == 0u || swc == 0u)
    {
        armed = false;
    }
    else if (swc >= SWITCH_UP_MAX && swc <= SWITCH_DOWN_MIN)
    {
        /* 上电和失联恢复后，先回中，才允许推杆动作。 */
        armed = true;
    }
    else if (armed)
    {
        if (swb < SWITCH_UP_MAX)
        {
            active = swc < SWITCH_UP_MAX ? B_EXTEND : B_RETRACT;
        }
        else if (swb > SWITCH_DOWN_MIN)
        {
            active = swc < SWITCH_UP_MAX ? A_RETRACT : A_EXTEND;
        }
    }

    apply_outputs(active, now);
}
