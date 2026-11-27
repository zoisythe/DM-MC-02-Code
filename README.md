# DM-MC-02 固件

目标板为达妙 DM-MC-02，主控为 STM32H723VGT6。整车固件控制四个 M3508 麦轮、一个 M2006 升降丝杆、四路舵机和两根电动推杆，全部由 FlySky 接收机的一路 SBUS 输入控制。接线按当前源码和板卡资料整理，实车线束及器件额定值以实物为准。

本轮调整前的源码快照和已编译固件保存在工作区的 `备份/MAIN_版本A`，目录内的 `版本A.md` 记录了回退方式。

## 构建与烧录

在 VS Code 中打开本仓库根目录，安装 STM32CubeIDE for VS Code 扩展及 `st-arm-clang` Bundle，然后在 CMake Tools 选择 `Debug` 或 `Release` 预设。也可以在仓库根目录运行以下命令。

```powershell
cmake --preset Debug
cmake --build --preset Debug
```

命令行需要 CMake 3.23 或更新版本、Ninja 和 `st-arm-clang`。工具链可通过 STM32Cube Bundle 或 `PATH` 提供。固件输出在 `build/Debug/` 或 `build/Release/`。

在 `MAIN` 与 `本机工具` 并排放置的 Windows 工作区，也可使用已有的 `只编译.bat`，或从 `MAIN` 运行以下命令。它使用本机 ARM GCC，生成 `build/firmware.elf`、`build/firmware.hex` 和 `build/firmware.bin`。

```powershell
& '..\本机工具\build.ps1' -ProjectRoot (Get-Location).Path
```

通过 ST-Link 和 STM32CubeProgrammer 烧录 `firmware.hex`。使用 `firmware.bin` 时，将起始地址设为 `0x08000000`。SWD 接 `SWDIO`、`SWCLK`、`GND` 及探头所需的目标电压参考端。SWD 和 USB 用于调试，整车正常遥控运行只需车载供电。

## 整车接线

### 主电源、CAN2 和五个无刷电机

| 线束 | 起点 → 终点 | 数量 |
| --- | --- | --- |
| 主电源输入 | 额定 24 V 主电源正、负极 → 配电板输入正、负极。使用 RoboMaster 中心板时，入口为 XT60 | 1 对 |
| 主控供电 | 配电板 24 V 支路 → DM-MC-02 黄色电源入口 `VCC INPUT / GND` | 1 对 |
| 电调供电 | 配电板 24 V 支路 → 四个 C620、一个 C610 各自的电源正、负极 | 5 对 |
| CAN 信号 | DM-MC-02 白色 CAN2 插座 `P3` 的 `1=CAN_L`、`2=CAN_H` → CAN 集线板或总线 → 五个电调。全程 `H→H`、`L→L`，经典 CAN 1 Mbps | 集线接法为主控 1 对、电调 5 对 |
| 麦轮电机线 | 每个 C620 → 对应 M3508，接三根三相动力线和一条 7 芯数据线 | 4 组 |
| 丝杆电机线 | C610 → M2006，接三根三相动力线和一条 4 芯转子位置反馈线 | 1 组 |

C620 设为 CAN 控制，ID `1` 左前、`2` 右前、`3` 左后、`4` 右后。C610 设为 ID `5`。CAN 总线两个物理端点各保留一个 120 Ω 终端，计算时计入 CAN2 原理图上的 `R9 = 120 Ω`。

`P8` 的 `1=VCC_OUT2`、`2=地` 是当前固件使用的 XT30 动力端。`PC13` 上电初始化时拉低，初始化完成后拉高开启 `VCC_OUT2`，电压跟随主电源输入。`PC15` 开启板上对外 5 V，例如 PWM 接口电源端。接收机 `P6` 使用板上系统 5 V。

### 接收机

