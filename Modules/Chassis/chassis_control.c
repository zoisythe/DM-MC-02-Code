/*
 * chassis_control.c
 * 四轮控制流程：接收遥控与四电机反馈，计算速度 PI，发送 CAN 电流指令并处理通信恢复。
 */
#include "chassis_speed_control.h"
#include "chassis.h"
#include "board.h"
#include "control_state.h"
#include "remote_uart.h"
#include "chassis_control.h"
#include "m2006_control.h"
#include "sbus.h"
#include "status_led.h"

#define CHASSIS_TX_MASK 1u /* CAN 发送缓冲区 0 */
#define CAN_ERROR_PASSIVE (1u << 5)
#define CAN_BUS_OFF (1u << 7)

/* 错误快照只用于调试；错误次数不等于损坏帧总数。 */
struct can_error_event
{
    uint32_t psr;             /* CAN 协议状态寄存器快照 */
    uint32_t ecr;             /* CAN 错误计数寄存器快照 */
    uint32_t ir;              /* CAN 中断/事件标志快照 */
    uint32_t milliseconds;    /* 模块运行毫秒数 */
    uint32_t cycles;          /* 错误发生时的 CPU 周期计数 */
    uint32_t phase;           /* 控制流程状态，编号定义在 control_state.h */
    int command_current;      /* 电流指令，使用该电调协议的原始单位 */
    uint32_t pins;            /* 故障时 GPIO 引脚输入状态 */
    uint32_t txbrp;           /* 故障时待发送请求标记 */
    uint32_t txbto;           /* 故障时成功发送标记 */
    uint32_t txbcf;           /* 故障时发送取消标记 */
    uint32_t requested;       /* 已请求发送的控制帧数 */
    uint32_t completed;       /* 已确认成功发送的控制帧数 */
    uint32_t feedback_frames; /* 已收到的有效电机反馈帧数 */
};
/* 各状态字段供调试器观察，volatile 确保按实际值读写。 */
volatile struct
{
    uint32_t phase;                    /* 控制流程状态，编号定义在 control_state.h */
    uint32_t milliseconds;             /* 模块运行毫秒数 */
    uint32_t requested;                /* 已请求发送的控制帧数 */
    uint32_t completed;                /* 已确认成功发送的控制帧数 */
    uint32_t feedback_frames;          /* 已收到的有效电机反馈帧数 */
    uint32_t last_feedback_ms;         /* 最近一次实际收到有效电机反馈的毫秒时刻 */
    uint32_t psr;                      /* CAN 协议状态寄存器快照 */
    uint32_t ecr;                      /* CAN 错误计数寄存器快照 */
    uint32_t stop_reason;              /* 保留的停止原因字段，0 表示无停止原因 */
    uint32_t test_start_ms;            /* 最近一次动作开始的毫秒时刻 */
    int command_current;               /* 电流指令，使用该电调协议的原始单位 */
    int rotor_rpm;                     /* 实测转子转速 rpm，不是减速后输出轴转速 */
    int measured_current;              /* 电调反馈的电流原始值 */
    uint32_t rotor_angle;              /* 转子单圈编码器原始角度值 */
    uint32_t last_tx_ms;               /* 最近提交发送请求的毫秒时刻 */
    uint32_t last_feedback_id;         /* 最近有效电机反馈的 CAN ID */
    uint32_t startup_errors;           /* 接入 CAN 的前 100 ms 中观察到的错误次数 */
    uint32_t fault_psr;                /* 保留的历史故障协议状态字段 */
    uint32_t fault_ecr;                /* 保留的历史故障计数字段 */
    uint32_t fault_ms;                 /* 保留的历史故障时间字段 */
    uint32_t peak_abs_rotor_rpm;       /* 运行期间实测转子转速绝对值峰值 */
    uint32_t peak_abs_command;         /* 运行期间电流指令绝对值峰值 */
    uint32_t capture_magic;            /* 调试记录标识，用于识别数据结构 */
    uint32_t capture_version;          /* 原控制版本编号，便于对照历史记录 */
    uint32_t error_events;             /* 观察到的错误事件总数 */
    struct can_error_event events[16]; /* 仅保留最早 16 个错误快照 */
    uint32_t retry_count;              /* 软件重试统计，不含全部硬件重发次数 */
    uint32_t tx_timeouts;              /* 发送等待超过 10 ms 的次数 */
    uint32_t recoveries;               /* 进入通信恢复流程的次数 */
    uint32_t busoff_count;             /* 观察到 CAN 总线关闭的次数 */
    uint32_t last_recovery_reason;     /* 最近恢复原因，定义在 control_state.h */
    uint32_t recovery_until_ms;        /* 本次恢复等待期结束的毫秒时刻 */
    uint32_t action_active;            /* 当前处于驱动或减速阶段 */
    uint32_t action_request_ms;        /* 最近动作触发的毫秒时刻 */
    uint32_t drive_ms;                 /* 本次触发后实际经过的毫秒数 */
    uint32_t actions_completed;        /* 动作完成的累计次数 */
    uint32_t remote_stops;             /* 遥控或运行条件导致取消输出的累计次数 */
    int action_direction;              /* M2006 方向：1 正向、-1 反向 */
    int target_rotor_rpm;              /* 目标转子转速 rpm */
    int speed_integral_x20;            /* 速度 PI 的整数积分存储值，使用时除以 20 */
    uint32_t recovery_zero_frames;     /* 当前恢复期成功发送的零电流帧数 */
} chassis_status;

