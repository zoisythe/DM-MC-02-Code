/*
 * chassis.h
 * 四轮麦轮运动解算。只根据摇杆计算四轮目标转速，不直接发送 CAN。
 */
#ifndef CHASSIS_H
#define CHASSIS_H
#include <stdbool.h>
#include <stdint.h>
#define RC_CENTER 1024            /* 遥控通道中位值 */
#define RC_SPAN 800               /* 中位到端点的有效幅度 */
#define RC_DEADBAND 40            /* 中位死区，抑制摇杆小幅抖动 */
#define WHEEL_MAX_RPM 3938        /* 2250 × 1.75，四舍五入到整数转子 rpm */
#define ROTATION_MAX_RPM 3150     /* 1800 × 1.75，原地满杆旋转的轮子目标转子 rpm */
#define FORWARD_INPUT_SIGN (-1)   /* 按现有实车前后方向取 -1 */
#define ROTATION_INPUT_SIGN (-1)  /* 根据实车反馈对调原旋转方向；平移方向不变 */
#define REVERSE_STEERING_TRANSITION 200 /* 归一化倒车输入达到此幅值时完成镜像 */
#define LEFT_VERTICAL_CHANNEL 1   /* CH2：数组下标从 0 开始，对应左杆前后 */
#define LEFT_HORIZONTAL_CHANNEL 0 /* CH1：左杆左右平移 */
#define CHASSIS_MODE_CHANNEL 6     /* CH7 / SWA：上位平移，下位旋转 */
#define CHASSIS_SWITCH_UP_MAX 632u
#define CHASSIS_SWITCH_DOWN_MIN 1416u
#define FRONT_LEFT_FORWARD_SIGN 1
#define FRONT_RIGHT_FORWARD_SIGN (-1)
#define REAR_LEFT_FORWARD_SIGN 1
#define REAR_RIGHT_FORWARD_SIGN (-1)

/* 3508/C620 反馈 ID 为 0x201 + 下标，0x200 指令也按同一下标写入。 */
enum chassis_wheel
{
    WHEEL_FRONT_LEFT = 0,  /* CAN ID 1 */
    WHEEL_FRONT_RIGHT,     /* CAN ID 2 */
    WHEEL_REAR_LEFT,       /* CAN ID 3 */
    WHEEL_REAR_RIGHT,      /* CAN ID 4 */
    CHASSIS_WHEEL_COUNT
};
struct chassis_state
{
    uint32_t armed;    /* 当前运行条件满足 */
    uint32_t moving;   /* 摇杆离开死区，有运动需求 */
    int forward;       /* 前后归一化需求 -1000～1000 */
    int right;         /* 平移模式左右需求 -1000～1000；旋转模式为 0 */
    int clockwise;     /* 单位圆归一化后的旋转输入 -1000～1000，倒车已镜像左右 */
    uint32_t rotation_mode; /* 0 平移、1 旋转；开关中间区间保持上次模式 */
    int targets[CHASSIS_WHEEL_COUNT]; /* 按 CAN ID 顺序存储的目标转子 rpm */
};
extern struct chassis_state chassis;
bool chassis_sticks_centered(uint16_t vertical, uint16_t horizontal);
/* 清除运动目标和运行标记，四个轮子的目标转速同时归零。 */
void chassis_disarm(void);
/* SWA 上位平移、下位旋转；按当前摇杆位置直接解算四轮目标。 */
void chassis_update(int have_command, int ready, uint16_t vertical, uint16_t horizontal,
                    uint16_t mode_switch);
#endif
