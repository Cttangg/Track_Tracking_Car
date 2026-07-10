/*
//                            _ooOoo_  
//                           o8888888o  
//                           88" . "88  
//                           (| -_- |)  
//                            O\ = /O  
//                        ____/`---'\____  
//                      .   ' \\| |// `.  
//                       / \\||| : |||// \  
//                     / _||||| -:- |||||- \  
//                       | | \\\ - /// | |  
//                     | \_| ''\---/'' | |  
//                      \ .-\__ `-` ___/-. /  
//                   ___`. .' /--.--\ `. . __  
//                ."" '< `.___\_<|>_/___.' >'"".  
//               | | : `- \`.;`\ _ /`;.`/ - ` : | |  
//                 \ \ `-. \_ __\ /__ _/ .-` / /  
//         ======`-.____`-.___\_____/___.-`____.-'======  
//                            `=---='  
//  
//         .............................................  
//                  佛祖保佑             永无BUG 
//          佛曰:  
//                  写字楼里写字间，写字间里程序员；  
//                  程序人员写程序，又拿程序换酒钱。  
//                  酒醒只在网上坐，酒醉还来网下眠；  
//                  酒醉酒醒日复日，网上网下年复年。  
//                  但愿老死电脑间，不愿鞠躬老板前；  
//                  奔驰宝马贵者趣，公交自行程序员。  
//                  别人笑我忒疯癫，我笑自己命太贱；  
//                  不见满街漂亮妹，哪个归得程序员？
*/

/*
 * Dual Motor PID Speed Control
 * =============================
 * Motor 1: AIN1=PA8, AIN2=PA9, PWM=PA12(TIMG0 CCP0), Enc=PA17(TIMA1)
 * Motor 2: BIN1=PB2, BIN2=PB3, PWM=PA13(TIMG0 CCP1), Enc=PA21(TIMA0)
 */

#include "ti_msp_dl_config.h"
#include "./Drivers/motor_control.h"
#include "./Drivers/trajectory.h"
#include "./Drivers/grayscale.h"
#include "./Drivers/uart.h"
#include "stdio.h"
#include <string.h>

#define CMD_BUF_SIZE      64

/* ==================== UART ====================
 * 收发统一走通用串口库 (g_uart0): TX 环形缓冲+中断, RX 中断入环形缓冲。
 * 命令解析仍在本文件 (应用层), 不写入通用库。 */

/* firewater: 9 channels — T1,R1,D1,F1,T2,R2,D2,F2,S (S=灰度传感器8bit二进制)
 * 用已验证可用的 sprintf + UART_Puts 路径; 电机状态先发, 灰度位后发。 */
static void firewater_send(void) {
    static char b[96];

    /* 1) 电机状态 (8 字段 + 逗号), 优先发出 */
    int n = sprintf(b, "%d,%d,%d,%d,%d,%d,%d,%d,",
        (int)motor_control_get_target_rpm(1),
        (int)motor_control_get_actual_rpm(1),
        (int)motor_control_get_duty(1),
        (int)motor_control_get_freq(1),
        (int)motor_control_get_target_rpm(2),
        (int)motor_control_get_actual_rpm(2),
        (int)motor_control_get_duty(2),
        (int)motor_control_get_freq(2));
    if (n > 0) UART_Puts(&g_uart0, b);

    /* 2) 灰度传感器 8bit 二进制 + 换行 */
    uint8_t sensor = Grayscale_Read();
    char sbits[11];
    for (int i = 7; i >= 0; i--)
        sbits[7 - i] = (sensor & (1 << i)) ? '1' : '0';
    sbits[8]  = '\r';
    sbits[9]  = '\n';
    sbits[10] = '\0';
    UART_Puts(&g_uart0, sbits);
}

/* ==================== 命令解析 ==================== */

static void cmd_show(void) {
    UART_Printf(&g_uart0, "M1: Tr=%d RPM=%d D=%d F=%d\r\n",
        (int)motor_control_get_target_rpm(1),
        (int)motor_control_get_actual_rpm(1),
        (int)motor_control_get_duty(1),
        (int)motor_control_get_freq(1));
    UART_Printf(&g_uart0, "M2: Tr=%d RPM=%d D=%d F=%d\r\n",
        (int)motor_control_get_target_rpm(2),
        (int)motor_control_get_actual_rpm(2),
        (int)motor_control_get_duty(2),
        (int)motor_control_get_freq(2));
}

