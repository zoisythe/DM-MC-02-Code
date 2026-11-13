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
- **底盘** 两种模式共用 2250 rpm 的电机转子目标上限，原地满杆旋转为 1800 rpm。加速、降速和换向按斜坡进行。松杆时，速度环跟踪四轮同步降速的目标，目标在 300 ms 内降到零。
- **丝杆** 用 VRA 控制升降速度。旋钮回中减速至停，顺时针上升，逆时针下降，越偏离中位速度越高。上电前将滑块降到底部，并将 VRA 回中。程序以首帧编码器位置为零点，接近行程两端时自动限速。
- **板载 WS2812** 黄灯表示底盘等待就绪或回中，绿灯表示已解锁，紫灯慢闪表示遥控失联，红灯快闪表示 CAN 或轮速反馈故障。

## 源码与参考资料

- `Core/` 存放程序入口和板级驱动。
- `Modules/Chassis/` 存放麦轮解算、速度控制及 CAN 恢复逻辑。
- `Modules/M2006/` 存放丝杆位置、速度控制和 VRA 映射。
- `Modules/Remote/` 存放 SBUS 接收与解码。
- `docs/` 存放电控通用驱动和 FS-i6X / iA6B 接收器参考资料。
