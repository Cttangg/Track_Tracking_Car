# 开环轨迹控制模块 (trajectory)

分段路径开环轨迹控制。轨迹 = 若干"段"(直线 / 圆弧)首尾相接构成的**闭合曲线**(如 8 字形)。

## 控制结构 (双环)

| 环 | 状态 | 职责 |
|----|------|------|
| 外环 | 当前**开环** | 运动学前馈: 每段几何解算出左右轮目标线速度 |
| 内环 | **闭环** | 由 motor_control 编码器 PID 维持轮速 |

> 闭环预留: 循迹传感器接入后, 通过反馈回调把"转向修正量"叠加到前馈上, 外环即变闭环。

## 差速运动学

```
w   = direction * v / R                       目标角速度 (rad/s)
v_L = v - w * L / 2                            左轮线速度 (m/s)
v_R = v + w * L / 2                            右轮线速度 (m/s)
n_motor = v_wheel * 60 / (2*pi*r) * 减速比      电机轴目标 RPM (set_speed 使用)
段时长: 圆弧 T = theta / |w|,  直线 T = distance / v
```

`direction`: `+1` = 逆时针(左转) / `-1` = 顺时针(右转)。

## 数据类型

```c
/* 段类型 */
typedef enum { SEG_STRAIGHT, SEG_ARC } seg_type_t;

/* 单段定义 */
typedef struct {
    seg_type_t type;
    float      R;          // 圆弧半径 (m); 直线段忽略
    float      length;     // 圆弧: 弧度 theta(rad) / 直线: 距离 distance(m)
    float      v;          // 段线速度 (m/s, > 0)
    int        direction;  // 圆弧: +1/-1; 直线段忽略
} traj_segment_t;

/* 运行状态 */
typedef enum { TRAJ_IDLE, TRAJ_RUNNING, TRAJ_DONE } traj_status_t;

/* 闭环反馈回调 (预留): 返回转向修正量 (m/s) */
typedef float (*traj_feedback_fn)(void);
```

## 函数用法

### 路径运行

```c
int trajectory_run_path(const traj_segment_t *segs, uint8_t num, uint8_t loop);
```
- `segs` : 段数组指针 (**运行期间必须保持有效**, 建议 `static const`)
- `num`  : 段数量
- `loop` : `1` = 闭合曲线, 走完最后一段回到第 0 段循环 (循迹用); `0` = 走完停车 (`TRAJ_DONE`)
- 返回 : `0` 成功 / `<0` 参数非法

### 单段便捷封装 (内部转为单段路径)

```c
int trajectory_arc(float R, float theta, float v, int direction); // 圆弧, 走完停
int trajectory_circle(float R, float v, int direction);           // 整圆循环 (loop)
int trajectory_straight(float distance, float v);                 // 直线, 走完停
```

### 控制与调度

```c
void trajectory_stop(void);     // 立即停止, 状态转 TRAJ_IDLE
void trajectory_update(void);   // 在 10ms 定时器 ISR 中调用一次
```
> `trajectory_update()` 必须在 `motor_control_update()` **之后**调用 (先测速+PID, 再叠加轨迹目标)。

### 闭环控制预留接口

```c
void trajectory_set_feedback(traj_feedback_fn fn);   // 注册循迹反馈回调, NULL 关闭
void trajectory_enable_closed_loop(uint8_t enable);  // 0 纯开环前馈 / 1 前馈+反馈修正
```
使能后, `trajectory_update()` 每 tick 叠加修正:
```
v_L_cmd = ff_v_L - corr,   v_R_cmd = ff_v_R + corr    (corr = feedback())
```

### 状态查询

```c
traj_status_t trajectory_get_status(void);         // TRAJ_IDLE / RUNNING / DONE
uint8_t       trajectory_get_segment_index(void);  // 当前段索引
uint32_t      trajectory_get_remaining_ms(void);   // 当前段剩余时间 (ms)
```

## 标定常量 (trajectory.c 顶部, 需按实车修改)

| 宏 | 默认值 | 说明 |
|----|--------|------|
| `WHEEL_BASE` | 0.16f | 左右轮距 L (m) |
| `WHEEL_RADIUS` | 0.033f | 轮半径 r (m) |
| `GEAR_RATIO` | 20.0f | 减速比 (电机轴 = 输出轴 × 20) |
| `CTRL_DT` | 0.01f | 控制周期 (10ms, 由 TIMG12 保证) |
| `MOTOR_L_ID` / `MOTOR_R_ID` | 1 / 2 | 左右轮电机映射 |
| `MOTOR_L_SIGN` / `MOTOR_R_SIGN` | +1 / +1 | 轮转向符号 (若正 RPM 使车后退改 -1) |

## 使用示例

### 8 字闭合曲线 (直线 + 反向圆弧)

```c
#include "./Drivers/trajectory.h"

// 尺寸为示例值, 需按实际赛道几何标定使 8 字闭合
static const traj_segment_t figure8[] = {
    // type,        R,     length,      v,     dir
    { SEG_STRAIGHT, 0.0f,  0.30f,       0.1f,  +1 },   // 交叉直线
    { SEG_ARC,      0.30f, 4.712389f,   0.1f,  +1 },   // 左环 CCW 270°
    { SEG_STRAIGHT, 0.0f,  0.30f,       0.1f,  +1 },   // 交叉直线
    { SEG_ARC,      0.30f, 4.712389f,   0.1f,  -1 },   // 右环 CW  270°
};

int main(void) {
    SYSCFG_DL_init();
    motor_control_init(1, 0.01f, 0.4f, 1.5f, 0.0f);
    motor_control_init(2, 0.01f, 0.4f, 1.5f, 0.0f);
    NVIC_EnableIRQ(TIMER_0_INST_INT_IRQN);
    DL_TimerG_startCounter(TIMER_0_INST);

    trajectory_run_path(figure8, 4, 1);   // loop=1 循环

    while (1) { /* 非实时任务 */ }
}

void TIMER_0_INST_IRQHandler(void) {
    motor_control_update();
    trajectory_update();
    DL_TimerG_clearInterruptStatus(TIMER_0_INST, DL_TIMERG_INTERRUPT_ZERO_EVENT);
}
```

### 后期闭环 (循迹传感器接入)

```c
static float line_get_correction(void) {
    // TODO: 读循迹传感器 -> PID -> 返回转向修正 (m/s)
    return 0.0f;
}

trajectory_set_feedback(line_get_correction);
trajectory_enable_closed_loop(1);
```

### 单段调用

```c
trajectory_arc(0.3f, 3.1415926f, 0.2f, 1);   // 半径0.3m 半圆 0.2m/s 逆时针
trajectory_circle(0.5f, 0.1f, -1);            // 半径0.5m 0.1m/s 顺时针 持续
trajectory_straight(1.0f, 0.2f);              // 直行 1m, 0.2m/s
trajectory_stop();                            // 停车
```
