#include "uart.h"

/* ============================================================
 * 通用 UART 库实现
 * ------------------------------------------------------------
 * 传输层 (本文件): 收发字节、双环形缓冲、ISR 分发、错误统计。
 * 协议层: 由 framer 插件实现 (定长/分隔符/自定义), 与传输层解耦。
 *
 * 数据流:
 *   RX: 硬件FIFO -> ISR -> [挂了 framer ? 喂 framer : 入 rx 环形缓冲]
 *                                                    -> 主循环 UART_Read*
 *   TX: 主循环 UART_Write -> tx 环形缓冲 -> (使能TX中断) -> ISR -> 硬件FIFO
 *
 * 并发模型 (单核, ISR 不可被主循环打断):
 *   rx 环形: 生产者=ISR, 消费者=主循环   (ISR 写 head, 主读 tail)
 *   tx 环形: 生产者=主循环, 消费者=ISR   (主写 head, ISR 读 tail)
 *   head/tail 声明为 volatile; 各自单写者, 故无需临界区。
 * ============================================================ */

/* ==================== 环形缓冲 (SPSC, 2 的幂容量) ==================== */
/* 容量强制 2 的幂: mask = size-1, 索引自增用位与取模, ISR 内无除法。
 * 满判定牺牲 1 个槽位 (next==tail 即满), 故可用容量 = size-1。 */

static void ring_init(UART_Ring *r, uint8_t *buf, uint16_t size)
{
    r->buf  = buf;
    r->mask = (uint16_t)(size - 1);   /* size 必须为 2 的幂 */
    r->head = 0;
    r->tail = 0;
}

static inline uint16_t ring_count(const UART_Ring *r)
{
    return (uint16_t)((r->head - r->tail) & r->mask);
}

/* 生产者调用: 成功 1, 满 0 */
static inline int ring_push(UART_Ring *r, uint8_t b)
{
    uint16_t next = (uint16_t)((r->head + 1) & r->mask);
    if (next == r->tail) return 0;
    r->buf[r->head] = b;
    r->head = next;
    return 1;
}

/* 消费者调用: 成功 1, 空 0 */
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
    p->inst   = inst;
    p->irqn   = irqn;
    p->framer = 0;
    p->err.rx_overflow = 0;
    p->err.hw_overrun  = 0;
    p->err.framing     = 0;
    p->err.parity      = 0;
    ring_init(&p->rx, rxbuf, rxsize);
    ring_init(&p->tx, txbuf, txsize);

    /* TX 为阻塞直发, 无需中断; RX 中断延后由 UART_RxEnable() 手动开,
     * 避免 PA11 浮空噪声在上电瞬间灌爆环形缓冲。 */
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

/* 使能 RX 中断 (在硬件稳定后调用, 避免上电噪声灌入) */
void UART_RxEnable(void)
{
    /* 先清空硬件 RX FIFO 和软件环形缓冲中的上电残留 */
    while (!DL_UART_isRXFIFOEmpty(UART_0_INST))
        DL_UART_Main_receiveData(UART_0_INST);
    UART_RxFlush(&g_uart0);

    DL_UART_Main_enableInterrupt(UART_0_INST, DL_UART_MAIN_INTERRUPT_RX);
    NVIC_ClearPendingIRQ(UART_0_INST_INT_IRQN);
    NVIC_EnableIRQ(UART_0_INST_INT_IRQN);
}

/* 关闭 RX 中断 (回退到纯 TX 模式) */
void UART_RxDisable(void)
{
    NVIC_DisableIRQ(UART_0_INST_INT_IRQN);
    DL_UART_Main_disableInterrupt(UART_0_INST, DL_UART_MAIN_INTERRUPT_RX);
    UART_RxFlush(&g_uart0);
    while (!DL_UART_isRXFIFOEmpty(UART_0_INST))
        DL_UART_Main_receiveData(UART_0_INST);
}

