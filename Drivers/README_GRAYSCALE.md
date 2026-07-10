# 灰度传感器模块 (grayscale)

8 路灰度循迹传感器驱动：读取 8 个数字 GPIO 电平，打包为一个字节；串口输出经通用 UART 库 (`g_uart0`)。

> 迁自 `Gray_Scale_Sensor` 项目，原独立的 `uart_driver` 已并入通用串口库 [`README_UART.md`](README_UART.md)。

## 硬件平台

- MCU: MSPM0G3507
- 传感器: 8 路灰度/循迹传感器，数字量输出（1=高电平 / 0=低电平）
- 输出: 经 UART0 (PA10 TX) 打印状态

## 引脚分配

| 传感器位 | 原引脚 | 说明 |
|----------|--------|------|
| PIN_0 (bit0) | PB26 | |
| PIN_1 (bit1) | PA25 | |
| PIN_2 (bit2) | PA23 | |
| PIN_3 (bit3) | PB23 | |
| PIN_4 (bit4) | PB21 | |
| PIN_5 (bit5) | PA11 | |
| PIN_6 (bit6) | PB19 | |
| PIN_7 (bit7) | PB17 | |

## 文件结构

```
Drivers/
  grayscale.h          模块接口
  grayscale.c          GPIO 读取 + 串口输出 (依赖通用 uart 库)
  README_GRAYSCALE.md  本文档
```

## API

```c
#include "./Drivers/grayscale.h"

/* 初始化 (引脚由 SysConfig 托管, 此处为预留软件钩子) */
void Grayscale_Init(void);

/* 读取 8 路电平, 打包返回:
 *   bit0 = PIN_0 ... bit7 = PIN_7  (1:高电平 0:低电平) */
uint8_t Grayscale_Read(void);

/* 以 8 位二进制明文 + CRLF 打印状态 (经通用库 g_uart0)
 * 例: 0b10011100 -> "10011100\r\n"  (bit7 在前) */
void Grayscale_PrintBinary8(uint8_t value);
```

## 使用示例

### 独立打印

```c
SYSCFG_DL_init();
UART_Init();            // 通用串口库 (Grayscale_PrintBinary8 依赖它)
Grayscale_Init();

while (1) {
    uint8_t s = Grayscale_Read();
    Grayscale_PrintBinary8(s);      // 输出如 "10011100\r\n"
    delay_cycles(CPUCLK_FREQ / 10); // ~100ms
}
```

### 并入 firewater 实时行 (本项目 empty.c 的用法)

```c
uint8_t sensor = Grayscale_Read();
char sbits[9];
for (int i = 7; i >= 0; i--)
    sbits[7 - i] = (sensor & (1 << i)) ? '1' : '0';
sbits[8] = '\0';
// 作为第 9 字段并入: T1,R1,D1,F1,T2,R2,D2,F2,S
UART_Printf(&g_uart0, "...,%s\r\n", sbits);
```

## 位序说明

```
返回值 result:
  bit7 bit6 bit5 bit4 bit3 bit2 bit1 bit0
  PIN7 PIN6 PIN5 PIN4 PIN3 PIN2 PIN1 PIN0

Grayscale_PrintBinary8 打印顺序: 高位(bit7/PIN7) 在最左
```

## 注意事项

| 事项 | 说明 |
|------|------|
| 依赖通用 UART | `Grayscale_PrintBinary8` 用 `g_uart0`，调用前需 `UART_Init()` |
| SysConfig 引脚 | 需提供 `GPIO_SENSOR_PIN_0..7_PORT/PIN` 宏，否则无法编译 |
| 引脚冲突 | 与电机 PWM/编码器冲突的位须重新分配 (见引脚表) |
| 读取为即时电平 | `Grayscale_Read` 无滤波/消抖，如需稳定值可多次采样取多数 |
