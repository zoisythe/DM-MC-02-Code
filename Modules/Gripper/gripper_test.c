#include "gripper_test.h"
#include "sbus.h"
#include "servo_position.h"
#include "servo_pwm.h"

enum
{
    SWD_CHANNEL = 9u, /* SBUS CH10 */
    SWD_UP_MAX = 632u,
    SWD_DOWN_MIN = 1416u,
    GRIPPER_OPEN_US = 500u,    /* 重新装配后的张开基准。 */
    GRIPPER_CLOSE_US = 1600u,  /* 重新装配后空载保持夹紧时实测约 0.17 A。 */
    GRIPPER_DRIVE_MS = 2000u,
    PITCH_STICK_CHANNEL = 2, /* SBUS CH3，右摇杆前后。 */
    PITCH_LOW_US = 950,
    PITCH_HIGH_US = 2000,
    PITCH_SPEED_US_PER_S = 400,
    PITCH_ACCELERATION_US_PER_S2 = 1200,
    PITCH_START_US =
#include "../../gripper_pitch_us.txt"
};

_Static_assert(PITCH_START_US == PITCH_LOW_US || PITCH_START_US == PITCH_HIGH_US,
               "Set gripper_pitch_us.txt to the measured top-of-stick endpoint.");
static const struct servo_position_level pitch_levels[] = {
    {240u, PITCH_LOW_US},
    {501u, 1117u}, {763u, 1283u}, {1024u, 1450u},
    {1285u, 1633u}, {1546u, 1817u},
    {1807u, PITCH_HIGH_US},
};
static struct servo_position pitch;

static enum { STARTUP_PENDING, STARTUP_OPENING, STARTUP_OPENED, STARTUP_FINISHED } startup;
static enum { WAIT_FOR_UP, SWITCH_UP, SWITCH_DOWN } switch_position;
static uint32_t last_frame, link_interruptions, move_started_ms;
static bool driving;

void gripper_test_init(void)
{
    servo_position_init(&pitch, PITCH_START_US, PITCH_SPEED_US_PER_S,
                        PITCH_ACCELERATION_US_PER_S2);
    servo_pwm_enable_pa2(pitch.pulse_us);
}

static void start_move(uint16_t pulse_us, uint32_t now)
{
    servo_pwm_set_pulse_us(pulse_us);
    move_started_ms = now;
    driving = true;
}

void gripper_test_poll(uint32_t now)
{
    /* 进入主循环后执行一次上电张开。 */
    if (startup == STARTUP_PENDING)
    {
        start_move(GRIPPER_OPEN_US, now);
        startup = STARTUP_OPENING;
    }

    bool finished_startup = false;
    if (driving && now - move_started_ms >= GRIPPER_DRIVE_MS)
    {
        servo_pwm_stop();
        driving = false;
        if (startup == STARTUP_OPENING)
        {
            startup = switch_position == WAIT_FOR_UP ? STARTUP_OPENED : STARTUP_FINISHED;
            finished_startup = true;
        }
    }

    if (sbus_status.failsafe_active || !sbus_has_recent_command(&sbus_status, now) ||
        link_interruptions != sbus_status.link_interruptions)
    {
        link_interruptions = sbus_status.link_interruptions;
        last_frame = sbus_status.good_frames;
        switch_position = WAIT_FOR_UP;
        servo_position_hold(&pitch);
        /* 首次连接前仍允许自动张开。已经建立过链路时沿用失联停机。 */
        if (sbus_status.has_command)
        {
            startup = STARTUP_FINISHED;
            if (driving)
            {
                servo_pwm_stop();
                driving = false;
            }
        }
        return;
    }

    bool new_frame = last_frame != sbus_status.good_frames;
    if (new_frame)
    {
        last_frame = sbus_status.good_frames;
        servo_position_select_level(&pitch, pitch_levels, sizeof pitch_levels / sizeof pitch_levels[0],
                                    sbus_status.channels[PITCH_STICK_CHANNEL], now, 0u);
    }
    if (servo_position_step(&pitch, now))
    {
        servo_pwm_set_pa2_pulse_us(pitch.pulse_us);
    }
    if (!new_frame && !finished_startup)
    {
        return;
    }

    uint16_t swd = sbus_status.channels[SWD_CHANNEL];
    if (swd > 0u && swd < SWD_UP_MAX && switch_position != SWITCH_UP)
    {
        switch_position = SWITCH_UP;
        if (startup == STARTUP_OPENED)
        {
            startup = STARTUP_FINISHED;
        }
        else if (startup == STARTUP_FINISHED)
        {
            start_move(GRIPPER_OPEN_US, now);
        }
    }
    else if (swd > SWD_DOWN_MIN && switch_position == SWITCH_UP &&
             startup == STARTUP_FINISHED)
    {
        switch_position = SWITCH_DOWN;
        start_move(GRIPPER_CLOSE_US, now);
    }
}
