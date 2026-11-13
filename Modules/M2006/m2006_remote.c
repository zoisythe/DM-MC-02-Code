#include "m2006_remote.h"

/* 将丝杆的 VRA 输入转换为整数百分比，并钳位端点。 */
int m2006_remote_percent(const struct sbus_receiver *receiver, uint32_t now)
{
    if (receiver->failsafe_active || !sbus_has_recent_command(receiver, now))
    {
        return -1;
    }
    uint32_t raw = receiver->channels[M2006_VRA_CHANNEL];
    if (raw <= M2006_VRA_LEFT)
    {
        return 0;
    }
    if (raw >= M2006_VRA_RIGHT)
    {
        return 100;
    }
    uint32_t span = M2006_VRA_RIGHT - M2006_VRA_LEFT;
    return (int)(((raw - M2006_VRA_LEFT) * 100u + span / 2u) / span);
}
