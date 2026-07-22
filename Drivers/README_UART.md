# 通用 UART 库 v2 (uart)

双环形缓冲 + 双向中断驱动的通用串口库。ISR 纯数据搬运，协议解析由主循环轮询 Framer 完成。

> 架构设计见 [`README_UART_DESIGN.md`](README_UART_DESIGN.md)。
> 调试操作见 [`DEBUG_GUIDE.md`](DEBUG_GUIDE.md)。

---

## 快速上手

```c
#include "./Drivers/uart.h"

SYSCFG_DL_init();              // 硬件初始化 (UART 外设/引脚/时钟)
UART_Init();                   // 初始化端口 + 环形缓冲 + NVIC
UART_RxEnable(&g_uart0);       // 开 RX 中断 (会先清空硬件 FIFO 和环形缓冲)
```

> **注意**: 不要在应用层定义 `UART_0_INST_IRQHandler`，库内已定义。

---

## 常用 API

### 打印

```c
UART_Puts(&g_uart0, "Hello\r\n");
UART_Printf(&g_uart0, "x=%.2f y=%d\r\n", 1.5f, 3);   // 全功能: %c%s%d%i%u%x%X%f%%
UART_PrintfFast(&g_uart0, "rpm=%d\r\n", rpm);         // 精简: 仅 %c%s%d%u%x%%
```

### 收发字节

```c
uint8_t ch;
if (UART_ReadByte(&g_uart0, &ch)) {           // 非阻塞取一字节
    UART_WriteByte(&g_uart0, ch);              // 非阻塞入队发送
}

uint8_t buf[64];
uint16_t n = UART_Read(&g_uart0, buf, 64);    // 批量读取
UART_Write(&g_uart0, buf, n);                 // 批量发送
```

### 读一行

```c
char line[64];
if (UART_ReadLine(&g_uart0, line, sizeof(line)) > 0) {
    // 收到完整一行 (含 '\n')
}
```

### 发送帧 (v2 新增)

```c
// 定长帧: AA + payload + BB
UART_SendFrameFixed(&g_uart0, 0xAA, 0xBB, payload, len);

// 长度+CRC帧: AA + len(2B LE) + cmd + payload + crc16(2B LE) + BB
UART_SendFrameLenCRC(&g_uart0, 0xAA, 0xBB, 0x01, payload, len);
```

### 协议 Framer (主循环轮询)

```c
static uint8_t   s_buf[64];
static UART_Framer s_framer;

void on_frame(const uint8_t *f, uint16_t len) {
    // 帧完成回调 (主循环上下文, 可做耗时操作)
}

// 定长帧
UART_FramerInitFixed(&s_framer, s_buf, 64, 0xAA, 0xBB, 7);
UART_FramerSetCallback(&s_framer, on_frame);

// 在主循环中轮询:
while (1) {
    UART_FramerPoll(&g_uart0);    // 从 RX ring 取字节喂 framer
    // ... 其他任务
}
```

### 错误与恢复

```c
const UART_Errors *e = UART_GetErrors(&g_uart0);
// e->rx_overflow  e->hw_overrun  e->framing  e->parity

UART_ClearErrors(&g_uart0);   // 清零错误计数
UART_Recover(&g_uart0);       // 异常恢复: 清 FIFO + ring + 重开中断
```

---

## 完整 API 清单

### 初始化
| 函数 | 说明 |
|------|------|
| `void UART_Init(void)` | 初始化端口 + 环形缓冲 + NVIC |
| `void UART_RxEnable(UART_Port *port)` | 清 FIFO/ring，开 RX 中断 |
| `void UART_RxDisable(UART_Port *port)` | 关 RX 中断，清 FIFO |

### 发送
| 函数 | 说明 |
|------|------|
| `uint16_t UART_Write(port, data, len)` | 非阻塞入队，返回入队字节数 |
| `int UART_WriteByte(port, b)` | 入队单字节 |
| `int UART_Puts(port, s)` | 发送 C 字符串 |
| `int UART_Printf(port, fmt, ...)` | 全功能格式化 (含 `%f`) |
| `int UART_PrintfFast(port, fmt, ...)` | 精简格式化 (无浮点，更快) |
| `void UART_WriteBlocking(port, data, len)` | 入队 + 等发完 |
| `void UART_TxFlush(port)` | 等 ring + FIFO 排空 |
| `uint16_t UART_TxFree(port)` | TX ring 剩余空间 |

