# 循迹小车 — 开发计划

> 当前状态：双模式闭环控制已完成 ✅
> bit0=最左, bit7=最右; 陀螺仪 UART1 预留。

## 一、已完成的模块

| 模块 | 文件 | 说明 |
|------|------|------|
| 双电机 PID | `Drivers/motor.c/h` `motor_control.h` | 编码器测速 + 轮速闭环 PID |
| 轨迹控制 | `Drivers/trajectory.c/h` | 分段路径 + 差速运动学前馈 + 闭环预留钩子 |
| 通用 UART | `Drivers/uart.c/h` | 阻塞 TX + 中断 RX, `UART_RxEnable` 延迟开 RX |
| 灰度传感器 | `Drivers/grayscale.c/h` | 8 路 GPIO (bit0=左 bit7=右) |
| 循线 PID | `Drivers/line_pid.c/h` ✅ 新增 | 重心法 + PID (模式 A) |
| 陀螺仪 PID | `Drivers/gyro_pid.c/h` ✅ 新增 | yaw rate PID, UART1 预留 (模式 B) |
| 转向调度 | `Drivers/steering.c/h` ✅ 新增 | 双模式自动切换 + 统一回调 |
| 闭环集成 | `empty.c` ✅ 新增 | `Steering_Init()` + 反馈钩子 + PID 调参命令 |
| 串口命令 | `empty.c cmd_do` | 电机/轨迹/传感器/PID/模式 全命令集 |
| 电机校准 | `trajectory.c` 顶部宏 | MOTOR_L_ID=2, _R_ID=1, _SIGN=-1, WHEEL_BASE=0.16 |

## 二、双模式闭环控制 ✅ 已实施

详见 `Drivers/README_STEERING.md`。

### 核心架构

```
每 10ms (TIMG12 ISR):
  trajectory_update() → apply_speed()
    ├── 前馈: ff_v_L, ff_v_R (当前段几何解算)
    ├── 反馈: Steering_GetCorrection()
    │     ├── Grayscale_Read()            → 8bit 传感器
    │     ├── Gyro_ReadYawRate()          → yaw rate (rad/s), 来自 IMU
    │     ├── 模式判断:
    │     │     line_detected? → Mode A: LinePID_Update(sensor)
    │     │     else           → Mode B: GyroPID_Update(yaw_rate)
    │     └── corr = 选中的模式输出
    ├── v_L = ff_v_L − corr
    └── v_R = ff_v_R + corr
```

### 模式 A — 灰度循线 PID

```
8 路传感器从左到右 (bit7 最左, bit0 最右):

center = (Σ bit_i × i) / (Σ bit_i)    重心 0.0~7.0
error  = center − 3.5                  偏差 −3.5~+3.5 (正=线偏左)
corr   = −Kp·error − Ki·∫error − Kd·d(error)/dt
```

`corr < 0` → 左轮加速、右轮减速 → **车左转追线**。

线丢失 (`Σ bit_i == 0`)：连续丢失 > N 个周期 → 自动切模式 B。

### 模式 B — 陀螺仪直行 PID

```
陀螺仪输出 yaw rate ω_z (rad/s):

error = 0 − ω_z                       目标: 偏航角速度 = 0
corr  = Kp_yaw·(−ω_z) + Ki·∫(−ω_z) + Kd·derivative
```

小车右转 (ω_z > 0) → error < 0 → corr > 0 → 右轮加速、左轮减速 → **车左转抵消右转**。

**可选**：绝对航向锁定——记录目标角度 target_angle，error = target_angle − current_angle。

### 模式切换逻辑

```
LineDetected() = (Grayscale_Read() != 0)

当前模式 A, 线丢失:
    counter++
    counter < N → 保持模式 A, 用最后一次已知偏差
    counter ≥ N → 切模式 B

当前模式 B, 线出现:
    立即切模式 A, counter=0, 清积分
```

### 预期文件结构

| 文件 | 职责 |
|------|------|
| `Drivers/line_pid.c/h` | 模式 A: 灰度位置偏差 + PID → 转向修正 |
| `Drivers/gyro_pid.c/h` | 模式 B: 陀螺仪 yaw rate → PID → 转向修正 |
| `Drivers/steering.c/h` | 模式调度器: 线检测 + 切换逻辑 + 统一回调 `Steering_GetCorrection` |

### 改动 empty.c

```c
#include "./Drivers/steering.h"

// main() 初始化段:
Steering_Init();
trajectory_set_feedback(Steering_GetCorrection);
trajectory_enable_closed_loop(1);
```

