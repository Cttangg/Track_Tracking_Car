# UART v2 — 迁移操作指南

## 一、复制清单

以下 4 个文件复制到目标工程的 `Drivers/` 目录：

```
Drivers/
├── uart.h                  ← 公开 API + 类型定义 + 宏
├── uart.c                  ← 实现 (双环形缓冲 + ISR + Framer + CRC)
├── README_UART.md          ← API 参考
└── README_UART_DESIGN.md   ← 架构设计 & TX Kick 机制说明
```

> `empty.syscfg` 和 `empty.c` **不复制**。目标工程需自行在 `.syscfg` 中配置 UART 实例，在 `main.c` 中调用库 API。

---

## 二、API 冲突检查

### 2.1 全局符号 (必查)

| 符号 | 类型 | 冲突风险 | 处理方式 |
|------|------|---------|---------|
| `UART_0_INST_IRQHandler` | 函数 → 展开为 `UART0_IRQHandler` | **高** | 全工程只能库内唯一定义，删除其他文件的定义 |
| `g_uart0` | 全局变量 | **高** | 类型 `UART_Port`，命名统一，查找同名变量 |
| `g_uart1` | 全局变量 | 中 | 仅在 `#define UART1_ENABLE` 时定义 |

### 2.2 库函数 (大写 `UART_` 前缀)

| 函数 | 冲突风险 |
|------|---------|
| `UART_Init` / `UART_RxEnable` / `UART_RxDisable` | 中 |
| `UART_Write` / `UART_WriteByte` / `UART_Puts` / `UART_Printf` / `UART_PrintfFast` | 中 |
| `UART_WriteBlocking` / `UART_TxFlush` / `UART_TxFree` | 中 |
| `UART_Available` / `UART_ReadByte` / `UART_Read` / `UART_Peek` / `UART_ReadLine` / `UART_RxFlush` | 中 |
| `UART_FramerInitFixed` / `UART_FramerInitDelim` / `UART_FramerInitLen` / `UART_FramerInitCustom` | 低 |
| `UART_FramerSetCallback` / `UART_FramerPoll` / `UART_FramerPollBytes` | 低 |
| `UART_CRC16` / `UART_SendFrameFixed` / `UART_SendFrameLenCRC` | 低 |
| `UART_GetErrors` / `UART_ClearErrors` / `UART_Recover` | 低 |
| `UART_ISR_Handler` | 低 |
| `UART_1_INST_IRQHandler` | 仅 `UART1_ENABLE` 时 |

**检查方法**: 在目标工程中搜索 `= UART_`、`(UART_` 或 `UART_Init` 等函数名，确认无重定义。

### 2.3 库宏 (非 SysConfig 生成)

| 宏 | 默认值 | 冲突风险 |
|----|--------|---------|
| `UART0_RX_SIZE` / `UART0_TX_SIZE` | 256 / 256 | 中 |
| `UART1_ENABLE` | (注释) | 低 |
| `UART1_RX_SIZE` / `UART1_TX_SIZE` | 256 / 256 | 低 |
| `UART_H` (header guard) | - | 低 |

### 2.4 静态符号 (无外部冲突)

以下全部 `static`，不会与外部冲突：`ring_init`、`ring_count`、`ring_push`、`ring_pop`、`port_setup`、`tx_byte`、`tx_kick_fifo`、`emit_*`、`framer_*_feed`、`rx_byte_isr`。

---

## 三、SysConfig 依赖宏 (由 SysConfig 生成，非库提供)

| 宏 | 含义 | 示例值 |
|----|------|--------|
| `UART_0_INST` | UART 寄存器基址 | `UART0` |
| `UART_0_INST_INT_IRQN` | NVIC 中断号 | `UART0_INT_IRQn` |
| `UART_0_INST_IRQHandler` | ISR 函数名宏 | `UART0_IRQHandler` |
| `CPUCLK_FREQ` | 系统时钟频率 | `32000000` |

若 SysConfig 中 UART 实例名为 `UART_0`，这些宏自动生成。若改为 `UART_1` 则宏名变为 `UART_1_INST` 系列，需同步修改 `uart.c` 中 `UART_Init()` 的引用。

---

## 四、目标工程 .syscfg 配置模板

在目标工程的 `.syscfg` 中添加：

```js
const UART   = scripting.addModule("/ti/driverlib/UART", {}, false);
const UART1  = UART.addInstance();

UART1.$name                    = "UART_0";
UART1.targetBaudRate           = 115200;
UART1.enabledInterrupts        = ["RX"];           // 不含 TX
UART1.peripheral.rxPin.$assign = "PA11";
UART1.peripheral.txPin.$assign = "PA10";
UART1.txPinConfig.$name        = "ti_driverlib_gpio_GPIOPinGeneric2";
UART1.rxPinConfig.$name        = "ti_driverlib_gpio_GPIOPinGeneric3";
UART1.peripheral.$suggestSolution = "UART0";
```

> `enabledInterrupts` **必须只含 `["RX"]`**。TX 中断由库动态管理，SysConfig 开启会导致初始 NVIC 洪水。

---

## 五、迁移步骤

1. 复制 `uart.h` / `uart.c` / `README_UART.md` / `README_UART_DESIGN.md` 到目标工程 `Drivers/`
2. 按第四节模板修改目标 `.syscfg`，添加 UART 实例
3. 重新生成 SysConfig (`sysconfig_cli`)
4. 执行第二节 API 冲突检查
5. 若发现 `UART0_IRQHandler` 重定义 → 删除其他文件中的定义
6. 若发现 `g_uart0` / `UART_*` 函数名冲突 → 重命名冲突项
7. 在 `main.c` 中 `#include "./Drivers/uart.h"`，按 API 文档调用
8. 编译验证

---

## 六、生成 & 编译

```powershell
# 1. 生成 SysConfig
& "C:\TI\sysconfig_1.26.2\sysconfig_cli.bat" `
  -s "C:\TI\mspm0_sdk_2_11_00_07\.metadata\product.json" `
  --script "<项目路径>\<目标.syscfg>" `
  -o "." --compiler ticlang

# 2. 编译
gmake -C "<项目路径>\Debug" clean all
```

---

## 七、常见问题

| 现象 | 排查 |
|------|------|
| 编译报错 `UART_0_INST` 未定义 | SysConfig 未生成或 .syscfg 无 UART 模块 |
| 链接错误 `UART0_IRQHandler multiply defined` | 有其他文件也定义了 ISR，删除重复 |
| 有启动信息但 echo 无回传 | PA11 接线/电平, 确认 `UART_RxEnable(&g_uart0)` 已调用 |
| 启动信息乱码 | 波特率不匹配 (115200 8N1) 或时钟不一致 |
| TX 中断洪水 | `.syscfg` 的 `enabledInterrupts` 含 TX，改为 `["RX"]` |
