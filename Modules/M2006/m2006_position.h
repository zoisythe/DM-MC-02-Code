/* 丝杆位置换算与多圈累计，全部位置均以转子编码器计数表示。 */
#ifndef M2006_POSITION_H
#define M2006_POSITION_H
#include <stdint.h>
#define M2006_DIRECTION -1 /* 实测修正：电机负转上升，正转下降。 */
#define M2006_LEAD_MM 8
#define M2006_TRAVEL_MM 300
#define M2006_GEAR_RATIO 36
#define M2006_COUNTS_PER_MM (8192 * M2006_GEAR_RATIO / M2006_LEAD_MM)
#define M2006_FULL_COUNTS (M2006_TRAVEL_MM * M2006_COUNTS_PER_MM)
#define M2006_MAX_ROTOR_RPM 3600 /* 输出轴 100 rpm，约 13.3 mm/s。 */
#define M2006_ACCEL_RPM_PER_MS 12
#define M2006_END_SLOWDOWN_MM 30
#define M2006_END_MARGIN_COUNTS (M2006_COUNTS_PER_MM / 10) /* 约 0.1 mm。 */
#define M2006_POSITION_GAP_MS 5u /* 3600 rpm 时半圈约 8.3 ms；丢位置后锁止至重启。 */
struct m2006_position_state {
    int32_t count;
    uint32_t last_ms;
    uint16_t last_angle;
    unsigned initialized, fault;
    int speed;
};
extern struct m2006_position_state m2006_position;
void m2006_position_feedback(uint16_t angle, uint32_t now);
/* 对带符号的升降速度做斜坡和行程限制，返回电机方向下的转子 rpm。 */
int m2006_position_speed(int requested_rpm);
void m2006_position_stop(void);
#endif
