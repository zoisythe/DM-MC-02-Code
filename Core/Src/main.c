#include "actuator_control.h"
#include "board.h"
#include "chassis_control.h"
#include "gripper_test.h"
#include "m2006_control.h"
#include "servo_pwm.h"
#include "slide_control.h"

int main(void)
{
    /* 推杆 GPIO 优先拉低，避免底盘初始化期间误动作。 */
    actuator_control_init();

    /* 3508 麦轮底盘：SWA 上位平移，下位旋转。 */
    chassis_control_init();

    /* M2006 丝杆：VRA 控制升降速度，共享底盘初始化的 CAN 和遥控串口。 */
    m2006_control_init();
    servo_pwm_init();
    gripper_test_init();
    slide_control_init();

    /* 初始化完成后，使能 PC15 对应的对外 5 V 和 PC13 对应的 VCC_OUT2。 */
    board_enable_5v_output();
    board_enable_power_out2();

    while (1)
    {
        /* 持续收遥控和电机反馈，模块内部按 1 ms 更新四轮控制。 */
        uint32_t now = chassis_control_poll();

        m2006_control_poll();
        gripper_test_poll(now);
        slide_control_poll(now);
        actuator_control_poll(now);
    }
}
