#include "gripper_test.h"
#include "sbus.h"
#include "servo_pwm.h"

enum
{
    SWD_CHANNEL = 9u, /* SBUS CH10 */
    SWD_UP_MAX = 632u,
    SWD_DOWN_MIN = 1416u,
    GRIPPER_OPEN_US = 500u,    /* 重新装配后的张开基准。 */
    GRIPPER_CLOSE_US = 1600u,  /* 重新装配后空载保持夹紧时实测约 0.17 A。 */
    GRIPPER_DRIVE_MS = 2000u,
};

static enum { WAIT_FOR_UP, SWITCH_UP, SWITCH_DOWN } switch_position;
static uint32_t last_frame, link_interruptions, move_started_ms;
static bool driving;

static void start_move(uint16_t pulse_us, uint32_t now)
{
    servo_pwm_set_pulse_us(pulse_us);
    move_started_ms = now;
    driving = true;
}

void gripper_test_poll(uint32_t now)
{
    if (sbus_status.failsafe_active || !sbus_has_recent_command(&sbus_status, now) ||
        link_interruptions != sbus_status.link_interruptions)
    {
        link_interruptions = sbus_status.link_interruptions;
        last_frame = sbus_status.good_frames;
        switch_position = WAIT_FOR_UP;
        if (driving)
        {
            servo_pwm_stop();
            driving = false;
        }
        return;
    }

    if (driving && now - move_started_ms >= GRIPPER_DRIVE_MS)
    {
        servo_pwm_stop();
        driving = false;
    }
    if (last_frame == sbus_status.good_frames)
    {
        return;
    }
    last_frame = sbus_status.good_frames;

    uint16_t swd = sbus_status.channels[SWD_CHANNEL];
    if (swd > 0u && swd < SWD_UP_MAX && switch_position != SWITCH_UP)
    {
        switch_position = SWITCH_UP;
        start_move(GRIPPER_OPEN_US, now);
    }
    else if (swd > SWD_DOWN_MIN && switch_position == SWITCH_UP)
    {
        switch_position = SWITCH_DOWN;
        start_move(GRIPPER_CLOSE_US, now);
    }
}
