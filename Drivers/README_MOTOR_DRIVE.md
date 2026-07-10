# 双电机 PID 速度控制模块

## 硬件平台

- MCU: MSPM0G3507 (LQFP-48)
- SDK: MSPM0 SDK 2.10.00.04
- PWM 频率: 10kHz, 分辨率 0~4000
- 编码器: 500 PPR, 单边沿计数 (仅上升沿)
- 控制频率: 100Hz (10ms 周期)
- 定时器: TIMG12 (32-bit, 无预分频器)

## 引脚分配

| 信号 | 电机 1 | 电机 2 |
|------|--------|--------|
| PWM | PA12 (TIMG0 CCP0) | PA13 (TIMG0 CCP1) |
| IN1 | PA8 | PB2 |
| IN2 | PA9 | PB3 |
| 编码器 | PA17 (TIMA1) | PA21 (TIMA0) |
| STBY | PB24 (共用) | PB24 (共用) |

## 文件结构

```
Drivers/
  motor.h           底层驱动接口 (motor_init, motor_set_duty, motor_set_direction)
  motor.c           底层驱动 + 电机控制模块实现
  motor_control.h   上层控制 API (所有对外接口)
  README.md         本文档
DC_Motor.c          主程序 (串口命令、firewater 输出、TIMG12 ISR)
DC_Motor.syscfg     SysConfig 外设配置
```

## 转速计算公式

```
freq (Hz) = delta_edges × 100          (100Hz 采样, delta 为 10ms 内的边沿增量)
RPM       = freq × 60 / ENCODER_PPR   (ENCODER_PPR = 500)
          = freq × 0.12

注: 显示的是电机轴转速 (减速前). 减速比 1:20, 输出轴转速 = 电机转速 / 20.
```

## 模块 API (motor_control.h)

```c
#include "motor_control.h"

/* ---- 初始化 ---- */
void motor_control_init(uint8_t motorID, float dt_sec,
                        float kp, float ki, float kd);
// motorID: 1 或 2
// dt_sec:  控制周期, 固定 0.01f (10ms)
// kp, ki, kd: PID 增益, 初始值 Kp=0.4, Ki=1.5, Kd=0

/* ---- 设定转速 (正=正向, 负=反向, 0=停止) ---- */
void motor_control_set_speed(uint8_t motorID, int32_t rpm);

/* ---- 停止 ---- */
void motor_control_stop(uint8_t motorID);

/* ---- 手动开环 PWM (0~4000), 调用 set_speed() 切回闭环 ---- */
void motor_control_set_duty(uint8_t motorID, uint32_t duty);

/* ---- ISR 更新 (在 10ms 定时器 ISR 中调用一次) ---- */
void motor_control_update(void);

/* ---- 状态查询 (可在 ISR 或主循环中调用) ---- */
int32_t  motor_control_get_target_rpm(uint8_t motorID);
int32_t  motor_control_get_actual_rpm(uint8_t motorID);
uint32_t motor_control_get_duty(uint8_t motorID);
uint32_t motor_control_get_freq(uint8_t motorID);

/* ---- 在线调参 ---- */
void motor_control_set_kp(uint8_t motorID, float kp);
void motor_control_set_ki(uint8_t motorID, float ki);
void motor_control_set_kd(uint8_t motorID, float kd);
```

## 使用示例

```c
// main() 中初始化
SYSCFG_DL_init();
motor_control_init(1, 0.01f, 0.4f, 1.5f, 0.0f);  // 电机 1
motor_control_init(2, 0.01f, 0.4f, 1.5f, 0.0f);  // 电机 2

// 启动 10ms 定时器 + NVIC
DL_TimerG_startCounter(TIMER_0_INST);
NVIC_EnableIRQ(TIMER_0_INST_INT_IRQN);

// ISR 中调用
void TIMER_0_INST_IRQHandler(void) {
    motor_control_update();
    DL_TimerG_clearInterruptStatus(TIMER_0_INST, DL_TIMERG_INTERRUPT_ZERO_EVENT);
}

// 应用层控制
motor_control_set_speed(1, 4000);   // M1 正向 4000 RPM
motor_control_set_speed(2, -3000);  // M2 反向 3000 RPM
motor_control_stop(1);               // M1 停止
```

