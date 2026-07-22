#include "uart.h"
#include <assert.h>

/* ============================================================
 * 通用 UART 库 v2 实现
 * ------------------------------------------------------------
 * 架构:
 *   ISR 层: 纯数据搬运
 *     RX: 硬件FIFO → ring_push()
 *     TX: ring_pop() → 硬件FIFO
 *   主循环:
 *     RX: UART_Read*() 从 ring 消费
 *     TX: UART_Write*() 入 ring → kick + ISR 搬运
 *   Framer: UART_FramerPoll() 在主循环中轮询
 *
 * TX Kick 机制:
 *   MSPM0 TX 中断为边沿触发 (FIFO 满→非满)。
 *   ISR 排空环形后关 TX 中断, FIFO 随后排空。
 *   主循环下次入队 → enableInterrupt 时无新边沿 → ISR 不触发。
 *   tx_byte() 入队后额外直写 FIFO 一个字节,
 *   人为制造填→排空边沿以启动 ISR 传输链。
 *   这是 MSPM0 平台的最小必要例外。
 * ============================================================ */

/* ==================== 环形缓冲 ==================== */
/* 强制 2 的幂容量, 满判定牺牲 1 槽位 */

static void ring_init(UART_Ring *r, uint8_t *buf, uint16_t size)
{
    assert(size > 0 && (size & (size - 1)) == 0);  /* 必须为 2 的幂 */
    r->buf      = buf;
    r->mask     = (uint16_t)(size - 1);
    r->head     = 0;
    r->tail     = 0;
}

static inline uint16_t ring_count(const UART_Ring *r)
{
    return (uint16_t)((r->head - r->tail) & r->mask);
}

static inline uint16_t ring_free(const UART_Ring *r)
{
    return (uint16_t)(r->mask - ring_count(r));
}

static inline int ring_push(UART_Ring *r, uint8_t b)
{
    uint16_t next = (uint16_t)((r->head + 1) & r->mask);
    if (next == r->tail) return 0;
    r->buf[r->head] = b;
    r->head = next;
    return 1;
}

static inline int ring_pop(UART_Ring *r, uint8_t *b)
{
    if (r->head == r->tail) return 0;
    *b = r->buf[r->tail];
    r->tail = (uint16_t)((r->tail + 1) & r->mask);
    return 1;
}

/* ==================== 全局端口与静态缓冲 ==================== */

static uint8_t g_uart0_rx[UART0_RX_SIZE];
static uint8_t g_uart0_tx[UART0_TX_SIZE];
UART_Port g_uart0;

#ifdef UART1_ENABLE
static uint8_t g_uart1_rx[UART1_RX_SIZE];
static uint8_t g_uart1_tx[UART1_TX_SIZE];
UART_Port g_uart1;
#endif

static void port_setup(UART_Port *p, UART_Regs *inst, IRQn_Type irqn,
                       uint8_t *rxbuf, uint16_t rxsize,
                       uint8_t *txbuf, uint16_t txsize)
{
    p->inst       = inst;
    p->irqn       = irqn;
    p->framer     = 0;
    p->tx_int_en  = 0;
    p->err.rx_overflow = 0;
    p->err.hw_overrun  = 0;
    p->err.framing     = 0;
    p->err.parity      = 0;
    ring_init(&p->rx, rxbuf, rxsize);
    ring_init(&p->tx, txbuf, txsize);

    /* 防御: 确保外设级 RX/TX 中断初始关闭, 避免 SysConfig 预设干扰 */
    DL_UART_Main_disableInterrupt(inst,
        DL_UART_MAIN_INTERRUPT_RX | DL_UART_MAIN_INTERRUPT_TX);

    NVIC_ClearPendingIRQ(irqn);
    NVIC_EnableIRQ(irqn);
}

void UART_Init(void)
{
    port_setup(&g_uart0, UART_0_INST, UART_0_INST_INT_IRQN,
               g_uart0_rx, UART0_RX_SIZE, g_uart0_tx, UART0_TX_SIZE);
#ifdef UART1_ENABLE
    port_setup(&g_uart1, UART_1_INST, UART_1_INST_INT_IRQN,
               g_uart1_rx, UART1_RX_SIZE, g_uart1_tx, UART1_TX_SIZE);
#endif
}

/* ==================== RX 中断控制 (参数化端口) ==================== */

