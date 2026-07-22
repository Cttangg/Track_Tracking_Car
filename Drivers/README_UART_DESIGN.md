# UART 库架构设计 v2

> 基于 v1 规格定稿，v2 新增 Framer 移出 ISR、多 UART 参数化、错误恢复、协议层 CRC/长度帧。

---

## 1. 架构

```
         UART Hardware
              │
         ISR (纯数据搬运, 零协议逻辑)
         ┌────┴────┐
        RX         TX
         │          │
    rx ring     tx ring
         │
    主循环任务
         ├── UART_Read* / ReadLine
         ├── UART_FramerPoll()  (帧解析, OnFrame 回调)
         └── UART_Write* / Printf / SendFrame
```

### ISR 规则

- **RX**: 硬件 FIFO → `ring_push`
- **TX**: `ring_pop` → 硬件 FIFO
- **禁止**: 调用 framer、printf、阻塞函数、复杂逻辑
- **安全**: 最多循环 128 次，防止硬件异常死循环

### 主循环规则

- **RX 消费**: `UART_ReadByte/Read/ReadLine` 从 rx ring 取数据
- **Framer**: `UART_FramerPoll()` 从 rx ring 取字节喂 framer，`OnFrame` 回调在主循环上下文
- **TX 生产**: `UART_Write/WriteByte/Printf` 入 tx ring
- **禁止**: 直接操作 FIFO（*除 TX Kick 例外*，见下文）

---

## 2. TX 中断与 Kick 机制

### 问题

MSPM0 UART TX 中断是**边沿触发**：FIFO 从"满"下降到"阈值以下"才产生中断边沿。

```
ISR 排空 ring → 关 TX 中断 → FIFO 排空
→ 主循环入队 → enableInterrupt(TX)
→ FIFO 已是空状态, 条件恒真, 无边沿 → ISR 永不触发
```

### 尝试过的方案

| 方案 | 结果 | 原因 |
|------|------|------|
| `NVIC_SetPendingIRQ` 强制 pend | 失败 | ISR 内读 IIDX 得不到 TX 标志，MIS 已置位但无新边沿 |
| 永不关闭 TX 中断 | 失败 | 边沿同样丢失 + 空转开销 |

### Kick 方案 (当前)

```
主循环: ring_push → enableInterrupt(TX) → 若 FIFO 有空: ring_pop + transmitData
ISR:    ring_pop → transmitData → ring空时 disableInterrupt(TX)
```

Kick 直写 FIFO 一个字节 → 该字节移位排空 → 制造"FIFO 非空→空"边沿 → ISR 触发正常消费环形队列。

**这是 MSPM0 平台特性导致的最小必要例外**，主循环仅在"启动传输链"时碰一次 FIFO，所有后续搬运仍由 ISR 完成。

---

## 3. Framer 协议解析 (v2 变化)

v1: ISR 内 `Feed` → `OnFrame` 回调在 ISR 上下文执行
v2: ISR 只写 rx ring，主循环 `UART_FramerPoll()` 逐字节喂 framer，`OnFrame` 在主循环上下文

| 版本 | Framer 运行位置 | 限制 |
|------|----------------|------|
| v1 | ISR 上下文 | 回调不能做耗时操作 (printf/计算) |
| v2 | 主循环上下文 | 无限制，可做任意操作 |

### 使用方式

```c
// 初始化 framer
UART_FramerInitFixed(&framer, buf, 64, 0xAA, 0xBB, 7);
UART_FramerSetCallback(&framer, on_frame);

// 主循环中轮询
while (1) {
    UART_FramerPoll(&g_uart0);  // 从 rx ring 取数据喂 framer
}
```

### Framer 类型

| 类型 | 初始化函数 | 帧格式 |
|------|-----------|--------|
| `FIXED` | `UART_FramerInitFixed` | head + ... + tail (定长) |
| `DELIM` | `UART_FramerInitDelim` | ... + delim (变长分隔) |
| `LEN` | `UART_FramerInitLen` | head + len + payload + crc + tail |
| `CUSTOM` | `UART_FramerInitCustom` | 自定义 feed 函数 |

---

## 4. 多 UART 参数化 (v2 变化)

v1: `UART_RxEnable()` / `UART_RxDisable()` 硬编码 `UART_0_INST`
v2: 接受 `UART_Port *port` 参数，可复用到任意实例

```c
// v1
UART_RxEnable();

// v2
UART_RxEnable(&g_uart0);
UART_RxEnable(&g_uart1);  // 需 #define UART1_ENABLE
```

---

## 5. 错误恢复 `UART_Recover()`

异常恢复流程：
1. 关 RX/TX 外设中断
2. 清硬件 RX FIFO
3. 清软件环形缓冲 (rx + tx)
4. 刷 IIDX 清残留中断标志
5. 重开 RX 中断

适用场景：长时间通信异常后恢复、协议同步丢失、FIFO 溢出链式错误。

---

## 6. SysConfig 配置注意

### UART 实例宏

SysConfig 生成的宏 (例 `UART_0` 命名):
```c
#define UART_0_INST               UART0          // 寄存器基址
#define UART_0_INST_IRQHandler    UART0_IRQHandler  // ISR 函数名
#define UART_0_INST_INT_IRQN      UART0_INT_IRQn    // NVIC 中断号
```

库代码通过 `UART_0_INST` / `UART_0_INST_INT_IRQN` 引用，依赖 SysConfig 生成。如果 SysConfig 中 UART 实例改名，需同步检查这些宏。

### enabledInterrupts

`.syscfg` 中 `UART1.enabledInterrupts` 应设为 `["RX"]`（不含 TX）：
- RX: 由 SysConfig 初始化时开启，`UART_RxEnable()` 接管
- TX: 完全由库动态管理 (enable on data / disable on empty)

若 SysConfig 开启了 TX 中断，库的 `port_setup()` 会防御性关闭。

### ISR 符号冲突

SysConfig 可能生成弱符号 `UART_0_INST_IRQHandler` (即 `UART0_IRQHandler`)。库的 `uart.c` 定义**强符号**，链接器优先选择。若有多个目标文件定义同名函数，链接报错 `symbol multiply defined`。
