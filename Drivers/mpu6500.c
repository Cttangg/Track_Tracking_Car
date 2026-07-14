#include "mpu6500.h"
#include "uart.h"
#include <stdio.h>

/* 官方中斷常規緩衝大小 */
#define I2C_TX_MAX_PACKET_SIZE (16)
#define I2C_RX_MAX_PACKET_SIZE (16)

/* 內部狀態機變數與快取（完全對齊官方例程命名与机制） */
static uint8_t gTxPacket[I2C_TX_MAX_PACKET_SIZE];
static uint32_t gTxLen, gTxCount;

static uint8_t gRxPacket[I2C_RX_MAX_PACKET_SIZE];
static uint32_t gRxLen, gRxCount;

enum I2cControllerStatus {
    I2C_STATUS_IDLE = 0,
    I2C_STATUS_TX_STARTED,
    I2C_STATUS_TX_INPROGRESS,
    I2C_STATUS_TX_COMPLETE,
    I2C_STATUS_RX_STARTED,
    I2C_STATUS_RX_INPROGRESS,
    I2C_STATUS_RX_COMPLETE,
    I2C_STATUS_ERROR,
};

static volatile enum I2cControllerStatus gI2cControllerStatus;

/* ==================== 內部私有：官方中斷驅動多位元組發送/接收 ==================== */

/**
 * @brief 嚴格遵循官方標準的中斷多位元組寫入函數
 */
static bool I2C_WritePacket(uint8_t targetAddr, uint8_t *pBuffer, uint8_t length) {
    if (length > I2C_TX_MAX_PACKET_SIZE) return false;
    
    for (uint8_t i = 0; i < length; i++) {
        gTxPacket[i] = pBuffer[i];
    }
    gTxLen = length;
    gTxCount = 0;
    gI2cControllerStatus = I2C_STATUS_TX_STARTED;

    /* 預先填充TX FIFO */
    gTxCount += DL_I2C_fillControllerTXFIFO(I2C_GYRO_INST, &gTxPacket[gTxCount], gTxLen - gTxCount);

    /* 根據資料長度判斷是否需要啟用 TX FIFO 觸發中斷 */
    if (gTxCount < gTxLen) {
        DL_I2C_enableInterrupt(I2C_GYRO_INST, DL_I2C_INTERRUPT_CONTROLLER_TXFIFO_TRIGGER);
    } else {
        DL_I2C_disableInterrupt(I2C_GYRO_INST, DL_I2C_INTERRUPT_CONTROLLER_TXFIFO_TRIGGER);
    }

    /* 啟動傳輸 */
    DL_I2C_startControllerTransfer(I2C_GYRO_INST, targetAddr, DL_I2C_CONTROLLER_DIRECTION_TX, gTxLen);

    /* 阻塞等待中斷狀態機完成 */
    while ((gI2cControllerStatus != I2C_STATUS_TX_COMPLETE) && 
           (gI2cControllerStatus != I2C_STATUS_ERROR));

    return (gI2cControllerStatus == I2C_STATUS_TX_COMPLETE);
}

/**
 * @brief 嚴格遵循官方標準的中斷多位元組讀取函數 (Write Reg Addr -> Restart -> Read Data)
 */
