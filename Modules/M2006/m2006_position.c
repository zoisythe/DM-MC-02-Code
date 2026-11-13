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
int m2006_position_speed(int percent)
{
    if (percent < 0 || !m2006_position.initialized || m2006_position.fault) {
        m2006_position_stop(); return 0;
    }
    if (percent > 100) percent = 100;
    m2006_position.target = (M2006_FULL_COUNTS / 100) * percent;
    int32_t error = m2006_position.target - m2006_position.count;
    /* 先限误差再乘增益，避免异常数值溢出；30 mm 误差达到限速。 */
    int32_t bounded = error;
    if (bounded > 30 * M2006_COUNTS_PER_MM) bounded = 30 * M2006_COUNTS_PER_MM;
    if (bounded < -30 * M2006_COUNTS_PER_MM) bounded = -30 * M2006_COUNTS_PER_MM;
    int desired = bounded * M2006_POSITION_KP / M2006_COUNTS_PER_MM;
    if (error <= M2006_POSITION_DEADBAND && error >= -M2006_POSITION_DEADBAND) desired = 0;
    int step = desired - m2006_position.speed;
    if (step > M2006_ACCEL_RPM_PER_MS) step = M2006_ACCEL_RPM_PER_MS;
    if (step < -M2006_ACCEL_RPM_PER_MS) step = -M2006_ACCEL_RPM_PER_MS;
    m2006_position.speed += step;
    /* 端点禁止继续向行程外给目标速度。 */
    if ((m2006_position.count <= 0 && m2006_position.speed < 0) ||
        (m2006_position.count >= M2006_FULL_COUNTS && m2006_position.speed > 0))
        m2006_position.speed = 0;
    return m2006_position.speed * M2006_DIRECTION;
}