### 接收
| 函数 | 说明 |
|------|------|
| `uint16_t UART_Available(port)` | RX ring 可读字节数 |
| `int UART_ReadByte(port, &b)` | 取一字节，返回 1=有/0=空 |
| `uint16_t UART_Read(port, buf, len)` | 批量取，返回实际数 |
| `int UART_Peek(port, &b)` | 查看不移除 |
| `int UART_ReadLine(port, buf, size)` | 读一行到 `\n` |
| `void UART_RxFlush(port)` | 丢弃所有未读 |

### Framer
| 函数 | 说明 |
|------|------|
| `UART_FramerInitFixed(f, buf, size, head, tail, frame_len)` | 定长帧 |
| `UART_FramerInitDelim(f, buf, size, delim)` | 分隔符帧 |
| `UART_FramerInitLen(f, buf, size, head, tail, len_off, len_sz, crc_off, crc_sz)` | 长度字段帧 (v2 新增) |
| `UART_FramerInitCustom(f, feed, on_frame)` | 自定义 feed |
| `UART_FramerSetCallback(f, on_frame)` | 设置/替换回调 |
| `uint16_t UART_FramerPoll(port)` | 从 RX ring 取字节喂已挂载的 framer |
| `uint16_t UART_FramerPollBytes(f, data, len)` | 手动喂字节到 framer |

### 帧发送工具 (v2 新增)
| 函数 | 说明 |
|------|------|
| `uint16_t UART_CRC16(data, len)` | CRC-16-CCITT (0x1021) |
| `UART_SendFrameFixed(port, head, tail, payload, len)` | 发送定长帧 |
| `UART_SendFrameLenCRC(port, head, tail, cmd, payload, len)` | 发送 长度+CRC 帧 |

### 错误 & 恢复
| 函数 | 说明 |
|------|------|
| `const UART_Errors *UART_GetErrors(port)` | 取错误计数结构体 |
| `void UART_ClearErrors(port)` | 清零错误计数 |
| `void UART_Recover(port)` | 异常恢复: 清 FIFO/ring/IIDX/重开中断 |

---

## 数据类型

```c
extern UART_Port g_uart0;              // UART0 全局端口 (PA10 TX / PA11 RX)

typedef struct { ... } UART_Port;      // 端口对象: inst + irqn + rx/tx ring + framer + err + tx_int_en
typedef struct { ... } UART_Ring;      // 环形缓冲: buf + mask + head/tail + max_used
typedef struct {                       // Framer: 帧解析器
    UART_FramerType     type;
    UART_FramerFeedFn   Feed;          // 逐字节解析
    UART_FramerFrameFn  OnFrame;       // 帧完成回调 (主循环上下文)
    uint8_t  *buf; uint16_t size;
    // ... 各类型专用参数
} UART_Framer;
typedef struct {                       // 错误计数
    volatile uint32_t rx_overflow, hw_overrun, framing, parity, tx_underrun;
} UART_Errors;
```

---

## 缓冲配置 (`uart.h`)

| 宏 | 默认 | 说明 |
|----|------|------|
| `UART0_RX_SIZE` / `UART0_TX_SIZE` | 256 / 256 | UART0 环形缓冲 (必须 2 的幂) |
| `UART1_ENABLE` | (注释) | 取消注释启用 UART1 |
| `UART1_RX_SIZE` / `UART1_TX_SIZE` | 256 / 256 | UART1 环形缓冲 |

---

## 注意事项

| 事项 | 说明 |
|------|------|
| ISR 唯一 | 禁止在其他模块定义 `UART_x_INST_IRQHandler` |
| Framer 在主循环 | `FramerPoll()` 在主循环调用，`OnFrame` 回调也在主循环上下文 |
| TX Kick | MSPM0 TX 中断为边沿触发，库在 `tx_byte()` 中直写 FIFO 一个字节制造边沿，详见 `README_UART_DESIGN.md` |
| 缓冲 2 的幂 | `assert()` 强制检查，不满足则编译期/运行期报错 |
| %f 自实现 | `UART_Printf` 用软浮点，不依赖 TI 库的 `%f`；热路径用 `UART_PrintfFast` |
| PA11 浮空 | 开 RX 中断前确认引脚接稳，噪声可能触发 ISR 洪水 |