void UART_RxEnable(UART_Port *port)
{
    while (!DL_UART_isRXFIFOEmpty(port->inst))
        DL_UART_Main_receiveData(port->inst);
    UART_RxFlush(port);

    DL_UART_Main_enableInterrupt(port->inst, DL_UART_MAIN_INTERRUPT_RX);
    NVIC_ClearPendingIRQ(port->irqn);
    if (!(NVIC->ISER[(uint32_t)port->irqn >> 5] & (1U << ((uint32_t)port->irqn & 0x1F))))
        NVIC_EnableIRQ(port->irqn);
}

void UART_RxDisable(UART_Port *port)
{
    DL_UART_Main_disableInterrupt(port->inst, DL_UART_MAIN_INTERRUPT_RX);
    UART_RxFlush(port);
    while (!DL_UART_isRXFIFOEmpty(port->inst))
        DL_UART_Main_receiveData(port->inst);
}

/* ==================== 发送 (纯 ISR 搬运, 主循环不碰 FIFO) ==================== */

/*
 * TX Kick 机制说明:
 * ───────────────────────────────────────────────
 * MSPM0 UART TX 中断是边沿触发: FIFO 从"满"下降到"阈值以下"才产生中断边沿。
 * 当 ISR 排空环形后关闭 TX 中断, 此时 FIFO 也排空移位完毕。
 * 下次主循环入队 → enableInterrupt(TX) 时, FIFO 已经是空的状态,
 * 条件始终为真, 不产生边沿 → ISR 永远不触发。
 *
 * 尝试过的替代方案:
 *   - NVIC_SetPendingIRQ: 强制 pend ISR, 但 ISR 内读 IIDX 得不到 TX 中断标志,
 *     因为外设 MIS 虽然置位, 但电平已经稳定, 无新边沿传入 NVIC。
 *   - 永不关闭 TX 中断: 环形空时 ISR 空转不操作, 但同样存在边沿丢失问题。
 *
 * 结论: 必须通过"填充 FIFO → 等待移位排空"来人工制造满→空边沿。
 * 因此 tx_byte() 入队后额外直写 FIFO 一个字节 (即 Kick)。
 * Kick 由主循环执行, 是 MSPM0 平台特性导致的最小必要例外。
 *
 * 数据路径:
 *   主循环: ring_push → enableInterrupt(TX) → Kick(直写FIFO)
 *   ISR:    ring_pop → transmitData → ring空 → disableInterrupt(TX)
 * ───────────────────────────────────────────────
 */

static inline void tx_kick_fifo(UART_Port *p)
{
    if (!DL_UART_isTXFIFOFull(p->inst)) {
        uint8_t b;
        if (ring_pop(&p->tx, &b))
            DL_UART_Main_transmitData(p->inst, b);
    }
}

static inline void tx_byte(UART_Port *p, uint8_t b)
{
    while (!ring_push(&p->tx, b)) {
        /* 环形满, 触发 ISR 消费 */
        if (!p->tx_int_en) {
            p->tx_int_en = 1;
            DL_UART_Main_enableInterrupt(p->inst, DL_UART_MAIN_INTERRUPT_TX);
        }
    }
    /* 入队成功, 开启中断 + kick 启动传输链 */
    if (!p->tx_int_en) {
        p->tx_int_en = 1;
        DL_UART_Main_enableInterrupt(p->inst, DL_UART_MAIN_INTERRUPT_TX);
    }
    tx_kick_fifo(p);
}

uint16_t UART_Write(UART_Port *port, const uint8_t *data, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++)
        tx_byte(port, data[i]);
    return len;
}

int UART_WriteByte(UART_Port *port, uint8_t b)
{
    tx_byte(port, b);
    return 1;
}

int UART_Puts(UART_Port *port, const char *s)
{
    uint16_t len = 0;
    while (s[len]) len++;
    return (int)UART_Write(port, (const uint8_t *)s, len);
}

/* ---------- printf 内部 ---------- */

typedef struct {
    UART_Port *port;
    int        count;
} fmt_ctx;

static inline void emit_c(fmt_ctx *c, char ch)
{
    tx_byte(c->port, (uint8_t)ch);
    c->count++;
}

