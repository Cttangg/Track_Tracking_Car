#ifndef UART_H
#define UART_H

#include "ti_msp_dl_config.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>

/* ============================================================
 * 通用 UART 库 (双环形缓冲 + 中断驱动 + 可插拔协议 framer)
 * 详见 Drivers/README_UART_DESIGN.md
 * ============================================================ */

/* ===== 缓冲区配置 (字节, 必须为 2 的幂) ===== */
#define UART0_RX_SIZE   256
#define UART0_TX_SIZE   256

/* 预留: 取消注释启用 UART1 (需 SysConfig 先添加 UART_1_INST) */
/* #define UART1_ENABLE */
#define UART1_RX_SIZE   256
#define UART1_TX_SIZE   256

/* ===== 环形缓冲 (单生产者/单消费者) ===== */
typedef struct {
    uint8_t          *buf;
    uint16_t          mask;    /* size-1, size 为 2 的幂 */
    volatile uint16_t head;    /* 写索引 (生产者) */
    volatile uint16_t tail;    /* 读索引 (消费者) */
} UART_Ring;

/* ===== 协议 framer (可插拔, 挂载后旁路环形缓冲) ===== */
struct UART_Framer;
typedef uint16_t (*UART_FramerFeedFn)(struct UART_Framer *self, uint8_t byte);
typedef void     (*UART_FramerFrameFn)(const uint8_t *frame, uint16_t len);

typedef struct UART_Framer {
    UART_FramerFeedFn  Feed;      /* 逐字节喂入, 返回帧长(完成) 或 0 */
    UART_FramerFrameFn OnFrame;   /* 帧完成回调 (在 ISR 上下文调用) */
    uint8_t  *buf;                /* 帧组装缓冲 */
    uint16_t  size;               /* 帧缓冲容量 */
    uint16_t  idx;                /* 当前组装位置 */
    uint8_t   head;               /* 定长帧: 帧头 */
    uint8_t   tail;               /* 定长帧: 帧尾 */
    uint16_t  frame_len;          /* 定长帧: 帧长 */
    uint8_t   delim;              /* 分隔帧: 分隔符 */
} UART_Framer;

/* ===== 错误计数 ===== */
typedef struct {
    volatile uint32_t rx_overflow;  /* 软件环形缓冲满, 丢弃新字节 */
    volatile uint32_t hw_overrun;   /* 硬件 FIFO 溢出 */
    volatile uint32_t framing;      /* 帧错误 */
    volatile uint32_t parity;       /* 校验错误 */
} UART_Errors;

/* ===== 端口对象 ===== */
typedef struct {
    UART_Regs   *inst;
    IRQn_Type    irqn;
    UART_Ring    rx;
    UART_Ring    tx;
    UART_Framer *framer;   /* NULL = 原始字节流 (走环形缓冲) */
    UART_Errors  err;
} UART_Port;

/* ===== 全局端口 ===== */
extern UART_Port g_uart0;              /* UART0: PA10(TX)/PA11(RX) */
#ifdef UART1_ENABLE
extern UART_Port g_uart1;
#endif

/* ===== 初始化 (初始化所有已启用端口, 只开 TX, RX 中断延后开) ===== */
void UART_Init(void);

/* 使能/关闭 RX 中断 (PA11 稳定后调用, 先清空硬件 FIFO 和环形缓冲) */
void UART_RxEnable(void);
void UART_RxDisable(void);

/* ===== 发送 (非阻塞入队 + TX 中断; 满则入队部分并返回实际数) ===== */
uint16_t UART_Write(UART_Port *port, const uint8_t *data, uint16_t len);
int      UART_WriteByte(UART_Port *port, uint8_t b);
int      UART_Puts(UART_Port *port, const char *s);
int      UART_Printf(UART_Port *port, const char *fmt, ...);     /* 全功能: %c%s%d%i%u%x%X%f%%, 支持 %.Nf / l */
int      UART_PrintfFast(UART_Port *port, const char *fmt, ...); /* 高性能精简: 仅 %c%s%d%u%x%%, 无浮点 */
void     UART_WriteBlocking(UART_Port *port, const uint8_t *data, uint16_t len);
void     UART_TxFlush(UART_Port *port);   /* 阻塞等待 TX 全部发出 */
uint16_t UART_TxFree(UART_Port *port);    /* TX 缓冲剩余可写空间 */

/* ===== 接收 (RX 中断入环形缓冲, 主循环消费) ===== */
uint16_t UART_Available(UART_Port *port);
int      UART_ReadByte(UART_Port *port, uint8_t *b);              /* 返回 1=有数据 0=空 */
uint16_t UART_Read(UART_Port *port, uint8_t *buf, uint16_t len);
int      UART_Peek(UART_Port *port, uint8_t *b);                 /* 读但不移除 */
int      UART_ReadLine(UART_Port *port, char *buf, uint16_t size); /* 读到 '\n', 返回长度; 无整行返回 0 */
void     UART_RxFlush(UART_Port *port);

/* ===== 协议 framer ===== */
void UART_FramerInitFixed(UART_Framer *f, uint8_t *buf, uint16_t size,
                          uint8_t head, uint8_t tail, uint16_t frame_len);
void UART_FramerInitDelim(UART_Framer *f, uint8_t *buf, uint16_t size, uint8_t delim);
void UART_FramerInitCustom(UART_Framer *f, UART_FramerFeedFn feed,
                           UART_FramerFrameFn on_frame);
void UART_AttachFramer(UART_Port *port, UART_Framer *f);
void UART_DetachFramer(UART_Port *port);

/* ===== 错误 ===== */
const UART_Errors *UART_GetErrors(UART_Port *port);
void UART_ClearErrors(UART_Port *port);

/* ===== ISR (全工程唯一定义) ===== */
void UART_ISR_Handler(UART_Port *port);
void UART_0_INST_IRQHandler(void);
#ifdef UART1_ENABLE
void UART_1_INST_IRQHandler(void);
#endif

#endif /* UART_H */
