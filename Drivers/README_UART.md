# 通用 UART 库 (uart)

双环形缓冲 + 中断驱动的通用串口库。协议解析由可插拔 framer 完成，与收发解耦。

> 设计决策/冲突分析见 [`README_UART_DESIGN.md`](README_UART_DESIGN.md)。

## 快速上手

```c
#include "./Drivers/uart.h"

SYSCFG_DL_init();   // 先配置好 UART 硬件 (波特率/引脚)
UART_Init();        // 初始化端口 + 开 RX 中断 (ISR 由库内自动接管)
```
> 应用层**不要**再定义 `UART_0_INST_IRQHandler`，库内已定义。

---

## 常用函数（先看这些）

### 1. 打印字符串 / 格式化

```c
UART_Puts(&g_uart0, "Hello\r\n");             // 发送字符串 (非阻塞入队)
UART_Printf(&g_uart0, "x=%.2f y=%d\r\n", 1.5f, 3);   // 全功能: 支持 %f
UART_PrintfFast(&g_uart0, "rpm=%d\r\n", rpm);        // 高性能精简: 无浮点, 热路径用
```

| 函数 | 注释 |
|------|------|
| `int UART_Puts(UART_Port *port, const char *s)` | 发送以 `'\0'` 结尾的字符串，返回实际入队字节数 |
| `int UART_Printf(UART_Port *port, const char *fmt, ...)` | 全功能：`%c %s %d %i %u %x %X %f %%`，支持 `%.Nf` 精度、`l` 长度；`%f` 自实现不依赖 TI 库；含 double 软浮点，较慢 |
| `int UART_PrintfFast(UART_Port *port, const char *fmt, ...)` | 高性能精简：仅 `%c %s %d %u %x %%`，无浮点/宽度/精度，直接写环形缓冲 |

### 2. 定长帧 —— 接收 + 处理

以 7 字节帧 `[AA][mode][xL][xH][yL][yH][BB]` 为例：

```c
static uint8_t   s_buf[7];
static UART_Framer s_framer;

// 帧完成回调 (在 ISR 上下文, 只解析+存数据, 别做耗时操作)
static void on_packet(const uint8_t *f, uint16_t len) {
    uint8_t mode = f[1];
    int16_t x = (int16_t)((f[3] << 8) | f[2]);   // 小端
    int16_t y = (int16_t)((f[5] << 8) | f[4]);
    // ... 存入全局变量供主循环使用
}

UART_FramerInitFixed(&s_framer, s_buf, sizeof(s_buf), 0xAA, 0xBB, 7);
s_framer.OnFrame = on_packet;      // 赋值完成回调
UART_AttachFramer(&g_uart0, &s_framer);   // 挂载后 RX 自动组帧
```

| 函数 | 注释 |
|------|------|
| `void UART_FramerInitFixed(f, buf, size, head, tail, frame_len)` | 初始化定长帧解析器 (帧头/帧尾/总长) |
| `void UART_AttachFramer(port, f)` | 挂载解析器；挂载后该端口 RX 旁路环形缓冲，逐字节喂 framer |
| `f->OnFrame = fn` | 一帧完整时的回调 `void fn(const uint8_t *frame, uint16_t len)` |

### 3. 定长帧 —— 发送

库不含专用组包函数，自己拼字节后 `UART_Write` 即可：

```c
// 发送 [AA][mode][xL][xH][yL][yH][BB]
void send_packet(uint8_t mode, int16_t x, int16_t y) {
    uint8_t f[7] = { 0xAA, mode,
                     (uint8_t)(x & 0xFF), (uint8_t)(x >> 8),   // 小端
                     (uint8_t)(y & 0xFF), (uint8_t)(y >> 8), 0xBB };
    UART_Write(&g_uart0, f, sizeof(f));
}
```

| 函数 | 注释 |
|------|------|
| `uint16_t UART_Write(port, data, len)` | 发送任意字节数组 (非阻塞入队)，返回实际入队数 |

### 4. 读一行文本命令 (不挂 framer 时)

```c
char line[64];
if (UART_ReadLine(&g_uart0, line, sizeof(line)) > 0) {
    // 收到一整行 (含 '\n')，解析 line ...
}
```

| 函数 | 注释 |
|------|------|
| `int UART_ReadLine(port, buf, size)` | 有整行(到 `'\n'`)则拷贝并消费，返回长度；否则不消费，返回 0 |

---

## 完整 API 参考（全部函数）

### 初始化
```c
void UART_Init(void);   // 初始化所有已启用端口, 开 RX 中断; 在 SYSCFG_DL_init() 之后调用
```

### 发送
```c
uint16_t UART_Write(UART_Port *port, const uint8_t *data, uint16_t len);
                    // 非阻塞入队, 满则入队部分, 返回实际入队字节数
int      UART_WriteByte(UART_Port *port, uint8_t b);
                    // 入队单字节, 返回 1=成功 / 0=缓冲满
int      UART_Puts(UART_Port *port, const char *s);
                    // 发送 '\0' 结尾字符串, 返回入队字节数
int      UART_Printf(UART_Port *port, const char *fmt, ...);
                    // 全功能格式化: %c %s %d %i %u %x %X %f %% (支持 %.Nf / l); 含软浮点较慢
int      UART_PrintfFast(UART_Port *port, const char *fmt, ...);
                    // 高性能精简: 仅 %c %s %d %u %x %%, 无浮点/宽度/精度, 直接写环形缓冲
void     UART_WriteBlocking(UART_Port *port, const uint8_t *data, uint16_t len);
                    // 阻塞发送: 缓冲满则忙等 ISR 排空, 保证全部发出
void     UART_TxFlush(UART_Port *port);
                    // 阻塞等待 TX 缓冲与移位寄存器全部发完
uint16_t UART_TxFree(UART_Port *port);
                    // 返回 TX 缓冲剩余可写空间 (字节)
```

