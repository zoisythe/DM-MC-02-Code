#include "chassis_control.h"
#include "gripper_test.h"
#include "m2006_control.h"
#include "servo_pwm.h"

int main(void)
{
    /* 3508 麦轮底盘：SWA 上位平移，下位旋转。 */
    chassis_control_init();

    /* M2006 丝杆：VRA 控制升降速度，共享底盘初始化的 CAN 和遥控串口。 */
    m2006_control_init();
    servo_pwm_init();

    while (1)
    {
        /* 持续收遥控和电机反馈，模块内部按 1 ms 更新四轮控制。 */
        uint32_t now = chassis_control_poll();

        m2006_control_poll();
        gripper_test_poll(now);
    }
}
