# 电机运动参数说明

> 所有校准常量位于 `Drivers/trajectory.c` 顶部。修改后重新编译生效。

## 已校准参数（按实际硬件）

| 宏 | 值 | 说明 |
|----|-----|------|
| `MOTOR_L_ID` | **2** | 左后轮 = 电机 2 |
| `MOTOR_R_ID` | **1** | 右后轮 = 电机 1 |
| `MOTOR_L_SIGN` | **-1** | 负 RPM = 左轮前进 |
| `MOTOR_R_SIGN` | **-1** | 负 RPM = 右轮前进 |
| `WHEEL_BASE` | 0.16f | 轮距 16 cm |
| `WHEEL_RADIUS` | 0.033f | 轮半径 3.3 cm |
| `GEAR_RATIO` | 20.0f | 减速比 1:20 |

## 函数参数与小车行为对照

### `trajectory_straight(distance, v_target)`

| 参数 | 含义 | 示例 |
|------|------|------|
| `distance` | 行驶距离 (m) | 0.5 = 前进 0.5 米 |
| `v_target` | 中心线速度 (m/s) | 0.1 = 以 0.1 m/s 匀速行驶 |

```
trajectory_straight(0.5f, 0.1f);   // 前进 0.5m @ 0.1m/s
trajectory_straight(1.0f, 0.2f);   // 前进 1.0m @ 0.2m/s
```

> 两轮同时以 `v_target` 线速度前进。正值始终表示前进方向。

### `trajectory_arc(R, theta, v_target, direction)`

| 参数 | 含义 | 示例 |
|------|------|------|
| `R` | 圆弧半径 (m) | 0.3 = 转弯半径 0.3m |
| `theta` | 旋转弧度 (rad) | 3.14 = 半圆 (180°) |
| `v_target` | 中心线速度 (m/s) | 0.1 |
| `direction` | 转向方向 | +1 = 逆时针(左转), -1 = 顺时针(右转) |

```
trajectory_arc(0.3f, 3.1416f, 0.1f, +1);   // 半径 0.3m, 半圆, 左转
trajectory_arc(0.3f, 3.1416f, 0.1f, -1);   // 半径 0.3m, 半圆, 右转
trajectory_arc(0.5f, 1.5708f, 0.1f, +1);   // 半径 0.5m, 90° 左转
```

### `trajectory_circle(R, v_target, direction)`

| 参数 | 含义 | 示例 |
|------|------|------|
| `R` | 圆弧半径 (m) | 0.5 |
| `v_target` | 中心线速度 (m/s) | 0.1 |
| `direction` | 转向方向 | +1 = 左转圈, -1 = 右转圈 |

```
trajectory_circle(0.5f, 0.1f, +1);   // 一直左转圈, 半径 0.5m
trajectory_circle(0.5f, 0.1f, -1);   // 一直右转圈
```

> 持续模式 (`loop=1`)：不倒计时、不停车，直到调用 `trajectory_stop()`。

### `trajectory_run_path(segs, num, loop)`

分段路径：数组定义若干段（直线/圆弧）首尾相接。

```c
static const traj_segment_t path[] = {
    // type,          R,    length,  v,    dir
    { SEG_STRAIGHT,   0.0f, 0.30f,  0.1f, +1 },  // 直线 0.3m
    { SEG_ARC,        0.30f, 3.14f, 0.1f, +1 },  // 圆弧 半径0.3m 半圆 左转
    { SEG_STRAIGHT,   0.0f, 0.30f,  0.1f, +1 },  // 直线 0.3m
};
trajectory_run_path(path, 3, 0);   // 走完停车 (loop=0)
trajectory_run_path(path, 3, 1);   // 走完回到第0段循环 (loop=1, 循迹用)
```

### `trajectory_stop()`

立即停转两个电机，状态重置为 `TRAJ_IDLE`。

### `motor_control_set_speed(motorID, rpm)`

| 参数 | 含义 | 说明 |
|------|------|------|
| `motorID` | 电机编号 | 1 = 右后轮, 2 = 左后轮 |
| `rpm` | 电机轴目标转速 | **负值 = 前进**, 正值 = 后退; 绝对值越高越快 |

```
motor_control_set_speed(1, -500);   // 右后轮前进 500 RPM
motor_control_set_speed(2, -500);   // 左后轮前进 500 RPM → 小车直行前进
motor_control_set_speed(1, -500);   // 右后轮前进
motor_control_set_speed(2,  500);   // 左后轮后退 → 小车逆时针原地旋转
```

> **方向规则**: `MOTOR_L_SIGN = MOTOR_R_SIGN = -1`，所以应用层的正速度（前进）被映射为负 RPM 下发给 `motor_control_set_speed`。直接调 `motor_control_set_speed` 时，记住 **负 RPM = 前进**。

## 差速运动学公式

```
w   = direction * v / R              目标角速度 (rad/s)
v_L = v - w * L / 2                   左轮线速度 (m/s)
v_R = v + w * L / 2                   右轮线速度 (m/s)

直线 (direction=0): v_L = v_R = v_target, 两轮等速
左转 (direction=+1): v_L < v_R (左轮慢、右轮快 → 逆时针)
右转 (direction=-1): v_L > v_R (左轮快、右轮慢 → 顺时针)

n_motor = v_wheel × 60 / (2πr) × 20  电机轴目标 RPM (含减速比 1:20)
                                     经 SIGN=-1 翻转后下发 (正线速度 → 负 RPM)
段时长: 圆弧 T = theta / |w|, 直线 T = distance / v
```

## 校准流程（以后改硬件时用）

1. 给 `MOTOR_L_ID` 轮一个正 RPM → 若轮子前进而非后退，说明该轮 `_SIGN` 应保持 +1，不改。
2. 让车走直线 (`trajectory_straight(1.0, 0.1)`) → 用卷尺量实际距离。若偏短，调大 `WHEEL_RADIUS`；偏长，调小。若跑偏，微量调整左右轮 `_SIGN` 或 PID Kp。
3. 让车走圆弧 (`trajectory_arc(0.3, 3.14, 0.1, +1)`) → 用卷尺量实际左右轮轨迹半径。若偏大，调大 `WHEEL_BASE`；偏小，调小。
