# 串口命令参考手册

> 实现位置: `empty.c` → `cmd_do()` / `cmd_show()` / `cmd_poll()`
> 通用库: `Drivers/uart.c/h` (阻塞 TX + 中断 RX, `UART_RxEnable` 延迟开)

## 解析流程

```
终端输入 "Tr1 4000\r\n"
  → cmd_poll():  逐字节 UART_ReadByte → 遇 \r/\n 组装成行 "Tr1 4000"
  → cmd_do():    sscanf 取第一 token "Tr1" → 匹配命令 → 提取参数 → 调用 API
```

命令以 `\r` 或 `\n` 结尾。空格分隔 token。大小写敏感（全部小写/混合）。

---

## 命令速查表（共 27 条）

### 状态查询

| 命令 | 示例 | 功能 |
|------|------|------|
| `?` | `?` | 打印双电机状态 + 完整帮助菜单 |

### 电机控制（单电机，末尾带 1/2）

| 命令 | 参数 | 示例 | 函数 |
|------|------|------|------|
| `Tr1` / `Tr2` | `<rpm>` | `Tr1 -500` | `motor_control_set_speed` (负=前进) |
| `Kp1` / `Kp2` | `<v>` | `Kp1 0.5` | `motor_control_set_kp` |
| `Ki1` / `Ki2` | `<v>` | `Ki2 2.0` | `motor_control_set_ki` |
| `Kd1` / `Kd2` | `<v>` | `Kd1 0.0` | `motor_control_set_kd` |
| `Dd1` / `Dd2` | `<duty 0~4000>` | `Dd1 2000` | `motor_control_set_duty` (手动开环 PWM) |
| `stop1` / `stop2` | 无 | `stop1` | `motor_control_stop` |

### 电机控制（双电机，无后缀）

| 命令 | 参数 | 示例 | 函数 |
|------|------|------|------|
| `Tr` | `<rpm>` | `Tr -300` | 双电机同 RPM |
| `Dd` | `<duty>` | `Dd 2000` | 双电机同 PWM |
| `stop` | 无 | `stop` | 双电机同时停 |

### 轨迹控制

| 命令 | 参数 | 示例 | 函数 |
|------|------|------|------|
| `st` | `<距离m> <速度m/s>` | `st 0.5 0.1` | `trajectory_straight` |
| `arc` | `<R_m> <theta_rad> <速度> <方向±1>` | `arc 0.3 3.14 0.1 1` | `trajectory_arc` |
| `cir` | `<R_m> <速度> <方向±1>` | `cir 0.5 0.1 -1` | `trajectory_circle` |
| `lf` | `<速度m/s>` | `lf 0.3` | `trajectory_linefollow` (闭环循线, 无限) |
| `stop_all` | 无 | `stop_all` | `trajectory_stop` (轨迹停车) |

### 传感器

| 命令 | 参数 | 示例 | 函数 |
|------|------|------|------|
| `gs` | 无 | `gs` | `Grayscale_Read` → 打印 `GS=01100000` |

### 闭环转向 PID 参数

| 命令 | 参数 | 示例 | 函数 |
|------|------|------|------|
| `Lp` | `<Kp>` | `Lp 0.4` | `LinePID_SetKp` |
| `Li` | `<Ki>` | `Li 0.1` | `LinePID_SetKi` |
| `Ld` | `<Kd>` | `Ld 0.0` | `LinePID_SetKd` |
| `Gp` | `<Kp>` | `Gp 0.5` | `GyroPID_SetKp` |
| `Gi` | `<Ki>` | `Gi 0.02` | `GyroPID_SetKi` |
| `Gd` | `<Kd>` | `Gd 0.0` | `GyroPID_SetKd` |
| `mode` | `<0\|1>` | `mode 1` | `Steering_SetMode` (0=循线A, 1=陀螺仪B) |

### 帮助输出 (`?`)

发 `?` 后 `cmd_show()` 打印:
```
M1: Tr=xxx RPM=xxx D=xxx F=xxx
M2: Tr=xxx RPM=xxx D=xxx F=xxx
--- HELP ---
Motor: Tr1|Tr2 <rpm>  Kp1|Kp2 <v>  Ki1|Ki2 <v>  Kd1|Kd2 <v>
       Dd1|Dd2 <dut>  stop1|stop2  Tr <rpm>  Dd <dut>  stop
Traj:  st <dist> <v>  arc <R> <th> <v> <dir>  cir <R> <v> <dir>
       stop_all
Sensor: gs
Steer: Lp|Li|Ld <v>  Gp|Gi|Gd <v>  mode 0|1
------------
```

