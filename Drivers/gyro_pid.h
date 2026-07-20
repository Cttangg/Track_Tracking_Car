#ifndef GYRO_PID_H_
#define GYRO_PID_H_

#include <stdint.h>

/* ============================================================
 * 模式 B — 陀螺仪直行 PID
 * ------------------------------------------------------------
 * 陀螺仪经 I2C MPU6500 接入 (I2C_GYRO)。
 * 用 Gyro_ReadYawRate() 获取 ω_z (°/s),
 * PID 使 ω_z → 0, 即小车保持直线航向。
 * ============================================================ */

void Gyro_Init(void);               /* 占位: MPU6500 初始化已在 empty.c 中完成 */
float Gyro_ReadYawRate(void);       /* 读偏航角速度 °/s, 来自 IMU_UpdateAttitude() */

void  GyroPID_Init(float kp, float ki, float kd);
void  GyroPID_SetKp(float kp);
void  GyroPID_SetKi(float ki);
void  GyroPID_SetKd(float kd);
void  GyroPID_Reset(void);
float GyroPID_Update(float yaw_rate);        /* 返回 转向修正 m/s */

#endif
