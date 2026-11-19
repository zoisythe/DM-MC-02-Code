/* 电机控制流程的状态编号；保留原版本数值，便于对照历史运行记录。 */
#ifndef CONTROL_STATE_H
#define CONTROL_STATE_H

#include <stdbool.h>
#include <stdint.h>

enum
{
    CHASSIS_FEEDBACK_TIMEOUT_MS = 100,
    CHASSIS_STOP_FEEDBACK_MS = 20,
    CHASSIS_TX_TIMEOUT_MS = 10,
    CHASSIS_RECOVERY_WAIT_MS = 100,
    CHASSIS_READY_FRAMES = 20
};

enum control_phase
{
    CONTROL_INITIALIZING_CLOCK = 1, /* 正在配置时钟 */
    CONTROL_INITIALIZING_CAN = 2,   /* 正在配置 CAN */
    CONTROL_WAITING = 3,            /* 已启动，等待运行条件满足 */
    CONTROL_RUNNING = 4,            /* 正在计算并发送驱动电流 */
    CONTROL_RECOVERING = 7,         /* 通信异常后暂发零电流，等待恢复 */
    CONTROL_ARMED = 8,              /* 已回中解锁，等待拨杆 */
    CONTROL_BRAKING = 9            /* 松杆后立即制动，等待四轮实际停稳 */
};

enum recovery_reason
{
    RECOVERY_NONE = 0,
    RECOVERY_STALE_FEEDBACK = 2, /* 电机反馈过期 */
    RECOVERY_CAN_ERROR = 3,      /* CAN 错误被动或总线关闭 */
    RECOVERY_TX_TIMEOUT = 4,     /* 发送请求等待超过 10 ms */
    RECOVERY_RX_OVERFLOW = 5     /* 接收 FIFO 溢出或丢帧 */
};

struct chassis_control_state
{
    enum control_phase phase;
    enum recovery_reason recovery_reason;
    uint32_t recovery_started_ms, recovery_zero_frames;
    uint32_t wait_frame, remote_interruptions;
    uint32_t tx_started_ms;
    bool ready_for_center;
    bool tx_active, tx_nonzero, tx_recovery_zero, tx_cancel_requested, tx_timed_out;
};

struct chassis_control_input
{
    uint32_t now, remote_frame, remote_interruptions;
    enum recovery_reason can_fault;
    bool remote_valid, centered;
    bool hardware_ready, feedback_ready, feedback_fresh, wheels_stopped;
    bool tx_pending, tx_sent;
};

struct chassis_control_output
{
    bool cancel_tx, tx_completed, tx_failed, tx_timeout, recovery_started, remote_stop;
};

struct chassis_control_output chassis_control_step(struct chassis_control_state *state,
                                                  const struct chassis_control_input *input);
void chassis_control_note_tx(struct chassis_control_state *state, uint32_t now, bool nonzero);

#endif
