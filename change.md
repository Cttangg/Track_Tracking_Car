# 变更记录 — 串口统一 + 灰度传感器接入

> 本次修改：将 `empty.c` 全面迁移到通用串口库，接入灰度传感器模块，并实现双档 printf。

## 概览

| 目标 | 结果 |
|------|------|
| 消除多套 UART 实现冲突 | `empty.c` 收发统一走通用库 `g_uart0`，单一 TX 驱动 |
| 接入灰度传感器 | `grayscale.c/.h` 迁入 `Drivers/`，串口部分改用通用库 |
| 实时输出加传感器状态 | firewater 行新增第 9 字段 `S`（8bit 二进制） |
| printf 支持浮点 | `UART_Printf` 全功能 + `UART_PrintfFast` 高性能精简 |

## 文件变动

| 文件 | 变动 |
|------|------|
| `empty.c` | 删除自带阻塞发送/轮询；改用通用库收发；`main` 加 `UART_Init()` |
| `Drivers/grayscale.h` | 新增（迁自 Gray_Scale_Sensor），加 `Grayscale_PrintBinary8` |
| `Drivers/grayscale.c` | 新增；GPIO 读取保留，串口改 `UART_WriteByte(&g_uart0,...)` |
| `Drivers/uart.c` / `uart.h` | `UART_Printf` 重写为自实现全功能；新增 `UART_PrintfFast` |
| `Drivers/README_UART.md` / `README_UART_DESIGN.md` | 文档更新（常用函数前置、去冗余） |
| `Drivers/README_GRAYSCALE.md` | 新增（灰度模块说明） |
| 根目录旧 `uart.c` / `uart.h` | 删除（旧视觉模块 UART，引脚/ISR 冲突） |

## 关键改动细节

### 1. empty.c 串口迁移（消除冲突）
- **原因**：`empty.c` 的阻塞发送与通用库的 TX 环形缓冲+中断两套驱动争用 UART0，且通用库未 `UART_Init()` 处于休眠，导致编译后串口无输出。
- **改法**：
  - `main()` 增加 `UART_Init()`（初始化 `g_uart0`，开 RX 中断，接管 UART0 ISR）。
  - 发送：`UART0_printf/sendStr` → `UART_Printf/UART_Puts(&g_uart0, ...)`。
  - 接收：`cmd_poll` 由轮询 FIFO 改为 `UART_ReadByte(&g_uart0,...)` 读 RX 环形缓冲。
  - **命令组装与解析（`cmd_poll`/`cmd_do`）仍在 `empty.c`，不写入通用库。**

### 2. 双档 printf
- `UART_Printf`：全功能，`%c %s %d %i %u %x %X %f %%`，支持 `%.Nf`/`l`；`%f` 自实现，不依赖 TI 库浮点 printf。
- `UART_PrintfFast`：仅 `%c %s %d %u %x %%`，无浮点/宽度/精度，直接写环形缓冲，热路径用。
- PID 增益回显改用 `%.3f`。

### 3. 灰度传感器接入
- 模块迁入 `Drivers/`；`Grayscale_Read()` 返回 8 路电平打包字节，`Grayscale_PrintBinary8()` 经通用库输出。
- firewater 实时行：`T1,R1,D1,F1,T2,R2,D2,F2,S`，`S` 为 8bit 二进制（如 `10011100`），每 50ms 输出。

## 上电验证清单

1. 开机收到 `Dual Motor Control Ready` + `M1/M2` 两行 → TX 通路 OK。
2. 发 `?` / `Tr1 2000`（以 `\n` 或 `\r` 结尾）有回显 → RX 中断→环形缓冲→解析 OK。
3. firewater 每 50ms 一行 9 字段。

## 待办 / 注意

- ⚠️ `Grayscale_Read()` 依赖 SysConfig 的 `GPIO_SENSOR_PIN_*` 宏，需手动在 SysConfig 添加 8 路输入引脚（未加前无法编译）。
- ⚠️ 原灰度项目引脚 PA12/PA13/PA17 与本项目电机 PWM/编码器冲突，合并时须重新分配。
- ⚠️ PA11 悬空开 RX 中断易被噪声刷爆，确认 RX 接稳/上拉。
