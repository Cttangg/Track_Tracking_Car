#include "gyro_pid.h"

/* ============================================================
 * 陀螺仪直行 PID — 实现
 * ------------------------------------------------------------
 * 陀螺仪: UART1 接入 (当前未连接)
 *   Gyro_ReadYawRate() 当前返回 0 → PID 输出恒 0 → 小车靠前馈走直线。
 *   陀螺仪接入后替换本函数实现即可, PID 层无需改动。
 *
 * PID 原理:
 *   error = 0 − ω_z         (目标 yaw rate = 0, 保持航向)
 *   ω_z>0(右转) → error<0 → corr<0 → 左转纠正 ✓
 * ============================================================ */

#define MAX_CORR  0.15f
#define MAX_INTEG 0.30f

static struct {
    float Kp, Ki, Kd, dt;
    float integral, prev_error;
    uint8_t init;
} g_pid;

/* ==================== 陀螺仪底层 (UART1 预留) ==================== */

void Gyro_Init(void)
{
    /* TODO: 配置 UART1 (需在 SysConfig 添加 UART_1_INST, 并取消 uart.h 中 UART1_ENABLE 注释)
     *       UART_Init() 已在 empty.c 中调用, 届时会自动初始化 g_uart1。
     *       然后通过 UART1 配置陀螺仪寄存器, 启动数据输出。
     */
}

float Gyro_ReadYawRate(void)
{
    /* TODO: 从 g_uart1 读取陀螺仪 UART 数据帧, 解析 yaw rate (rad/s)
     *       例: MPU6050 DMP 输出格式、或自定义协议帧。
     *       当前返回 0 — 未连接陀螺仪时小车仅靠前馈直线行驶。
     */
    return 0.0f;
}

/* ==================== PID ==================== */

void GyroPID_Init(float kp, float ki, float kd)
{
    g_pid.Kp = kp; g_pid.Ki = ki; g_pid.Kd = kd;
    g_pid.dt = 0.01f;
    g_pid.integral   = 0.0f;
    g_pid.prev_error = 0.0f;
    g_pid.init = 1;
}

void GyroPID_SetKp(float kp) { g_pid.Kp = kp; g_pid.integral = 0.0f; g_pid.prev_error = 0.0f; }
void GyroPID_SetKi(float ki) { g_pid.Ki = ki; g_pid.integral = 0.0f; g_pid.prev_error = 0.0f; }
void GyroPID_SetKd(float kd) { g_pid.Kd = kd; g_pid.integral = 0.0f; g_pid.prev_error = 0.0f; }

void GyroPID_Reset(void)
{
    g_pid.integral   = 0.0f;
    g_pid.prev_error = 0.0f;
}

float GyroPID_Update(float yaw_rate)
{
    if (!g_pid.init) return 0.0f;

    float error = 0.0f - yaw_rate;              /* ω_z>0(右转) → error<0 → 左转纠正 */

    g_pid.integral += error * g_pid.dt;
    if (g_pid.integral >  MAX_INTEG) g_pid.integral =  MAX_INTEG;
    if (g_pid.integral < -MAX_INTEG) g_pid.integral = -MAX_INTEG;

    float deriv = (error - g_pid.prev_error) / g_pid.dt;
    g_pid.prev_error = error;

    float corr = g_pid.Kp * error
               + g_pid.Ki * g_pid.integral
               + g_pid.Kd * deriv;

    if (corr >  MAX_CORR) corr =  MAX_CORR;
    if (corr < -MAX_CORR) corr = -MAX_CORR;

    return corr;
}