static void emit_uint(fmt_ctx *c, unsigned long v, unsigned base, int upper)
{
    char tmp[11];
    int  i = 0;
    const char *dig = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    if (v == 0) tmp[i++] = '0';
    while (v) { tmp[i++] = dig[v % base]; v /= base; }
    while (i) emit_c(c, tmp[--i]);
}

static void emit_int(fmt_ctx *c, long v, unsigned base, int upper)
{
    if (v < 0) { emit_c(c, '-'); emit_uint(c, (unsigned long)(-v), base, upper); }
    else       { emit_uint(c, (unsigned long)v, base, upper); }
}

static void emit_str(fmt_ctx *c, const char *s)
{
    if (!s) s = "(null)";
    while (*s) emit_c(c, *s++);
}

static void emit_float(fmt_ctx *c, double v, int prec)
{
    if (prec < 0) prec = 6;
    if (v < 0) { emit_c(c, '-'); v = -v; }

    double half = 0.5;
    for (int i = 0; i < prec; i++) half *= 0.1;
    v += half;

    unsigned long ip   = (unsigned long)v;
    double        frac = v - (double)ip;
    emit_uint(c, ip, 10, 0);

    if (prec > 0) {
        emit_c(c, '.');
        for (int i = 0; i < prec; i++) {
            frac *= 10.0;
            int d = (int)frac;
            emit_c(c, (char)('0' + d));
            frac -= d;
        }
    }
}

int UART_Printf(UART_Port *port, const char *fmt, ...)
{
    fmt_ctx c = { port, 0 };
    va_list ap;
    va_start(ap, fmt);

    for (; *fmt; fmt++) {
        if (*fmt != '%') { emit_c(&c, *fmt); continue; }
        fmt++;

        int prec = -1;
        if (*fmt == '.') {
            fmt++; prec = 0;
            while (*fmt >= '0' && *fmt <= '9') { prec = prec * 10 + (*fmt - '0'); fmt++; }
        }

        int lng = 0;
        while (*fmt == 'l') { lng++; fmt++; }

        switch (*fmt) {
        case 'd': case 'i':
            emit_int(&c, lng ? va_arg(ap, long) : va_arg(ap, int), 10, 0); break;
        case 'u':
            emit_uint(&c, lng ? va_arg(ap, unsigned long) : va_arg(ap, unsigned), 10, 0); break;
        case 'x':
            emit_uint(&c, lng ? va_arg(ap, unsigned long) : va_arg(ap, unsigned), 16, 0); break;
        case 'X':
            emit_uint(&c, lng ? va_arg(ap, unsigned long) : va_arg(ap, unsigned), 16, 1); break;
        case 'c': emit_c(&c, (char)va_arg(ap, int));            break;
        case 's': emit_str(&c, va_arg(ap, const char *));       break;
        case 'f': case 'F': emit_float(&c, va_arg(ap, double), prec); break;
        case '%': emit_c(&c, '%');                              break;
        case '\0': goto done;
        default:  emit_c(&c, '%'); emit_c(&c, *fmt);            break;
        }
    }
done:
    va_end(ap);
    return c.count;
}

int UART_PrintfFast(UART_Port *port, const char *fmt, ...)
{
    fmt_ctx c = { port, 0 };
    va_list ap;
    va_start(ap, fmt);

    for (; *fmt; fmt++) {
        if (*fmt != '%') { emit_c(&c, *fmt); continue; }
        switch (*++fmt) {
        case 'd': emit_int(&c, va_arg(ap, int), 10, 0);       break;
        case 'u': emit_uint(&c, va_arg(ap, unsigned), 10, 0); break;
        case 'x': emit_uint(&c, va_arg(ap, unsigned), 16, 0); break;
        case 'c': emit_c(&c, (char)va_arg(ap, int));          break;
        case 's': emit_str(&c, va_arg(ap, const char *));     break;
        case '%': emit_c(&c, '%');                            break;
        case '\0': goto done;
        default:  emit_c(&c, *fmt);                           break;
        }
    }
done:
    va_end(ap);
    return c.count;
}

void UART_WriteBlocking(UART_Port *port, const uint8_t *data, uint16_t len)
{
    UART_Write(port, data, len);
    UART_TxFlush(port);
}

void UART_TxFlush(UART_Port *port)
{
    while (ring_count(&port->tx) > 0 || DL_UART_isBusy(port->inst)) { }
}

uint16_t UART_TxFree(UART_Port *port)
{
    return ring_free(&port->tx);
}

