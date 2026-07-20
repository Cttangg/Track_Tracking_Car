#include "gyro_pid.h"

/* ============================================================
 * 陀螺仪直行 PID — 实现
 * ------------------------------------------------------------
 * 陀螺仪: I2C MPU6500 接入 (I2C_GYRO)
 *   Gyro_ReadYawRate() 从 empty.c 的全局变量 g_yaw_rate 读取
 *   当前 Z 轴角速率 (°/s), 由 IMU_UpdateAttitude() 在主循环中持续更新。
 *
 * PID 原理:
 *   error = 0 − ω_z         (目标 yaw rate = 0, 保持航向)
 *   ω_z>0(右转) → error<0 → corr<0 → 左转纠正 ✓
 * ============================================================ */

#define MAX_CORR  0.15f
#define MAX_INTEG 0.30f

/* 来自 empty.c 的全局 yaw rate (单位: °/s), 由 IMU_UpdateAttitude() 实时更新 */
extern volatile float g_yaw_rate;

static struct {
    float Kp, Ki, Kd, dt;
    float integral, prev_error;
    uint8_t init;
} g_pid;

/* ==================== 陀螺仪底层 (I2C MPU6500) ==================== */

void Gyro_Init(void)
{
    /* MPU6500 的完整初始化 (含 Z 轴零偏校准) 已在 empty.c 的 main() 中完成。
     * 本函数保留作为占位，供 Steering_Init() 统一调用。
     */
}

float Gyro_ReadYawRate(void)
{
    /* 从 empty.c 的 IMU_UpdateAttitude() 获取实时 Z 轴角速率 (°/s)。
     * 该值已经过零偏校准、动态漂移补偿和死区过滤。
     */
    return g_yaw_rate;
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