### 接收
```c
uint16_t UART_Available(UART_Port *port);
                    // RX 缓冲中当前可读字节数
int      UART_ReadByte(UART_Port *port, uint8_t *b);
                    // 取一字节, 返回 1=有数据 / 0=空 (非阻塞)
uint16_t UART_Read(UART_Port *port, uint8_t *buf, uint16_t len);
                    // 取至多 len 字节, 返回实际读取数
int      UART_Peek(UART_Port *port, uint8_t *b);
                    // 查看下一字节但不移除, 返回 1=有 / 0=空
int      UART_ReadLine(UART_Port *port, char *buf, uint16_t size);
                    // 读到 '\n' 的一整行(含换行, 追加'\0'); 无整行返回 0 且不消费
void     UART_RxFlush(UART_Port *port);
                    // 丢弃 RX 缓冲中所有未读数据
```

### 协议 framer
```c
void UART_FramerInitFixed(UART_Framer *f, uint8_t *buf, uint16_t size,
                          uint8_t head, uint8_t tail, uint16_t frame_len);
                    // 定长帧: [head]...[tail], 总长 frame_len
void UART_FramerInitDelim(UART_Framer *f, uint8_t *buf, uint16_t size, uint8_t delim);
                    // 分隔符帧: 累积到 delim(含) 为一帧, 如 '\n' 行
void UART_FramerInitCustom(UART_Framer *f, UART_FramerFeedFn feed,
                           UART_FramerFrameFn on_frame);
                    // 完全自定义: feed 逐字节返回帧长, 状态自管
void UART_AttachFramer(UART_Port *port, UART_Framer *f);
                    // 挂载 framer (RX 旁路环形缓冲, 逐字节喂 framer)
void UART_DetachFramer(UART_Port *port);
                    // 卸载 framer (回落到原始字节流, 用 UART_Read* 读)
```
> 三种 `Init` 后都需自行给 `f->OnFrame` 赋值 (帧完成回调, ISR 上下文)。

### 错误
```c
const UART_Errors *UART_GetErrors(UART_Port *port);
                    // 取错误计数结构 (rx_overflow/hw_overrun/framing/parity)
void UART_ClearErrors(UART_Port *port);
                    // 清零错误计数
```

### ISR（库内已实现，无需调用）
```c
void UART_ISR_Handler(UART_Port *port);   // 通用 ISR 主体: RX→framer/ring, TX→FIFO, 记错误
void UART_0_INST_IRQHandler(void);        // 库内定义 → 转调 UART_ISR_Handler(&g_uart0)
// void UART_1_INST_IRQHandler(void);     // UART1_ENABLE 时启用
```

---

## 数据类型

```c
extern UART_Port g_uart0;              // UART0 全局端口 (PA10 TX / PA11 RX)

typedef struct { ... } UART_Port;      // 端口对象 (rx/tx 环形缓冲 + framer + 错误)
typedef struct { ... } UART_Ring;      // 环形缓冲 (2 的幂容量)
typedef struct {                       // 协议解析器
    UART_FramerFeedFn  Feed;           // 逐字节喂, 返回帧长(完成)/0
    UART_FramerFrameFn OnFrame;        // 帧完成回调, 需自行赋值
    uint8_t *buf; uint16_t size, idx;  // 帧组装缓冲/状态
    uint8_t head, tail; uint16_t frame_len; uint8_t delim;  // 内置 framer 配置
} UART_Framer;
typedef struct {                       // 错误计数
    volatile uint32_t rx_overflow, hw_overrun, framing, parity;
} UART_Errors;
```

## 缓冲配置 (uart.h, 必须为 2 的幂)

| 宏 | 默认 | 说明 |
|----|------|------|
| `UART0_RX_SIZE` / `UART0_TX_SIZE` | 256 / 256 | UART0 收/发环形缓冲 |
| `UART1_ENABLE` | (注释) | 启用 UART1 (需 SysConfig 先加 `UART_1_INST`) |
| `UART1_RX_SIZE` / `UART1_TX_SIZE` | 256 / 256 | UART1 收/发环形缓冲 |

## 注意事项

| 事项 | 说明 |
|------|------|
| ISR 唯一 | 各模块删除自己的 `UART_x_INST_IRQHandler`，只用库内的，避免链接期符号重定义 |
| `%f` 打印 | 用全功能 `UART_Printf`(自实现浮点)；热路径/高频用 `UART_PrintfFast`(无浮点更快) |
| 缓冲 2 的幂 | `UARTx_RX_SIZE/TX_SIZE` 必须为 2 的幂，否则取模失效 |
| `OnFrame` 在 ISR | 回调只存数据，耗时处理放主循环 |
| PA11 浮空 | 开 RX 中断前确认上拉/接稳，否则噪声触发中断死机 (见 `Drivers/README.md`) |