/* ==================== 接收 ==================== */

uint16_t UART_Available(UART_Port *port)
{
    return ring_count(&port->rx);
}

int UART_ReadByte(UART_Port *port, uint8_t *b)
{
    return ring_pop(&port->rx, b);
}

uint16_t UART_Read(UART_Port *port, uint8_t *buf, uint16_t len)
{
    uint16_t n = 0;
    while (n < len && ring_pop(&port->rx, &buf[n]))
        n++;
    return n;
}

int UART_Peek(UART_Port *port, uint8_t *b)
{
    UART_Ring *r = &port->rx;
    if (r->head == r->tail) return 0;
    *b = r->buf[r->tail];
    return 1;
}

int UART_ReadLine(UART_Port *port, char *buf, uint16_t size)
{
    UART_Ring *r = &port->rx;

    uint16_t i = r->tail, n = 0;
    int found = 0;
    while (i != r->head) {
        n++;
        if (r->buf[i] == '\n') { found = 1; break; }
        i = (uint16_t)((i + 1) & r->mask);
    }
    if (!found) return 0;

    uint16_t out = 0;
    for (uint16_t k = 0; k < n; k++) {
        uint8_t c;
        ring_pop(r, &c);
        if (out < (uint16_t)(size - 1))
            buf[out++] = (char)c;
    }
    buf[out] = '\0';
    return (int)out;
}

void UART_RxFlush(UART_Port *port)
{
    port->rx.tail = port->rx.head;
}

/* ==================== 协议 framer (纯主循环轮询) ==================== */

static uint16_t framer_fixed_feed(UART_Framer *f, uint8_t b)
{
    if (f->idx == 0) {
        if (b == f->head) f->buf[f->idx++] = b;
    } else if (f->idx < (uint16_t)(f->frame_len - 1)) {
        f->buf[f->idx++] = b;
    } else {
        if (b == f->tail) {
            f->buf[f->idx] = b;
            f->idx = 0;
            return f->frame_len;
        }
        f->idx = 0;
    }
    return 0;
}

static uint16_t framer_delim_feed(UART_Framer *f, uint8_t b)
{
    if (f->idx < f->size)
        f->buf[f->idx++] = b;
    else
        f->idx = 0;

    if (b == f->delim) {
        uint16_t len = f->idx;
        f->idx = 0;
        return len;
    }
    return 0;
}

static uint16_t framer_len_feed(UART_Framer *f, uint8_t b)
{
    if (f->idx == 0) {
        if (b == f->head) f->buf[f->idx++] = b;
        return 0;
    }

    if (f->idx < f->size)
        f->buf[f->idx++] = b;
    else
        { f->idx = 0; return 0; }

    uint16_t min_len = (uint16_t)f->len_offset + f->len_size + f->crc_size + 1 /*tail*/;
    if (f->idx < min_len) return 0;

    if (f->idx == min_len) {
        /* 解析长度字段 */
        f->payload_len = 0;
        for (uint8_t k = 0; k < f->len_size; k++)
            f->payload_len |= (uint16_t)f->buf[f->len_offset + k] << (8 * k);
        if (f->payload_len == 0) { f->idx = 0; return 0; }
    }

    uint16_t total = min_len + f->payload_len;
    if (f->idx >= total) {
        if (f->buf[total - 1] == f->tail) {
            f->idx = 0;
            return total;
        }
        f->idx = 0;
    }
    return 0;
}

void UART_FramerInitFixed(UART_Framer *f, uint8_t *buf, uint16_t size,
                          uint8_t head, uint8_t tail, uint16_t frame_len)
{
    f->type       = UART_FRAMER_FIXED;
    f->Feed       = framer_fixed_feed;
    f->OnFrame    = 0;
    f->buf        = buf;
    f->size       = size;
    f->idx        = 0;
    f->head       = head;
    f->tail       = tail;
    f->frame_len  = frame_len;
    f->delim      = 0;
}

void UART_FramerInitDelim(UART_Framer *f, uint8_t *buf, uint16_t size, uint8_t delim)
{
    f->type       = UART_FRAMER_DELIM;
    f->Feed       = framer_delim_feed;
    f->OnFrame    = 0;
    f->buf        = buf;
    f->size       = size;
    f->idx        = 0;
    f->head       = 0;
    f->tail       = 0;
    f->frame_len  = 0;
    f->delim      = delim;
}

