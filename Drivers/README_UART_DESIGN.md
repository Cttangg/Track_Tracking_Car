# 通用 UART 库设计方案 (Step 1 规格定稿)

> 状态: **设计阶段** (尚未实现)。用于合并多个模块各自的 UART 实现。
> 两轮决策后定稿: 双环形缓冲 + 内部静态分配 + 大写下划线命名 + framer 自定义协议。

## 1. 背景与冲突分析

多人代码各写各的 UART 实现, 合并时冲突:

| 冲突 | `empty.c` | `uart.c/h` |
|------|-----------|------------|
| UART0 RX 归属 | **轮询** `cmd_poll()` (`DL_UART_isRXFIFOEmpty`) | **中断** `UART_0_INST_IRQHandler` |
| 发送助手 | `UART0_sendStr` / `UART0_printf` | `UART_Printf` / `UART_SendPacket` |
| DL API 风格 | `DL_UART_transmitDataBlocking` | `DL_UART_Main_transmitDataBlocking` |
| 结构 | 无 | 每实例静态缓冲、硬编码 UART0/1 |

关键风险:
- 两套同时驱动 UART0 会互相抢数据。
- 多个模块各自定义 `UART_x_INST_IRQHandler` → **链接期符号重定义**。
- `Drivers/README.md` 记录过 "PA11 浮空噪声触发 RX 中断死机才改的轮询", 直接开 UART0 RX 中断可能复现。

**根因**: 现有 `uart.c` 把传输层 (收发字节) 与应用协议层 (AA..BB 帧解析) 焊死, 无法通用。

## 2. 全部决策汇总

| 维度 | 决策 |
|------|------|
| 缓冲分配 | 库内部 `static` 数组 + `#define UARTx_RX_SIZE / UARTx_TX_SIZE` 宏配置大小 |
| 环形缓冲约束 | **强制 2 的幂** (位与取模, ISR 更快) |
| 默认缓冲大小 | RX 256 / TX 256 字节 |
| 实例策略 | **UART0 写死 (PA10&PA11)** 加全局 `g_uart0`; UART1 等通过宏开关 (`#define UART1_ENABLE`) 预留 |
| TX 满策略 | 非阻塞入队, 返回实际入队字节数; 同时提供 `UART_WriteBlocking` 阻塞回退 |
| RX 满策略 | 丢弃新字节 + 溢出计数 |
| printf 策略 | 提供 `UART_Printf`, 栈缓冲 **128 字节**, **仅整数** (%f 不支持, 浮点用整数拆分) |
| 旧 API 兼容 | **硬替换**, 同步修改所有调用处 |
| 文件位置 | **移到 Drivers/uart.c + uart.h** |
| API 命名 | **保留大写下划线 `UART_`** (延续现有 uart.h 风格) |
| framer 分流 | 挂 framer 时 **旁路环形缓冲, 只喂 framer** (低延迟) |
| 坐标包 AA..BB | **剥离到应用层**, 视觉模块用 `UART_FramerInitFixed` 自行构建 |
| UART0 文本命令 | **不挂 framer, 原始环形缓冲 + UART_ReadLine** |
| 阻塞发送 | **同时提供阻塞版 UART_WriteBlocking 作为回退** |

## 3. 三层解耦架构

| 层 | 职责 | 说明 |
|----|------|------|
| L1 传输层 | 收发字节、环形缓冲、ISR 分发、错误统计 | 协议无关, MCU 通用核心 |
| L2 工具层 | Write/Printf、Read/ReadLine/Peek | 基于端口句柄, 与硬件实例无关 |
| L3 协议层 | 帧解析 (AA..BB / 行 / 自定义) | 各模块自定义 framer, 插在核心之上 |

端口对象 `UART_Port` 持有 `inst/irqn + rx/tx 环形缓冲 + framer + 错误计数`；UART0 写死为全局 `g_uart0`，UART1 等由 `UART1_ENABLE` 宏预留。

> **API 与数据类型详见 [`README_UART.md`](README_UART.md)**，此处不重复。

## 4. 各模块迁移方式

| 模块 | 原方式 | 迁移后 |
|------|--------|--------|
| `empty.c` 主循环 | `cmd_poll()` 轮询 FIFO | `UART_ReadLine(&g_uart0, ...)` 从环形缓冲读行 |
| `empty.c` 发送 | `UART0_sendStr` / `UART0_printf` | `UART_Puts` / `UART_Printf` |
| `empty.c` ISR | (无 UART ISR) | 由通用库的 `UART_0_INST_IRQHandler` 接管 |
| 视觉模块 | `UART_ParsePacket` / `UART_SendPacket` | 用 `UART_FramerInitFixed` 构建 + `UART_Write` 发送, 全移掉自己的 ISR |
| 视觉模块 ISR | `UART_1_INST_IRQHandler` | 由通用库的 `UART_1_INST_IRQHandler` 接管 |

## 5. 冲突消除要点

- 所有 UART ISR **只在通用库定义一次**; 其他模块一律不再写自己的 ISR。
- AA..BB 坐标包协议**从核心剥离**; 视觉模块自行 `UART_FramerInitFixed` + `UART_AttachFramer`。
- 统一 DL API 风格 (`DL_UART_Main_*`)。
- UART0 文本命令使用原始环形缓冲 + `UART_ReadLine` (不挂 framer, 保持现有的行命令交互模式)。

## 6. MCU 移植注意

- 单生产者 (ISR) / 单消费者 (主循环) 环形缓冲, 用 `volatile` head/tail; TX 相反 (主生产 / ISR 消费)。
- 环形缓冲强制 2 的幂, mask = size-1, ISR 内 `head = (head+1) & mask` (无除法)。
- TI minimal printf 不支持 `%f`, 需整数拆分输出浮点。
- ⚠️ PA11 浮空开 RX 中断会死机 (见 `Drivers/README.md`): 开中断前确认引脚上拉/接稳。

## 7. 实施步骤

1. ~~规划通用 UART 功能~~ (本文档)
2. 改写为通用 `Drivers/uart.c` + `Drivers/uart.h` 库实现
3. 覆写原代码 (`empty.c`) 的 UART 实现, 迁移到通用库
4. 导入另一模块 (视觉) 后, 再次用通用库覆写其 UART 实现
