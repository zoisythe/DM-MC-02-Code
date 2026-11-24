#include "slide_control.h"
#include "sbus.h"
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

    VRB_CENTER = 1024,
    VRB_MIN_RAW = 240,
    VRB_MAX_RAW = 1807,
    VRB_DEADBAND = 4,
    VRB_START_CENTER_TOLERANCE = 32,
    PITCH_USE_VRB = 1,         /* 0：上电输出固定脉宽，1：由 VRB 控制。 */
    PITCH_CENTER_US = 1200,    /* VRB 中位，单位为 μs。 */
    PITCH_MIN_RAW_US = 600,    /* 修正后，VRB 向上到底是 SBUS 低值端。 */
    PITCH_MAX_RAW_US = 1400,   /* 修正后，VRB 向下到底是 SBUS 高值端。 */
};

static uint32_t last_frame;
static uint32_t link_interruptions;
static bool travel_armed;
static bool receiving;
static bool pitch_armed;

void slide_control_init(void)
{
    servo_pwm_enable_slide();
    servo_pwm_set_slide_pitch_us(PITCH_CENTER_US);
}

static uint16_t pitch_pulse_us(int offset)
{
    if (offset >= -VRB_DEADBAND && offset <= VRB_DEADBAND)
    {
        return PITCH_CENTER_US;
    }

    const int minus_span = VRB_CENTER - VRB_MIN_RAW;
    const int plus_span = VRB_MAX_RAW - VRB_CENTER;
    if (offset <= -minus_span)
    {
        return PITCH_MIN_RAW_US;
    }
    if (offset >= plus_span)
    {
        return PITCH_MAX_RAW_US;
    }
    if (offset < 0)
    {
        return (uint16_t)(PITCH_CENTER_US + (-offset - VRB_DEADBAND) *
                          (PITCH_MIN_RAW_US - PITCH_CENTER_US) / (minus_span - VRB_DEADBAND));
    }
    return (uint16_t)(PITCH_CENTER_US + (offset - VRB_DEADBAND) *
                      (PITCH_MAX_RAW_US - PITCH_CENTER_US) / (plus_span - VRB_DEADBAND));
}

void slide_control_poll(uint32_t now)
{
    if (sbus_status.failsafe_active || !sbus_has_recent_command(&sbus_status, now) ||
        link_interruptions != sbus_status.link_interruptions)
    {
        link_interruptions = sbus_status.link_interruptions;
        last_frame = sbus_status.good_frames;
        travel_armed = false;
        if (receiving)
        {
            servo_pwm_set_slide_travel_us(TRAVEL_STOP_US);
            if (PITCH_USE_VRB && pitch_armed)
            {
                servo_pwm_set_slide_pitch_us(0u);
            }
            receiving = false;
        }
        return;
    }
    if (last_frame == sbus_status.good_frames)
    {
        return;
    }
    last_frame = sbus_status.good_frames;
    receiving = true;

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

    if (PITCH_USE_VRB)
    {
        int pitch_offset = (int)sbus_status.channels[VRB_CHANNEL] - VRB_CENTER;
        if (pitch_armed)
        {
            servo_pwm_set_slide_pitch_us(pitch_pulse_us(pitch_offset));
        }
        else if (pitch_offset >= -VRB_START_CENTER_TOLERANCE && pitch_offset <= VRB_START_CENTER_TOLERANCE)
        {
            /* 先让上电的 1200 μs 回中指令保持到 VRB 回到中位附近。 */
            pitch_armed = true;
        }
    }
}