static bool I2C_ReadPacket(uint8_t targetAddr, uint8_t regAddr, uint8_t *pBuffer, uint8_t length) {
    if (length > I2C_RX_MAX_PACKET_SIZE) return false;

    /* ---------------- 階段 1: 寫入目標暫存器地址 ---------------- */
    gTxPacket[0] = regAddr;
    gTxLen = 1;
    gTxCount = 0;
    gI2cControllerStatus = I2C_STATUS_TX_STARTED;
    
    DL_I2C_fillControllerTXFIFO(I2C_GYRO_INST, &gTxPacket[gTxCount], gTxLen - gTxCount);
    gTxCount = 1;
    
    DL_I2C_disableInterrupt(I2C_GYRO_INST, DL_I2C_INTERRUPT_CONTROLLER_TXFIFO_TRIGGER);
    DL_I2C_startControllerTransfer(I2C_GYRO_INST, targetAddr, DL_I2C_CONTROLLER_DIRECTION_TX, gTxLen);
    
    while ((gI2cControllerStatus != I2C_STATUS_TX_COMPLETE) && 
           (gI2cControllerStatus != I2C_STATUS_ERROR));
           
    if (gI2cControllerStatus == I2C_STATUS_ERROR) return false;
    
    /* ---------------- 階段 2: 啟動接收狀態機讀取資料 ---------------- */
    gRxLen = length;
    gRxCount = 0;
    gI2cControllerStatus = I2C_STATUS_RX_STARTED;
    
    DL_I2C_startControllerTransfer(I2C_GYRO_INST, targetAddr, DL_I2C_CONTROLLER_DIRECTION_RX, gRxLen);
    
    while ((gI2cControllerStatus != I2C_STATUS_RX_COMPLETE) && 
           (gI2cControllerStatus != I2C_STATUS_ERROR));
           
    if (gI2cControllerStatus == I2C_STATUS_RX_COMPLETE) {
        for (uint8_t i = 0; i < length; i++) {
            pBuffer[i] = gRxPacket[i];
        }
        return true;
    }
    
    return false;
}

/* 底層暫存器包裝 */
static bool I2C_WriteReg(uint8_t reg, uint8_t value) {
    uint8_t buf[2];
    buf[0] = reg;
    buf[1] = value;
    return I2C_WritePacket(MPU6500_ADDR, buf, 2);
}

static bool I2C_ReadRegs(uint8_t reg, uint8_t *value, uint8_t len) {
    return I2C_ReadPacket(MPU6500_ADDR, reg, value, len);
}

/* ==================== 公共功能實作 (無任何外部串口列印依賴) ==================== */

/**
 * @brief 初始化 MPU6500 (保持中断模式不变，仅将串口输出精简为纯英文)
 * @return bool 初始化成功返回 true，失败返回 false
 */
bool MPU6500_Init(void)
{
    uint8_t who_am_i = 0;

    // 1. 提示初始化启动
    UART_Puts(&g_uart1, "MPU6500 Init...\r\n");

    // 2. 配置电源管理 (唤醒并设置时钟源)
    if (!I2C_WriteReg(MPU6500_PWR_MGMT_1, 0x01)) {
        UART_Puts(&g_uart1, "Err: PWR_MGMT\r\n");
        return false;
    }
    
    // 3. 配置采样率分频器
    if (!I2C_WriteReg(MPU6500_SMPLRT_DIV, 0x07)) {
        UART_Puts(&g_uart1, "Err: SMPLRT_DIV\r\n");
        return false;
    }
    
    // 4. 配置低通滤波器配置寄存器
    if (!I2C_WriteReg(MPU6500_CONFIG, 0x03)) {
        UART_Puts(&g_uart1, "Err: CONFIG\r\n");
        return false;
    }
    
    // 5. 配置陀螺仪满量程范围
    if (!I2C_WriteReg(MPU6500_GYRO_CONFIG, 0x18)) {
        UART_Puts(&g_uart1, "Err: GYRO_CONFIG\r\n");
        return false;
    }
    
    // 6. 读取 WHO_AM_I 寄存器验证设备通信
    // (注：此处继续沿用你原本写好的中断级多字节读取函数，如 I2C_ReadRegs)
    if (!I2C_ReadRegs(MPU6500_WHO_AM_I, &who_am_i, 1)) {
        UART_Puts(&g_uart1, "Err: Read ID\r\n");
        return false;
    }

    // 7. 校验器件 ID 是否匹配 (MPU6500 正常为 0x70，MPU6050 为 0x68)
    if (who_am_i != 0x70 && who_am_i != 0x68) {
        char error_log[32];
        sprintf(error_log, "Err: ID Mismatch 0x%02X\r\n", who_am_i);
        UART_Puts(&g_uart1, error_log);
        return false;
    }

    // 8. 成功完成初始化提示
    UART_Puts(&g_uart1, "MPU6500 OK\r\n");
    return true;
}