---

## 错误处理

| 输入 | 输出 |
|------|------|
| 无效命令 | `ERR: unknown cmd 'xxx'` |
| 旧格式命令缺 motorID 后缀 | `ERR: unknown cmd 'xxx'` |
| 单电机命令缺值 | `ERR: M1 Tr needs value` |
| 轨迹命令参数不足 | `ERR: st <dist_m> <speed_mps>` |
| mode 参数错误 | `ERR: mode 0(A/line) or 1(B/gyro)` |

---

## 解析顺序（匹配优先级）

`cmd_do` 按以下顺序匹配命令，命中即 `return`：

1. `?` → 状态 + 帮助
2. `stop_all` → 轨迹停车
3. `gs` → 灰度状态
4. PID 参数 (`Lp/Li/Ld/Gp/Gi/Gd/mode`)
5. 轨迹 (`st/arc/cir`)
6. 双电机 (`stop/Tr/Dd` 无后缀)
7. 单电机 (`Tr1/Tr2/Kp1/Kp2/...` 末尾数字提取 motorID)
8. 无匹配 → 错误

---

## 典型操作序列

### 直行测试
```
st 1.0 0.1              → 前进 1m @ 0.1m/s, 到站自动停
```

### 圆弧转弯
```
arc 0.3 3.14 0.1 1      → 半径 0.3m 半圆左转
arc 0.3 3.14 0.1 -1     → 半径 0.3m 半圆右转
```

### 闭环循迹调参
```
Lp 0.4                  → 设循线 Kp=0.4
Li 0.1                  → 设循线 Ki=0.1
mode 0                  → 强制切回循线模式
```

### 电机测试
```
Tr -300                 → 双电机前进 300 RPM
stop                    → 立即停转
```

### 完整调参流程
```
Kp1 0.5                 → 设电机1 Kp
Ki1 2.0                 → 设电机1 Ki
Tr1 -1000               → 电机1 1000 RPM 前进
?                       → 查看实际 RPM 是否跟踪目标
stop1                   → 停电机1
```

---

## 默认参数速查

### 电机 PID（增量式，`motor_control_init`）

| 参数 | 默认值 | 命令 |
|------|--------|------|
| Kp | 2.0 | `Kp1 2.0` / `Kp2 2.0` |
| Ki | 10.0 | `Ki1 10.0` / `Ki2 10.0` |
| Kd | 0.0 | `Kd1 0.0` / `Kd2 0.0` |
| 控制周期 | 10ms | 硬件固定 |

### 循线 PID（模式 A，`LinePID_Init`）

| 参数 | 默认值 | 命令 |
|------|--------|------|
| Kp | 0.3 | `Lp 0.3` |
| Ki | 0.05 | `Li 0.05` |
| Kd | 0.0 | `Ld 0.0` |

### 陀螺仪 PID（模式 B，`GyroPID_Init`）

| 参数 | 默认值 | 命令 |
|------|--------|------|
| Kp | 0.5 | `Gp 0.5` |
| Ki | 0.02 | `Gi 0.02` |
| Kd | 0.0 | `Gd 0.0` |

### 转向

| 参数 | 默认值 | 命令 |
|------|--------|------|
| 模式 | 0 (循线 A) | `mode 0` / `mode 1` |

### 硬件常量（`trajectory.c`）

| 参数 | 值 |
|------|-----|
| 轮距 WHEEL_BASE | 0.14 m |
| 轮半径 WHEEL_RADIUS | 0.024 m (直径 0.048m) |
| 减速比 GEAR_RATIO | 20:1 |
| 控制周期 CTRL_DT | 0.01 s |
| 循迹修正上限 MAX_CORR | ±0.15 m/s |
| 速度调制范围 | 1.0(居中) ~ 0.4(偏线) |
| EMA 滤波系数 α | 0.3 |
| 转向修正频率 | 20 Hz |

### 编码器

| 参数 | 值 |
|------|-----|
| PPR | 500 |
| 采样频率 | 100 Hz |
| 溢出检测 | 软件 16-bit 回绕 |
