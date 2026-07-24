#ifndef UART_H
#define UART_H

#include "ti_msp_dl_config.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>

/* ============================================================
 * 通用 UART 库 v2 (双环形缓冲 + 双向中断驱动)
 *
 * 架构:
 *   ISR 层: 纯数据搬运 (RX→ring, ring→TX FIFO), 零协议逻辑
 *   主循环: 从 ring 消费 RX 字节, 由调用者自行驱动协议解析
 *   Framer: 纯主循环轮询, 不在 ISR 上下文运行
 *
 * TX 策略:
 *   - 外设 TX 中断按需开启, 环形空时 ISR 关闭
 *   - 入队后 kick 直写 FIFO 首个字节 (MSPM0 边沿触发硬件限制,
 *     详见 uart.c 顶部 TX Kick 机制说明)
 *   - ISR 负责后续 ring→FIFO 搬运
 * ============================================================ */

/* ===== 缓冲区配置 (字节, 必须为 2 的幂) ===== */
#define UART0_RX_SIZE   256
#define UART0_TX_SIZE   512

/* 预留: 取消注释启用 UART1 (需 SysConfig 先添加 UART_1_INST) */
/* #define UART1_ENABLE */
#define UART1_RX_SIZE   256
#define UART1_TX_SIZE   256

/* ===== 环形缓冲 (SPSC, 2 的幂容量) ===== */
typedef struct {
    uint8_t          *buf;
    uint16_t          mask;        /* size-1, size 为 2 的幂 */
    volatile uint16_t head;        /* 生产者写索引 */
    volatile uint16_t tail;        /* 消费者读索引 */
} UART_Ring;

/* ===== 协议 framer 类型声明 (纯主循环轮询, 不在 ISR 中运行) ===== */
typedef enum {
    UART_FRAMER_NONE   = 0,
    UART_FRAMER_FIXED  = 1,       /* 定长帧: head+...+tail */
    UART_FRAMER_DELIM  = 2,       /* 分隔符帧: 到 delim 为止 */
    UART_FRAMER_LEN    = 3,       /* 长度字段帧: head+len+payload+crc+tail */
    UART_FRAMER_CUSTOM = 4,       /* 自定义 feed 函数 */
} UART_FramerType;

struct UART_Framer;
typedef uint16_t (*UART_FramerFeedFn)(struct UART_Framer *self, uint8_t byte);
typedef void     (*UART_FramerFrameFn)(const uint8_t *frame, uint16_t len);

typedef struct UART_Framer {
    UART_FramerType     type;
    UART_FramerFeedFn   Feed;     /* 逐字节喂入, 返回帧长(完成) 或 0 */
    UART_FramerFrameFn  OnFrame;  /* 帧完成回调 (主循环上下文) */
    uint8_t  *buf;                /* 帧组装缓冲 */
    uint16_t  size;               /* 帧缓冲容量 */
    uint16_t  idx;                /* 当前组装位置 */
    /* 定长帧参数 */
    uint8_t   head;               /* 帧头 */
    uint8_t   tail;               /* 帧尾 */
    uint16_t  frame_len;          /* 帧总长 */
    /* 分隔帧参数 */
    uint8_t   delim;              /* 分隔符 */
    /* 长度字段帧参数 */
    uint16_t  payload_len;        /* 解析出的负载长度 */
    uint8_t   len_offset;         /* 长度字段在帧中的偏移(不含帧头) */
    uint8_t   len_size;           /* 长度字段字节数 (1/2) */
    uint8_t   crc_offset;         /* CRC 起始偏移(不含帧头) */
    uint8_t   crc_size;           /* CRC 字节数 (0/1/2) */
} UART_Framer;

/* ===== 错误计数 ===== */
typedef struct {
    volatile uint32_t rx_overflow;  /* 软件环形缓冲满, 丢弃新字节 */
    volatile uint32_t hw_overrun;   /* 硬件 FIFO 溢出 (OE) */
    volatile uint32_t framing;      /* 帧错误 (FE) */
    volatile uint32_t parity;       /* 校验错误 (PE) */
} UART_Errors;

