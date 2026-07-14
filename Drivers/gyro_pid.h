#ifndef GYRO_PID_H_
#define GYRO_PID_H_

/* ============================================================
 * 模式 B — 陀螺仪直行 PID
 * ------------------------------------------------------------
 * 陀螺仪经 UART1 接入 (当前未连接, 预留接口, 返回恒 0)。
 * 接入后用 Gyro_ReadYawRate() 获取 ω_z (rad/s),
 * PID 使 ω_z → 0, 即小车保持直线航向。
 * ============================================================ */

void Gyro_Init(void);               /* 预留: UART1 陀螺仪初始化 */
float Gyro_ReadYawRate(void);       /* 预留: 读偏航角速度 rad/s, 当前返回 0 */

void  GyroPID_Init(float kp, float ki, float kd);
void  GyroPID_SetKp(float kp);
void  GyroPID_SetKi(float ki);
void  GyroPID_SetKd(float kd);
void  GyroPID_Reset(void);
float GyroPID_Update(float yaw_rate);        /* 返回 转向修正 m/s */

#endif
