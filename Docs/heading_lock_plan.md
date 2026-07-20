# 陀螺仪航向锁定 — 实施方案 (v2)

> filter.c 新增后重建方案。利用 `BiquadFilter` 低通滤波 + 航向积分 + 角度 PID。
> 不改现有 API 签名，仅新增函数 + 内部扩展。

## 一、可用模块

### gyro_pid.c (当前)
- `extern volatile float g_yaw_rate` — 来自 empty.c 的全局 yaw rate (°/s)，由 `IMU_UpdateAttitude()` 实时更新
- `Gyro_ReadYawRate()` — 返回 `g_yaw_rate`
- `GyroPID_Update(yaw_rate)` — 纯速率 PID：`error = 0 − ω_z` → corr

### filter.c (新增可用)
- `BiquadFilter` — 双二阶 IIR，可配置为低通滤波器，滤除陀螺仪高频噪声
- `KalmanFilter2D` — 2D 卡尔曼 `[角度, 角速度]`，**预留**用于后期传感器融合（磁力计/加速度计→绝对角度），当前方案不用

## 二、升级方案

### 数据流

```
g_yaw_rate (°/s, 含噪声)
    │
    ▼
Biquad_LPF (低通, fc≈10Hz)    ← 用 filter.c 的 BiquadFilter
    │
    ▼
filtered_yaw (°/s) × dt → heading_angle (rad)
    │
    ▼
heading_locked?
    ├── 是: error = target_heading − heading  (角度 PID)
    └── 否: error = 0 − yaw_rate              (速率 PID, 原行为)
    │
    ▼
PID → corr (m/s)
```

### 低通滤波器参数

陀螺仪采样率 200Hz，设计 10Hz 低通（滤除电机振动/传感器噪声）：

```c
// 二阶 Butterworth 低通, fc=10Hz, fs=200Hz
// 系数用在线工具 (如 https://www.earlevel.com/main/2016/09/29/biquad-calculator-v3/)
Biquad_Init(&g_lpf,
    0.020083f, 0.040167f, 0.020083f,   // b0, b1, b2
    1.000000f, -1.561018f, 0.641352f);  // a0, a1, a2
```

## 三、改动详情

### gyro_pid.h — +2 函数

```c
void GyroPID_EnableHeadingLock(uint8_t enable);   // 1=锁定当前航向
void GyroPID_SetHeadingGains(float kp, float ki, float kd);
```

现有 6 个函数签名不动。

### gyro_pid.c — 内部扩展

**新增字段** (`g_pid` 结构体内):

```c
BiquadFilter lpf;              // 低通滤波器 (filter.c)
float   heading;               // 累积航向角 (rad)
float   target_heading;        // 锁定目标角 (rad)
float   Kp_h, Ki_h, Kd_h;     // 航向模式独立增益
uint8_t heading_locked;        // 0=速率, 1=航向
```

**GyroPID_Init 修改**：初始化低通滤波器 + 航向增益默认=速率增益。

**GyroPID_Update 修改**：

```c
float GyroPID_Update(float yaw_rate)
{
    if (!g_pid.init) return 0.0f;

    /* 低通滤波 */
    float filtered = Biquad_Process(&g_pid.lpf, yaw_rate);

    /* 始终积分航向 (°/s → rad) */
    g_pid.heading += filtered * (3.1415926f / 180.0f) * g_pid.dt;

    float error;
    float kp, ki, kd;
    if (g_pid.heading_locked) {
        error = g_pid.target_heading - g_pid.heading;   // 角度误差 (rad)
        kp = g_pid.Kp_h; ki = g_pid.Ki_h; kd = g_pid.Kd_h;
    } else {
        error = 0.0f - yaw_rate;                         // 速率误差 (°/s)
        kp = g_pid.Kp;   ki = g_pid.Ki;   kd = g_pid.Kd;
    }

    /* PID 计算 (共用) */
    g_pid.integral += error * g_pid.dt;
    if (g_pid.integral >  MAX_INTEG) g_pid.integral =  MAX_INTEG;
    if (g_pid.integral < -MAX_INTEG) g_pid.integral = -MAX_INTEG;

    float deriv = (error - g_pid.prev_error) / g_pid.dt;
    g_pid.prev_error = error;

    float corr = kp * error + ki * g_pid.integral + kd * deriv;
    if (corr >  MAX_CORR) corr =  MAX_CORR;
    if (corr < -MAX_CORR) corr = -MAX_CORR;
    return corr;
}
```

**GyroPID_EnableHeadingLock**：

```c
void GyroPID_EnableHeadingLock(uint8_t enable) {
    if (enable) {
        g_pid.heading        = 0.0f;
        g_pid.target_heading = 0.0f;
        g_pid.heading_locked = 1;
    } else {
        g_pid.heading_locked = 0;
    }
    GyroPID_Reset();    // 清积分 + prev_error
}
```

**GyroPID_Reset** 加 `g_pid.heading = 0.0f`。

### steering.c — +1 行

```c
void Steering_Init(void) {
    ...
    GyroPID_EnableHeadingLock(1);     /* ← 新增 */
    ...
}
```

### empty.c — +3 条串口命令

| 命令 | 功能 |
|------|------|
| `lock 1` / `lock 0` | 强制开/关航向锁定 |
| `Gh <v>` | 航向模式 Kp |
| `Gi_h <v>` | 航向模式 Ki |

## 四、航向 PID 初值

| 参数 | 速率模式 | 航向模式 | 说明 |
|------|---------|---------|------|
| Kp | 0.5 | **2.0** | 航向偏差量级小 (rad<0.1)，需更大 Kp |
| Ki | 0.02 | **0.05** | 缓慢补偿漂移 |
| Kd | 0 | 0 | 关闭 |
| LPF fc | — | 10 Hz | Butterworth 二阶低通 |

## 五、改动汇总

| 文件 | 改动 |
|------|------|
| `gyro_pid.h` | +2 声明 |
| `gyro_pid.c` | `#include "filter.h"`, `g_pid` +6 字段, 低通初始化, `GyroPID_Update` 加滤波+分支, +2 函数 |
| `steering.c` | +1 行 |
| `empty.c` | +3 条命令 |

**不动**: gyro_pid.h 现有 6 个签名, steering.h/c 其余逻辑, line_pid, trajectory, motor, uart, grayscale, filter 本身。

## 六、Kalman 预留

`filter.c` 的 `KalmanFilter2D` 当前不用。后期若加入磁力计/加速度计提供绝对航向角，可在 `GyroPID_Update` 中用 `Kalman2D_Predict + Update` 替代低通+积分：
- `meas_angle` = 磁力计航向角
- `meas_velocity` = 陀螺仪角速度
- `kf->x[0]` = 融合后的航向角 (替代 `g_pid.heading`)

此时只需改 `GyroPID_Update` 内部，不影响外部调用。
