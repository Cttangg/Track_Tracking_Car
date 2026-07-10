# 循迹小车 — 本次改动说明

> 详细函数用法见 [`Drivers/README_OPENROUND_CONTROL.md`](Drivers/README_OPENROUND_CONTROL.md)
> 电机/PID 驱动见 [`Drivers/README.md`](Drivers/README.md)

## 本次改动概览

新增**开环轨迹控制模块** (`Drivers/trajectory.c` / `trajectory.h`)，并为后期循迹闭环预留接口。控制方式：**电机实时控制在 10ms 中断 (TIMG12 ISR)**，串口命令在主循环轮询。

| 文件 | 变动 |
|------|------|
| `Drivers/trajectory.h` | 新增 (分段路径 API + 闭环预留接口 + 数据类型) |
| `Drivers/trajectory.c` | 新增 (运动学解算 + 段状态机 + 前馈下发) |
| `Drivers/README_OPENROUND_CONTROL.md` | 新增 (模块函数用法文档) |
| `empty.c` | ISR 内加 `trajectory_update()`; main 内运行 8 字闭合路径 |

## 改动一：分段路径状态机 (适配闭合曲线)

原来的"单段 + 时间倒计时"改为**分段路径**：轨迹 = 若干"段"(直线 / 圆弧)首尾相接的闭合曲线 (如 8 字形)。

- 新增段类型与段定义：
  ```c
  typedef enum { SEG_STRAIGHT, SEG_ARC } seg_type_t;
  typedef struct {
      seg_type_t type;
      float R;          // 圆弧半径 (m); 直线忽略
      float length;     // 圆弧: theta(rad) / 直线: distance(m)
      float v;          // 段线速度 (m/s)
      int   direction;  // 圆弧: +1逆时针 / -1顺时针; 直线忽略
  } traj_segment_t;
  ```
- 状态结构体改为持有段数组：`segs / num_segs / seg_index / loop`。
- 段结束自动切下一段；`loop = 1` 时走完最后一段回到第 0 段 → **闭合曲线循环** (循迹用)。
- 新增 `trajectory_run_path(segs, num, loop)`；`arc / circle / straight` 变为内部单段路径的便捷封装 (行为不变)。

## 改动二：闭环控制预留接口

当前为开环前馈 (运动学外环开环 + 轮速内环 PID 闭环)。为后期加入循迹传感器闭环预留：

- 状态结构体新增 `closed_loop` 标志 + `traj_feedback_fn feedback` 回调。
- `trajectory_update()` 改为**每 tick 下发**轮速，闭环修正直接叠加：
  ```
  v_L_cmd = ff_v_L - corr,   v_R_cmd = ff_v_R + corr     (corr = feedback())
  ```
  开环时 `corr` 恒为 0，行为与纯前馈一致；循迹 PID 后期无需改控制主干即可挂入。
- 新增接口：
  ```c
  typedef float (*traj_feedback_fn)(void);        // 返回转向修正 (m/s)
  void trajectory_set_feedback(traj_feedback_fn fn);
  void trajectory_enable_closed_loop(uint8_t enable);
  ```

## 控制方式：中断 vs 轮询

- **电机控制 (实时) → 中断**：`TIMER_0_INST_IRQHandler` (TIMG12，10ms) 内依次调用
  `motor_control_update()` (测速→PID→驱动) 和 `trajectory_update()` (轨迹状态机)。
- **串口命令 (非实时) → 轮询**：主循环 `cmd_poll()` 轮询 `DL_UART_isRXFIFOEmpty` + firewater 输出。

```c
void TIMER_0_INST_IRQHandler(void) {
    motor_control_update();   // 内环: 编码器测速 + 轮速 PID
    trajectory_update();      // 外环: 分段路径前馈 (+ 预留闭环修正)
    DL_TimerG_clearInterruptStatus(TIMER_0_INST, DL_TIMERG_INTERRUPT_ZERO_EVENT);
}
```

## 8 字闭合曲线示例 (empty.c)

```c
static const traj_segment_t g_figure8[] = {
    // type,        R,     length,      v,     dir
    { SEG_STRAIGHT, 0.0f,  0.30f,       0.1f,  +1 },   // 交叉直线
    { SEG_ARC,      0.30f, 4.712389f,   0.1f,  +1 },   // 左环 CCW 270°
    { SEG_STRAIGHT, 0.0f,  0.30f,       0.1f,  +1 },   // 交叉直线
    { SEG_ARC,      0.30f, 4.712389f,   0.1f,  -1 },   // 右环 CW  270°
};
trajectory_run_path(g_figure8, 4, 1);   // loop=1 循环循迹
```

## 上电注意事项

1. **8 字尺寸为示例值**：`R=0.3`、直线 `0.3`、270° 需按实际赛道几何标定使 8 字真正闭合。
2. **标定常量** (`trajectory.c` 顶部)：`WHEEL_BASE=0.16`、`WHEEL_RADIUS=0.033`、`GEAR_RATIO=20` 必须按实车修改。
3. **方向/接线**：若转向与预期相反，翻转 `direction` 或 `MOTOR_L_SIGN / MOTOR_R_SIGN`。
4. **RPM 为电机轴转速** (减速前)：`set_speed` 使用值 = 轮输出轴转速 × 减速比。

## 构建

`Drivers` 为普通托管构建源文件夹，CCS 每次构建自动重新生成 `Debug/*.mk` 并编译 `trajectory.c`，无需手动改 makefile。