/* 解析 "Tr1 4000", "Kp2 0.5", "Dd1 2000" 等 */
static void cmd_do(const char *line) {
    char k[8]={0}; float v=0;
    if (line[0]=='?') { cmd_show(); return; }
    if (sscanf(line,"%7s %f",k,&v)<1) return;

    /* 提取命令末尾的数字作为 motorID (1 或 2) */
    int len = (int)strlen(k);
    int id = (len > 0 && k[len-1] >= '1' && k[len-1] <= '2') ? (k[len-1] - '0') : 0;
    if (id == 0) { UART_Printf(&g_uart0, "ERR: need motor ID suffix (1/2)\r\n"); return; }
    k[len-1] = '\0';  /* 去掉末尾数字, 如 "Tr1" -> "Tr" */

    if (!strcmp(k,"Tr")) {
        motor_control_set_speed((uint8_t)id, (int32_t)v);
        UART_Printf(&g_uart0, "OK M%d Tr=%d\r\n", id, (int)v);
    } else if (!strcmp(k,"Kp")) {
        motor_control_set_kp((uint8_t)id, v);
        UART_Printf(&g_uart0, "OK M%d Kp=%.3f\r\n", id, v);
    } else if (!strcmp(k,"Ki")) {
        motor_control_set_ki((uint8_t)id, v);
        UART_Printf(&g_uart0, "OK M%d Ki=%.3f\r\n", id, v);
    } else if (!strcmp(k,"Kd")) {
        motor_control_set_kd((uint8_t)id, v);
        UART_Printf(&g_uart0, "OK M%d Kd=%.3f\r\n", id, v);
    } else if (!strcmp(k,"Dd")) {
        motor_control_set_duty((uint8_t)id, (uint32_t)v);
        UART_Printf(&g_uart0, "OK M%d Dd=%d\r\n", id, (int)v);
    } else if (!strcmp(k,"stop")) {
        motor_control_stop((uint8_t)id);
        UART_Printf(&g_uart0, "OK M%d stopped\r\n", id);
    } else {
        UART_Printf(&g_uart0, "ERR: %s\r\n", k);
    }
}

/* 从通用库 RX 环形缓冲取字节, 本文件组装成行后交给 cmd_do。
 * 每轮限量处理, 防止 RX 持续来数据 (如 PA11 噪声) 时独占主循环, 保证 firewater 能被调用。 */
static void cmd_poll(void) {
    static char b[CMD_BUF_SIZE]; static int i=0;
    uint8_t c;
    int budget = 2 * CMD_BUF_SIZE;
    while (budget-- > 0 && UART_ReadByte(&g_uart0, &c)) {
        if (c=='\n'||c=='\r') { if(i>0){b[i]=0; cmd_do(b); i=0;} }
        else if (i<CMD_BUF_SIZE-1) b[i++]=(char)c;
    }
}

/* ==================== TIMG12 ISR ==================== */

static volatile int g_fw_ready = 0;
static int g_fw_div = 0;   /* 降频计数器: 每5次ISR发一次firewater */

void TIMER_0_INST_IRQHandler(void)
{
    motor_control_update();
    trajectory_update();
    if (++g_fw_div >= 5) {
        g_fw_div = 0;
        g_fw_ready = 1;
    }
    DL_TimerG_clearInterruptStatus(TIMER_0_INST, DL_TIMERG_INTERRUPT_ZERO_EVENT);
}

/* ==================== Main ==================== */

int main(void)
{
    SYSCFG_DL_init();
    UART_Init();          /* 通用串口库: 初始化 g_uart0, 开 RX 中断, 接管 UART0 ISR */
    delay_cycles(CPUCLK_FREQ / 100);   /* ~10ms: 等上电/复位 TX 瞬态稳定, 避免开头乱码 */

    /* 初始化两个电机: dt=10ms, Kp=0.4, Ki=1.5, Kd=0 */
    motor_control_init(1, 0.01f, 0.4f, 1.5f, 0.0f);
    motor_control_init(2, 0.01f, 0.4f, 1.5f, 0.0f);

    /* 启动 TIMER_0 (TIMG12, 10ms PERIODIC) */
    NVIC_ClearPendingIRQ(TIMER_0_INST_INT_IRQN);
    NVIC_EnableIRQ(TIMER_0_INST_INT_IRQN);
    DL_TimerG_startCounter(TIMER_0_INST);

    UART_Puts(&g_uart0, "\r\nDual Motor Control Ready\r\n");   /* 前导换行隔开复位瞬态乱码 */
    cmd_show();

    /*
     * ====== 测试代码 (取消注释一项即可) =====
     */
    trajectory_straight(1.0f, 0.1f);              // 直线前进 1m @ 0.1m/s
    /* trajectory_straight(0.5f, 0.2f); */        // 直线前进 0.5m 稍快
    /* trajectory_circle(0.5f, 0.1f, +1); */      // 左转圈 R=0.5m
    /* trajectory_circle(0.5f, 0.1f, -1); */      // 右转圈 R=0.5m
    /* trajectory_arc(0.3f,3.1416f,0.1f,+1); */   // 半圆左转 R=0.3m

    while (1) {
        cmd_poll();
        if (g_fw_ready) {
            g_fw_ready = 0;
            firewater_send();
        }
    }
}
