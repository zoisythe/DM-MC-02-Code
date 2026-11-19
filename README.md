# DM-MC-02 固件

目标板为达妙 DM-MC-02，主控为 STM32H723VGT6。固件控制四个 3508 麦轮和一个 M2006 丝杆，使用 CMake、Ninja 和 ST Arm Clang 构建。

## 构建与烧录

在 VS Code 中打开本仓库根目录，安装 STM32CubeIDE for VS Code 扩展及 `st-arm-clang` Bundle，然后在 CMake Tools 选择 `Debug` 或 `Release` 预设。也可以在仓库根目录运行以下命令。

```powershell
cmake --preset Debug
cmake --build --preset Debug
```

命令行需要 CMake 3.23 或更新版本及 Ninja。若当前 PowerShell 已设置 vcpkg 等外部工具链，先运行 `$env:CMAKE_TOOLCHAIN_FILE = $null`。固件输出在 `build/Debug/` 或 `build/Release/`。

通过 ST-Link 和 STM32CubeProgrammer 烧录 `firmware.hex`。使用 `firmware.bin` 时，将起始地址设为 `0x08000000`。

## 接线与上电

- **CAN1** 使用经典 CAN、1 Mbps。C620 电调 ID `1、2、3、4` 分别对应左前、右前、左后、右后。M2006 的 C610 电调使用 ID `5`。
- **遥控器** 的 SBUS 接 P6。左摇杆控制底盘，SWA 上位为平移、下位为旋转。上电或通信恢复后，左摇杆回中才能解锁底盘。
- **底盘** 摇杆直接给出四轮目标转速，中位死区为 1024±40。两种模式共用 2250 rpm 的转子目标上限，原地满杆旋转为 1800 rpm。回中时四轮目标立即归零，制动增强横移阻尼并按同一比例限流。当四轮实测转子转速的绝对值都不超过 30 rpm 时解除制动。
- **丝杆** 用 VRA 控制升降速度。旋钮回中减速至停，顺时针上升，逆时针下降，越偏离中位速度越高。上电前将滑块降到底部，并将 VRA 回中。程序以首帧编码器位置为零点，接近行程两端时自动限速。
- **板载 WS2812** 黄灯表示底盘等待就绪或回中，绿灯表示已解锁，紫灯慢闪表示遥控失联，红灯快闪表示 CAN 或轮速反馈故障。

## 夹爪开合测试

1. 共地时，舵机的电源正负线直接接独立 7.4 V 电源，另用一根参考地线将 7.4 V 负极与 DM-MC-02 的 GND 相连。舵机信号线接 **TIM2_CH1（PA0）**。
2. 按 `docs/` 中的 FS-i6X / iA6B 文档设置 SBUS 和 10 个通道，在辅助通道菜单中将 `CH10` 分配给 `SWD`。
3. 舵机供电后，用 `SWD` 上位开启遥控器。主控首次收到有效的上位指令时张开（500 μs），下拨收紧（1600 μs），拨回上位张开。每次动作输出约 2 秒 50 Hz PWM，保持档位时只执行一次。遥控失联时停止脉冲，恢复后从上位重新开始。

LDX-335MG 重新装配后，以 500 μs 为张开基准。空载试验中，1300 μs 和 1500 μs 时夹口仍留缝，1650 μs 夹紧时约 0.36 A。回到 1600 μs 后仍保持夹紧，电流降到约 0.17 A，因此选用 1600 μs 作为闭合设定。开合脉宽在 `Modules/Gripper/gripper_test.c` 中设置。

带载台架试验中，从 500 μs 张开位置发出 1600 μs 闭合指令约 2 秒，物品夹稳，PWM 停止后目视无松脱。0.17 A 为上述空载夹紧时的电流。

重新装配前的闭合设定为 1050 μs，1100 μs 进一步挤压时实测约 0.3 A。当时 400 μs 与 500 μs 对照时，目视开度无明显差别。

需要保持两侧电源隔离时，用默认输出为低的非反相数字隔离器传递 PWM。例如 [ISO7710F](https://www.ti.com/lit/ds/symlink/iso7710.pdf) 的 `IN` 接 PA0，`VCC1`、`GND1` 接主控侧 3.3 V、GND。`OUT` 接舵机信号，`GND2` 接舵机电源负极，`VCC2` 由该电源稳压至舵机信号允许的电平。两侧地保持隔离。接上舵机前，先以 `GND2` 为参考确认 `OUT` 在空闲时为低、切换 SWD 时输出 50 Hz PWM。

### 无遥控器的舵机台架固件

Windows 上可使用 STM32Cube Bundle 中的 CMake 执行以下命令。

```powershell
cmake --preset Release -B build/GripperBench -DGRIPPER_BENCH=ON
cmake --build build/GripperBench
```

烧录 `build/GripperBench/firmware.hex` 后，舵机信号保持低电平。用 `starm-nm` 从同目录的 `firmware.elf` 查找 `gripper_bench_request_us` 的地址。下例通过 ST-Link 热插拔连接发送 1500 μs（`0x05DC`），把 `$requestAddress` 替换为查得的地址。固件读取请求后立即清零，所以写入后加 `-nv`，用 TIM2 的 `CCR1` 验证脉宽。

```powershell
STM32_Programmer_CLI.exe -c port=SWD mode=HOTPLUG -w16 $requestAddress 0x05DC -nv
```

固件输出约 2 秒 50 Hz PWM，然后回到低电平。每次写入一个值并观察舵盘或夹爪角度。常规 SWD 控制使用 `build/Release/firmware.hex`。

## 源码与参考资料

- `Core/` 存放程序入口和板级驱动。
- `Modules/Chassis/` 存放麦轮解算、速度控制及 CAN 恢复逻辑。
- `Modules/M2006/` 存放丝杆位置、速度控制和 VRA 映射。
- `Modules/Gripper/` 存放 SWD 夹爪开合测试逻辑。
- `Modules/Remote/` 存放 SBUS 接收与解码。
- `docs/` 存放电控通用驱动和 FS-i6X / iA6B 接收器参考资料。