/* ==================== 发送 (阻塞直发) ====================
 * TX 采用阻塞直写 FIFO (与原工程可用写法一致), 不用 TX 中断/环形缓冲,
 * 以规避中断驱动 TX 的时序问题。RX 仍为中断 + 环形缓冲。 */

/* 阻塞发送单字节: 等 TX FIFO 有空位再写入 */
static inline void tx_byte(UART_Port *p, uint8_t b)
{
    while (DL_UART_isTXFIFOFull(p->inst)) { }
    DL_UART_Main_transmitData(p->inst, b);
}

/* 发送 len 字节, 返回发送字节数 */
uint16_t UART_Write(UART_Port *port, const uint8_t *data, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++)
        tx_byte(port, data[i]);
    return len;
}

/* 发送单字节, 返回 1 */
int UART_WriteByte(UART_Port *port, uint8_t b)
{
    tx_byte(port, b);
    return 1;
}

/* 发送以 '\0' 结尾的字符串, 返回发送字节数 */
int UART_Puts(UART_Port *port, const char *s)
{
    uint16_t len = 0;
    while (s[len]) len++;
    return (int)UART_Write(port, (const uint8_t *)s, len);
}

/* ---------- printf 内部 ---------- */

typedef struct {
    UART_Port *port;
    int        count;      /* 已发送字节数 */
} fmt_ctx;

static inline void emit_c(fmt_ctx *c, char ch)
{
    tx_byte(c->port, (uint8_t)ch);
    c->count++;
}

/* 无符号整数按 base 输出 (upper: 十六进制大写) */
static void emit_uint(fmt_ctx *c, unsigned long v, unsigned base, int upper)
{
    char tmp[11];               /* 32 位十进制最多 10 位 */
    int  i = 0;
    const char *dig = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    if (v == 0) tmp[i++] = '0';
    while (v) { tmp[i++] = dig[v % base]; v /= base; }
    while (i) emit_c(c, tmp[--i]);
}

/* 有符号整数 */
static void emit_int(fmt_ctx *c, long v, unsigned base, int upper)
{
    if (v < 0) { emit_c(c, '-'); emit_uint(c, (unsigned long)(-v), base, upper); }
    else       { emit_uint(c, (unsigned long)v, base, upper); }
}

/* 字符串 */
static void emit_str(fmt_ctx *c, const char *s)
{
    if (!s) s = "(null)";
    while (*s) emit_c(c, *s++);
}

/* 浮点: 手写转换, 不依赖 TI 库的浮点 printf; prec<0 用默认 6 位小数 */
static void emit_float(fmt_ctx *c, double v, int prec)
{
    if (prec < 0) prec = 6;

    if (v < 0) { emit_c(c, '-'); v = -v; }

    /* 加半个末位单位以实现四舍五入 */
    double half = 0.5;
    for (int i = 0; i < prec; i++) half *= 0.1;
    v += half;

    unsigned long ip   = (unsigned long)v;   /* 整数部分 (溢出风险: 超 ~4.29e9) */
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

/* 全功能: 支持 %c %s %d %i %u %x %X %f %% , %f 支持 "%.Nf" 精度, 支持 'l' 长度修饰 */
int UART_Printf(UART_Port *port, const char *fmt, ...)
{
    fmt_ctx c = { port, 0 };
    va_list ap;
    va_start(ap, fmt);

    for (; *fmt; fmt++) {
        if (*fmt != '%') { emit_c(&c, *fmt); continue; }
        fmt++;                                  /* 跳过 '%' */

        int prec = -1;                          /* 精度 (仅 %f 用) */
        if (*fmt == '.') {
            fmt++; prec = 0;
            while (*fmt >= '0' && *fmt <= '9') { prec = prec * 10 + (*fmt - '0'); fmt++; }
        }

        int lng = 0;                            /* 长度修饰 'l' */
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
        case '\0': goto done;                   /* 结尾孤立的 '%' */
        default:  emit_c(&c, '%'); emit_c(&c, *fmt);            break;
        }
    }
done:
    va_end(ap);
    return c.count;
}