接收机实际配置为输出 S.BUS 的 `SERVO/i-BUS` 插座接主控 `P6`，共三芯。`P6-1 S` 接总线信号，`P6-2 5V` 接接收机正极，`P6-3 GND` 接接收机负极。信号经板载反相电路进入 `PD2 / UART5 RX`，串口为 `100000 baud、8E2`。旧接线照片中为 FS-iA10B，仓库设置文档以 FS-iA6B 为例。以车上接收机外壳标注及实际 `s.bus` 输出口为准。

### 自制板和四路舵机

主控 `P11` 的四个 PWM 信号通过四芯线到自制板。自制板每组舵机接口有三针，分别接信号、电源正极和地。下表使用 DM-MC-02 原理图上的引脚编号。

| P11 信号脚 | MCU / 定时器 | 接到自制板对应舵机的信号脚 |
| --- | --- | --- |
| 1 | `PA0 / TIM2_CH1` | 夹爪开合，LDX-335MG |
| 2 | `PA2 / TIM2_CH3` | 夹爪俯仰 |
| 3 | `PE9 / TIM1_CH1` | 滑轨前后，连续旋转舵机 |
| 4 | `PE13 / TIM1_CH3` | 滑轨俯仰 |

另接**舵机电源正、负极**到自制板电源轨，以及**电源负极到主控 GND** 的参考地。原理图上 `P11-5` 为对外 5 V，`P11-6` 为 GND。使用外部舵机电源时，参考地接主控 GND，外部电源正极接自制板电源轨。夹爪开合已记录使用独立 7.4 V 供电。其余三只舵机的型号与额定电压、自制板是否分电源轨，需按实物确认。若四路正极在自制板共用一条电源轨，电压须同时满足四只舵机的规格。四路 PWM 周期均为 20 ms（50 Hz）。

### L298N 和双推杆

| 主控插座 | MCU 引脚 | L298N 输入 | 固件定义 |
| --- | --- | --- | --- |
| `P13 / UART7` 第 3 脚 | `PE7` | `IN1` | A 伸长 |
| `P13 / UART7` 第 4 脚 | `PE8` | `IN2` | A 收缩 |
| `P15 / UART10` 第 3 脚 | `PE2` | `IN3` | B 伸长 |
| `P15 / UART10` 第 4 脚 | `PE3` | `IN4` | B 收缩 |

- **共地**。`P13-2` 或 `P15-2` 的主控 `GND` 接 L298N `GND`。两个插座的第 1 脚为对外 5 V。上表四个信号脚由固件配置成 GPIO。
- **驱动板供电**。电机电源正、负极接 L298N 动力电源端，电压按推杆和驱动模块的规格选取。逻辑 5 V 按模块稳压跳帽配置供电。`ENA`、`ENB` 由跳帽或等效连线保持高电平。
- **推杆电机线**。A 的两根电机线接 `OUT1/OUT2`，B 的两根接 `OUT3/OUT4`。上表是固件定义，实物伸缩由各对输出线极性决定。核对推杆铭牌、A/B 的机械安装位置、模块 `5V-EN` 跳帽及推杆是否自带限位。

