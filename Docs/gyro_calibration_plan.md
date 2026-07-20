# 陀螺仪直线闭环校准 — 实施计划

> 以当前文件 API 为准。gyro_pid.c 为纯速率 PID，filter.c 可用但未接入。

## 一、当前状态总览

### gyro_pid.c/h — 纯速率 PID
```
GyroPID_Update(yaw_rate °/s):
    error = 0 − yaw_rate
    PID(Kp=0.5, Ki=0.02, Kd=0) → corr (m/s, 限幅 ±0.15)
```

- `Gyro_ReadYawRate()` → `g_yaw_rate`（来自 empty.c 的 `IMU_UpdateAttitude`）
- **无航向积分，无低通滤波，无 heading lock**

### steering.c — 模式调度
- `Steering_Init()`: `GyroPID_Init(0.5, 0.02, 0)`
- 模式 B（陀螺仪）→ `GyroPID_Update(Gyro_ReadYawRate())`
- 模式 A/B 切换正常

### filter.c/h — 可用但未接入 gyro_pid
- `BiquadFilter`: 可做低通 (fc≈10Hz)，滤电机振动
- `KalmanFilter2D`: 预留，当前不用

### empty.c — 已有基础设施
- `g_yaw_rate` (volatile float °/s) — 由 `IMU_UpdateAttitude()` 实时更新
- MPU6500 经 I2C 正常采集
- 串口命令 `Gp/Gi/Gd` 可调速率 PID
- `mode 1` 可手动切陀螺仪模式

## 二、校准需要的能力

| 需求 | 当前 | 需要 |
|------|------|------|
| 串口调 PID | ✅ `Gp/Gi/Gd/mode` | 不变 |
| 看 yaw rate 实时值 | ❌ | `diag` 命令或 firewater 加字段 |
| 低通滤波 | ❌ | BiquadFilter 接入 `GyroPID_Update` |
| 航向角积分 | ❌ | 内部累积 heading（不改变 Update 签名） |
| 航向锁定 (angle PID) | ❌ | +`EnableHeadingLock` + heading gain setters |
| 校准流程文档 | ❌ | 本文档 |

## 三、实施计划

### 阶段 1: 诊断输出（5 行代码，无 API 变化）

**目的**: 能看到 yaw rate 和 corr，才能判断 PID 是否有效。

改动 `gyro_pid.h`: + getter
```c
float GyroPID_GetLastYawRate(void);     // 返回最近一次传入的 yaw_rate
float GyroPID_GetLastCorrection(void);  // 返回最近一次 PID 输出
```

改动 `gyro_pid.c`: 2 个 getter + 存 last 值。

改动 `empty.c`: + `diag` 命令，打印 `YAW=±x.x °/s CORR=±x.xxx HEAD=±x.xxx`。

### 阶段 2: 低通滤波（10 行，1 个字段）

**目的**: 电机振动产生高频噪声 → PID 输出抖动 → 车走不直。

改动 `gyro_pid.c`:
- `g_pid` 加 `BiquadFilter lpf` 字段
- `GyroPID_Init` 中 `Biquad_Init` (fc≈10Hz)
- `GyroPID_Update` 首行 `yaw_rate = Biquad_Process(&g_pid.lpf, yaw_rate)`
- `#include "filter.h"`

对外 API 不变。

### 阶段 3: 航向积分 + 锁定（15 行，+3 函数）

**目的**: 纯速率 PID 只能"抑制转弯"，无法"保持方向"。零漂导致缓慢偏航。需要锁定一个初始航向角，偏离时主动拉回。

改动 `gyro_pid.h`: +3 声明
```c
void  GyroPID_EnableHeadingLock(uint8_t enable);
void  GyroPID_SetHeadingKp(float kp);
void  GyroPID_SetHeadingKi(float ki);
float GyroPID_GetHeading(void);
```

改动 `gyro_pid.c`:
- `g_pid` 加 `heading, target_heading, Kp_h, Ki_h, heading_locked`
- `GyroPID_Init` 初始化上诉字段
- `GyroPID_Update` 内部: `heading += filtered_yaw * dt` → 按 `heading_locked` 走角度/速率 PID
- +3 新函数

改动 `steering.c`: `Steering_Init()` 加 1 行 `GyroPID_EnableHeadingLock(1)`

改动 `empty.c`: +3 命令 `lock 0/1`, `Gh <v>`, `Gi_h <v>`

### 阶段 4: 校准文档

更新 `README_STEERING.md` 或 `README_MPU6500.md`，写入校准流程。

## 四、校准流程（实施后执行）

1. `mode 1` — 强制切陀螺仪模式，`lock 0` — 先关航向锁定
2. `st 1 0.2` — 直行，观察 firewater 和 `diag`，看 yaw rate 是否在 ±2 °/s 内
3. `Gp 0.8` — 增大 P，看修正是否足够（yaw rate 被抑制到 ±1 °/s 以下）
4. `Gi 0.05` — 加 I，消除静差
5. `lock 1` — 开航向锁定，`Gh 2.0` — 调 heading Kp
6. 重复直行，确认小车不再偏航

## 五、改动汇总

| 文件 | 阶段 1 | 阶段 2 | 阶段 3 | 总计 |
|------|--------|--------|--------|------|
| `gyro_pid.h` | +2 getter | 不变 | +3 声明 | +5 |
| `gyro_pid.c` | +4 行 | +3 行 +1 include | +15 行 | ~22 行 |
| `steering.c` | 不变 | 不变 | +1 行 | +1 |
| `empty.c` | +1 命令 | 不变 | +3 命令 | +4 命令 |
| 现有 API | 不变 | 不变 | 不变 | 0 破坏 |

**不动**: motor.c, trajectory.c, line_pid.c, grayscale.c, mpu6500.c, filter.c, uart.c
