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
#include "./Drivers/steering.h"
#include "./Drivers/line_pid.h"
#include "./Drivers/gyro_pid.h"
#include "./Drivers/uart.h"
#include "./Drivers/mpu6500.h"
#include "stdio.h"
#include <string.h>

#define CMD_BUF_SIZE      64

/* ==================== UART ====================
 * 收发统一走通用串口库 (g_uart0): TX 环形缓冲+中断, RX 中断入环形缓冲。
 * 命令解析仍在本文件 (应用层), 不写入通用库。 */

/* firewater: 9 channels — T1,R1,D1,F1,T2,R2,D2,F2,S (S=灰度传感器8bit二进制)
 * 用已验证可用的 sprintf + UART_Puts 路径; 电机状态先发, 灰度位后发。 */
static __attribute__((noinline)) void firewater_send(void) {
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

static __attribute__((noinline)) void cmd_show(void) {
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
    /* 帮助 */
    UART_Puts(&g_uart0,
        "--- HELP ---\r\n"
        "Motor: Tr1|Tr2 <rpm>  Kp1|Kp2 <v>  Ki1|Ki2 <v>  Kd1|Kd2 <v>\r\n"
        "       Dd1|Dd2 <dut>  stop1|stop2  Tr <rpm>  Dd <dut>  stop\r\n"
        "Traj:  st <dist> <v>  arc <R> <th> <v> <dir>  cir <R> <v> <dir>\r\n"
        "       stop_all\r\n"
        "Sensor: gs\r\n"
        "------------\r\n");
}

/* 命令解析: 先取第一个 token 作为命令名, 按命令匹配参数 */
static __attribute__((noinline)) void cmd_do(const char *line) {
    char k[16] = {0};
    float v1 = 0, v2 = 0, v3 = 0;
    int dir = 1;

    if (line[0] == '\0') return;
    sscanf(line, "%15s", k);

    /* ---- 无参数命令 ---- */
    if (!strcmp(k, "?"))      { cmd_show(); return; }
    if (!strcmp(k, "stop_all")) { trajectory_stop(); UART_Puts(&g_uart0, "OK all stopped\r\n"); return; }
    if (!strcmp(k, "gs")) {
        uint8_t s = Grayscale_Read(); char b[9];
        for (int i = 7; i >= 0; i--) b[7 - i] = (s & (1 << i)) ? '1' : '0';
        b[8] = '\0';
        UART_Printf(&g_uart0, "GS=%s\r\n", b);
        return;
    }

    /* ---- 闭环转向 PID 参数 ---- */
    if (sscanf(line, "%*s %f", &v1) >= 1) {
        if (!strcmp(k, "Lp")) { LinePID_SetKp(v1); UART_Printf(&g_uart0, "OK Lp=%.3f\r\n", v1); return; }
        if (!strcmp(k, "Li")) { LinePID_SetKi(v1); UART_Printf(&g_uart0, "OK Li=%.3f\r\n", v1); return; }
        if (!strcmp(k, "Ld")) { LinePID_SetKd(v1); UART_Printf(&g_uart0, "OK Ld=%.3f\r\n", v1); return; }
        if (!strcmp(k, "Gp")) { GyroPID_SetKp(v1); UART_Printf(&g_uart0, "OK Gp=%.3f\r\n", v1); return; }
        if (!strcmp(k, "Gi")) { GyroPID_SetKi(v1); UART_Printf(&g_uart0, "OK Gi=%.3f\r\n", v1); return; }
        if (!strcmp(k, "Gd")) { GyroPID_SetKd(v1); UART_Printf(&g_uart0, "OK Gd=%.3f\r\n", v1); return; }
    }
    if (!strcmp(k, "mode")) {
        int m;
        if (sscanf(line, "mode %d", &m) == 1) {
            Steering_SetMode((uint8_t)m);
            UART_Printf(&g_uart0, "OK mode %s\r\n", m==0?"A(line)":"B(gyro)");
        } else {
            UART_Puts(&g_uart0, "ERR: mode 0(A/line) or 1(B/gyro)\r\n");
        }
        return;
    }

    /* ---- 轨迹: st <dist> <speed> ---- */
    if (!strcmp(k, "st")) {
        if (sscanf(line, "st %f %f", &v1, &v2) == 2) {
            trajectory_straight(v1, v2);
            UART_Printf(&g_uart0, "OK st d=%.2f v=%.2f\r\n", v1, v2);
        } else UART_Puts(&g_uart0, "ERR: st <dist_m> <speed_mps>\r\n");
        return;
    }
    /* ---- 轨迹: arc <R> <theta> <speed> <dir> ---- */
    if (!strcmp(k, "arc")) {
        if (sscanf(line, "arc %f %f %f %d", &v1, &v2, &v3, &dir) == 4) {
            trajectory_arc(v1, v2, v3, dir);
            UART_Printf(&g_uart0, "OK arc R=%.2f th=%.2f v=%.2f d=%d\r\n", v1, v2, v3, dir);
        } else UART_Puts(&g_uart0, "ERR: arc <R_m> <th_rad> <spd> <dir>\r\n");
        return;
    }
    /* ---- 轨迹: cir <R> <speed> <dir> ---- */
    if (!strcmp(k, "cir")) {
        if (sscanf(line, "cir %f %f %d", &v1, &v2, &dir) == 3) {
            trajectory_circle(v1, v2, dir);
            UART_Printf(&g_uart0, "OK cir R=%.2f v=%.2f d=%d\r\n", v1, v2, dir);
        } else UART_Puts(&g_uart0, "ERR: cir <R_m> <spd> <dir>\r\n");
        return;
    }

    /* ---- 双电机（无后缀）---- */
    if (!strcmp(k, "stop")) {
        motor_control_stop(1); motor_control_stop(2);
        UART_Puts(&g_uart0, "OK both stopped\r\n"); return;
    }
    if (!strcmp(k, "Tr")) {
        if (sscanf(line, "Tr %f", &v1) == 1) {
            motor_control_set_speed(1, (int32_t)v1);
            motor_control_set_speed(2, (int32_t)v1);
            UART_Printf(&g_uart0, "OK both Tr=%d\r\n", (int)v1);
        } else UART_Puts(&g_uart0, "ERR: Tr <rpm>\r\n");
        return;
    }
    if (!strcmp(k, "Dd")) {
        if (sscanf(line, "Dd %f", &v1) == 1) {
            motor_control_set_duty(1, (uint32_t)v1);
            motor_control_set_duty(2, (uint32_t)v1);
            UART_Printf(&g_uart0, "OK both Dd=%d\r\n", (int)v1);
        } else UART_Puts(&g_uart0, "ERR: Dd <duty>\r\n");
        return;
    }

    /* ---- 旧命令: 末尾数字提取 motorID (1/2) ---- */
    {
        int len = (int)strlen(k);
        int id = 0;
        if (len > 0 && (k[len - 1] == '1' || k[len - 1] == '2')) {
            id = k[len - 1] - '0';
            k[len - 1] = '\0';
        }
        if (id == 0) {
            UART_Printf(&g_uart0, "ERR: unknown cmd '%s'\r\n", k);
            return;
        }
        float v = 0;
        if (sscanf(line, "%*s %f", &v) < 1 && strcmp(k, "stop")) {
            UART_Printf(&g_uart0, "ERR: M%d %s needs value\r\n", id, k);
            return;
        }
        if (!strcmp(k, "Tr")) {
            motor_control_set_speed((uint8_t)id, (int32_t)v);
            UART_Printf(&g_uart0, "OK M%d Tr=%d\r\n", id, (int)v);
        } else if (!strcmp(k, "Kp")) {
            motor_control_set_kp((uint8_t)id, v);
            UART_Printf(&g_uart0, "OK M%d Kp=%.3f\r\n", id, v);
        } else if (!strcmp(k, "Ki")) {
            motor_control_set_ki((uint8_t)id, v);
            UART_Printf(&g_uart0, "OK M%d Ki=%.3f\r\n", id, v);
        } else if (!strcmp(k, "Kd")) {
            motor_control_set_kd((uint8_t)id, v);
            UART_Printf(&g_uart0, "OK M%d Kd=%.3f\r\n", id, v);
        } else if (!strcmp(k, "Dd")) {
            motor_control_set_duty((uint8_t)id, (uint32_t)v);
            UART_Printf(&g_uart0, "OK M%d Dd=%d\r\n", id, (int)v);
        } else if (!strcmp(k, "stop")) {
            motor_control_stop((uint8_t)id);
            UART_Printf(&g_uart0, "OK M%d stopped\r\n", id);
        } else {
            UART_Printf(&g_uart0, "ERR: %s\r\n", k);
        }
    }
}

/* 从通用库 RX 环形缓冲取字节, 本文件组装成行后交给 cmd_do。
 * 每轮限量处理, 防止 RX 持续来数据 (如 PA11 噪声) 时独占主循环, 保证 firewater 能被调用。 */
static __attribute__((noinline)) void cmd_poll(void) {
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

/* ==================== 陀螺仪轮询 (独立函数, 避免 main 过大触发编译器 bug) ==================== */

static __attribute__((noinline)) void gyro_poll(void) {
    MPU6500_GyroData gyroData;
    if (MPU6500_ReadGyro(&gyroData)) {
        UART_Printf(&g_uart1, "GX: %.2f | GY: %.2f | GZ: %.2f\r\n",
                    gyroData.gyro_x, gyroData.gyro_y, gyroData.gyro_z);
    } else {
        UART_Puts(&g_uart1, "MPU6500 Read Error!\r\n");
    }
}

/* ==================== 系统初始化 ==================== */

static __attribute__((noinline)) void system_setup(void) {
    NVIC_EnableIRQ(I2C_GYRO_INST_INT_IRQN);
    NVIC_EnableIRQ(UART_GYRO_INST_INT_IRQN);
    DL_SYSCTL_disableSleepOnExit();

    if (MPU6500_Init()) {
        UART_Puts(&g_uart1, "MPU6500 OK!\r\n");
    } else {
        UART_Puts(&g_uart1, "MPU6500 FAILED!\r\n");
        while(1);
    }

    motor_control_init(1, 0.01f, 0.4f, 1.5f, 0.0f);
    motor_control_init(2, 0.01f, 0.4f, 1.5f, 0.0f);

    NVIC_ClearPendingIRQ(TIMER_0_INST_INT_IRQN);
    NVIC_EnableIRQ(TIMER_0_INST_INT_IRQN);
    DL_TimerG_startCounter(TIMER_0_INST);

    Steering_Init();
    trajectory_set_feedback(Steering_GetCorrection);
    trajectory_enable_closed_loop(1);
}

/* ==================== Main (noinline 拆分防止编译器常量岛 bug) ==================== */

static __attribute__((noinline)) void main_loop(void) {
    while (1) {
        cmd_poll();
        if (g_fw_ready) {
            g_fw_ready = 0;
            firewater_send();
        }
        gyro_poll();
    }
}

int main(void)
{
    SYSCFG_DL_init();
    UART_Init();
    delay_cycles(CPUCLK_FREQ * 1);
    UART_RxEnable();
    __enable_irq();

    system_setup();

    UART_Puts(&g_uart0, "Dual Motor Control Ready\r\n");
    cmd_show();

    trajectory_straight(0.9f, 0.3f);

    main_loop();
}
