#ifndef GRIPPER_TEST_H
#define GRIPPER_TEST_H

#include <stdint.h>

/* 在 PWM 时基初始化后设置夹爪俯仰的上电测试位置。 */
void gripper_test_init(void);

/* 处理 SWD 的开合命令。now 为共享的毫秒计数。 */
void gripper_test_poll(uint32_t now);

#endif
