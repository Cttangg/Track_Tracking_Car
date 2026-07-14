# UART 初始化失败 / 串口乱码 调试说明书

## 症状

- 烧录后开机**经常**出现乱码（非 100% 复现，间歇性）。
- `UART_Init()` 偶尔初始化失败，串口无输出或全是乱码。
- 即使 banner 正常，后续也可能混入乱码。

## 根因：PA11 浮空 + RX 中断过早使能

```
上电 → SYSCFG_DL_init() 使能 UART 外设
     → UART_Init() 立即开 RX 中断 + NVIC
     → PA11 浮空 → UART 硬件把噪声识别为数据帧
     → RX 中断持续触发 → ISR 把噪声字节灌入环形缓冲
     → cmd_poll() 读到乱码 / ISR 占 CPU 导致 TX 被挤压 / 烧录时序被干扰
```

**为什么间歇性？** PA11 浮空时收到的噪声取决于环境电磁干扰、上电瞬间的电平、调试器连接状态等因素，不同时间可能不同。

**为什么原 DC_Motor 没这问题？** 原代码 **RX 用轮询**（`DL_UART_isRXFIFOEmpty`），**不开 RX 中断**，噪声最多堆 4 字节硬件 FIFO，主循环 `cmd_poll` 转一圈就清空，不影响整体运行。迁移到通用库后 RX 变成**中断模式**，噪声触发 ISR 持续运行。

## 修复方案：延迟开 RX 中断

### 改动 1: `Drivers/uart.c`

`UART_Init()` **不再开 RX 中断** — 只初始化端口结构体和环形缓冲，TX 阻塞直发。

新增两个函数：

```c
void UART_RxEnable(void);   // 先清空硬件 FIFO + 环形缓冲，再开 RX 中断 + NVIC
void UART_RxDisable(void);  // 关 RX 中断，清空 FIFO + 环形缓冲，回退到纯 TX
```

### 改动 2: `empty.c` main()

```c
SYSCFG_DL_init();
UART_Init();                        // 仅 TX
delay_cycles(CPUCLK_FREQ * 1);      // 静默 1 秒，等 PA11 稳定
UART_RxEnable();                    // 先清残留噪声，再开 RX 中断
// ... 此后 banner / cmd_poll 正常工作
```

**原理**：上电 1 秒内 PA11 的初始噪声已消散（或被 `UART_RxEnable` 内部清空），此后 RX 中断收到的才是有效数据。

## 如果仍有问题 — 进阶排查

### 1. 确认 PA11 是否接稳
万用表量 PA11 对 GND 电压：空闲高电平应 ≈ 3.3V。若悬空/跳变，外部上拉 10kΩ 到 3.3V。

### 2. 回退到纯 TX 模式测试
```c
// main 里只用 TX, 不开 RX:
UART_Init();
// 不调 UART_RxEnable();
UART_Puts(&g_uart0, "TX only test\r\n");
while(1);  // 看串口助手是否收到干净的一行
```
若能收到 → TX 正常，问题确定在 RX 侧。

### 3. 临时切回轮询 RX
若中断 RX 持续有问题，可以完全不调 `UART_RxEnable()`，改用 `DL_UART_isRXFIFOEmpty` 直接读 FIFO（和原版一样）：
```c
// cmd_poll 备用实现 (轮询)
while (!DL_UART_isRXFIFOEmpty(UART_0_INST)) {
    char c = (char)DL_UART_receiveData(UART_0_INST);
    // ... 组装行
}
```
此时通用库的 RX 中断完全不用，仅用其 TX 阻塞直发功能。

## 影响范围

| 文件 | 改动 |
|------|------|
| `Drivers/uart.c` | `port_setup` 移除 RX 中断使能；新增 `UART_RxEnable/RxDisable` |
| `Drivers/uart.h` | 新增 `UART_RxEnable/RxDisable` 声明 |
| `empty.c` | `main()` 增加 `UART_RxEnable()` 调用（延后到 1 秒静默后） |

无其他文件变动。旧命令/轨迹/灰度代码不受影响。
