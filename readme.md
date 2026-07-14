# 循迹小车 (Track Tracking Car)

> MSPM0G3507 双电机差速循迹小车 — 后驱，8 路灰度传感器，双模式闭环控制。
> 完整开发计划见 [`PLAN.md`](PLAN.md)

## 快速开始

### 编译 & 烧录
用 CCS Theia 打开项目 → 构建 → 烧录。`Drivers/` 为托管构建源文件夹，CCS 自动重新生成 makefile。

### 串口连接
波特率 115200，UART0 (PA10 TX / PA11 RX)。上电 1 秒静默后输出 banner。命令以 `\n` 或 `\r` 结尾。

### 命令速查
发 `?` 可随时查看帮助。完整命令手册 → [`Docs/cmd_reference.md`](Docs/cmd_reference.md)

| 类别 | 常用命令 | 示例 |
|------|---------|------|
| 状态 | `?` `gs` | `gs` → 打印灰度 8bit |
| 电机 | `Tr1 <rpm>` `stop1` `stop` | `Tr -300` → 双电机前进 |
| 轨迹 | `st <dist> <v>` `arc/cir` | `st 0.5 0.1` → 直行 0.5m |
| 循线 PID | `Lp <v>` `mode 0/1` | `Lp 0.4` → 设循线 Kp |

### 默认行为
上电后自动启动闭环转向控制（灰度循线模式 A），给你一条轨迹命令（如 `st 0.5 0.1`）即可看到循线效果。

## 目录结构

```
Track_Tracking_Car/
├── empty.c                 主程序 (main + cmd_do + TIMG12 ISR)
├── empty.syscfg             SysConfig 外设配置
├── Drivers/
│   ├── motor.c/h           底层电机驱动 (AIN1/AIN2 + PWM)
│   ├── motor_control.h     上层控制 API (PID 闭环 + 手动开环)
│   ├── trajectory.c/h      分段路径轨迹 + 差速运动学前馈 + 闭环反馈钩子
│   ├── grayscale.c/h       8 路灰度传感器 (GPIO 读取 + 串口输出)
│   ├── line_pid.c/h        模式 A: 灰度循线 PID (重心法)
│   ├── gyro_pid.c/h        模式 B: 陀螺仪直行 PID (UART1 预留)
│   ├── steering.c/h        模式调度器 (自动切换 A/B + 统一回调)
│   └── uart.c/h            通用串口库 (阻塞 TX + 中断 RX)
└── Docs/
    ├── cmd_reference.md     串口命令参考手册 (27 条)
    ├── motor_params.md      电机参数-函数-行为对照 + 差速公式
    └── uart_debug.md        UART 初始化失败 / PA11 噪声调试说明
```

## 文档导航

| 想看什么 | 文档 |
|----------|------|
| 命令怎么用 | [`Docs/cmd_reference.md`](Docs/cmd_reference.md) |
| 电机参数 (方向/轮距/轮径) | [`Docs/motor_params.md`](Docs/motor_params.md) |
| 串口出问题怎么修 | [`Docs/uart_debug.md`](Docs/uart_debug.md) |
| 电机 PID 驱动怎么用 | [`Drivers/README_MOTOR_DRIVE.md`](Drivers/README_MOTOR_DRIVE.md) |
| 轨迹控制怎么用 | [`Drivers/README_OPENROUND_CONTROL.md`](Drivers/README_OPENROUND_CONTROL.md) |
| 通用串口库怎么用 | [`Drivers/README_UART.md`](Drivers/README_UART.md) |
| 灰度传感器怎么配 | [`Drivers/README_GRAYSCALE.md`](Drivers/README_GRAYSCALE.md) |
| 闭环转向 (循线+陀螺仪) | [`Drivers/README_STEERING.md`](Drivers/README_STEERING.md) |
| 串口库怎么设计的 | [`Drivers/README_UART_DESIGN.md`](Drivers/README_UART_DESIGN.md) |
| 开发计划和已完成模块 | [`PLAN.md`](PLAN.md) |

## 硬件配置

| 参数 | 值 | 定义位置 |
|------|-----|---------|
| 电机映射 | L=电机2, R=电机1 (后驱) | `trajectory.c` |
| 轮距 | 16 cm | `trajectory.c` WHEEL_BASE |
| 轮径 | 3.3 cm (直径 6.6) | `trajectory.c` WHEEL_RADIUS |
| 减速比 | 1:20 | `trajectory.c` GEAR_RATIO |
| 控制周期 | 10 ms | TIMG12 ISR |
| 传感器映射 | bit0=最左, bit7=最右 | `line_pid.c` |
