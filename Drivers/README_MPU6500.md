# 陀螺仪模块 (MPU6500)

6 轴 IMU 传感器驱动，I2C 通信，I2C 中断状态机。输出 6 轴原始数据 + Kalman 融合姿态角。Z 轴航向角供闭环直行 PID 使用。

> 当前状态：**已完整实现并接入**。`empty.c` 的 `IMU_UpdateAttitude()` 在主循环中持续更新全局 `g_yaw_rate`，`gyro_pid.c` 读取该值驱动闭环 PID。
> 闭环架构详见 [`README_STEERING.md`](README_STEERING.md)。

## 硬件平台

- MCU: MSPM0G3507
- 传感器: MPU6500 / ICM-20602 (兼容 MPU6050 / MPU9250)
- 通信: **I2C**，SysConfig 实例名 `I2C_GYRO`
- 地址: 7-bit **0x68** (AD0 = GND)

## 引脚分配

> SysConfig 中 `I2C_GYRO` 实例配置。

| 信号 | 引脚 | 说明 |
|------|------|------|
| SCL | PA31 | I2C 时钟线 |
| SDA | PA28 | I2C 数据线 |
| AD0 | GND | I2C 地址选择 (GND = 0x68) |
| VCC | 3.3V | 供电 |
| GND | GND | 地 |
| INT | 未使用 | — |

## 文件结构

```
Drivers/
  mpu6500.h/c          I2C 底层驱动 (中断状态机 + 寄存器读写 + 6 轴数据采集)
  filter.h/c           滤波器 (Biquad 低通 + Kalman 2D 融合)
  gyro_pid.h/c         模式 B 陀螺仪直行 PID (速率/航向锁定双模式)
  steering.h/c         模式调度器
  README_MPU6500.md    本文档
  README_STEERING.md   闭环转向模块总说明

empty.c
  IMU_UpdateAttitude() 姿态解算: Biquad 低通 → Kalman X/Y 融合 → Z 轴漂移补偿 → 更新 g_yaw_rate
  g_yaw_rate           全局角速率 (°/s)，供 gyro_pid.c 读取
```

## 硬件配置

| 参数 | 值 | 寄存器 | 说明 |
|------|-----|--------|------|
| 采样率 | 1 kHz / (1+7) = 125 Hz | `SMPLRT_DIV` = 0x07 | 内部分频 |
| 低通滤波 | DLPF_CFG = 3 (~41 Hz) | `CONFIG` = 0x03 | 陀螺仪+加速度计共用 |
| 陀螺仪量程 | ±2000 dps | `GYRO_CONFIG` = 0x18 | 换算系数 **16.4 LSB/(°/s)** |
| 加速度计量程 | ±2g | `ACCEL_CONFIG` = 0x00 | 换算系数 **16384 LSB/g** |
| 时钟源 | PLL with X gyro | `PWR_MGMT_1` = 0x01 | 上电后切换 |

## 数据流

```
MPU6500 硬件
    │
    ▼  I2C 中断状态机 (mpu6500.c)
MPU6500_ReadIMU() → MPU6500_IMUData {accel_x/y/z, gyro_x/y/z}
    │
    ▼  empty.c 主循环
IMU_UpdateAttitude():
    ├── X/Y 轴: Biquad 低通加速度计角度 → Kalman2D 融合 → roll, pitch
    └── Z 轴:  零偏校准 → 静止漂移补偿 → 死区过滤 → g_yaw_rate (°/s)
                                   │
                                   ▼  gyro_pid.c 每 10ms
                              GyroPID_Update(yaw_rate) → corr (m/s)
                                   │
                                   ▼  steering.c → trajectory.c
                              v_L = ff_v_L − corr, v_R = ff_v_R + corr
```

## API

### mpu6500.h

```c
#include "./Drivers/mpu6500.h"

typedef struct {
    float accel_x, accel_y, accel_z;  // 加速度 (g)
    float gyro_x,  gyro_y,  gyro_z;   // 角速度 (°/s)
} MPU6500_IMUData;

bool MPU6500_Init(void);                    // 7 步初始化 + WHO_AM_I 验证 + 串口诊断
bool MPU6500_ReadIMU(MPU6500_IMUData *data); // 读 14 字节 → 解析为实际物理量
```

### gyro_pid.h

```c
extern volatile float g_yaw_rate;     // 定义在 empty.c, 由 IMU_UpdateAttitude 更新

float Gyro_ReadYawRate(void);         // 返回 g_yaw_rate
void  GyroPID_Update(float yaw_rate); // 低速滤波 → PID → corr (航向锁定已启用)
```

## 初始化流程 (empty.c main)

```
MPU6500_Init()                    → 7 步诊断输出到串口
陀螺仪 Z 轴零偏校准 (100 次采样取均值)
IMU_UpdateAttitude() 开始主循环  → 每轮更新 g_yaw_rate
Steering_Init()                  → GyroPID_Init + EnableHeadingLock(1)
```

## Z 轴航向漂移补偿

`IMU_UpdateAttitude()` 内实现了三层补偿：

| 层 | 方法 | 说明 |
|----|------|------|
| 零偏校准 | 上电 100 次采样取均值 | 消除静态偏置 |
| 静止检测 | `\|gyro_x\| < 1.5 && \|gyro_y\| < 1.5 && \|gyro_z\| < 1.5` | 判断是否静止 |
| 动态漂移补偿 | 静止时逐渐拉回零偏参考值 | 每 1000ms 静止 → z_offset 滑动更新 |
| 死区过滤 | `\|value\| < 0.5` → 归零 | 滤除微小噪声 |

## I2C 中断状态机

`mpu6500.c` 使用 I2C 控制器中断 + WFE 等待机制：

- **TX**：写寄存器地址 / 数据 → TX_DONE 完成
- **RX**：写寄存器地址 (TX) → 重启为 RX → RXFIFO_TRIGGER 收数据 → RX_DONE 完成
- **错误处理**：NACK → `I2C_STATUS_ERROR`，超时 → 串口打印诊断信息
- **ISR**：`I2C_GYRO_INST_IRQHandler()` 处理 TXFIFO/RXFIFO/STOP/NACK/仲裁丢失等所有 I2C 中断

## 串口诊断输出

`MPU6500_Init()` 启动时打印 7 步初始化过程：

```
===== MPU6500 Init Diagnostics =====
[ OK ] Step 1: Software Reset
[ OK ] Step 2: Clock Config
[ OK ] Step 3: SMPLRT_DIV
[ OK ] Step 4: CONFIG
[ OK ] Step 5: GYRO_CONFIG
[ OK ] Step 6: ACCEL_CONFIG
[INFO] WHO_AM_I Read Output: 0x70
===== MPU6500 Init SUCCESS =====
```

I2C 通信失败时打印 `[FAIL] Step X: ...` 和 `[DEBUG] Reason: ...`。

## 注意事项

| 事项 | 说明 |
|------|------|
| I2C 实例名 | SysConfig 中命名为 `I2C_GYRO`，不是 `I2C_0` |
| NVIC 优先级 | `NVIC_SetPriority(I2C_GYRO_INST_INT_IRQN, 0)` — 最高优先级 |
| Errata I2C_ERR_13 | 每次 `startControllerTransfer` 后必须 `delayCycles(32)` |
| MPU6500 兼容 | 兼容 MPU6050 (ID=0x68), MPU9250 (ID=0x71) |
| 晶振 | 代码假设使用外部晶振；若用内部振荡器需尝试多次上电 |
| 零偏校准 | 上电后 MPU6500 需保持静止约 1 秒供校准 |
