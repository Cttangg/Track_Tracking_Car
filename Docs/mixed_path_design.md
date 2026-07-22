# 混合路径（循线 + 陀螺仪闭环）设计方案

> 实际赛道场景：黑线段和不连续的无黑线段交替出现。
> 目标：自动检测黑线有无 → 切换 PID 源，用陀螺仪补全无线段的角度/位移。

## 一、赛道模型

```
         无黑线段 1m 直走
         ←────────────→
    ┌─────────────────────┐
    │                     │
    │   有黑线半圆 (R=0.4) │  第2段: 循线 + 陀螺仪闭环角度
    │        ↻            │
    └─────────┐───────────┘
              │
    ┌─────────┘───────────┐
    │   有黑线半圆 (R=0.4) │  第4段: 循线 + 陀螺仪闭环角度
    │        ↻            │
    └─────────────────────┘
    │                     │
    │   无黑线段 1m 直走   │
    ←────────────→
```

4 段交替：无黑线直行 → 有黑线半圆弯道 → 无黑线直行 → 有黑线半圆弯道。

## 二、核心问题

### 2.1 黑线消失检测（30ms 消抖）

**3 拍确认**：有黑线段（`use_line=1`）中连续 3 次（30ms）灰度全为零 → 判定黑线消失。防止传感器短暂闪烁（线边缘抖动）导致误触发。

有黑线圆弧段线丢失时的处理：
1. 记录当前已累积的陀螺仪角度 `accum`
2. 计算剩余角度 `remaining = theta − accum`
3. 停止当前段 → 立即调用 `trajectory_rotate(remaining, 0.05, direction)` 用极慢速补全角度
4. rot 完成后 → 进入下一段

### 2.2 有黑线的圆弧段

黑线提供**横向位置偏差**（LinePID），陀螺仪积分**记录已转过角度**。

若黑线在半路消失（锯齿弯、断线）：
- → 立即切 `rot(剩余角度, 0.05, 方向)` 用陀螺仪闭环补全
- → 不依赖线是否连续

### 2.3 切换策略（即时响应 + rot 补角）

```
每 10ms, 当前段 use_line=1 且有弧:
  Grayscale_Read() → 有黑线?
    ├── 是 → LinePID 横向修正, 陀螺仪积分角度
    │        角度到 π? → 切下一段
    └── 否 → 立即停止当前段
              remaining = π − rotate_accum
              trajectory_rotate(remaining, 0.05, direction)  ← 慢速陀螺仪闭环补角
              rot 完成后 → 切下一段

当前段 use_line=0 (无黑线直行):
  直接 GyroPID 直行, 时间倒数
```

## 三、实现方案

### 3.1 扩展现有架构（最小侵入）

不改 `steering.c` 的双模式切换逻辑（它已正确处理 A↔B 自动切换）。

**改为在 `trajectory.c` 中增加「段配置」字段**，段定义时声明该段的完成条件：

```c
typedef struct {
    seg_type_t type;
    float   R, length, v;
    int     direction;
    uint8_t use_line;       /* 新增: 1=该段启用循线反馈 */
    uint8_t gyro_stop;      /* 新增: 1=该段用陀螺仪角度做停止判定 */
} traj_segment_t;
```

### 3.2 各段配置（顺时针闭合）

| 段 | type | use_line | gyro_stop | dir | 前馈 | 反馈 | 停止条件 |
|----|------|----------|-----------|-----|------|------|---------|
| 1 | SEG_STRAIGHT | 0 | 1 | — | 两轮等速 | GyroPID | 时间倒数 1m |
| 2 | SEG_ARC | 1 | 1 | **-1** | 差速(R=0.4) | LinePID(横向) | 陀螺仪=π rad；线丢则 `rot` 补 |
| 3 | SEG_STRAIGHT | 0 | 1 | — | 两轮等速 | GyroPID | 时间倒数 1m |
| 4 | SEG_ARC | 1 | 1 | **-1** | 差速(R=0.4) | LinePID(横向) | 陀螺仪=π rad；线丢则 `rot` 补 |

> 两次圆弧均为 **顺时针 (CW, direction=-1)**。段 2 转 180° 后车头反向，段 3 直行回到起点附近，段 4 再转 180° 恢复原方向 → **闭合**。

### 3.3 关键改动点

**`trajectory.c` — `apply_speed()`**

对 `use_line=1` 的段，且灰度读到黑线时用 LinePID；否则用 GyroPID。

**`trajectory.c` — `trajectory_update()`**

对 `use_line=1` 的弧段：陀螺仪积分角度。黑线存在→抵达目标弧度→切段。黑线消失→记录 `rotate_accum` → 停当前段 → 调用 `trajectory_rotate(剩余, 0.05, dir)` 补角 → 完成后切段。

**`trajectory.c` — line detection helper**

```c
uint8_t traj_line_present(void) { return Grayscale_Read() != 0; }
```

每 tick 检测，丢失即触发切换（**不等 500ms**）。

## 四、段定义 API 扩展

### `trajectory.h`

```c
typedef struct {
    seg_type_t type;
    float      R, length, v;
    int        direction;
    uint8_t    use_line;      /* 该段是否启用地磁传感反馈 */
    uint8_t    gyro_stop;     /* 该段是否使用陀螺仪角度停止 */
} traj_segment_t;
```

### 预置路径函数

```c
int trajectory_mix1(void);   /* 预置 4 段混合赛道, 顺时针闭合 */
// int trajectory_mix2(void);  /* 预留 */
// int trajectory_mix3(void);  /* 预留 */
```

内部实现：
```c
static const traj_segment_t s_mix1[] = {
    { SEG_STRAIGHT, 0,   1.0f,  0.2f, 0,  0, 1 },  // 段1: 无黑线直行1m
    { SEG_ARC,      0.4f, 3.14f, 0.15f,-1, 1, 1 },  // 段2: 黑线半圆 CW
    { SEG_STRAIGHT, 0,   1.0f,  0.2f, 0,  0, 1 },  // 段3: 无黑线直行1m (车反向)
    { SEG_ARC,      0.4f, 3.14f, 0.15f,-1, 1, 1 },  // 段4: 黑线半圆 CW (恢复方向)
};
trajectory_run_path(s_mix1, 4, 0);
```

### 串口命令

| 命令 | 功能 |
|------|------|
| `mix1` | 执行预置 4 段混合赛道 |
| `mix2` | 预留 |
| `mix3` | 预留 |

## 五、改动汇总

| 文件 | 改动 |
|------|------|
| `trajectory.h` | `traj_segment_t` 加 `use_line` / `gyro_stop` 字段；+`trajectory_mix1` 声明；预留 `trajectory_mix2/mix3` |
| `trajectory.c` | `apply_speed` 按 `use_line` 选择 LinePID/GyroPID；`trajectory_update` 30ms 消抖检测线消失 → rot 补角；+`trajectory_mix1` 实现 |
| `empty.c` | +`mix1` 命令；预留 `mix2`/`mix3` |
| `steering.c` | **不动** — 黑线检测自动切换模式已在 |

## 七、测试流程

```
mix1           → 执行 4 段混合赛道
  段1: 无黑线直行1m (GyroPID 直行)
  段2: 黑线半圆 CW (LinePID 横向 + 陀螺仪角度 π 停止)
       若途中黑线消失 → 30ms 后确认 → rot(剩余, 0.05, -1) 补全
  段3: 无黑线直行1m (GyroPID 直行, 车头反向)
  段4: 黑线半圆 CW (LinePID 横向 + 陀螺仪角度 π 停止)
       若途中黑线消失 → 同上补全
  回到起点, 恢复原方向
```
