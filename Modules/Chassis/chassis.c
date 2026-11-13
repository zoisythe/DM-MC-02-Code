/*
 * chassis.c
 * 四轮麦轮运动解算。只根据摇杆计算四轮目标转速，不直接发送 CAN。
 */
#include "chassis.h"
struct chassis_state chassis;
/* 计算摇杆或轮速需求的绝对值，用于死区和归一化。 */
static int magnitude(int n)
{
    return n < 0 ? -n : n;
}

static int ramp_target(int current, int requested)
{
    /* 换向先降到零，再按加速斜坡进入反方向。 */
    if ((current > 0 && requested < 0) || (current < 0 && requested > 0))
    {
        requested = 0;
    }
    int delta = requested - current;
    int remaining = magnitude(delta);
    int limit = magnitude(requested) < magnitude(current)
                    ? TARGET_DECEL_RPM_PER_MS : TARGET_ACCEL_RPM_PER_MS;
    int step = (remaining + TARGET_APPROACH_DIVISOR - 1) / TARGET_APPROACH_DIVISOR;
    if (step > limit)
    {
        step = limit;
    }
    return current + (delta < 0 ? -step : step);
}

/* 将 SBUS 原始通道值减去中位，应用死区后映射到 -1000～1000。 */
static int axis(uint16_t raw)
{
    int n = (int)raw - RC_CENTER;
    int a = magnitude(n);
    if (a <= RC_DEADBAND)
    {
        return 0;
    }
    if (a > RC_SPAN)
    {
        a = RC_SPAN;
    }
    int result = (a - RC_DEADBAND) * 1000 / (RC_SPAN - RC_DEADBAND);
    return n < 0 ? -result : result;
}

bool chassis_sticks_centered(uint16_t vertical, uint16_t horizontal)
{
    return axis(vertical) == 0 && axis(horizontal) == 0;
}
/* 单位圆内保留幅度；圆外按同一比例缩放两轴，保持方向。
 * 使用向上取整的整数平方根，避免浮点库并保证舍入后模长不超过 1000。 */
static void normalize_rotation_axes(int *forward, int *turn)
{
    uint32_t squared = (uint32_t)(*forward * *forward + *turn * *turn);
    if (squared <= 1000000u)
    {
        return;
    }
    /* 根号内放大 32² 倍，保留根号的小数精度；最大值 2048000000，未溢出。 */
    squared *= 1024u;
    uint32_t low = 32001u, high = 45255u;
    while (low < high)
    {
        uint32_t middle = (low + high) / 2u;
        if (middle * middle < squared)
        {
            low = middle + 1u;
        }
        else
        {
            high = middle;
        }
    }
    *forward = *forward * 32000 / (int)low;
    *turn = *turn * 32000 / (int)low;
}
/* 清除运动目标和运行标记，四个轮子的目标转速同时归零。 */
void chassis_disarm(void)
{
    chassis.armed = 0;
    chassis.moving = 0;
    chassis.forward = 0;
    chassis.right = 0;
    chassis.clockwise = 0;
    for (int i = 0; i < CHASSIS_WHEEL_COUNT; ++i)
    {
        chassis.targets[i] = 0;
    }
}

void chassis_decelerate(uint32_t elapsed_ms)
{
    static int start_targets[CHASSIS_WHEEL_COUNT];
    chassis.moving = 0;
    chassis.forward = 0;
    chassis.right = 0;
    chassis.clockwise = 0;

    if (elapsed_ms == 0u)
    {
        for (int i = 0; i < CHASSIS_WHEEL_COUNT; ++i)
        {
            start_targets[i] = chassis.targets[i];
        }
    }
    int remaining_ms = elapsed_ms < CHASSIS_STOP_RAMP_MS
                           ? (int)(CHASSIS_STOP_RAMP_MS - elapsed_ms) : 0;
    for (int i = 0; i < CHASSIS_WHEEL_COUNT; ++i)
    {
        chassis.targets[i] = start_targets[i] * remaining_ms / (int)CHASSIS_STOP_RAMP_MS;
    }
}

/* SWA 上位平移、下位旋转；中间区间保持上次模式（上电默认平移）。
 * 旋转模式将前后与旋转两轴按欧几里得模长归一化，不再优先截掉旋转输入。 */
void chassis_update(int have_command, int ready, uint16_t vertical, uint16_t horizontal,
                    uint16_t mode_switch)
{
    if (!have_command || !ready)
    {
        chassis_disarm();
        return;
    }
    if (mode_switch < CHASSIS_SWITCH_UP_MAX)
    {
        chassis.rotation_mode = 0;
    }
    else if (mode_switch > CHASSIS_SWITCH_DOWN_MIN)
    {
        chassis.rotation_mode = 1;
    }
    int f = FORWARD_INPUT_SIGN * axis(vertical);
    int horizontal_input = axis(horizontal);
    int r = 0, turn_rpm = 0;
    if (chassis.rotation_mode != 0u)
    {
        if (f <= -REVERSE_STEERING_TRANSITION)
        {
            horizontal_input = -horizontal_input;
        }
        else if (f < 0)
        {
            horizontal_input = horizontal_input * (REVERSE_STEERING_TRANSITION + 2 * f) /
                               REVERSE_STEERING_TRANSITION;
        }
        normalize_rotation_axes(&f, &horizontal_input);
        turn_rpm = ROTATION_INPUT_SIGN * horizontal_input * ROTATION_MAX_RPM / 1000;
    }
    else
    {
        r = horizontal_input;
    }
    int neutral = f == 0 && r == 0 && turn_rpm == 0;
    chassis.armed = 1;
    chassis.forward = f;
    chassis.right = r;
    chassis.clockwise = chassis.rotation_mode != 0u ? horizontal_input : 0;
    chassis.moving = (uint32_t)!neutral;
    if (neutral)
    {
        for (int i = 0; i < CHASSIS_WHEEL_COUNT; ++i)
        {
            chassis.targets[i] = 0;
        }
        return;
    }

    /* X 型麦轮按轮位解算，随后应用各电机的前进符号。 */
    int forward_rpm = f * WHEEL_MAX_RPM / 1000;
    int right_rpm = r * WHEEL_MAX_RPM / 1000;
    int demand[CHASSIS_WHEEL_COUNT] = {
        [WHEEL_FRONT_LEFT] = forward_rpm - right_rpm + turn_rpm,
        [WHEEL_FRONT_RIGHT] = forward_rpm + right_rpm - turn_rpm,
        [WHEEL_REAR_LEFT] = forward_rpm + right_rpm + turn_rpm,
        [WHEEL_REAR_RIGHT] = forward_rpm - right_rpm - turn_rpm,
    };
    const int sign[CHASSIS_WHEEL_COUNT] = {
        [WHEEL_FRONT_LEFT] = FRONT_LEFT_FORWARD_SIGN,
        [WHEEL_FRONT_RIGHT] = FRONT_RIGHT_FORWARD_SIGN,
        [WHEEL_REAR_LEFT] = REAR_LEFT_FORWARD_SIGN,
        [WHEEL_REAR_RIGHT] = REAR_RIGHT_FORWARD_SIGN,
    };
    int scale = WHEEL_MAX_RPM;
    for (int i = 0; i < CHASSIS_WHEEL_COUNT; ++i)
    {
        if (magnitude(demand[i]) > scale)
        {
            scale = magnitude(demand[i]);
        }
    }
    for (int i = 0; i < CHASSIS_WHEEL_COUNT; ++i)
    {
        int requested = demand[i] * WHEEL_MAX_RPM / scale * sign[i];
        chassis.targets[i] = ramp_target(chassis.targets[i], requested);
    }
}