/* ===== 端口对象 ===== */
typedef struct {
    UART_Regs   *inst;
    IRQn_Type    irqn;
    UART_Ring    rx;
    UART_Ring    tx;
    UART_Framer *framer;       /* 主循环轮询的 framer (ISR 不触碰) */
    UART_Errors  err;
    volatile uint8_t tx_int_en; /* 0=TX 外设中断关闭, 1=开启 (主循环写, ISR 读) */
} UART_Port;

/* ===== 全局端口 ===== */
extern UART_Port g_uart0;
#ifdef UART1_ENABLE
extern UART_Port g_uart1;
#endif

/* ===== 初始化 ===== */
void UART_Init(void);

/* ===== RX 中断控制 (参数化端口) ===== */
void UART_RxEnable(UART_Port *port);
void UART_RxDisable(UART_Port *port);

/* ===== 发送 (非阻塞入队 + ISR 搬运 FIFO) ===== */
uint16_t UART_Write(UART_Port *port, const uint8_t *data, uint16_t len);
int      UART_WriteByte(UART_Port *port, uint8_t b);
int      UART_Puts(UART_Port *port, const char *s);
int      UART_Printf(UART_Port *port, const char *fmt, ...);
int      UART_PrintfFast(UART_Port *port, const char *fmt, ...);
void     UART_WriteBlocking(UART_Port *port, const uint8_t *data, uint16_t len);
void     UART_TxFlush(UART_Port *port);
uint16_t UART_TxFree(UART_Port *port);

/* ===== 接收 (主循环消费) ===== */
uint16_t UART_Available(UART_Port *port);
int      UART_ReadByte(UART_Port *port, uint8_t *b);
uint16_t UART_Read(UART_Port *port, uint8_t *buf, uint16_t len);
int      UART_Peek(UART_Port *port, uint8_t *b);
int      UART_ReadLine(UART_Port *port, char *buf, uint16_t size);
void     UART_RxFlush(UART_Port *port);

/* ===== 协议 framer (主循环轮询, 不在 ISR 中运行) ===== */
void     UART_FramerInitFixed(UART_Framer *f, uint8_t *buf, uint16_t size,
                              uint8_t head, uint8_t tail, uint16_t frame_len);
void     UART_FramerInitDelim(UART_Framer *f, uint8_t *buf, uint16_t size, uint8_t delim);
void     UART_FramerInitLen(UART_Framer *f, uint8_t *buf, uint16_t size,
                            uint8_t head, uint8_t tail,
                            uint8_t len_offset, uint8_t len_size,
                            uint8_t crc_offset, uint8_t crc_size);
void     UART_FramerInitCustom(UART_Framer *f, UART_FramerFeedFn feed,
                               UART_FramerFrameFn on_frame);
void     UART_FramerSetCallback(UART_Framer *f, UART_FramerFrameFn on_frame);
uint16_t UART_FramerPoll(UART_Port *port);
uint16_t UART_FramerPollBytes(UART_Framer *f, const uint8_t *data, uint16_t len);

/* ===== 协议发送工具 ===== */
uint16_t UART_CRC16(const uint8_t *data, uint16_t len);
uint16_t UART_SendFrameFixed(UART_Port *port,
                             uint8_t head, uint8_t tail,
                             const uint8_t *payload, uint16_t len);
uint16_t UART_SendFrameLenCRC(UART_Port *port,
                              uint8_t head, uint8_t tail,
                              uint8_t cmd, const uint8_t *payload, uint16_t len);

/* ===== 错误与恢复 ===== */
const UART_Errors *UART_GetErrors(UART_Port *port);
void               UART_ClearErrors(UART_Port *port);
void               UART_Recover(UART_Port *port);

/* ===== 调试 ===== */
void UART_DumpDebug(UART_Port *port);

/* ===== ISR (全工程唯一定义) ===== */
void UART_ISR_Handler(UART_Port *port);
void UART_0_INST_IRQHandler(void);
#ifdef UART1_ENABLE
void UART_1_INST_IRQHandler(void);
#endif

#endif /* UART_H */