### PID 初始参数

| 参数 | 模式 A (循线) | 模式 B (陀螺仪) | 说明 |
|------|-------------|---------------|------|
| Kp | 0.3 | 0.5 | 修正 m/s 每单位偏差 |
| Ki | 0.05 | 0.02 | 积分累积 |
| Kd | 0.0 | 0.0 | 微分（初期关闭） |
| 输出限幅 | ±0.15 | ±0.15 | m/s |
| 积分限幅 | ±0.30 | ±0.30 | m/s |

### 陀螺仪接口（待确认）

| 项目 | 待定 |
|------|------|
| 芯片型号 | MPU6050 / ICM-20602 / 其他 |
| 通信协议 | I2C (SDA+SCL) / SPI |
| 接口函数 | `Gyro_Init()` / `Gyro_ReadYawRate()` → float (rad/s) |

### 串口命令扩展

| 命令 | 示例 | 功能 |
|------|------|------|
| `Lp <v>` | `Lp 0.4` | 设置循线 PID Kp |
| `Li <v>` | `Li 0.1` | 设置循线 PID Ki |
| `Ld <v>` | `Ld 0.0` | 设置循线 PID Kd |
| `Gp <v>` | `Gp 0.5` | 设置陀螺仪 PID Kp |
| `Gi <v>` | `Gi 0.02` | 设置陀螺仪 PID Ki |
| `Gd <v>` | `Gd 0.0` | 设置陀螺仪 PID Kd |
| `mode a/b` | `mode a` | 手动切模式 A/B |

### 调试顺序

1. **模式 A 独立调**：铺黑线，`st 0.5 0.1` 直行接近线，看循线响应。只调 Kp。
2. **模式 B 独立调**：无黑线，`st 1.0 0.1` 直行，看是否走直线。只调 Kp。
3. **两模式同开**：混合赛道，验证切换平滑。

## 三、已校准参数

```
MOTOR_L_ID=2     MOTOR_R_ID=1        // 电机2=左后轮, 电机1=右后轮
MOTOR_L_SIGN=-1  MOTOR_R_SIGN=-1      // 负 RPM = 前进
WHEEL_BASE=0.16f                       // 轮距 16 cm
WHEEL_RADIUS=0.033f                    // 轮半径 3.3 cm
GEAR_RATIO=20.0f                       // 减速比 1:20
```

## 四、串口命令速查

```
?                 → 电机状态 + 帮助菜单
gs                → 灰度传感器 8bit 二进制
Tr1 <rpm>         → 右后轮 RPM
Tr2 <rpm>         → 左后轮 RPM
Tr <rpm>          → 双电机同 RPM
Kp1/Ki1/Kd1 <v>   → 电机1 PID
Kp2/Ki2/Kd2 <v>   → 电机2 PID
Dd1/Dd2 <duty>    → 手动 PWM
stop1/stop2/stop  → 停电机
st <dist> <v>     → 直线
arc <R> <th> <v> <dir> → 圆弧
cir <R> <v> <dir> → 转圈
stop_all          → 轨迹停车
Lp/Li/Ld <v>      → 循线 PID 增益 (模式 A)
Gp/Gi/Gd <v>      → 陀螺仪 PID 增益 (模式 B)
mode 0            → 强制切模式 A (循线)
mode 1            → 强制切模式 B (陀螺仪)
```

## 五、文档索引

| 文档 | 内容 |
|------|------|
| `change.md` | 串口统一 + 灰度接入变动记录 |
| `grayscale_debug.md` | 灰度传感器引脚调试全程 |
| `firewater_debug.md` | 串口/Firewater 调试全程（已解决） |
| `Docs/motor_params.md` | 电机参数-函数-行为对照 + 差速公式 |
| `Docs/cmd_extend_plan.md` | 串口命令扩展详细分析 |
| `Docs/uart_debug.md` | UART 初始化失败 / 乱码 调试说明书 |
| `Docs/line_pid_plan.md` | 闭环循迹 PID 初期方案（被本 PLAN 取代） |
| `Drivers/README_UART.md` | 通用串口库使用说明 |
| `Drivers/README_UART_DESIGN.md` | 通用串口库设计决策 |
| `Drivers/README_GRAYSCALE.md` | 灰度传感器模块说明 |
| `Drivers/README_OPENROUND_CONTROL.md` | 轨迹控制模块使用说明 |
| `Drivers/README.md` | 电机 PID 控制模块说明 |
