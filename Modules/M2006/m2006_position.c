#include "m2006_position.h"
struct m2006_position_state m2006_position;
_Static_assert(M2006_DIRECTION == 1 || M2006_DIRECTION == -1, "Direction must be +/-1");
void m2006_position_feedback(uint16_t angle, uint32_t now)
{
    if (angle >= 8192u) { m2006_position.fault = 1; return; }
    if (!m2006_position.initialized) {
        /* 用户每次操作结束回到底端；下次上电将首帧当前位置记为零点。 */
        m2006_position.initialized = 1;
        m2006_position.count = 0;
    } else {
        if (now - m2006_position.last_ms >= M2006_POSITION_GAP_MS)
            m2006_position.fault = 1;
        if (!m2006_position.fault) {
            int delta = (int)angle - (int)m2006_position.last_angle;
            if (delta > 4096) delta -= 8192;
            else if (delta < -4096) delta += 8192;
            m2006_position.count += delta * M2006_DIRECTION;
            /* 超出设定行程 1 mm 后锁止，避免错误方向持续运动。 */
            if (m2006_position.count < -M2006_COUNTS_PER_MM ||
                m2006_position.count > M2006_FULL_COUNTS + M2006_COUNTS_PER_MM)
                m2006_position.fault = 1;
        }
    }
    m2006_position.last_angle = angle;
    m2006_position.last_ms = now;
}
void m2006_position_stop(void) { m2006_position.speed = 0; }

/* 保留原来接近端点时的减速斜率。 */
static int end_speed_limit(int32_t remaining_counts)
{
    if (remaining_counts <= M2006_END_MARGIN_COUNTS) {
        return 0;
    }
    const int32_t slowdown_counts = M2006_END_SLOWDOWN_MM * M2006_COUNTS_PER_MM;
    if (remaining_counts >= slowdown_counts) {
        return M2006_MAX_ROTOR_RPM;
    }
    const int rpm_per_mm = M2006_MAX_ROTOR_RPM / M2006_END_SLOWDOWN_MM;
    return remaining_counts * rpm_per_mm / M2006_COUNTS_PER_MM;
}

int m2006_position_speed(int requested_rpm)
{
    if (!m2006_position.initialized || m2006_position.fault) {
        m2006_position_stop();
        return 0;
    }
    int step = requested_rpm - m2006_position.speed;
    if (step > M2006_ACCEL_RPM_PER_MS) step = M2006_ACCEL_RPM_PER_MS;
    if (step < -M2006_ACCEL_RPM_PER_MS) step = -M2006_ACCEL_RPM_PER_MS;
    m2006_position.speed += step;
    /* 斜坡之后仍受当前方向的行程限制，从端点向内运行时可正常提速。 */
    if (m2006_position.speed > 0) {
        int upper_limit = end_speed_limit(M2006_FULL_COUNTS - m2006_position.count);
        if (m2006_position.speed > upper_limit) {
            m2006_position.speed = upper_limit;
        }
    } else if (m2006_position.speed < 0) {
        int lower_limit = -end_speed_limit(m2006_position.count);
        if (m2006_position.speed < lower_limit) {
            m2006_position.speed = lower_limit;
        }
    }
    return m2006_position.speed * M2006_DIRECTION;
}
