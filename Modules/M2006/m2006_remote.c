#include "m2006_remote.h"
#include "m2006_position.h"

int m2006_remote_speed_rpm(uint16_t raw)
{
    const int lower = M2006_VRA_CENTER - M2006_VRA_DEADBAND;
    const int upper = M2006_VRA_CENTER + M2006_VRA_DEADBAND;
    int value = raw;
    if (value < (int)M2006_VRA_LEFT)
    {
        value = M2006_VRA_LEFT;
    }
    if (value > (int)M2006_VRA_RIGHT)
    {
        value = M2006_VRA_RIGHT;
    }
    if (value < lower)
    {
        return (value - lower) * M2006_MAX_ROTOR_RPM / (lower - (int)M2006_VRA_LEFT);
    }
    if (value > upper)
    {
        return (value - upper) * M2006_MAX_ROTOR_RPM / ((int)M2006_VRA_RIGHT - upper);
    }
    return 0;
}