板卡原理图和中心板说明位于工作区同级 `参考` 目录，推杆主控引脚图为同级 `DM-MC02-L298N-pins.png`。电调与 H 桥接线可参照 [C610 手册](https://rm-static.djicdn.com/tem/17348/RoboMaster%20C610%20Brushless%20DC%20Motor%20Speed%20Controller%20User%20Guide.pdf) 和 [L298 数据手册](https://www.st.com/resource/en/datasheet/l298.pdf)。

## 遥控器与器件动作

FS-i6X 的输出设置选 `PPM`，串行总线选 `s.bus`。开启辅助开关，把通道数设为 10，并按下表分配 CH5 至 CH10。设置菜单见第 9 页。 [接收机设置文档](docs/FS-i6x搭配iA6B接收端使用文档.pdf)

| 输入 | 受控器件 | 动作 |
| --- | --- | --- |
| CH1 · 左摇杆左右 | 四个 C620 → 四个 M3508 | SWA 上位时左右横移，可与前后合成斜移。SWA 下位时控制车身旋转，纵轴回中可原地转，倒车时转向逐步反向 |
| CH2 · 左摇杆前后 | 四个 C620 → 四个 M3508 | 两种底盘模式均控制前后。主控用四轮反馈计算独立的电流命令，通过 CAN `0x200` 下发 |
| CH3 · 右摇杆前后 | `PA2` 夹爪俯仰舵机 | 上电按 `gripper_pitch_us.txt` 输出 1200 μs。SBUS 低端到高端均分五档，依次为 950、1200、1450、1700、2000 μs，回中对应 1450 μs |
| CH4 · 右摇杆左右 | `PE9` 滑轨前后连续旋转舵机 | `864–1184` 发 1500 μs 停转，`>1184` 发 1600 μs，`<864` 发 1400 μs。保持偏杆就持续输出对应脉宽，固件没有位置、时长或行程限位。机械方向按实车确认 |
| CH5 · VRA | C610 ID5 → M2006 升降丝杆 | 控制升降速度。中间约一半行程 `632–1416` 请求零速，两端各约 1/4 行程线性覆盖零速至满速。低端和高端分别请求 −3600 和 +3600 转子 rpm，约合输出轴 ±100 rpm。正常回中按斜坡减速，电流命令经 CAN `0x1FF` 下发。升降方向按实车确认 |
| CH6 · VRB | `PE13` 滑轨俯仰舵机 | 上电 800 μs。VRB 从上端到下端均分五档，依次为 800、1000、1200、1300、1400 μs，第 3 档对应中位 |
| CH7 · SWA | 四轮底盘模式 | 上位平移，下位旋转，中间区间保持上次模式，上电默认平移 |
| CH8 · SWB | L298N 推杆通道 | 上位选 B，下位选 A。已解锁且 SWC 在动作档时，拨动 SWB 会按新选中推杆的方向映射直接切换动作 |
| CH9 · SWC | L298N 选中的推杆 | B 上伸（`IN3`）、下收（`IN4`）。A 上收（`IN2`）、下伸（`IN1`）。两根推杆均在中位停止，四个 `IN` 全低，`ENA`、`ENB` 为高时两路电制动 |
| CH10 · SWD | `PA0` 夹爪开合舵机 | 上位张开 500 μs，下位夹紧 1600 μs。每次有效换档输出约 2 秒 PWM，随后信号拉低 |

### 上电、松杆和失联

- **底盘**。左杆死区 `1024 ±40`。左杆控制的轮速目标按版本 A 的 1.75 倍设置，两种模式的每轮目标转子上限为 3938 rpm（3937.5 取整），原地满杆旋转为 3150 rpm。上电或重新收到有效遥控后先将左杆回中，待 CAN 和四轮反馈就绪后解锁。正常松杆时四轮目标归零并制动，四轮实测均进入 ±30 rpm 后解除制动。板载 WS2812 黄灯表示等待就绪或回中，绿灯表示已解锁，紫灯慢闪表示遥控失联，红灯快闪表示底盘 CAN 或轮速反馈故障。
- **丝杆**。上电前把 VRA 回中。收到有效遥控且电调就绪后，代码直接按当前旋钮位置给速度，恢复遥控时也一样。反馈超时、FIFO 溢出或发送超时触发丝杆故障锁止，需重启解除。
- **滑轨前后与两路俯仰**。上电和失联恢复后，滑轨前后先将右杆横轴回中。两路俯仰以档位中心的中点为边界，采用 24 的回差，稳定 150 ms 后确认目标，每 20 ms 最多改变 8 μs。运行中失联时，滑轨前后发 1500 μs 停转，滑轨俯仰停止发送脉冲，夹爪俯仰保持最后输出的脉宽。
- **推杆与夹爪开合**。推杆在上电或失联恢复后先将 SWC 拨到中位，失联时四个方向输出立即拉低。夹爪开合失联时切断 PWM，恢复后先收到一次 SWD 上位命令才接受夹紧。
- **超时与行程**。最后一帧有效 SBUS 后 300 ms 无新有效命令即超时。接收机主动报告 failsafe 也会进入保护。丝杆、滑轨前后和双推杆的固件均未接入行程限位。

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

固件输出约 2 秒 50 Hz PWM，然后回到低电平。每次写入一个值并观察舵盘或夹爪角度。恢复遥控时烧录正常整车固件。CMake Release 的输出为 `build/Release/firmware.hex`，本机 ARM GCC 的输出为 `build/firmware.hex`。

## 工程结构与修改入口

正常固件入口为 [`Core/Src/main.c`](Core/Src/main.c)。上电先初始化推杆 GPIO，再初始化底盘和共用的 CAN2、SBUS 与时基，随后初始化丝杆和舵机模块。主循环先轮询底盘，其内部更新 SBUS、接收 CAN 反馈并推进毫秒时基，再依次更新丝杆、夹爪、滑轨、推杆。

| 改动内容 | 源码入口 |
| --- | --- |
| 底盘通道、摇杆方向、轮位、转速上限 | [`chassis.h`](Modules/Chassis/chassis.h)、[`chassis.c`](Modules/Chassis/chassis.c) |
| 底盘解锁、制动、速度闭环、CAN 收发与故障恢复 | [`control_state.c`](Modules/Chassis/control_state.c)、[`chassis_speed_control.c`](Modules/Chassis/chassis_speed_control.c)、[`chassis_control.c`](Modules/Chassis/chassis_control.c) |
| 丝杆通道、死区、方向、速度、斜坡 | [`m2006_remote.h`](Modules/M2006/m2006_remote.h)、[`m2006_remote.c`](Modules/M2006/m2006_remote.c)、[`m2006_control.c`](Modules/M2006/m2006_control.c) |
| 夹爪开合、俯仰通道和档位、初始俯仰 | [`gripper_test.c`](Modules/Gripper/gripper_test.c)、[`gripper_pitch_us.txt`](gripper_pitch_us.txt) |
| 滑轨前后死区、停转脉宽和俯仰档位 | [`slide_control.c`](Modules/Slide/slide_control.c)，停转标定参数为 `TRAVEL_STOP_US` |
| 双推杆方向引脚、SWB 和 SWC、回中解锁 | [`actuator_control.c`](Modules/Actuator/actuator_control.c) |
| 两路俯仰共用的档位确认和脉宽斜坡 | [`servo_position.c`](Core/Src/servo_position.c) |
| 舵机定时器、引脚、公共 PWM 周期 | [`servo_pwm.c`](Core/Src/servo_pwm.c) |
| CAN2 时序、电源使能、主控时钟、状态灯 | [`board.c`](Core/Src/board.c)、[`status_led.c`](Core/Src/status_led.c) |
| SBUS 串口、接收解码和有效命令超时 | [`remote_uart.c`](Modules/Remote/remote_uart.c)、[`sbus.c`](Modules/Remote/sbus.c)、[`sbus.h`](Modules/Remote/sbus.h) |

修改 CAN 时，保留底盘转发 C610 反馈的路径，并核对 `0x200` 四轮发送和 `0x1FF` 丝杆发送分别占用的缓冲区。修改舵机 PWM 时，`PA0` 与 `PA2` 共用 TIM2，`PE9` 与 `PE13` 共用 TIM1。同一计时器上的两路各自有独立脉宽，共用周期。

CMake 的板级源码列在 [`cmake/stm32cubemx/CMakeLists.txt`](cmake/stm32cubemx/CMakeLists.txt)，功能源码列在各 `Modules/*/CMakeLists.txt`，模块由[根 CMakeLists.txt](CMakeLists.txt)接入。新增源码时同步更新相应清单。`GRIPPER_BENCH=ON` 单独选用台架入口 `gripper_bench.c`。日常控制和本机 ARM GCC 一键编译走整车入口。