## 串口命令 (由 DC_Motor.c 提供)

### 设定命令

| 命令 | 示例 | 说明 |
|------|------|------|
| `Tr1 4000` | `Tr2 -3000` | 设定目标 RPM (正=正向, 负=反向) |
| `Kp1 0.5` | `Ki2 2.0` | 调 PID 增益 |
| `Dd1 2000` | `Dd2 1000` | 手动开环 PWM |
| `stop1` | `stop2` | 停止指定电机 |
| `?` | | 查看双电机状态 |

### Firewater 输出 (每 10ms 一行)

```
T1,R1,D1,F1,T2,R2,D2,F2\r\n
```

| 通道 | 含义 |
|------|------|
| T1 | 电机 1 目标 RPM (绝对值, 不区分正反) |
| R1 | 电机 1 实际 RPM |
| D1 | 电机 1 PWM 占空比 (0~4000) |
| F1 | 电机 1 编码器频率 (Hz) |
| T2,R2,D2,F2 | 电机 2 同 |

## PID 参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| Kp | 0.4 | 比例增益: 1000 RPM 误差 → 400 duty 立即响应 |
| Ki | 1.5 | 积分增益: 120ms 内补偿 2000 RPM 负载扰动 |
| Kd | 0 | 微分关闭 (电机惯性本身提供阻尼) |
| Anti-windup | 5000 | 积分限幅 |
| PWM 范围 | 0~4000 | |

## 关键实现细节

### 编码器测速

- 使用 COMPARE 模块的 EDGE_COUNT_UP 模式, 16-bit 计数器, 周期 65535
- 软件检测 16-bit 回绕 (c0 < c0_prev → 溢出一段)
- 电机 1: `DL_TimerA_getTimerCount(COMPARE_0_INST)` (TIMA1, PA17)
- 电机 2: `DL_TimerA_getTimerCount(COMPARE_2_INST)` (TIMA0, PA21)

### 定时器 ISR

- TIMG12, PERIODIC 模式, 10ms, ZERO 中断
- ISR 中完成所有实时任务: 测速 → PID → 电机驱动
- 主循环非实时: 串口命令 + firewater 输出
- 采样窗口由硬件保证, 与 UART TX 完全解耦

### 方向控制

- `g_dir[2]` 数组存储各电机方向
- `motor_set_direction()` 设置方向标志
- `motor_set_duty()` 每次根据方向标志选择 AIN/BIN 组合
- 正向 (dir=0): IN1=LOW, IN2=HIGH + PWM
- 反向 (dir=1): IN1=HIGH, IN2=LOW + PWM

### 试错经验

| 问题 | 根因 | 解决 |
|------|------|------|
| 编码器 C0 始终为 0 | COMPARE_0 的 subscriber 配置导致 external trigger 锁死计数器 | 从 .syscfg 删除 COMPARE_0 的 subscriberPort/channel |
| COMPARE_1 溢出计数永远为 0 | TIMA trigger mode 下只能计数 CCP 边沿, 不能计数 event 事件 | 改为软件检测 16-bit 回绕 |
| TIMA0 ISR 死循环 | `DL_TimerA_clearInterruptStatus` 内部重定向到通用函数, TIMA 寄存器偏移不兼容 | 改用 TIMG12 + `DL_TimerG_clearInterruptStatus` |
| UART RX 中断导致 MCU 无反应 | PA11 浮空产生噪声触发持续中断 | 改为轮询 `DL_UART_isRXFIFOEmpty` |
| `%f` 显示为字面量 `f` | TI minimal printf 不支持浮点 | 整数拆分 `(int)val, ((int)(val*1000))%1000` |
| 编码器 R=0 但电机在转 | 重写双电机模块时漏掉了 `DL_TimerA_startCounter(COMPARE_*_INST)` | 在 `motor_control_init` 中加入编码器启动 |
