#include "ym3260_control.h"
#include "sbus.h"
#include "servo_pwm.h"

enum
{
    VRB_CHANNEL = 5, /* SBUS CH6，遥控器辅助通道菜单需分配为 VRB。 */
    VRB_CENTER = 1024,
    VRB_MIN_RAW = 240,
    VRB_MAX_RAW = 1807,
    VRB_ARM_TOLERANCE = 32,
    VRB_DEADBAND = 4,

    /* 将零位脉宽标定到期望的上电姿态。沿用原 ±90° 脉宽跨度的一半。 */
    YM3260_ZERO_US = 1500,
    YM3260_MINUS_45_DELTA_US = -450,
    YM3260_PLUS_45_DELTA_US = 450,
};

static uint32_t last_frame;
static uint32_t link_interruptions;
static int remote_center;
static bool armed;
static bool output_active;

static uint16_t target_pulse_us(int offset)
{
    if (offset >= -VRB_DEADBAND && offset <= VRB_DEADBAND)
    {
        return YM3260_ZERO_US;
    }

    const int minus_span = remote_center - VRB_MIN_RAW;
    const int plus_span = VRB_MAX_RAW - remote_center;
    if (offset <= -minus_span)
    {
        return YM3260_ZERO_US + YM3260_MINUS_45_DELTA_US;
    }
    if (offset >= plus_span)
    {
        return YM3260_ZERO_US + YM3260_PLUS_45_DELTA_US;
    }
    int pulse_delta = offset < 0 ? (-offset - VRB_DEADBAND) * YM3260_MINUS_45_DELTA_US / (minus_span - VRB_DEADBAND)
                                 : (offset - VRB_DEADBAND) * YM3260_PLUS_45_DELTA_US / (plus_span - VRB_DEADBAND);
    return (uint16_t)(YM3260_ZERO_US + pulse_delta);
}

void ym3260_control_poll(uint32_t now)
{
    if (sbus_status.failsafe_active || !sbus_has_recent_command(&sbus_status, now) ||
        link_interruptions != sbus_status.link_interruptions)
    {
        link_interruptions = sbus_status.link_interruptions;
        last_frame = sbus_status.good_frames;
        armed = false;
        if (output_active)
        {
            servo_pwm_set_pa2_pulse_us(0u);
            output_active = false;
        }
        return;
    }
    if (last_frame == sbus_status.good_frames)
    {
        return;
    }
    last_frame = sbus_status.good_frames;

    int raw = (int)sbus_status.channels[VRB_CHANNEL];
    if (!armed)
    {
        if (raw >= VRB_CENTER - VRB_ARM_TOLERANCE && raw <= VRB_CENTER + VRB_ARM_TOLERANCE)
        {
            remote_center = raw;
            armed = true;
        }
        return;
    }

    int offset = raw - remote_center;
    if (output_active || offset < -VRB_DEADBAND || offset > VRB_DEADBAND)
    {
        servo_pwm_set_pa2_pulse_us(target_pulse_us(offset));
        output_active = true;
    }
}