/* 高性能精简版: 只支持 %c %s %d %u %x %% , 无浮点/宽度/精度/长度修饰。
 * 无 double 运算, 直接写环形缓冲, 适合高频/热路径打印。 */
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

/* 阻塞发送 (与 UART_Write 相同, 保留以兼容旧调用) */
void UART_WriteBlocking(UART_Port *port, const uint8_t *data, uint16_t len)
{
    UART_Write(port, data, len);
}

/* 阻塞等待发送完成 (移位寄存器发空) */
void UART_TxFlush(UART_Port *port)
{
    while (DL_UART_isBusy(port->inst)) { /* 等待发完 */ }
}

/* TX FIFO 是否可写 (1=有空位, 0=满); 阻塞发送下意义有限 */
uint16_t UART_TxFree(UART_Port *port)
{
    return DL_UART_isTXFIFOFull(port->inst) ? 0 : 1;
}

/* ==================== 接收 ==================== */

/* RX 缓冲中当前可读字节数 */
uint16_t UART_Available(UART_Port *port)
{
    return ring_count(&port->rx);
}

/* 取一个字节, 返回 1=有数据 0=空 (非阻塞) */
int UART_ReadByte(UART_Port *port, uint8_t *b)
{
    return ring_pop(&port->rx, b);
}

/* 取至多 len 字节到 buf, 返回实际读取数 */
uint16_t UART_Read(UART_Port *port, uint8_t *buf, uint16_t len)
{
    uint16_t n = 0;
    while (n < len && ring_pop(&port->rx, &buf[n]))
        n++;
    return n;
}

/* 查看下一个字节但不移除, 返回 1=有数据 0=空 */
int UART_Peek(UART_Port *port, uint8_t *b)
{
    UART_Ring *r = &port->rx;
    if (r->head == r->tail) return 0;
    *b = r->buf[r->tail];
    return 1;
}

/* 读取一整行 (到 '\n', 含换行, 追加 '\0'):
 *   有完整行  -> 拷贝到 buf (上限 size-1) 并消费, 返回字符串长度
 *   无完整行  -> 不消费任何字节, 返回 0
 * 两趟处理: 先扫描定位 '\n', 再弹出, 避免半行占用 buf。 */
