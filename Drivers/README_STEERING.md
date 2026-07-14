# 闭环转向控制模块 (steering / line_pid / gyro_pid)

双模式自动切换的闭环转向控制器，通过 `trajectory_set_feedback()` 钩子接入轨迹模块。

> 设计文档见 `PLAN.md` 第二章。

## 架构

```
trajectory_update() [10ms ISR]
  └── apply_speed()
        ├── ff_v_L, ff_v_R           (前馈轮速)
        ├── Steering_GetCorrection()  (回调)
        │     ├── Grayscale_Read()
        │     ├── 有黑线? → LinePID    → corr
        │     └── 无线   → GyroPID    → corr
        ├── v_L = ff_v_L − corr
        └── v_R = ff_v_R + corr
```

## 文件

| 文件 | 职责 |
|------|------|
| `line_pid.h/c` | 模式 A: 灰度循线 PID |
| `gyro_pid.h/c` | 模式 B: 陀螺仪直行 PID (UART1 预留) |
| `steering.h/c` | 模式调度: 线检测 + 自动切换 + 统一回调 |

## 模式 A — 灰度循线 PID

**原理**: 8 路传感器 (bit0=最左, bit7=最右, 1=黑线) → 重心法计算线位置偏差 → PID → 转向修正。

```
center = Σ(bit_i × i) / Σ(bit_i)      重心 (0~7)
error  = center − 3.5                  偏差 (−3.5~+3.5, 负=偏左)
corr   = Kp·error + Ki·∫error + Kd·d(error)/dt
```

**误差为负(线偏左) → corr<0 → 左轮加速/右轮减速 → 车左转追线**。

### API

```c
void LinePID_Init(0.3f, 0.05f, 0.0f);     // Kp, Ki, Kd
float LinePID_Update(sensor_8bit);           // 返回 corr (m/s)
uint8_t LinePID_LineDetected(void);          // 是否有黑线
void LinePID_SetKp/Ki/Kd(v);                 // 在线调参 (清零积分)
```

### 默认参数

| 参数 | 值 | 说明 |
|------|-----|------|
| Kp | 0.3 | 每 1 传感器间距偏差 → 0.3 m/s 修正 |
| Ki | 0.05 | 积分补偿持续偏航 |
| Kd | 0.0 | 微分关闭 |
| MAX_CORR | 0.15 m/s | 转向力度上限 |

## 模式 B — 陀螺仪直行 PID

**原理**: 陀螺仪输出偏航角速度 ω_z (rad/s) → PID 使 ω_z → 0，即保持航向不变。

```
error = 0 − ω_z                  目标: 不转
corr  = Kp·error + Ki·∫error +   方向: ω_z>0(右转)→corr<0→左转纠正
```

陀螺仪当前**未连接** (UART1 预留)，`Gyro_ReadYawRate()` 返回 0，PID 输出恒 0 → 小车靠前馈直线行驶。陀螺仪接入后替换底层读取即可，PID 层不动。

### API

```c
void  GyroPID_Init(0.5f, 0.02f, 0.0f);
float GyroPID_Update(yaw_rate_rad_s);    // 返回 corr (m/s)
void  GyroPID_SetKp/Ki/Kd(v);

void  Gyro_Init(void);                   // 预留: UART1 陀螺仪初始化
float Gyro_ReadYawRate(void);            // 预留: 当前返回 0
```

### 陀螺仪接入步骤

1. SysConfig 添加 `UART_1_INST` (RX 引脚需指定)
2. `uart.h` 取消 `UART1_ENABLE` 注释
3. 在 `Gyro_Init()` / `Gyro_ReadYawRate()` 中实现:
   - 用 `g_uart1` + framer 解析陀螺仪 UART 数据帧
   - 返回 yaw rate (rad/s)

## 模式调度 (steering)

### 切换逻辑

```
有黑线 → 立即模式 A (清积分)
无黑线:
  丢失次数 < 50 (500ms) → 保持 A, sensor=0 → 偏差=0 → 直走
  丢失次数 ≥ 50           → 模式 B (陀螺仪直行)
```

### API

```c
void  Steering_Init(void);                    // 初始化两个 PID + 模式
float Steering_GetCorrection(void);            // traj_feedback_fn 回调
void  Steering_SetMode(0=A / 1=B);
```

### 在 empty.c 中的接入

```c
#include "./Drivers/steering.h"

Steering_Init();
trajectory_set_feedback(Steering_GetCorrection);
trajectory_enable_closed_loop(1);
```

## 串口命令

| 命令 | 示例 | 功能 |
|------|------|------|
| `Lp 0.4` | `Li 0.1` | 循线 PID 增益 |
| `Gp 0.5` | `Gi 0.02` | 陀螺仪 PID 增益 |
| `mode 0` | `mode 1` | 手动切模式 A/B |
