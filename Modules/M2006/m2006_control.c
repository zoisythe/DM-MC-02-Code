/* VRA -> 带行程限制的升降速度 -> 转速 PI -> C610 ID5。 */
#include "m2006_control.h"
#include "m2006_remote.h"
#include "m2006_position.h"
#include "m2006_speed_control.h"
#include "board.h"
#include "sbus.h"
#define M2006_TX_MASK 2u
#define M2006_TX_ADDR (RAM + TX_OFFSET + 16u)
volatile struct {
    uint32_t milliseconds, feedback_frames, last_feedback_ms;
    uint32_t requested, completed, tx_timeouts, position_fault;
    int requested_rpm, target_rotor_rpm, rotor_rpm, measured_current, command_current, speed_integral_x20;
} m2006_status;
static uint32_t enabled, tick, processed_tick, pending, last_tx_ms, tx_fault;
void m2006_control_set_time(uint32_t now) { tick = now; }
/* FIFO 溢出可能丢掉跨圈信息；必须手动放回最低点并重启。 */
void m2006_control_feedback_lost(void)
{
    if (enabled) m2006_position.fault = 1;
}
static int signed16(uint32_t value)
{
    value &= 65535u;
    return (value & 32768u) != 0u ? (int)value - 65536 : (int)value;
}
void m2006_control_feedback(uint32_t header, uint32_t control, uint32_t a, uint32_t b)
{
    if (!enabled || ((header >> 18) & 0x7FFu) != 0x205u ||
        (header & ((1u << 30) | (1u << 29))) != 0u ||
        ((control >> 16) & 0x3Fu) != 8u || (control & (3u << 20)) != 0u) return;
    uint16_t angle = (uint16_t)(((a & 255u) << 8) | ((a >> 8) & 255u));
    m2006_position_feedback(angle, tick);
    m2006_status.rotor_rpm = signed16((((a >> 16) & 255u) << 8) | (a >> 24));
    m2006_status.measured_current = signed16(((b & 255u) << 8) | ((b >> 8) & 255u));
    /* 超出解圈设计速度时不能继续信任位置。 */
    if (m2006_status.rotor_rpm > 6000 || m2006_status.rotor_rpm < -6000)
        m2006_position.fault = 1;
    m2006_status.last_feedback_ms = tick;
    ++m2006_status.feedback_frames;
}
void m2006_control_init(void)
{
    enabled = 1;
}
void m2006_control_poll(void)
{
    if (!enabled || processed_tick == tick) return;
    processed_tick = tick;
    uint32_t now = tick;
    m2006_status.milliseconds = now;
    if (m2006_position.initialized && now - m2006_position.last_ms >= M2006_POSITION_GAP_MS)
        m2006_position.fault = 1;
    m2006_status.position_fault = m2006_position.fault;
    uint32_t psr = REG(CAN + PSR);
    if (pending && (REG(CAN + TXBRP) & M2006_TX_MASK) == 0u) {
        if ((REG(CAN + TXBTO) & M2006_TX_MASK) != 0u) ++m2006_status.completed;
        pending = 0;
    }
    bool remote_valid = !sbus_status.failsafe_active && sbus_has_recent_command(&sbus_status, now);
    int requested_rpm = remote_valid ? m2006_remote_speed_rpm(sbus_status.channels[M2006_VRA_CHANNEL]) : 0;
    m2006_status.requested_rpm = requested_rpm;
    int ready = now >= 2000u && remote_valid && m2006_status.feedback_frames >= 20u &&
                !m2006_position.fault && !tx_fault && (psr & 0xA0u) == 0u &&
                (REG(CAN + CCCR) & 1u) == 0u;
    if (ready) {
        int previous_target = m2006_status.target_rotor_rpm;
        int target = m2006_position_speed(requested_rpm);
        int integral = m2006_status.speed_integral_x20;
        /* 停车或换向时清除前一运动方向积累的电流。 */
        if ((previous_target > 0 && target <= 0) || (previous_target < 0 && target >= 0)) {
            integral = 0;
        }
        m2006_status.target_rotor_rpm = target;
        m2006_status.command_current = m2006_speed_current(target, m2006_status.rotor_rpm, &integral);
        m2006_status.speed_integral_x20 = integral;
    } else {
        m2006_position_stop();
        m2006_status.target_rotor_rpm = 0;
        m2006_status.command_current = 0;
        m2006_status.speed_integral_x20 = 0;
        /* 取消尚未发送的旧运动指令；不触碰四轮缓冲区 0。 */
        if (pending) REG(CAN + TXBCR) = M2006_TX_MASK;
    }
    if ((REG(CAN + TXBRP) & M2006_TX_MASK) != 0u) {
        if (now - last_tx_ms >= 10u) {
            REG(CAN + TXBCR) = M2006_TX_MASK;
            ++m2006_status.tx_timeouts;
            tx_fault = 1; /* 重启解除，避免通信故障后继续运动。 */
            m2006_status.command_current = 0;
        }
        return;
    }
    if ((psr & 128u) != 0u || (REG(CAN + CCCR) & 1u) != 0u) return;
    uint32_t value = (uint32_t)(uint16_t)m2006_status.command_current;
    REG(M2006_TX_ADDR) = 0x1FFu << 18;
    REG(M2006_TX_ADDR + 4u) = 8u << 16;
    REG(M2006_TX_ADDR + 8u) = ((value >> 8) & 255u) | ((value & 255u) << 8);
    REG(M2006_TX_ADDR + 12u) = 0u;
    __asm volatile("dsb" ::: "memory");
    REG(CAN + TXBAR) = M2006_TX_MASK;
    last_tx_ms = now;
    pending = 1;
    ++m2006_status.requested;
}
