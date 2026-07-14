# 串口命令扩展计划

## 当前命令系统分析

### 解析架构
```
cmd_poll() → 逐字节组装成行 → cmd_do(line)
                                    ↓
                            sscanf(line, "%7s %f", k, v)
                                    ↓
                            提取 k 末尾数字 → motorID
                            去掉末尾数字 → 纯命令名
                                    ↓
                            strcmp 匹配命令 → 执行
```

### 现有命令（仅单参数 + motorID 后缀）
| 命令 | 参数 | 函数 | 说明 |
|------|------|------|------|
| `Tr1 4000` | RPM | `motor_control_set_speed` | 设目标转速 |
| `Kp1 0.5` | float | `motor_control_set_kp` | PID P 参数 |
| `Ki1 1.5` | float | `motor_control_set_ki` | PID I 参数 |
| `Kd1 0.0` | float | `motor_control_set_kd` | PID D 参数 |
| `Dd1 2000` | duty | `motor_control_set_duty` | 手动 PWM |
| `stop1` | 无 | `motor_control_stop` | 停指定电机 |
| `?` | 无 | `cmd_show` | 打印状态 |

### 局限性
1. **只解析一个 float 参数**（`sscanf "%7s %f"`），无法支持多参数命令。
2. **强制要求 motorID 后缀**（命令末尾必须是 1 或 2），无法支持无 ID 的轨迹命令。
3. 纯文本命令（如直接输出传感器状态）无法触发。

## 可新增的命令（对应已有 API）

### A. 轨迹控制（2～3 参数，无 motorID）

| 命令 | 参数 | 函数 | 示例 |
|------|------|------|------|
| `st` | `<距离m> <速度m/s>` | `trajectory_straight` | `st 0.5 0.1` |
| `arc` | `<半径m> <弧度rad> <速度m/s> <方向+1/-1>` | `trajectory_arc` | `arc 0.3 3.14 0.1 1` |
| `cir` | `<半径m> <速度m/s> <方向+1/-1>` | `trajectory_circle` | `cir 0.5 0.1 -1` |
| `allstop` | 无 | `trajectory_stop` | `allstop` |

### B. 传感器读取（无参数）

| 命令 | 参数 | 函数 | 示例 |
|------|------|------|------|
| `gs` | 无 | `Grayscale_Read` | `gs` → 打印 8bit 二进制 |

### C. 现有命令扩展（增加双电机同时控制）

| 命令 | 参数 | 函数 | 示例 |
|------|------|------|------|
| `Tr` 不带后缀 | `<rpm>` | 两个电机同时设相同转速 | `Tr 4000` → M1+M2 同 RPM |

## 解析器改造方案

### 方案：通用 token 解析

放弃强制 motorID 后缀，改为：
1. 先取第一个 token 作为命令名。
2. 用 `strcmp` 匹配命令表，每个命令自带参数数量和格式。
3. 根据预期参数数量，`sscanf` 提取对应数量的值。

```
cmd_do(line):
    取命令名 k
    if k == "?":     cmd_show()
    elif k == "gs":  Grayscale_Read() + 打印
    elif k == "allstop": trajectory_stop()
    elif k == "st":  sscanf(line,"st %f %f", &d, &v) → trajectory_straight(d,v)
    elif k == "arc": sscanf(line,"arc %f %f %f %d", &R,&theta,&v,&dir) → trajectory_arc(...)
    elif k == "cir": sscanf(line,"cir %f %f %d", &R,&v,&dir) → trajectory_circle(...)
    elif k == "Tr":                     // 无后缀 → 双电机同 RPM
    elif k == "Tr1" 或 k == "Tr2":      // 有后缀 → 单电机
    elif ...                             // 旧命令保持兼容
```

### 优点
- 保留旧命令格式不变（`Tr1`/`Tr2`/`Kp1` — 末尾带数字），兼容已有操作习惯。
- 新命令用简短字母，不占用 `Tr` 系列命名空间。
- 多参数命令直接用 `sscanf` 一次性取完，简单可靠。
- 无参数命令（`?` / `gs` / `allstop`）直接执行。

### 实现成本
- 重写 `cmd_do` 约 80 行 → 120 行。
- `cmd_poll`（字节组装行）**不动**。
- 解析变复杂一点但仍在单函数内，无新文件。

## 风险评估

| 风险 | 缓解 |
|------|------|
| 旧命令格式被打破 | 保留末尾数字检测，`Tr1`/`Tr2` 老格式仍可用 |
| `sscanf` 多次调用效率低 | 命令是人工输入（非高频实时），可接受 |
| 命令拼错无提示 | 匹配失败统一回复 `ERR: unknown` |

## 实施步骤

1. 重写 `cmd_do` 函数，改为 token 匹配。
2. 在 `cmd_show` 中加入命令列表提示（发 `?` 时打印帮助）。
3. 测试：`st 0.5 0.1` → 直行；`arc 0.3 3.14 0.1 1` → 半圆；`gs` → 灰度状态。
4. 确认旧命令 `Tr1 4000` / `Kp1 0.5` / `?` 仍正常。