void UART_FramerInitLen(UART_Framer *f, uint8_t *buf, uint16_t size,
                        uint8_t head, uint8_t tail,
                        uint8_t len_offset, uint8_t len_size,
                        uint8_t crc_offset, uint8_t crc_size)
{
    f->type        = UART_FRAMER_LEN;
    f->Feed        = framer_len_feed;
    f->OnFrame     = 0;
    f->buf         = buf;
    f->size        = size;
    f->idx         = 0;
    f->head        = head;
    f->tail        = tail;
    f->frame_len   = 0;
    f->delim       = 0;
    f->payload_len = 0;
    f->len_offset  = len_offset;
    f->len_size    = len_size;
    f->crc_offset  = crc_offset;
    f->crc_size    = crc_size;
}

void UART_FramerInitCustom(UART_Framer *f, UART_FramerFeedFn feed,
                           UART_FramerFrameFn on_frame)
{
    f->type     = UART_FRAMER_CUSTOM;
    f->Feed     = feed;
    f->OnFrame  = on_frame;
    f->buf      = 0;
    f->size     = 0;
    f->idx      = 0;
}

void UART_FramerSetCallback(UART_Framer *f, UART_FramerFrameFn on_frame)
{
    f->OnFrame = on_frame;
}

uint16_t UART_FramerPollBytes(UART_Framer *f, const uint8_t *data, uint16_t len)
{
    uint16_t frames = 0;
    for (uint16_t i = 0; i < len; i++) {
        uint16_t flen = f->Feed(f, data[i]);
        if (flen && f->OnFrame) {
            f->OnFrame(f->buf, flen);
            frames++;
        }
    }
    return frames;
}

uint16_t UART_FramerPoll(UART_Port *port)
{
    UART_Framer *f = port->framer;
    if (!f) return 0;

    uint16_t frames = 0;
    uint16_t avail = ring_count(&port->rx);
    for (uint16_t i = 0; i < avail; i++) {
        uint8_t b;
        if (ring_pop(&port->rx, &b)) {
            uint16_t flen = f->Feed(f, b);
            if (flen && f->OnFrame) {
                f->OnFrame(f->buf, flen);
                frames++;
            }
        }
    }
    return frames;
}

/* ==================== CRC16 ==================== */

uint16_t UART_CRC16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x8000)
                crc = (uint16_t)((crc << 1) ^ 0x1021);
            else
                crc = (uint16_t)(crc << 1);
        }
    }
    return crc;
}

/* ==================== 帧发送工具 ==================== */

uint16_t UART_SendFrameFixed(UART_Port *port,
                             uint8_t head, uint8_t tail,
                             const uint8_t *payload, uint16_t len)
{
    UART_WriteByte(port, head);
    if (payload && len)
        UART_Write(port, payload, len);
    UART_WriteByte(port, tail);
    return len + 2;
}

uint16_t UART_SendFrameLenCRC(UART_Port *port,
                              uint8_t head, uint8_t tail,
                              uint8_t cmd, const uint8_t *payload, uint16_t len)
{
    /* 帧格式: head | len(2B LE) | cmd | payload | crc16(2B LE) | tail */
    uint16_t data_len = (uint16_t)(len + 1);  /* cmd + payload */
    uint16_t crc;

    UART_WriteByte(port, head);

    UART_WriteByte(port, (uint8_t)(data_len & 0xFF));
    UART_WriteByte(port, (uint8_t)((data_len >> 8) & 0xFF));

    UART_WriteByte(port, cmd);

    if (payload && len)
        UART_Write(port, payload, len);

    /* 计算 CRC: 对 len|cmd|payload 字段 */
    uint8_t crc_buf[258];  /* 2(len) + 1(cmd) + 255(max payload) */
    crc_buf[0] = (uint8_t)(data_len & 0xFF);
    crc_buf[1] = (uint8_t)((data_len >> 8) & 0xFF);
    crc_buf[2] = cmd;
    if (payload && len) {
        for (uint16_t i = 0; i < len && i < 255; i++)
            crc_buf[3 + i] = payload[i];
    }
    crc = UART_CRC16(crc_buf, (uint16_t)(3 + len));

    UART_WriteByte(port, (uint8_t)(crc & 0xFF));
    UART_WriteByte(port, (uint8_t)((crc >> 8) & 0xFF));

    UART_WriteByte(port, tail);

    return (uint16_t)(6 + len);  /* head(1)+len(2)+cmd(1)+payload+CRC(2)+tail(1) */
}

