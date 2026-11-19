/*
 * chassis_speed_control.c
 * 四轮速度 PI。转速单位为转子 rpm，电流为 C620 协议原始值，16384 对应约 20 A。
 */
#include "chassis_speed_control.h"

enum
{
    DRIVE_KP = 3,
    BRAKE_KP = 2,
    BRAKE_LATERAL_KP = 4
};

/* 速度 PI：先计算误差与候选积分；限流且误差继续推向饱和时，冻结积分避免越积越大。 */
int chassis_speed_current(int target, int measured, int *integral_x20)
{
    /* 用目标减实测求误差；积分用整数的 20 倍保存，避免浮点开销。 */
    int error = target - measured;
    int next = *integral_x20 + error;
    if (next > 20 * CHASSIS_MAX_CURRENT)
    {
        next = 20 * CHASSIS_MAX_CURRENT;
    }
    if (next < -20 * CHASSIS_MAX_CURRENT)
    {
        next = -20 * CHASSIS_MAX_CURRENT;
    }
    int output = DRIVE_KP * error + next / 20;
    /* 输出已经饱和且误差同向时，不继续积累积分。 */
    if (!((output > CHASSIS_MAX_CURRENT && error > 0) ||
          (output < -CHASSIS_MAX_CURRENT && error < 0)))
    {
        *integral_x20 = next;
    }
    output = DRIVE_KP * error + *integral_x20 / 20;
    if (output > CHASSIS_MAX_CURRENT)
    {
        output = CHASSIS_MAX_CURRENT;
    }
    if (output < -CHASSIS_MAX_CURRENT)
    {
        output = -CHASSIS_MAX_CURRENT;
    }
    return output;
}

void chassis_brake_currents(const int measured[CHASSIS_WHEEL_COUNT],
                            int current[CHASSIS_WHEEL_COUNT])
{
    /* X 型麦轮右移方向在四个电机转子坐标系中的符号。 */
    const int lateral_sign[CHASSIS_WHEEL_COUNT] = {
        [WHEEL_FRONT_LEFT] = -FRONT_LEFT_FORWARD_SIGN,
        [WHEEL_FRONT_RIGHT] = FRONT_RIGHT_FORWARD_SIGN,
        [WHEEL_REAR_LEFT] = REAR_LEFT_FORWARD_SIGN,
        [WHEEL_REAR_RIGHT] = -REAR_RIGHT_FORWARD_SIGN,
    };
    int lateral_sum = 0;
    for (int i = 0; i < CHASSIS_WHEEL_COUNT; ++i)
    {
        lateral_sum += lateral_sign[i] * measured[i];
    }
    int lateral_correction = (BRAKE_LATERAL_KP - BRAKE_KP) * lateral_sum / CHASSIS_WHEEL_COUNT;

    int peak = CHASSIS_MAX_CURRENT;
    for (int i = 0; i < CHASSIS_WHEEL_COUNT; ++i)
    {
        current[i] = -BRAKE_KP * measured[i] - lateral_sign[i] * lateral_correction;
        int magnitude = current[i] < 0 ? -current[i] : current[i];
        if (magnitude > peak)
        {
            peak = magnitude;
        }
    }
    for (int i = 0; i < CHASSIS_WHEEL_COUNT; ++i)
    {
        current[i] = current[i] * CHASSIS_MAX_CURRENT / peak;
    }
}
