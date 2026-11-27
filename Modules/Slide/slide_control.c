#include "slide_control.h"
#include "sbus.h"
#include "servo_position.h"
#include "servo_pwm.h"

enum
{
    RIGHT_HORIZONTAL_CHANNEL = 3, /* SBUS CH4，右摇杆左右。 */
    VRB_CHANNEL = 5,            /* SBUS CH6，遥控器辅助通道菜单需分配为 VRB。 */
    STICK_CENTER = 1024,
    STICK_DEADBAND = 160,
    TRAVEL_STOP_US = 1500,      /* 回中仍缓慢转动时，校准连续旋转舵机的停转脉宽。 */
    TRAVEL_FORWARD_US = 1600,   /* 实车方向相反时对调前后脉宽。 */
    TRAVEL_BACKWARD_US = 1400,

    PITCH_START_US = 800,      /* 上电位置，单位为 μs。 */
};

/* VRB 从 240 到 1807 均分五档，1024 中位对应 1200 μs。 */
static const struct servo_position_level pitch_levels[] = {
    {240u, 800u}, {632u, 1000u}, {1024u, 1200u}, {1415u, 1300u}, {1807u, 1400u},
};
static struct servo_position pitch;
static uint32_t last_frame;
static uint32_t link_interruptions;
static bool travel_armed;
static bool receiving;

void slide_control_init(void)
{
    servo_pwm_enable_slide();
    servo_position_init(&pitch, PITCH_START_US);
    servo_pwm_set_slide_pitch_us(pitch.pulse_us);
}

void slide_control_poll(uint32_t now)
{
    if (sbus_status.failsafe_active || !sbus_has_recent_command(&sbus_status, now) ||
        link_interruptions != sbus_status.link_interruptions)
    {
        link_interruptions = sbus_status.link_interruptions;
        last_frame = sbus_status.good_frames;
        travel_armed = false;
        servo_position_hold(&pitch);
        if (receiving)
        {
            servo_pwm_set_slide_travel_us(TRAVEL_STOP_US);
            servo_pwm_set_slide_pitch_us(0u);
            receiving = false;
        }
        return;
    }
    bool new_frame = last_frame != sbus_status.good_frames;
    if (!receiving && new_frame)
    {
        servo_pwm_set_slide_pitch_us(pitch.pulse_us);
        receiving = true;
    }
    if (servo_position_update(&pitch, pitch_levels, sizeof pitch_levels / sizeof pitch_levels[0],
                              sbus_status.channels[VRB_CHANNEL], new_frame, now))
    {
        servo_pwm_set_slide_pitch_us(pitch.pulse_us);
    }
    if (!new_frame)
    {
        return;
    }
    last_frame = sbus_status.good_frames;

    int travel_offset = (int)sbus_status.channels[RIGHT_HORIZONTAL_CHANNEL] - STICK_CENTER;
    uint16_t travel_pulse = TRAVEL_STOP_US;
    if (travel_offset >= -STICK_DEADBAND && travel_offset <= STICK_DEADBAND)
    {
        travel_armed = true;
    }
    else if (travel_armed)
    {
        travel_pulse = travel_offset > 0 ? TRAVEL_FORWARD_US : TRAVEL_BACKWARD_US;
    }
    servo_pwm_set_slide_travel_us(travel_pulse);

}