int UART_ReadLine(UART_Port *port, char *buf, uint16_t size)
{
    UART_Ring *r = &port->rx;

    /* 第 1 趟: 扫描是否已有完整一行 (含 '\n') */
    uint16_t i = r->tail, n = 0;
    int found = 0;
    while (i != r->head) {
        n++;
        if (r->buf[i] == '\n') { found = 1; break; }
        i = (uint16_t)((i + 1) & r->mask);
    }
    if (!found) return 0;

    /* 第 2 趟: 取出 n 字节 (含换行), 拷贝到 buf (上限 size-1) */
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

/* 丢弃 RX 缓冲中所有未读数据 */
void UART_RxFlush(UART_Port *port)
{
    port->rx.tail = port->rx.head;
}

/* ==================== 协议 framer ==================== */

/* 定长帧: [head][...][tail], 总长 frame_len */
static uint16_t framer_fixed_feed(UART_Framer *f, uint8_t b)
{
    if (f->idx == 0) {
        if (b == f->head) f->buf[f->idx++] = b;
    } else if (f->idx < (uint16_t)(f->frame_len - 1)) {
        f->buf[f->idx++] = b;
    } else {  /* idx == frame_len-1, 期望帧尾 */
        if (b == f->tail) {
            f->buf[f->idx] = b;
            f->idx = 0;
            return f->frame_len;   /* 完成 */
        }
        f->idx = 0;                /* 帧尾不符, 重新同步 */
    }
    return 0;
}

/* 分隔帧: 累积到 delim (含) 为一帧 */
static uint16_t framer_delim_feed(UART_Framer *f, uint8_t b)
{
    if (f->idx < f->size)
        f->buf[f->idx++] = b;
    else
        f->idx = 0;                /* 溢出, 丢弃重来 */

    if (b == f->delim) {
        uint16_t len = f->idx;
        f->idx = 0;
        return len;
    }
    return 0;
}

void UART_FramerInitFixed(UART_Framer *f, uint8_t *buf, uint16_t size,
                          uint8_t head, uint8_t tail, uint16_t frame_len)
{
    f->Feed      = framer_fixed_feed;
    f->OnFrame   = 0;                  /* 由调用者赋值 */
    f->buf       = buf;
    f->size      = size;
    f->idx       = 0;
    f->head      = head;
    f->tail      = tail;
    f->frame_len = frame_len;
    f->delim     = 0;
}

void UART_FramerInitDelim(UART_Framer *f, uint8_t *buf, uint16_t size, uint8_t delim)
{
    f->Feed      = framer_delim_feed;
    f->OnFrame   = 0;                  /* 由调用者赋值 */
    f->buf       = buf;
    f->size      = size;
    f->idx       = 0;
    f->head      = 0;
    f->tail      = 0;
    f->frame_len = 0;
    f->delim     = delim;
}

/* 完全自定义: feed 逐字节返回帧长, on_frame 完成回调; 状态由 feed 自行维护 */
void UART_FramerInitCustom(UART_Framer *f, UART_FramerFeedFn feed,
                           UART_FramerFrameFn on_frame)
{
    f->Feed      = feed;
    f->OnFrame   = on_frame;
    f->buf       = 0;
    f->size      = 0;
    f->idx       = 0;
    f->head      = 0;
    f->tail      = 0;
    f->frame_len = 0;
    f->delim     = 0;
}

/* 挂载 framer: 挂载后该端口 RX 旁路环形缓冲, 字节直接喂 framer */
void UART_AttachFramer(UART_Port *port, UART_Framer *f)
{
    f->idx = 0;
    port->framer = f;
}

/* 卸载 framer: 回落到原始字节流 (走环形缓冲, 用 UART_Read* 读取) */
void UART_DetachFramer(UART_Port *port)
{
    port->framer = 0;
}

/* ==================== 错误 ==================== */

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

/* ==================== ISR ==================== */

/* 处理单个收到的字节: 挂了 framer 就喂 framer (完成则回调), 否则入环形缓冲 */
static inline void rx_byte(UART_Port *p, uint8_t b)
{
    UART_Framer *f = p->framer;
    if (f) {
        uint16_t len = f->Feed(f, b);
        if (len && f->OnFrame)
            f->OnFrame(f->buf, len);
    } else if (!ring_push(&p->rx, b)) {
        p->err.rx_overflow++;      /* 环形缓冲满, 丢弃新字节 */
    }
}

/* 通用 ISR 主体: 循环读 IIDX 直到无中断, 分别处理 RX/TX/错误。
 * 各具体 UARTx_IRQHandler 只需转调本函数 (全工程 ISR 唯一定义)。 */
void UART_ISR_Handler(UART_Port *port)
{
    UART_Regs *inst = port->inst;
    DL_UART_IIDX idx;
    int loop_limit = 128;  /* 安全阀: 防止死循环 */

    while (loop_limit-- > 0 &&
           (idx = DL_UART_Main_getPendingInterrupt(inst)) != DL_UART_IIDX_NO_INTERRUPT) {
        switch (idx) {
        case DL_UART_MAIN_IIDX_RX:
            while (!DL_UART_isRXFIFOEmpty(inst))
                rx_byte(port, DL_UART_Main_receiveData(inst));
            break;

        case DL_UART_IIDX_OVERRUN_ERROR:
            port->err.hw_overrun++;
            if (!DL_UART_isRXFIFOEmpty(inst))
                (void)DL_UART_Main_receiveData(inst);   /* 读一次清错误 */
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
            /* 未知中断: 清掉所有可能的中断标志以防死循环 */
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