/* ==================== 错误与恢复 ==================== */

const UART_Errors *UART_GetErrors(UART_Port *port)
{
    return &port->err;
}

void UART_ClearErrors(UART_Port *port)
{
    port->err.rx_overflow = 0;
    port->err.hw_overrun  = 0;
    port->err.framing     = 0;
    port->err.parity      = 0;
}

void UART_Recover(UART_Port *port)
{
    UART_Regs *inst = port->inst;

    /* 停 RX/TX 外设中断 */
    DL_UART_Main_disableInterrupt(inst, DL_UART_MAIN_INTERRUPT_RX);
    DL_UART_Main_disableInterrupt(inst, DL_UART_MAIN_INTERRUPT_TX);
    port->tx_int_en = 0;

    /* 清硬件 FIFO */
    while (!DL_UART_isRXFIFOEmpty(inst))
        DL_UART_Main_receiveData(inst);
    while (!DL_UART_isTXFIFOEmpty(inst))
        (void)DL_UART_isTXFIFOEmpty(inst);

    /* 清软件环形缓冲 */
    UART_RxFlush(port);
    port->tx.tail = port->tx.head;

    /* 清错误计数 (不清零, 保留已累计数用于诊断) */
    /* 刷一次 IIDX 清残留中断标志 */
    while (DL_UART_Main_getPendingInterrupt(inst) != DL_UART_IIDX_NO_INTERRUPT) { }

    /* 重新开 RX 中断 */
    DL_UART_Main_enableInterrupt(inst, DL_UART_MAIN_INTERRUPT_RX);
    NVIC_ClearPendingIRQ(port->irqn);
}

/* ==================== ISR (纯数据搬运, 零协议逻辑) ==================== */

static inline void rx_byte_isr(UART_Port *p, uint8_t b)
{
    if (!ring_push(&p->rx, b))
        p->err.rx_overflow++;
}

void UART_ISR_Handler(UART_Port *port)
{
    UART_Regs *inst = port->inst;
    DL_UART_IIDX idx;
    int loop_limit = 128;

    while (loop_limit-- > 0 &&
           (idx = DL_UART_Main_getPendingInterrupt(inst)) != DL_UART_IIDX_NO_INTERRUPT) {
        switch (idx) {
        case DL_UART_MAIN_IIDX_RX:
            while (!DL_UART_isRXFIFOEmpty(inst))
                rx_byte_isr(port, DL_UART_Main_receiveData(inst));
            break;

        case DL_UART_MAIN_IIDX_TX:
            while (!DL_UART_isTXFIFOFull(inst)) {
                uint8_t b;
                if (ring_pop(&port->tx, &b)) {
                    DL_UART_Main_transmitData(inst, b);
                } else {
                    port->tx_int_en = 0;
                    DL_UART_Main_disableInterrupt(inst, DL_UART_MAIN_INTERRUPT_TX);
                    break;
                }
            }
            break;

        case DL_UART_IIDX_OVERRUN_ERROR:
            port->err.hw_overrun++;
            if (!DL_UART_isRXFIFOEmpty(inst))
                (void)DL_UART_Main_receiveData(inst);
            break;

        case DL_UART_IIDX_FRAMING_ERROR:
            port->err.framing++;
            if (!DL_UART_isRXFIFOEmpty(inst))
                (void)DL_UART_Main_receiveData(inst);
            break;

        case DL_UART_IIDX_PARITY_ERROR:
            port->err.parity++;
            if (!DL_UART_isRXFIFOEmpty(inst))
                (void)DL_UART_Main_receiveData(inst);
            break;

        default:
            (void)DL_UART_Main_getPendingInterrupt(inst);
            if (!DL_UART_isRXFIFOEmpty(inst))
                (void)DL_UART_Main_receiveData(inst);
            break;
        }
    }
}

void UART_0_INST_IRQHandler(void)
{
    UART_ISR_Handler(&g_uart0);
}

#ifdef UART1_ENABLE
void UART_1_INST_IRQHandler(void)
{
    UART_ISR_Handler(&g_uart1);
}
#endif
