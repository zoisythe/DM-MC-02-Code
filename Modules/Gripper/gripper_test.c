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
};

/* PA2 上电位置。右摇杆纵轴从 240 到 1807 均分五档。 */
static const uint16_t gripper_pitch_start_us =
#include "../../gripper_pitch_us.txt"
    ;
static const struct servo_position_level pitch_levels[] = {
    {240u, 950u}, {632u, 1200u}, {1024u, 1450u}, {1415u, 1700u}, {1807u, 2000u},
};
static struct servo_position pitch;

static enum { WAIT_FOR_UP, SWITCH_UP, SWITCH_DOWN } switch_position;
static uint32_t last_frame, link_interruptions, move_started_ms;
static bool driving;

void gripper_test_init(void)
{
    servo_position_init(&pitch, gripper_pitch_start_us);
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
    if (sbus_status.failsafe_active || !sbus_has_recent_command(&sbus_status, now) ||
        link_interruptions != sbus_status.link_interruptions)
    {
        link_interruptions = sbus_status.link_interruptions;
        last_frame = sbus_status.good_frames;
        switch_position = WAIT_FOR_UP;
        servo_position_hold(&pitch);
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
    bool new_frame = last_frame != sbus_status.good_frames;
    if (servo_position_update(&pitch, pitch_levels, sizeof pitch_levels / sizeof pitch_levels[0],
                              sbus_status.channels[PITCH_STICK_CHANNEL], new_frame, now))
    {
        servo_pwm_set_pa2_pulse_us(pitch.pulse_us);
    }
    if (!new_frame)
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
