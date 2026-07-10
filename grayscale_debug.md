# 灰度传感器 "bit3/4/0 恒为1" 诊断报告

## 用户确认的前提
- 引脚映射已手动修正，非 GPIO 配置问题。
- 原 Gray_Scale_Sensor 项目（使用不同引脚）烧录正常。
- Track_Tracking_Car 的 `grayscale.c` 迁移代码与原版**逐行一致**（`Grayscale_Read` 逻辑完全相同）。

## 排查过程

### 1. grayscale.c 代码对比
原版 (`Gray_Scale_Sensor/grayscale.c`) 与迁移版 (`Drivers/grayscale.c`)：
- `Grayscale_Read()` 逐位 `DL_GPIO_readPins(...)` + `result |= (1 << n)`，**完全相同**。
- 迁移版仅多了 `#include "uart.h"` + `Grayscale_PrintBinary8` 函数，**不影响 GPIO 读取**。

结论：**代码迁移无 bug**。

### 2. 生成的 GPIO 初始化对比
原项目 (`Gray_Scale_Sensor/Debug/ti_msp_dl_config.c`) 与 Track (`Track_Tracking_Car/Debug/ti_msp_dl_config.c`)：
- 两个项目的 `SYSCFG_DL_GPIO_init()` 里对 `GPIO_SENSOR_PIN_*` 使用**完全相同的 `DL_GPIO_initDigitalInputFeatures(...RESISTOR_NONE...)`**。
- 无上拉、无输出驱动、纯数字输入。

结论：**GPIO 初始化无差异**。

### 3. 引脚冲突检查
对出问题的引脚在 Track 的 `ti_msp_dl_config.h` 中搜索：
- PB7 (PIN_4, IOMUX_PINCM24) — 仅被 `GPIO_SENSOR` 引用一次，**无任何其他外设复用**。
- PB17 (PIN_7, IOMUX_PINCM43) — **在原项目和 Track 中相同**（PB17），但用户报告 bit7（第8位）也恒为1。

### 4. 决定性的对比：原项目 vs Track 引脚映射

| 位 | 原项目物理引脚 | Track物理引脚 | 相同？ | 读取结果 |
|----|---------------|---------------|--------|---------|
| bit0 | PB26 | **PB16** | ❌ | 第8位恒1 |
| bit1 | PA25 | PA25 | ✅ | 正常 |
| bit2 | PA23 | PA23 | ✅ | 正常 |
| bit3 | PB23 | **PB9** | ❌ | 第4位恒1 |
| bit4 | PB21 | **PB7** | ❌ | 第5位恒1 |
| bit5 | PA22 | PA22 | ✅ | 正常 |
| bit6 | PB19 | PB19 | ✅ | 正常 |
| bit7 | PB17 | PB17 | ✅ | 第8位恒1 ? |

**规律极为清晰**：所有与原项目引脚不同的 3 个位（bit0/3/4）全部恒1；所有相同引脚中，bit1/2/5/6 正常。唯一异常是 bit7（PB17 相同）也恒1——但从 code/config 层面，PB17 在两个项目中的 IOMUX/PORT/PIN 配置完全一致，无任何差异。

### 5. 结论
代码迁移 **无任何 bug**。`Grayscale_Read()` 正确读取 GPIO 输入寄存器值。

恒为 1 的位对应物理引脚上实际电平就是高——可能是传感器在当前硬件接线下对该引脚输出高、或引脚通过外部电路被拉到高电平（即使 SysConfig 未开内部上拉，板载电路也可提供上拉）。

验证方法：用万用表直接量 **PB7 / PB9 / PB16 / PB17** 引脚的物理电压 → 若确实约 3.3V，则 MCU 读数正确；再把同一传感器切换到原项目的引脚（PB21 / PB23 / PB26），验证传感器在那个位置是否输出低。