bool MPU6500_ReadGyro(MPU6500_GyroData *data) {
    uint8_t buf[6];
    if (!I2C_ReadRegs(MPU6500_GYRO_XOUT_H, buf, 6)) return false;
    
    data->raw_x = (int16_t)((buf[0] << 8) | buf[1]);
    data->raw_y = (int16_t)((buf[2] << 8) | buf[3]);
    data->raw_z = (int16_t)((buf[4] << 8) | buf[5]);
    
    /* FS_SEL = 3 (±2000 °/s), 靈敏度 16.4 LSB/(°/s) */
    data->gyro_x = (float)data->raw_x / 16.4f;
    data->gyro_y = (float)data->raw_y / 16.4f;
    data->gyro_z = (float)data->raw_z / 16.4f;
    
    return true;
}

/* ==================== 🛠 官方標準 I2C 中斷服務常式 (ISR) ==================== */

void I2C_GYRO_INST_IRQHandler(void)
{
    switch (DL_I2C_getPendingInterrupt(I2C_GYRO_INST)) {
        case DL_I2C_IIDX_CONTROLLER_RX_DONE:
            while(!DL_I2C_isControllerRXFIFOEmpty(I2C_GYRO_INST))
            {
                if(gRxCount < gRxLen)
                {
                    gRxPacket[gRxCount++] =
                        DL_I2C_receiveControllerData(I2C_GYRO_INST);
                }
                else
                {
                    DL_I2C_receiveControllerData(I2C_GYRO_INST);
                }
            }
            gI2cControllerStatus = I2C_STATUS_RX_COMPLETE;
            break;
        case DL_I2C_IIDX_CONTROLLER_TX_DONE:
            DL_I2C_disableInterrupt(I2C_GYRO_INST, DL_I2C_INTERRUPT_CONTROLLER_TXFIFO_TRIGGER);
            gI2cControllerStatus = I2C_STATUS_TX_COMPLETE;
            break;
        case DL_I2C_IIDX_CONTROLLER_RXFIFO_TRIGGER:
            gI2cControllerStatus = I2C_STATUS_RX_INPROGRESS;
            while (DL_I2C_isControllerRXFIFOEmpty(I2C_GYRO_INST) != true) {
                if (gRxCount < gRxLen) {
                    gRxPacket[gRxCount++] = DL_I2C_receiveControllerData(I2C_GYRO_INST);
                } else {
                    DL_I2C_receiveControllerData(I2C_GYRO_INST);
                }
            }
            break;
        case DL_I2C_IIDX_CONTROLLER_TXFIFO_TRIGGER:
            gI2cControllerStatus = I2C_STATUS_TX_INPROGRESS;
            if (gTxCount < gTxLen) {
                gTxCount += DL_I2C_fillControllerTXFIFO(I2C_GYRO_INST, &gTxPacket[gTxCount], gTxLen - gTxCount);
            }
            break;
        case DL_I2C_IIDX_CONTROLLER_ARBITRATION_LOST:
        case DL_I2C_IIDX_CONTROLLER_NACK:
            if ((gI2cControllerStatus == I2C_STATUS_RX_STARTED) ||
                (gI2cControllerStatus == I2C_STATUS_TX_STARTED) ||
                (gI2cControllerStatus == I2C_STATUS_RX_INPROGRESS) ||
                (gI2cControllerStatus == I2C_STATUS_TX_INPROGRESS)) {
                gI2cControllerStatus = I2C_STATUS_ERROR;
            }
            break;
        default:
            break;
    }
}