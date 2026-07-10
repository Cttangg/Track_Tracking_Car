# firewater / 串口调试记录（已解决）

## 根因与修复

**根因**：我最初写的**中断驱动 TX**（`tx_kick` + TX 中断 + 环形缓冲）有时序问题，把发出的字节改写成错误值 → 乱码，连 `TICK`/banner 都被损坏。

**修复**：`Drivers/uart.c` 的 TX 改为**阻塞直写 FIFO**（等同原工程可用写法），删除 `tx_kick`/TX 中断/ISR 的 TX 分支；RX 保持中断+环形缓冲。

修复后：启动提示干净、`TICK` 每 50ms 正常 → 整条链路（TX / 定时器 ISR / g_fw_ready / firewater）全通。已恢复 `firewater_send()`。

## 开头乱码（残留，无害）

- 现象：一切正常，但**最开头**有一小段乱码。
- 性质：**上电/复位瞬间** TX 引脚的电平瞬态，终端在 MCU 复位时收到的杂散字节。几乎所有 MCU 都有，一次性、无害。
- 缓解（已加入 `empty.c`）：
  ```c
  UART_Init();
  delay_cycles(CPUCLK_FREQ / 100);   // ~10ms 等 TX 线路稳定
  ...
  UART_Puts(&g_uart0, "\r\nDual Motor Control Ready\r\n");  // 前导换行隔开
  ```
- 若想彻底消除：只能靠硬件（TX 加上拉、复位期间不接收），或在上位机忽略首行。固件侧无法 100% 消除复位瞬态。

## 结论

串口收发、定时器状态输出、灰度状态均已正常工作。开头乱码为复位瞬态，可忽略。