struct motor_feedback
{
    uint32_t feedback_frames;    /* 已收到的有效电机反馈帧数 */
    uint32_t last_feedback_ms;   /* 最近一次实际收到有效电机反馈的毫秒时刻 */
    int rotor_rpm;               /* 实测转子转速 rpm，不是减速后输出轴转速 */
    int measured_current;        /* 电调反馈的电流原始值 */
    uint32_t rotor_angle;        /* 转子单圈编码器原始角度值 */
    int command_current;         /* 电流指令，使用该电调协议的原始单位 */
    int integral_x20;            /* 该轮独立的整数积分存储值，使用时除以 20 */
    uint32_t peak_abs_rotor_rpm; /* 运行期间实测转子转速绝对值峰值 */
    uint32_t peak_abs_command;   /* 运行期间电流指令绝对值峰值 */
};
volatile struct motor_feedback motors[CHASSIS_WHEEL_COUNT];
volatile uint32_t feedback_mask;
static struct chassis_control_state control;
static bool rx_overflow, busoff_seen, startup_complete;
/* 四轮各至少收到 20 帧，且最近反馈未超过 timeout 毫秒时，才认为反馈已就绪。 */
static bool all_feedback_ready(uint32_t now, uint32_t timeout)
{
    for (uint32_t i = 0; i < CHASSIS_WHEEL_COUNT; ++i)
    {
        if (motors[i].feedback_frames < CHASSIS_READY_FRAMES ||
            now - motors[i].last_feedback_ms >= timeout)
        {
            return false;
        }
    }
    return true;
}

/* 记录当前模块的初始化故障并停止；phase 高位 0xE 表示初始化异常。 */
static void control_fail(uint32_t code)
{
    chassis_status.phase = 0xE0000000u | code;
    for (;;)
    {
        __asm volatile("nop");
    }
}

