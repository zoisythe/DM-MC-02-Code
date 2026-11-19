#ifndef GRIPPER_TEST_H
#define GRIPPER_TEST_H

#include <stdint.h>

/* 处理 SWD 的开合命令。now 为共享的毫秒计数。 */
void gripper_test_poll(uint32_t now);

#endif
