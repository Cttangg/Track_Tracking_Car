#ifndef MPU6500_H_
#define MPU6500_H_

#include <stdint.h>
#include <stdbool.h>
#include "ti_msp_dl_config.h"

/* ------------------------------------------------------------------
 * 兼容性別名：將原始碼使用的通用名稱映射到 SysConfig 生成的實例名稱
 * ------------------------------------------------------------------ */
#define I2C_INST               I2C_GYRO_INST
#define I2C_INST_INT_IRQN      I2C_GYRO_INST_INT_IRQN
#define I2C_INST_IRQHandler    I2C_GYRO_INST_IRQHandler

/* MPU6500 7位元設備地址 (AD0接地時預設為 0x68) */
#define MPU6500_ADDR         0x68

/* 核心暫存器地址定義 */
#define MPU6500_SMPLRT_DIV   0x19  // 採樣率分頻器
#define MPU6500_CONFIG       0x1A  // 配置暫存器(低通濾波器)
#define MPU6500_GYRO_CONFIG  0x1B  // 陀螺儀配置(量程)
#define MPU6500_PWR_MGMT_1   0x6B  // 電源管理 1
#define MPU6500_WHO_AM_I     0x75  // 器件ID暫存器

// 陀螺儀資料暫存器首地址
#define MPU6500_GYRO_XOUT_H  0x43

// 陀螺儀資料結構體
typedef struct {
    int16_t raw_x;
    int16_t raw_y;
    int16_t raw_z;
    float gyro_x; // 轉換後的實際角速度 (°/s)
    float gyro_y;
    float gyro_z;
} MPU6500_GyroData;

// 函式宣告
bool MPU6500_Init(void);
bool MPU6500_ReadGyro(MPU6500_GyroData *data);

#endif /* MPU6500_H_ */