/* 把 CAN 反馈里的无符号 16 位数据还原成有正负的转速或电流。 */
static int signed16(uint32_t value)
{
    value &= 0xFFFFu;
    return (value & 0x8000u) ? (int)value - 65536 : (int)value;
}
/* 每次主循环读取 CAN 错误并保存最早 16 个快照，避免后续成功帧覆盖错误原因。 */
static void poll_can_errors(void)
{

    uint32_t psr = REG(CAN + PSR), ecr = REG(CAN + ECR), ir = REG(CAN + 0x50u);
    uint32_t lec = psr & 7u, flags = ir & 0x1B800000u;
    chassis_status.psr = psr;
    chassis_status.ecr = ecr;
    if ((lec > 0u && lec < 7u) || (ecr & 0x00FF0000u) != 0u || flags != 0u)
    {
        uint32_t slot = chassis_status.error_events;
        if (slot < 16u)
        {
            volatile struct can_error_event *event = &chassis_status.events[slot];
            event->psr = psr;
            event->ecr = ecr;
            event->ir = ir;
            event->milliseconds = chassis_status.milliseconds;
            event->cycles = REG(0xE0001004u);
            event->phase = chassis_status.phase;
            event->command_current = chassis_status.command_current;
            event->pins = REG(GPIOD + 0x10u);
            event->txbrp = REG(CAN + TXBRP);
            event->txbto = REG(CAN + TXBTO);
            event->txbcf = REG(CAN + 0xDCu);
            event->requested = chassis_status.requested;
            event->completed = chassis_status.completed;
            event->feedback_frames = chassis_status.feedback_frames;
            __asm volatile("dsb" ::: "memory");
        }
        if (slot != 0xFFFFFFFFu)
        {
            chassis_status.error_events = slot + 1u;
        }

        if (flags != 0u)
        {
            REG(CAN + 0x50u) = flags;
        }
    }
}
/* 取出 CAN FIFO 中的反馈，检查 ID/帧长后更新转速、电流、角度和实际收包时间。 */
static void receive_feedback(void)
{
    if ((REG(CAN + RXF0S) & (1u << 25)) != 0u)
    {
        m2006_control_feedback_lost();
        REG(CAN + 0x50u) = 1u << 3;
        rx_overflow = true;
    }
    for (uint32_t n = 0; n < 8; n++)
    {
        uint32_t state = REG(CAN + RXF0S);
        if ((state & 127u) == 0u)
        {
            return;
        }
        uint32_t index = (state >> 8) & 63u, addr = RAM + 16u * index;
        uint32_t header = REG(addr), control = REG(addr + 4u);
        /* 共享接收 FIFO：在释放帧之前分发 M2006 反馈，四轮处理保持原样。 */
        m2006_control_feedback(header, control, REG(addr + 8u), REG(addr + 12u));
        uint32_t id = (header >> 18) & 0x7FFu;
        if (id >= 0x201u && id <= 0x204u && (header & ((1u << 30) | (1u << 29))) == 0u &&
            ((control >> 16) & 0x3Fu) == 8u && (control & (3u << 20)) == 0u)
        {
            uint32_t a = REG(addr + 8u), b = REG(addr + 12u), motor = id - 0x201u;
            volatile struct motor_feedback *m = &motors[motor];
            m->rotor_angle = ((a & 255u) << 8) | ((a >> 8) & 255u);
            m->rotor_rpm = signed16((((a >> 16) & 255u) << 8) | (a >> 24));
            uint32_t speed = (uint32_t)((m->rotor_rpm < 0) ? -m->rotor_rpm : m->rotor_rpm);
            if (speed > m->peak_abs_rotor_rpm)
            {
                m->peak_abs_rotor_rpm = speed;
            }
            m->measured_current = signed16(((b & 255u) << 8) | ((b >> 8) & 255u));
            m->last_feedback_ms = chassis_status.milliseconds;
            ++m->feedback_frames;
            feedback_mask |= 1u << motor;
            if (motor == 0u)
            {
                chassis_status.rotor_angle = m->rotor_angle;
                chassis_status.rotor_rpm = m->rotor_rpm;
                chassis_status.measured_current = m->measured_current;
                chassis_status.last_feedback_ms = m->last_feedback_ms;
                chassis_status.peak_abs_rotor_rpm = m->peak_abs_rotor_rpm;
            }
            chassis_status.last_feedback_id = id;
            ++chassis_status.feedback_frames;
        }
        REG(CAN + RXF0A) = index;
    }
}
/* 把带符号电流转换成线上的高字节在前格式，供四轮打包共用。 */
static uint32_t wire_current(int current)
{
    uint32_t value = (uint32_t)(uint16_t)current;
    return ((value >> 8) & 255u) | ((value & 255u) << 8);
}
/* 发送 0x200 电流控制帧：每个电机占两字节；保持各模块原来的电机槽位。 */
static void send_current(uint32_t now)
{
    REG(RAM + TX_OFFSET) = 0x200u << 18;
    REG(RAM + TX_OFFSET + 4u) = 8u << 16;

    REG(RAM + TX_OFFSET + 8u) =
        wire_current(motors[WHEEL_FRONT_LEFT].command_current) |
        (wire_current(motors[WHEEL_FRONT_RIGHT].command_current) << 16);
    REG(RAM + TX_OFFSET + 12u) =
        wire_current(motors[WHEEL_REAR_LEFT].command_current) |
        (wire_current(motors[WHEEL_REAR_RIGHT].command_current) << 16);
    bool nonzero = false;
    for (uint32_t i = 0; i < CHASSIS_WHEEL_COUNT; ++i)
    {
        nonzero |= motors[i].command_current != 0;
    }
    __asm volatile("dsb" ::: "memory");
    REG(CAN + TXBAR) = CHASSIS_TX_MASK;
    chassis_control_note_tx(&control, now, nonzero);
    chassis_status.last_tx_ms = now;
    ++chassis_status.requested;
}
/* 启动四轮控制和其遥控接收。上电仅调用一次，不在循环中重复初始化。 */
void chassis_control_init(void)
{
    board_set_fault_handler(control_fail);
    safe_outputs();
    chassis_status.phase = CONTROL_INITIALIZING_CLOCK;
    clock_init();
    status_led_init();
    chassis_status.phase = CONTROL_INITIALIZING_CAN;
    can_init();
    remote_uart_init();

    REG(CAN + CCCR) |= 1u;
    wait_bits(CAN + CCCR, 1u, 1u, 11u);
    REG(CAN + CCCR) |= 2u;
    wait_bits(CAN + CCCR, 3u, 3u, 12u);
    REG(CAN + CCCR) = 3u;
    REG(CAN + CCCR) &= ~1u;
    wait_bits(CAN + CCCR, 1u, 0u, 13u);
    REG(0xE000EDFCu) |= 1u << 24;
    REG(0xE0001004u) = 0;
    REG(0xE0001000u) |= 1u;
    chassis_status.capture_magic = 0x43414E45u;
    chassis_status.capture_version = 11;
    control.phase = CONTROL_WAITING;
    chassis_status.phase = control.phase;
}
/* 运行状态已经由控制状态机确定，电机输出只由本周期状态产生。 */
static void update_motor_commands(uint16_t vertical, uint16_t horizontal)
{
    bool driving = control.phase == CONTROL_RUNNING;
    bool braking = control.phase == CONTROL_BRAKING;
    bool armed = driving || braking || control.phase == CONTROL_ARMED;
    chassis_update(armed, armed, vertical, horizontal, sbus_status.channels[CHASSIS_MODE_CHANNEL]);

    for (uint32_t i = 0; i < CHASSIS_WHEEL_COUNT; ++i)
    {
        int integral = 0;
        int command = 0;
        if (driving && chassis.moving)
        {
            integral = motors[i].integral_x20;
            command = chassis_speed_current(chassis.targets[i], motors[i].rotor_rpm, &integral);
        }
        else if (braking)
        {
            command = chassis_brake_current(motors[i].rotor_rpm);
        }
        motors[i].integral_x20 = integral;
        motors[i].command_current = command;
        uint32_t magnitude = (uint32_t)(command < 0 ? -command : command);
        if (magnitude > motors[i].peak_abs_command)
        {
            motors[i].peak_abs_command = magnitude;
        }
    }
    chassis_status.action_active = driving || braking;
    chassis_status.target_rotor_rpm = chassis.targets[WHEEL_FRONT_LEFT];
    chassis_status.command_current = motors[WHEEL_FRONT_LEFT].command_current;
    chassis_status.speed_integral_x20 = motors[WHEEL_FRONT_LEFT].integral_x20;
    chassis_status.peak_abs_command = motors[WHEEL_FRONT_LEFT].peak_abs_command;
}

