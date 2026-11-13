/*
 * m2006_speed_control.c
 * M2006 速度 PI。转速单位为转子 rpm，电流为 C610 协议原始值，1000 对应约 1 A。
 */
#include "m2006_speed_control.h"

/* 速度 PI：先计算误差与候选积分；限流且误差继续推向饱和时，冻结积分避免越积越大。 */
int m2006_speed_current(int target, int measured, int *integral_x20)
{
    /* 用目标减实测求误差；积分用整数的 20 倍保存，避免浮点开销。 */
    int error = target - measured;
    int next = *integral_x20 + error;
    if (next > 20 * M2006_MAX_CURRENT)
    {
        next = 20 * M2006_MAX_CURRENT;
    }
    if (next < -20 * M2006_MAX_CURRENT)
    {
        next = -20 * M2006_MAX_CURRENT;
    }
    /* 原版增益：比例系数为 2，积分每毫秒累加误差后除以 20。 */
    int output = 2 * error + next / 20;
    /* 输出已经饱和且误差同向时，不继续积累积分。 */
    if (!((output > M2006_MAX_CURRENT && error > 0) || (output < -M2006_MAX_CURRENT && error < 0)))
    {
        *integral_x20 = next;
    }
    output = 2 * error + *integral_x20 / 20;
    if (output > M2006_MAX_CURRENT)
    {
        output = M2006_MAX_CURRENT;
    }
    if (output < -M2006_MAX_CURRENT)
    {
        output = -M2006_MAX_CURRENT;
    }
    return output;
}