static bool all_wheels_slow(void)
{
    for (uint32_t i = 0; i < CHASSIS_WHEEL_COUNT; ++i)
    {
        int rpm = motors[i].rotor_rpm;
        if (rpm < -CHASSIS_BRAKE_STOP_RPM || rpm > CHASSIS_BRAKE_STOP_RPM)
        {
            return false;
        }
    }
    return true;
}

static uint32_t status_color(uint32_t now, bool remote_valid)
{
    if (control.phase == CONTROL_RECOVERING)
    {
        return (now / 125u) % 2u == 0u ? 0x300000u : 0u;
    }
    if (!remote_valid && (sbus_status.failsafe_active || sbus_status.has_command || now >= SBUS_TIMEOUT_MS))
    {
        return (now / 250u) % 2u == 0u ? 0x200028u : 0u;
    }
    return control.phase == CONTROL_WAITING ? 0x282000u : 0x003000u;
}

/* 通信接收持续轮询，底盘状态与 0x200 指令每 1 ms 更新一次。 */
void chassis_control_poll(void)
{
    poll_remote(chassis_status.milliseconds);
    poll_can_errors();
    receive_feedback();
    if ((REG(0xE000E010u) & (1u << 16)) == 0u)
    {
        return;
    }
    uint32_t now = ++chassis_status.milliseconds;
    m2006_control_set_time(now);

    uint32_t psr = chassis_status.psr;
    bool busoff = (psr & CAN_BUS_OFF) != 0u;
    bool can_in_init = (REG(CAN + CCCR) & 1u) != 0u;
    if (busoff)
    {
        if (!busoff_seen)
        {
            ++chassis_status.busoff_count;
        }
        if (can_in_init)
        {
            REG(CAN + CCCR) &= ~1u;
        }
    }
    busoff_seen = busoff;

    enum recovery_reason can_fault = RECOVERY_NONE;
    if ((psr & (CAN_BUS_OFF | CAN_ERROR_PASSIVE)) != 0u || can_in_init)
    {
        can_fault = RECOVERY_CAN_ERROR;
    }
    else if (rx_overflow)
    {
        can_fault = RECOVERY_RX_OVERFLOW;
    }
    rx_overflow = false;

    startup_complete |= now >= CHASSIS_RECOVERY_WAIT_MS;
    bool startup_wait = !startup_complete;
    if (startup_wait)
    {
        uint32_t lec = psr & 7u;
        if ((lec > 0u && lec < 7u) || (chassis_status.ecr & 0x7FFFu) != 0u)
        {
            ++chassis_status.startup_errors;
        }
    }
    uint16_t vertical = sbus_status.channels[LEFT_VERTICAL_CHANNEL];
    uint16_t horizontal = sbus_status.channels[LEFT_HORIZONTAL_CHANNEL];
    bool feedback_ready = all_feedback_ready(now, CHASSIS_FEEDBACK_TIMEOUT_MS);
    bool tx_pending = (REG(CAN + TXBRP) & CHASSIS_TX_MASK) != 0u;
    struct chassis_control_input input = {
        .now = now,
        .remote_frame = sbus_status.good_frames,
        .remote_interruptions = sbus_status.link_interruptions,
        .can_fault = can_fault,
        .remote_valid = !sbus_status.failsafe_active && sbus_has_recent_command(&sbus_status, now),
        .centered = chassis_sticks_centered(vertical, horizontal),
        .hardware_ready = !startup_wait && feedback_ready &&
                          chassis_status.completed >= CHASSIS_READY_FRAMES,
        .feedback_ready = feedback_ready,
        .feedback_fresh = all_feedback_ready(now, CHASSIS_BRAKE_FEEDBACK_MS),
        .wheels_slow = all_wheels_slow(),
        .tx_pending = tx_pending,
        .tx_sent = !tx_pending && (REG(CAN + TXBTO) & CHASSIS_TX_MASK) != 0u,
    };
    struct chassis_control_output output = chassis_control_step(&control, &input);
    if (output.cancel_tx)
    {
        REG(CAN + TXBCR) = CHASSIS_TX_MASK;
    }
    chassis_status.completed += (uint32_t)output.tx_completed;
    chassis_status.retry_count += (uint32_t)output.tx_failed + (uint32_t)output.tx_timeout;
    chassis_status.tx_timeouts += (uint32_t)output.tx_timeout;
    chassis_status.remote_stops += (uint32_t)output.remote_stop;
    chassis_status.recoveries += (uint32_t)output.recovery_started;
    chassis_status.phase = control.phase;
    chassis_status.last_recovery_reason = control.recovery_reason;
    chassis_status.recovery_until_ms = control.recovery_started_ms + CHASSIS_RECOVERY_WAIT_MS;
    chassis_status.recovery_zero_frames = control.recovery_zero_frames;
    update_motor_commands(vertical, horizontal);

    if (!tx_pending && !startup_wait && !busoff && !can_in_init)
    {
        send_current(now);
    }
    status_led_poll(now, status_color(now, input.remote_valid));
}
